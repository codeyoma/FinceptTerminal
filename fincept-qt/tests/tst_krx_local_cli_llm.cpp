// Unit tests for src/services/krx_quant/LocalCliLlm.h and LocalCliRunner.h
//
// Fincept's LLM rounds can run through the Claude Code or Codex CLI the user is logged in to
// (codeyoma/krx-quant#96). Each round starts the CLI once, without tools of its own, with the
// prompt on stdin, and reads one answer. These tests pin the arguments each CLI gets, how each
// CLI's output is read (answer, usage, errors), Codex's model list, and, with a stand-in CLI,
// that the runner finds the program and hands it the prompt.

#include "services/krx_quant/LocalCliLlm.h"
#include "services/krx_quant/LocalCliRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTemporaryDir>
#include <QTest>

#ifndef Q_OS_WIN
#    include <csignal>
#endif

using namespace fincept::krx_quant::cli_llm;

namespace {

// `claude -p --output-format json`, trimmed to the fields that are read.
const char* kClaudeOk =
    R"({"type":"result","subtype":"success","is_error":false,"result":"PONG","stop_reason":"end_turn",)"
    R"("usage":{"input_tokens":2,"cache_creation_input_tokens":1136,"cache_read_input_tokens":10,"output_tokens":5}})";

// The same with a model the account cannot use: the CLI exits 1 and says why in `result`.
const char* kClaudeBadModel =
    R"({"type":"result","subtype":"success","is_error":true,"api_error_status":404,)"
    R"("result":"There is an issue with the selected model (no-such-model-x).","usage":{"input_tokens":0,"output_tokens":0}})";

// `codex exec --json`: one event per line.
const char* kCodexOk = R"({"type":"thread.started","thread_id":"t1"}
{"type":"turn.started"}
{"type":"item.completed","item":{"id":"item_0","type":"agent_message","text":"Looking it up."}}
{"type":"item.completed","item":{"id":"item_1","type":"agent_message","text":"PONG"}}
{"type":"turn.completed","usage":{"input_tokens":23602,"cached_input_tokens":11648,"output_tokens":6}}
)";

// A model the account cannot use: a warning item, then the turn fails with an API envelope as
// its message. Built in code rather than as a raw string: moc misreads quote characters inside
// raw strings (an apostrophe in one hid this whole test class from it).
QByteArray codex_bad_model() {
    const QString envelope =
        QString::fromUtf8(QJsonDocument(QJsonObject{
                                            {"type", "error"},
                                            {"status", 400},
                                            {"error", QJsonObject{{"type", "invalid_request_error"},
                                                                  {"message", "The 'x' model is not supported."}}},
                                        })
                              .toJson(QJsonDocument::Compact));
    const QList<QJsonObject> events{
        {{"type", "thread.started"}, {"thread_id", "t2"}},
        {{"type", "item.completed"},
         {"item", QJsonObject{{"id", "item_0"}, {"type", "error"}, {"message", "Model metadata for `x` not found."}}}},
        {{"type", "turn.started"}},
        {{"type", "error"}, {"message", envelope}},
        {{"type", "turn.failed"}, {"error", QJsonObject{{"message", envelope}}}},
    };
    QByteArray out;
    for (const QJsonObject& ev : events)
        out += QJsonDocument(ev).toJson(QJsonDocument::Compact) + "\n";
    return out;
}

// $CODEX_HOME/models_cache.json, trimmed.
const char* kCodexModels = R"({"fetched_at":"2026-10-12","models":[
  {"slug":"gpt-6-astra","visibility":"list","priority":2},
  {"slug":"gpt-reserve","visibility":"hide","priority":4},
  {"slug":"gpt-6.1-sol","visibility":"list","priority":1},
  {"slug":"gpt-5.6-terra","visibility":"list","priority":8}
]})";

int index_of(const QStringList& args, const QString& arg) {
    return static_cast<int>(args.indexOf(arg));
}

} // namespace

class TestKrxLocalCliLlm : public QObject {
    Q_OBJECT

  private slots:
    void providers_are_the_two_clis() {
        QVERIFY(is_cli_provider("claude_cli"));
        QVERIFY(is_cli_provider("Codex_CLI"));
        QVERIFY(!is_cli_provider("anthropic"));
        QVERIFY(!is_cli_provider("ollama"));
        QCOMPARE(providers(), (QStringList{"claude_cli", "codex_cli"}));
        QCOMPARE(program("claude_cli"), QString("claude"));
        QCOMPARE(program("codex_cli"), QString("codex"));
        QCOMPARE(fallback_models("claude_cli").first(), kDefaultModel);
        QCOMPARE(fallback_models("codex_cli"), QStringList{kDefaultModel});
    }

