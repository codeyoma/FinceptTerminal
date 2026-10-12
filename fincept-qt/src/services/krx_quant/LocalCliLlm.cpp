#include "services/krx_quant/LocalCliLlm.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>

#include <algorithm>

namespace fincept::krx_quant::cli_llm {

namespace {

// Replaces Claude Code's own coding-agent system prompt. Fincept's instructions, tool catalog
// and conversation arrive in the user message, exactly as on the Fincept LLM path.
const QString kClaudeSystemPrompt =
    QStringLiteral("You are the language model behind the assistant in Fincept Terminal, a desktop financial "
                   "research application. The user message holds the assistant's full instructions, the "
                   "conversation so far and the tools it can call. You have no tools of your own. When the "
                   "instructions tell you to call a tool, write the <tool_call> block they describe and nothing "
                   "else; Fincept runs it and calls you again with the result.");

// Codex features that give the model tools of its own: shell, app connectors, browser and
// computer use, image generation, sub-agents, skills, plugins, memories, hooks and the like.
// Turned off with `-c features.<name>=false` rather than --disable, which fails on a name it
// does not know: a Codex release that renames or drops one still runs.
const char* const kCodexFeaturesOff[] = {
    "shell_tool",
    "unified_exec",
    "apps",
    "browser_use",
    "browser_use_external",
    "computer_use",
    "code_mode_host",
    "image_generation",
    "multi_agent",
    "goals",
    "hooks",
    "memories",
    "plugins",
    "remote_plugin",
    "skill_search",
    "skill_mcp_dependency_install",
    "sleep_tool",
    "tool_suggest",
    "view_image",
    "in_app_browser",
    "in_app_local_automation",
    "workspace_dependencies",
    "worktrees",
    "realtime_conversation",
    "tool_call_mcp_elicitation",
};

QString provider_id(const QString& provider) {
    return provider.trimmed().toLower();
}

/// The last JSON object on its own line in ``out``, or the whole of ``out`` if it is one.
QJsonObject last_json_object(const QByteArray& out) {
    const QJsonDocument whole = QJsonDocument::fromJson(out.trimmed());
    if (whole.isObject())
        return whole.object();
    const QList<QByteArray> lines = out.split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QJsonDocument doc = QJsonDocument::fromJson(it->trimmed());
        if (doc.isObject())
            return doc.object();
    }
    return {};
}

/// The readable part of an error message that may itself be an API error envelope.
QString unwrap_error(const QString& message) {
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject())
        return message.trimmed();
    const QJsonObject root = doc.object();
    const QString inner = root.value("error").toObject().value("message").toString();
    if (!inner.isEmpty())
        return inner;
    const QString top = root.value("message").toString();
    return top.isEmpty() ? message.trimmed() : top;
}

/// What the process said on stderr, shortened for a chat bubble, or the exit code.
QString process_failure(const QByteArray& err, int exit_code) {
    QString text = QString::fromUtf8(err).trimmed();
    if (text.size() > 600)
        text = QStringLiteral("…") + text.right(600);
    if (!text.isEmpty())
        return text;
    return QStringLiteral("exited with code %1 and printed nothing").arg(exit_code);
}

Answer parse_claude(const QByteArray& out, const QByteArray& err, int exit_code) {
    Answer a;
    const QJsonObject result = last_json_object(out);
    if (result.value("type").toString() != QLatin1String("result")) {
        a.error = QStringLiteral("Claude Code CLI: ") + process_failure(err, exit_code);
        return a;
    }
    const QJsonObject usage = result.value("usage").toObject();
    a.input_tokens = usage.value("input_tokens").toInt() + usage.value("cache_creation_input_tokens").toInt() +
                     usage.value("cache_read_input_tokens").toInt();
    a.output_tokens = usage.value("output_tokens").toInt();

    const QString text = result.value("result").toString();
    if (result.value("is_error").toBool()) {
        a.error = QStringLiteral("Claude Code CLI: ") +
                  (text.isEmpty() ? result.value("subtype").toString() : text.trimmed());
        return a;
    }
    if (text.trimmed().isEmpty()) {
        a.error = QStringLiteral("Claude Code CLI returned an empty answer");
        return a;
    }
    a.ok = true;
    a.text = text;
    return a;
}

