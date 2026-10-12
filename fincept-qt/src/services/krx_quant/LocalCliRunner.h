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
    QString program;         ///< what is started: path, or cmd.exe for a Windows .cmd shim
    QStringList prefix_args; ///< arguments before the CLI's own (cmd.exe's /d /c path)
    QString error;           ///< why it was not found; empty when found
};

/// The CLI for ``provider``: ``override_path`` when set (the "CLI path" field), else the first
/// match on search_path().
Executable find_executable(const QString& provider, const QString& override_path);

/// Where to look for a CLI and the programs it needs (Codex runs on node): the app's own PATH,
/// the login shell's PATH and the usual install folders. A GUI app on macOS starts with a minimal
/// PATH, so the app's own is not enough. Computed once.
QStringList search_path();

/// The environment for running the CLI at ``path``: PATH is its folder, then search_path().
QProcessEnvironment environment_for(const QString& path);

/// The empty folder a CLI runs in, so no project file there is read as instructions.
QString work_dir();

/// One process run.
struct RunResult {
    bool started = false;
    bool timed_out = false;
    int exit_code = -1;
    QByteArray out;
    QByteArray err;
    QString error; ///< why it did not start or finish
};

/// Runs ``exe`` with ``args``, writes ``input`` to its stdin, and waits. Blocking: call from a
/// worker thread.
RunResult run_blocking(const Executable& exe, const QStringList& args, const QByteArray& input, int timeout_ms);

/// One LLM round: find the CLI, run it on ``prompt``, read its answer. Blocking.
Answer ask(const QString& provider, const QString& model, const QString& override_path, const QString& prompt);

/// Checks that the CLI runs (its --version) and lists its models, without blocking. ``done``
/// runs on ``context``'s thread with the models, or an error.
void probe(const QString& provider, const QString& override_path, QObject* context,
           std::function<void(const QStringList& models, const QString& error)> done);

} // namespace fincept::krx_quant::cli_llm
