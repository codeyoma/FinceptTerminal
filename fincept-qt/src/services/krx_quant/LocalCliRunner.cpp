#include "services/krx_quant/LocalCliRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

namespace fincept::krx_quant::cli_llm {

namespace {

constexpr int kStartTimeoutMs = 15000;
constexpr int kLoginShellTimeoutMs = 3000;
constexpr int kProbeTimeoutMs = 20000;

QString install_hint(const QString& provider) {
    if (provider.compare(kCodex, Qt::CaseInsensitive) == 0)
        return QStringLiteral("Install Codex (npm install -g @openai/codex) and run `codex login`");
    return QStringLiteral("Install Claude Code and run `claude` once to log in");
}

#ifndef Q_OS_WIN
/// The login shell's PATH: where Homebrew, npm and the CLI installers put their programs.
QStringList login_shell_path() {
    QString shell = qEnvironmentVariable("SHELL");
    if (shell.isEmpty())
        shell = QStringLiteral("/bin/zsh");
    QProcess p;
    p.start(shell, {QStringLiteral("-l"), QStringLiteral("-c"), QStringLiteral("printf '\\n%s' \"$PATH\"")});
    p.closeWriteChannel();
    if (!p.waitForFinished(kLoginShellTimeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        return {};
    }
    // A profile may print its own lines first; PATH is the last one.
    const QString last = QString::fromUtf8(p.readAllStandardOutput()).split('\n').last().trimmed();
    return last.split(':', Qt::SkipEmptyParts);
}
#endif

QString expand_home(const QString& path) {
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

QString codex_home() {
    const QString env = qEnvironmentVariable("CODEX_HOME");
    return env.isEmpty() ? QDir::homePath() + QStringLiteral("/.codex") : env;
}

/// The models Settings offers once the CLI runs.
QStringList models_for(const QString& provider) {
    if (provider.compare(kCodex, Qt::CaseInsensitive) != 0)
        return fallback_models(provider);
    QStringList models{kDefaultModel};
    QFile cache(codex_home() + QStringLiteral("/models_cache.json"));
    if (cache.open(QIODevice::ReadOnly)) {
        for (const QString& m : codex_cached_models(cache.readAll()))
            if (!models.contains(m))
                models << m;
    }
    return models;
}

/// Starts ``exe`` with ``args``. A batch file goes through cmd.exe with one quoted command line,
/// which QProcess's own argument quoting cannot express.
void start(QProcess& p, const Executable& exe, const QStringList& args) {
#ifdef Q_OS_WIN
    if (exe.via_cmd) {
        p.setProgram(exe.program);
        p.setNativeArguments(cmd_arguments(exe.path, args));
        p.start();
        return;
    }
#endif
    p.start(exe.program, exe.prefix_args + args);
}

QString shortened(const QByteArray& bytes) {
    QString text = QString::fromUtf8(bytes).trimmed();
    if (text.size() > 400)
        text = QStringLiteral("…") + text.right(400);
    return text;
}

} // namespace

QStringList search_path() {
    static const QStringList dirs = [] {
        QStringList out;
        auto add = [&out](const QString& d) {
            if (!d.isEmpty() && !out.contains(d))
                out << d;
        };
        for (const QString& d : qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts))
            add(d);
        const QString home = QDir::homePath();
#ifdef Q_OS_WIN
        add(home + QStringLiteral("/.local/bin"));
        add(qEnvironmentVariable("APPDATA") + QStringLiteral("/npm"));
#else
        for (const QString& d : login_shell_path())
            add(d);
        for (const QString& d :
             {home + QStringLiteral("/.local/bin"), home + QStringLiteral("/.claude/local"),
              QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"),
              home + QStringLiteral("/.npm-global/bin"), home + QStringLiteral("/.bun/bin"),
              home + QStringLiteral("/.volta/bin"), QStringLiteral("/usr/bin"), QStringLiteral("/bin")})
            add(d);
#endif
        return out;
    }();
    return dirs;
}

Executable find_executable(const QString& provider, const QString& override_path) {
    Executable e;
    const QString name = program(provider);
    if (name.isEmpty()) {
        e.error = QStringLiteral("Not a local CLI provider: ") + provider;
        return e;
    }

    const QString pinned = expand_home(override_path.trimmed());
    if (!pinned.isEmpty()) {
        const QFileInfo fi(pinned);
        if (fi.isDir())
            e.path = QStandardPaths::findExecutable(name, {fi.absoluteFilePath()});
        else if (fi.isFile() && fi.isExecutable())
            e.path = fi.absoluteFilePath();
        if (e.path.isEmpty()) {
            e.error = QStringLiteral("%1: the CLI path \"%2\" is not an executable %3. Fix it in Settings → LLM "
                                     "Config, or clear it to find %3 automatically.")
                          .arg(display_name(provider), pinned, name);
            return e;
        }
    } else {
        e.path = QStandardPaths::findExecutable(name, search_path());
        if (e.path.isEmpty()) {
            e.error = QStringLiteral("%1: `%2` was not found on this computer. %3, or set its CLI path in "
                                     "Settings → LLM Config.")
                          .arg(display_name(provider), name, install_hint(provider));
            return e;
        }
    }

    e.program = e.path;
#ifdef Q_OS_WIN
    // npm installs `codex` and `claude` as .cmd shims, which CreateProcess cannot start. Run the
    // shim's node script with node; failing that, run the shim through cmd.exe.
    const QFileInfo shim(e.path);
    const QString suffix = shim.suffix().toLower();
    if (suffix == QLatin1String("cmd") || suffix == QLatin1String("bat")) {
        QString script;
        QFile f(e.path);
        if (f.open(QIODevice::ReadOnly))
            script = npm_shim_script(e.path, f.read(64 * 1024));
        const QString node = script.isEmpty()
                                 ? QString()
                                 : QStandardPaths::findExecutable(QStringLiteral("node"),
                                                                  QStringList{shim.absolutePath()} + search_path());
        if (!script.isEmpty() && QFileInfo::exists(script) && !node.isEmpty()) {
            e.program = node;
            e.prefix_args = {QDir::toNativeSeparators(script)};
        } else {
            const QString comspec = qEnvironmentVariable("ComSpec");
            e.program = comspec.isEmpty() ? QStringLiteral("cmd.exe") : comspec;
            e.via_cmd = true;
        }
    }
#endif
    return e;
}

QString npm_shim_script(const QString& shim_path, const QByteArray& shim_text) {
    // npm's cmd-shim: "%dp0%\node_modules\...\x.js" (SET dp0=%~dp0), or "%~dp0\..." in older ones.
    static const QRegularExpression re(QStringLiteral(R"re("%(?:dp0%|~dp0)\\?([^"]+\.[cm]?js)")re"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(QString::fromUtf8(shim_text));
    if (!m.hasMatch())
        return {};
    QString rel = m.captured(1);
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir(QFileInfo(shim_path).path()).filePath(rel); // shim_path is already absolute
}

QString cmd_arguments(const QString& path, const QStringList& args) {
    static const QRegularExpression needs_quotes(QStringLiteral(R"re([\s"&|<>^(),;=!%])re"));
    auto quoted = [](QString a) {
        if (a.isEmpty())
            return QStringLiteral("\"\"");
        if (!needs_quotes.match(a).hasMatch())
            return a;
        a.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + a + QLatin1Char('"');
    };
    QStringList line{quoted(QDir::toNativeSeparators(path))};
    for (const QString& a : args)
        line << quoted(a);
    return QStringLiteral("/d /s /c \"") + line.join(QLatin1Char(' ')) + QLatin1Char('"');
}

QProcessEnvironment environment_for(const QString& path) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList dirs{QFileInfo(path).absolutePath()};
    for (const QString& d : search_path())
        if (!dirs.contains(d))
            dirs << d;
#ifdef Q_OS_WIN
    for (QString& d : dirs)
        d = QDir::toNativeSeparators(d);
#endif
    env.insert(QStringLiteral("PATH"), dirs.join(QDir::listSeparator()));
    return env;
}

QString work_dir() {
    const QString dir = QDir::tempPath() + QStringLiteral("/fincept-llm-cli");
    QDir().mkpath(dir);
    return dir;
}

RunResult run_blocking(const Executable& exe, const QStringList& args, const QByteArray& input, int timeout_ms) {
    RunResult r;
    QProcess p;
    p.setProcessEnvironment(environment_for(exe.path));
    p.setWorkingDirectory(work_dir());
    start(p, exe, args);
    if (!p.waitForStarted(kStartTimeoutMs)) {
        r.error = QStringLiteral("could not start %1: %2").arg(exe.path, p.errorString());
        return r;
    }
    r.started = true;
    p.write(input);
    p.closeWriteChannel();
    if (!p.waitForFinished(timeout_ms)) {
        r.timed_out = true;
        p.kill();
        p.waitForFinished(2000);
    }
    r.out = p.readAllStandardOutput();
    r.err = p.readAllStandardError();
    r.exit_code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return r;
}

Answer ask(const QString& provider, const QString& model, const QString& override_path, const QString& prompt) {
    Answer a;
    const Executable exe = find_executable(provider, override_path);
    if (!exe.error.isEmpty()) {
        a.error = exe.error;
        return a;
    }
    const RunResult r = run_blocking(exe, arguments(provider, model, work_dir()), prompt.toUtf8(), kRoundTimeoutMs);
    if (!r.started) {
        a.error = display_name(provider) + QStringLiteral(": ") + r.error;
        return a;
    }
    if (r.timed_out) {
        a.error =
            QStringLiteral("%1 did not answer within %2 s").arg(display_name(provider)).arg(kRoundTimeoutMs / 1000);
        return a;
    }
    return parse_output(provider, r.out, r.err, r.exit_code);
}

void probe(const QString& provider, const QString& override_path, QObject* context,
           std::function<void(const QStringList& models, const QString& error)> done) {
    const Executable exe = find_executable(provider, override_path);
    if (!exe.error.isEmpty()) {
        QMetaObject::invokeMethod(context, [done, error = exe.error]() { done({}, error); }, Qt::QueuedConnection);
        return;
    }

    auto* proc = new QProcess(context);
    proc->setProcessEnvironment(environment_for(exe.path));
    proc->setWorkingDirectory(work_dir());
    auto answered = std::make_shared<bool>(false);
    auto timed_out = std::make_shared<bool>(false);
    const QString name = display_name(provider);
    const QString path = exe.path;

    auto finish = [proc, answered, done](const QStringList& models, const QString& error) {
        if (*answered)
            return;
        *answered = true;
        proc->deleteLater();
        done(models, error);
    };

    QTimer::singleShot(kProbeTimeoutMs, proc, [proc, timed_out]() {
        *timed_out = true;
        proc->kill();
    });
    QObject::connect(proc, &QProcess::errorOccurred, context, [finish, proc, name, path](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            finish({}, QStringLiteral("%1: could not start %2: %3").arg(name, path, proc->errorString()));
    });
    QObject::connect(
        proc, &QProcess::finished, context,
        [finish, proc, provider, name, timed_out](int code, QProcess::ExitStatus status) {
            if (*timed_out) {
                finish({},
                       QStringLiteral("%1 did not answer --version within %2 s").arg(name).arg(kProbeTimeoutMs / 1000));
                return;
            }
            if (status != QProcess::NormalExit || code != 0) {
                const QString why = shortened(proc->readAllStandardError());
                finish({}, QStringLiteral("%1 failed to run (exit code %2)%3")
                               .arg(name)
                               .arg(code)
                               .arg(why.isEmpty() ? QString() : QStringLiteral(": ") + why));
                return;
            }
            finish(models_for(provider), {});
        });
    start(*proc, exe, version_arguments());
}

} // namespace fincept::krx_quant::cli_llm