    void claude_runs_in_print_mode_without_tools_or_customizations() {
        const QStringList args = arguments("claude_cli", "default", "/tmp/w");
        for (const char* a : {"-p", "--safe-mode", "--no-session-persistence"})
            QVERIFY2(args.contains(a), a);
        QCOMPARE(args.at(index_of(args, "--output-format") + 1), QString("json"));
        // --tools "" disables every built-in tool, and must not swallow a bare word after it.
        const int tools = index_of(args, "--tools");
        QVERIFY(tools >= 0);
        QCOMPARE(args.at(tools + 1), QString());
        QVERIFY(args.at(tools + 2).startsWith("--"));
        QVERIFY(!args.at(index_of(args, "--system-prompt") + 1).isEmpty());
        QVERIFY2(!args.contains("--model"), "the default model passes no --model");
    }

    void claude_takes_a_chosen_model() {
        const QStringList args = arguments("claude_cli", " sonnet ", "/tmp/w");
        QCOMPARE(args.at(index_of(args, "--model") + 1), QString("sonnet"));
        QVERIFY(!arguments("claude_cli", "", "/tmp/w").contains("--model"));
    }

    void codex_runs_read_only_without_shell_or_user_config() {
        const QStringList args = arguments("codex_cli", "default", "/tmp/w");
        QCOMPARE(args.first(), QString("exec"));
        for (const char* a :
             {"--ignore-user-config", "--ignore-rules", "--ephemeral", "--skip-git-repo-check", "--json"})
            QVERIFY2(args.contains(a), a);
        QCOMPARE(args.at(index_of(args, "--sandbox") + 1), QString("read-only"));
        QCOMPARE(args.at(index_of(args, "--cd") + 1), QString("/tmp/w"));
        // Codex's own tools are off, by -c so an unknown feature name does not fail the run.
        QCOMPARE(args.at(index_of(args, "web_search=\"disabled\"") - 1), QString("-c"));
        for (const char* f : {"shell_tool", "unified_exec", "apps", "browser_use", "computer_use", "image_generation",
                              "multi_agent", "plugins", "memories", "hooks"}) {
            const QString flag = QStringLiteral("features.%1=false").arg(f);
            QVERIFY2(args.contains(flag), qPrintable(flag));
            QCOMPARE(args.at(index_of(args, flag) - 1), QString("-c"));
        }
        QVERIFY(!args.contains("--disable"));
        QCOMPARE(args.last(), QString("-")); // the prompt comes on stdin
        QVERIFY(!args.contains("--model"));

        const QStringList chosen = arguments("codex_cli", "gpt-6-astra", "/tmp/w");
        QCOMPARE(chosen.at(index_of(chosen, "--model") + 1), QString("gpt-6-astra"));
        QCOMPARE(chosen.last(), QString("-"));
    }

    void claude_answer_and_usage() {
        const Answer a = parse_output("claude_cli", kClaudeOk, {}, 0);
        QVERIFY(a.ok);
        QCOMPARE(a.text, QString("PONG"));
        QCOMPARE(a.input_tokens, 2 + 1136 + 10);
        QCOMPARE(a.output_tokens, 5);
    }

    void claude_error_is_its_own_words() {
        const Answer a = parse_output("claude_cli", kClaudeBadModel, "[claude-code:unrecognized_model]", 1);
        QVERIFY(!a.ok);
        QVERIFY2(a.error.contains("issue with the selected model"), qPrintable(a.error));
        QVERIFY(a.error.startsWith("Claude Code CLI: "));
    }

    void claude_without_a_result_reports_stderr() {
        const Answer a = parse_output("claude_cli", "not json\n", "Error: Not logged in", 1);
        QVERIFY(!a.ok);
        QVERIFY2(a.error.contains("Not logged in"), qPrintable(a.error));

        const Answer silent = parse_output("claude_cli", {}, {}, 3);
        QVERIFY(!silent.ok);
        QVERIFY2(silent.error.contains("code 3"), qPrintable(silent.error));

        const Answer empty =
            parse_output("claude_cli", R"({"type":"result","is_error":false,"result":"  ","usage":{}})", {}, 0);
        QVERIFY(!empty.ok);
    }

    void codex_answer_is_the_last_agent_message() {
        const Answer a = parse_output("codex_cli", kCodexOk, {}, 0);
        QVERIFY(a.ok);
        QCOMPARE(a.text, QString("PONG"));
        QCOMPARE(a.input_tokens, 23602);
        QCOMPARE(a.output_tokens, 6);
    }

