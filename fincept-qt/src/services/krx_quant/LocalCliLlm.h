#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

/// The local CLI LLM providers (codeyoma/krx-quant#96, custom/README.md): Fincept's LLM rounds
/// run through the Claude Code or Codex CLI the user is logged in to, instead of an API key or
/// Fincept credits. This file is the pure part: which providers these are, the arguments for one
/// round, and reading a round's output. Qt Core only, unit-tested on its own
/// (tests/tst_krx_local_cli_llm.cpp). LocalCliRunner runs the process.
///
/// A CLI never gets tools of its own. LlmService drives it with the same flat-prompt loop as
/// the Fincept LLM (LlmFinceptAsync.cpp): Fincept's tool catalog goes into the prompt, the model
/// answers with <tool_call> blocks, and Fincept runs the tools.
namespace fincept::krx_quant::cli_llm {

inline const QString kClaude = QStringLiteral("claude_cli");
inline const QString kCodex = QStringLiteral("codex_cli");

/// The model that leaves the choice to the CLI: no model argument is passed.
inline const QString kDefaultModel = QStringLiteral("default");

/// Budget for the whole prompt of one round. The loop re-sends the full transcript every round,
/// so this bounds each round's size and cost; the CLIs' own context is larger.
inline constexpr int kMaxPromptChars = 200000;

/// Wall-clock limit for one round, process start to exit.
inline constexpr int kRoundTimeoutMs = 300000;

/// claude_cli or codex_cli, any case.
bool is_cli_provider(const QString& provider);

/// The provider ids, in the order the settings list shows them.
QStringList providers();

/// The name shown in Settings → LLM Config.
QString display_name(const QString& provider);

/// The executable's base name: "claude" or "codex".
QString program(const QString& provider);

/// The models offered before a fetch. Claude takes its CLI aliases; Codex lists its own models
/// on fetch (codex_cached_models).
QStringList fallback_models(const QString& provider);

/// The arguments for one non-interactive round; the prompt goes on stdin. ``workdir`` is the
/// empty folder the CLI runs in. An empty model or kDefaultModel passes no model.
///  - Claude: print mode, safe mode (no CLAUDE.md, hooks, MCP servers, skills, plugins), no
///    built-in tools, no saved session, one JSON result.
///  - Codex: no user config.toml or rules, no saved session, read-only sandbox, no web search,
///    and its tool-giving features off (shell, app connectors, browser and computer use, image
///    generation, sub-agents, skills, plugins, memories, hooks), JSONL events.
QStringList arguments(const QString& provider, const QString& model, const QString& workdir);

/// The arguments that print the CLI's version, to check that it runs.
QStringList version_arguments();

/// One round's result.
struct Answer {
    bool ok = false;
    QString text;  ///< the model's reply
    QString error; ///< why it failed, for the chat
    int input_tokens = 0;
    int output_tokens = 0;
};

/// Reads one round's stdout and stderr and the exit code.
Answer parse_output(const QString& provider, const QByteArray& out, const QByteArray& err, int exit_code);

/// Codex's selectable models from its models_cache.json (CODEX_HOME), by its priority. Hidden
/// models are left out. Empty when the file is not that shape.
QStringList codex_cached_models(const QByteArray& json);

} // namespace fincept::krx_quant::cli_llm
