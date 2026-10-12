#pragma once

#include "services/krx_quant/LocalCliLlm.h"

#include <QByteArray>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>

/// Runs the local CLI LLM providers (codeyoma/krx-quant#96, custom/README.md): finds the CLI,
/// runs one round with the prompt on stdin, and checks from Settings that it runs. Qt Core only.
namespace fincept::krx_quant::cli_llm {

/// How to start a provider's CLI.
struct Executable {
    QString path;            ///< the CLI as found
    QString program;         ///< what is started: path, node for an npm shim, or cmd.exe
    QStringList prefix_args; ///< arguments before the CLI's own (the shim's node script)
    bool via_cmd = false;    ///< a Windows .cmd/.bat started as cmd.exe with cmd_arguments()
    QString error;           ///< why it was not found; empty when found
};

/// The node script an npm .cmd shim runs (its "%dp0%\..." or "%~dp0\..." path), in the folder
/// of ``shim_path``. Empty when ``shim_text`` is not such a shim. On Windows
/// npm installs `codex` and `claude` as these shims; running the script with node directly avoids
/// cmd.exe and its quoting rules.
QString npm_shim_script(const QString& shim_path, const QByteArray& shim_text);

/// cmd.exe's native argument string for running the batch file ``path`` with ``args``:
/// /d /s /c "<command line>". With /s, cmd.exe drops only the outer quotes, so a path or an
/// argument with spaces keeps its own.
QString cmd_arguments(const QString& path, const QStringList& args);

/// The CLI for ``provider``: ``override_path`` when set (the "CLI path" field), else the first
/// match on search_path().
Executable find_executable(const QString& provider, const QString& override_path);

/// Where to look for a CLI and the programs it needs (Codex runs on node): the app's own PATH,
/// the login shell's PATH and the usual install folders. A GUI app on macOS starts with a minimal
/// PATH, so the app's own is not enough. Computed once.
QStringList search_path();

/// The environment for running the CLI at ``path``: PATH is its folder, then search_path().
QProcessEnvironment environment_for(const QString& path);

/// One process run.
struct RunResult {
    bool started = false;
    bool timed_out = false;
    int exit_code = -1;
    QByteArray out;
    QByteArray err;
    QString error; ///< why it did not start or finish
};

/// Runs ``exe`` with ``args`` in ``workdir``, writes ``input`` to its stdin, and waits. On a
/// timeout it stops the CLI and every process it started (Codex's node launcher runs the native
/// binary as a child): a process group on macOS and Linux, a job object on Windows. Blocking:
/// call from a worker thread.
RunResult run_blocking(const Executable& exe, const QStringList& args, const QByteArray& input, int timeout_ms,
                       const QString& workdir);

/// One LLM round: find the CLI, run it on ``prompt`` in a new empty temporary folder (removed
/// afterwards, so no project file there is read as instructions), read its answer. Blocking.
Answer ask(const QString& provider, const QString& model, const QString& override_path, const QString& prompt);

/// Checks that the CLI runs (its --version) and lists its models, without blocking. ``done``
/// runs on ``context``'s thread with the models, or an error.
void probe(const QString& provider, const QString& override_path, QObject* context,
           std::function<void(const QStringList& models, const QString& error)> done);

} // namespace fincept::krx_quant::cli_llm