Answer parse_codex(const QByteArray& out, const QByteArray& err, int exit_code) {
    Answer a;
    QString reply;
    QString failure;
    bool saw_event = false;
    for (const QByteArray& line : out.split('\n')) {
        const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed());
        if (!doc.isObject())
            continue;
        saw_event = true;
        const QJsonObject ev = doc.object();
        const QString type = ev.value("type").toString();
        if (type == QLatin1String("item.completed")) {
            const QJsonObject item = ev.value("item").toObject();
            // The last agent message is the answer; earlier ones are commentary. An item of
            // type "error" is a warning (e.g. unknown model metadata), not a failure.
            if (item.value("type").toString() == QLatin1String("agent_message"))
                reply = item.value("text").toString();
        } else if (type == QLatin1String("turn.completed")) {
            const QJsonObject usage = ev.value("usage").toObject();
            a.input_tokens = usage.value("input_tokens").toInt();
            a.output_tokens = usage.value("output_tokens").toInt();
        } else if (type == QLatin1String("turn.failed")) {
            failure = unwrap_error(ev.value("error").toObject().value("message").toString());
        } else if (type == QLatin1String("error") && failure.isEmpty()) {
            failure = unwrap_error(ev.value("message").toString());
        }
    }

    if (!failure.isEmpty()) {
        a.error = QStringLiteral("Codex CLI: ") + failure;
        return a;
    }
    if (!saw_event) {
        a.error = QStringLiteral("Codex CLI: ") + process_failure(err, exit_code);
        return a;
    }
    if (reply.trimmed().isEmpty()) {
        a.error = exit_code != 0 ? QStringLiteral("Codex CLI: ") + process_failure(err, exit_code)
                                 : QStringLiteral("Codex CLI returned an empty answer");
        return a;
    }
    a.ok = true;
    a.text = reply;
    return a;
}

} // namespace

bool is_cli_provider(const QString& provider) {
    const QString p = provider_id(provider);
    return p == kClaude || p == kCodex;
}

QStringList providers() {
    return {kClaude, kCodex};
}

QString display_name(const QString& provider) {
    const QString p = provider_id(provider);
    if (p == kClaude)
        return QStringLiteral("Claude Code CLI (local)");
    if (p == kCodex)
        return QStringLiteral("Codex CLI (local)");
    return provider;
}

QString program(const QString& provider) {
    const QString p = provider_id(provider);
    if (p == kClaude)
        return QStringLiteral("claude");
    if (p == kCodex)
        return QStringLiteral("codex");
    return {};
}

QStringList fallback_models(const QString& provider) {
    const QString p = provider_id(provider);
    if (p == kClaude)
        return {kDefaultModel, QStringLiteral("opus"), QStringLiteral("sonnet"), QStringLiteral("haiku")};
    if (p == kCodex)
        return {kDefaultModel};
    return {};
}

QStringList arguments(const QString& provider, const QString& model, const QString& workdir) {
    const QString p = provider_id(provider);
    const QString m = model.trimmed();
    const bool pick_model = !m.isEmpty() && m.compare(kDefaultModel, Qt::CaseInsensitive) != 0;
    QStringList args;
    if (p == kClaude) {
        // --tools takes a list; the empty string disables every built-in tool. It must be
        // followed by another option, never by a bare word it would swallow as a tool name.
        args << "-p" << "--safe-mode" << "--tools" << "" << "--no-session-persistence" << "--output-format"
             << "json" << "--system-prompt" << kClaudeSystemPrompt;
        if (pick_model)
            args << "--model" << m;
    } else if (p == kCodex) {
        args << "exec" << "--ignore-user-config" << "--ignore-rules" << "--ephemeral" << "--skip-git-repo-check"
             << "--sandbox" << "read-only" << "--json" << "--cd" << workdir;
        // No web search: it defaults on and would answer live-data questions around Fincept's tools.
        args << "-c" << "web_search=\"disabled\"";
        for (const char* feature : kCodexFeaturesOff)
            args << "-c" << QStringLiteral("features.%1=false").arg(QLatin1String(feature));
        if (pick_model)
            args << "--model" << m;
        args << "-"; // read the prompt from stdin
    }
    return args;
}

QStringList version_arguments() {
    return {QStringLiteral("--version")};
}

Answer parse_output(const QString& provider, const QByteArray& out, const QByteArray& err, int exit_code) {
    const QString p = provider_id(provider);
    if (p == kClaude)
        return parse_claude(out, err, exit_code);
    if (p == kCodex)
        return parse_codex(out, err, exit_code);
    Answer a;
    a.error = QStringLiteral("Not a local CLI provider: ") + provider;
    return a;
}

QStringList codex_cached_models(const QByteArray& json) {
    struct Entry {
        QString slug;
        int priority;
    };
    QList<Entry> entries;
    const QJsonArray models = QJsonDocument::fromJson(json).object().value("models").toArray();
    for (const auto& v : models) {
        const QJsonObject m = v.toObject();
        const QString slug = m.value("slug").toString();
        if (slug.isEmpty() || m.value("visibility").toString() == QLatin1String("hide"))
            continue;
        entries.append({slug, m.value("priority").toInt(1000)});
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& a, const Entry& b) { return a.priority < b.priority; });
    QStringList out;
    for (const auto& e : entries)
        out << e.slug;
    return out;
}

} // namespace fincept::krx_quant::cli_llm