    void codex_failure_unwraps_the_api_message() {
        const Answer a = parse_output("codex_cli", codex_bad_model(), {}, 1);
        QVERIFY(!a.ok);
        QCOMPARE(a.error, QString("Codex CLI: The 'x' model is not supported."));
    }

    void codex_without_events_reports_stderr() {
        const Answer a = parse_output("codex_cli", {}, "env: node: No such file or directory", 127);
        QVERIFY(!a.ok);
        QVERIFY2(a.error.contains("node"), qPrintable(a.error));
    }

    void codex_models_listed_by_priority_without_hidden_ones() {
        QCOMPARE(codex_cached_models(kCodexModels), (QStringList{"gpt-6.1-sol", "gpt-6-astra", "gpt-5.6-terra"}));
        QVERIFY(codex_cached_models("not json").isEmpty());
    }

    void npm_shim_runs_its_node_script() {
        // npm's cmd-shim for `codex`, as `npm install -g` writes it on Windows.
        const QByteArray shim = "@ECHO off\r\nGOTO start\r\n:find_dp0\r\nSET dp0=%~dp0\r\nEXIT /b\r\n:start\r\n"
                                "SETLOCAL\r\nCALL :find_dp0\r\n\r\nIF EXIST \"%dp0%\\node.exe\" (\r\n"
                                "  SET \"_prog=%dp0%\\node.exe\"\r\n) ELSE (\r\n  SET \"_prog=node\"\r\n)\r\n\r\n"
                                "endLocal & goto #_undefined_# 2>NUL || title %COMSPEC% & \"%_prog%\"  "
                                "\"%dp0%\\node_modules\\@openai\\codex\\bin\\codex.js\" %*\r\n";
        QCOMPARE(npm_shim_script("C:/Users/A B/AppData/Roaming/npm/codex.cmd", shim),
                 QString("C:/Users/A B/AppData/Roaming/npm/node_modules/@openai/codex/bin/codex.js"));
        // Older shims name the folder as %~dp0.
        QCOMPARE(
            npm_shim_script("C:/npm/claude.cmd", "@\"%~dp0\\node_modules\\@anthropic-ai\\claude-code\\cli.js\" %*"),
            QString("C:/npm/node_modules/@anthropic-ai/claude-code/cli.js"));
        QVERIFY(npm_shim_script("C:/tools/codex.cmd", "@echo off\r\ncodex.exe %*\r\n").isEmpty());
    }

    void cmd_line_keeps_each_argument_whole() {
        // /s: cmd.exe drops only the outer pair of quotes, so a path with spaces keeps its own.
        QCOMPARE(cmd_arguments("C:/Users/A B/npm/codex.cmd", {"exec", "--cd", "C:/Temp/x y", "-", ""}),
                 QString(R"(/d /s /c ""C:/Users/A B/npm/codex.cmd" exec --cd "C:/Temp/x y" - """)")
                     .replace("C:/Users/A B/npm/codex.cmd", QDir::toNativeSeparators("C:/Users/A B/npm/codex.cmd")));
        QCOMPARE(cmd_arguments("C:/npm/claude.cmd", {"--system-prompt", "a <tool_call> b; c"}),
                 QString(R"(/d /s /c "%1 --system-prompt "a <tool_call> b; c"")")
                     .arg(QDir::toNativeSeparators("C:/npm/claude.cmd")));
    }

#ifndef Q_OS_WIN
    void runner_finds_the_cli_and_hands_it_the_prompt() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // A stand-in `claude` that answers with whatever came on stdin.
        const QString path = dir.filePath("claude");
        QFile script(path);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(
            "#!/bin/sh\n"
            "input=$(cat)\n"
            "printf '{\"type\":\"result\",\"is_error\":false,\"result\":\"%s\",\"usage\":{\"output_tokens\":1}}\\n' "
            "\"$input\"\n");
        script.close();
        QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        // A pinned file, and a pinned folder holding it.
        for (const QString& pinned : {path, dir.path()}) {
            const Executable exe = find_executable("claude_cli", pinned);
            QVERIFY2(exe.error.isEmpty(), qPrintable(exe.error));
            QCOMPARE(exe.path, path);
            const Answer a = ask("claude_cli", "default", pinned, "PING");
            QVERIFY2(a.ok, qPrintable(a.error));
            QCOMPARE(a.text, QString("PING"));
        }

        // The CLI's own folder leads PATH, so a node next to it is found.
        const QString env_path = environment_for(path).value("PATH");
        QVERIFY(env_path.startsWith(dir.path() + ":"));
    }

    void runner_says_where_a_pinned_path_is_wrong() {
        const Executable exe = find_executable("codex_cli", "/no/such/codex");
        QVERIFY(!exe.error.isEmpty());
        QVERIFY2(exe.error.contains("/no/such/codex"), qPrintable(exe.error));
        const Answer a = ask("codex_cli", "default", "/no/such/codex", "hi");
        QVERIFY(!a.ok);
        QCOMPARE(a.error, exe.error);
    }

    void probe_lists_models_when_the_cli_runs() {
        QTemporaryDir dir;
        const QString ok = dir.filePath("claude");
        QFile good(ok);
        QVERIFY(good.open(QIODevice::WriteOnly));
        good.write("#!/bin/sh\necho '2.1.286 (Claude Code)'\n");
        good.close();
        QVERIFY(good.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        QObject context;
        bool done = false;
        QStringList models;
        QString error;
        probe("claude_cli", ok, &context, [&](const QStringList& m, const QString& e) {
            done = true;
            models = m;
            error = e;
        });
        QTRY_VERIFY(done);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(models, fallback_models("claude_cli"));

        // A CLI that fails says so, with what it printed.
        const QString bad = dir.filePath("codex");
        QFile broken(bad);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("#!/bin/sh\necho 'env: node: No such file or directory' >&2\nexit 127\n");
        broken.close();
        QVERIFY(broken.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        done = false;
        probe("codex_cli", bad, &context, [&](const QStringList& m, const QString& e) {
            done = true;
            models = m;
            error = e;
        });
        QTRY_VERIFY(done);
        QVERIFY(models.isEmpty());
        QVERIFY2(error.contains("127") && error.contains("node"), qPrintable(error));

        // A CLI that is not there answers at once, still through the callback.
        done = false;
        probe("codex_cli", "/no/such/codex", &context, [&](const QStringList&, const QString& e) {
            done = true;
            error = e;
        });
        QVERIFY(!done);
        QTRY_VERIFY(done);
        QVERIFY(error.contains("/no/such/codex"));
    }

    void runner_stops_a_cli_that_does_not_answer() {
        QTemporaryDir dir;
        const QString path = dir.filePath("codex");
        QFile script(path);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nsleep 30\n");
        script.close();
        QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const Executable exe = find_executable("codex_cli", path);
        QVERIFY(exe.error.isEmpty());
        const RunResult r = run_blocking(exe, {}, "hi", 500, dir.path());
        QVERIFY(r.started);
        QVERIFY(r.timed_out);
    }

    void timeout_stops_what_the_cli_started() {
        // Like Codex's node launcher, a CLI that runs its real work as a child process.
        QTemporaryDir dir;
        const QString path = dir.filePath("codex");
        const QString pid_file = dir.filePath("child.pid");
        QFile script(path);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nsleep 30 &\necho $! > \"$1\"\nwait\n");
        script.close();
        QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const Executable exe = find_executable("codex_cli", path);
        const RunResult r = run_blocking(exe, {pid_file}, {}, 1000, dir.path());
        QVERIFY(r.timed_out);

        QFile f(pid_file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const pid_t child = static_cast<pid_t>(f.readAll().trimmed().toLongLong());
        QVERIFY(child > 0);
        QTRY_VERIFY_WITH_TIMEOUT(::kill(child, 0) != 0, 3000);
    }

    void each_round_runs_in_a_new_empty_folder() {
        QTemporaryDir dir;
        const QString path = dir.filePath("claude");
        QFile script(path);
        QVERIFY(script.open(QIODevice::WriteOnly));
        // Answers with its working folder and how many entries it holds.
        script.write("#!/bin/sh\ncat > /dev/null\nn=$(ls -A | wc -l | tr -d ' ')\n"
                     "printf '{\"type\":\"result\",\"is_error\":false,\"result\":\"%s %s\"}\\n' \"$PWD\" \"$n\"\n");
        script.close();
        QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const Answer first = ask("claude_cli", "default", path, "hi");
        const Answer second = ask("claude_cli", "default", path, "hi");
        QVERIFY2(first.ok && second.ok, qPrintable(first.error + second.error));
        QVERIFY2(first.text.endsWith(" 0"), qPrintable(first.text));
        QVERIFY(first.text.contains("fincept-llm-cli-"));
        QVERIFY(first.text != second.text);
        QVERIFY(!QFileInfo::exists(first.text.section(' ', 0, 0))); // removed afterwards
    }
#endif
};

QTEST_GUILESS_MAIN(TestKrxLocalCliLlm)
#include "tst_krx_local_cli_llm.moc"
