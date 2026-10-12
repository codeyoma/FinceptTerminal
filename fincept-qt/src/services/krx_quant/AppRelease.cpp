#include "services/krx_quant/AppRelease.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSysInfo>

namespace fincept::krx_quant {

std::optional<BuildTag> parse_build_tag(const QString& tag) {
    static const QRegularExpression re(QStringLiteral("^custom-v(\\d+)\\.(\\d+)\\.(\\d+)-krx\\.(\\d+)$"));
    const auto m = re.match(tag);
    if (!m.hasMatch())
        return std::nullopt;
    return BuildTag{m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt(), m.captured(4).toInt()};
}

std::optional<AppRelease> newest_release(const QJsonDocument& releases, const QString& running, const QString& suffix) {
    const auto current = parse_build_tag(running);
    if (!current || !releases.isArray())
        return std::nullopt;
    static const QRegularExpression sha256(QStringLiteral("^sha256:([0-9a-fA-F]{64})$"));
    std::optional<BuildTag> best_tag;
    std::optional<AppRelease> best;
    for (const auto& value : releases.array()) {
        const QJsonObject release = value.toObject();
        const QString tag = release.value("tag_name").toString();
        const auto build = parse_build_tag(tag);
        if (!build || release.value("draft").toBool() || *build <= *current || (best_tag && *build <= *best_tag))
            continue;
        const QString name = QStringLiteral("FinceptTerminal-%1-%2").arg(tag, suffix);
        for (const auto& a : release.value("assets").toArray()) {
            const QJsonObject asset = a.toObject();
            const auto digest = sha256.match(asset.value("digest").toString());
            if (asset.value("name").toString() != name || !digest.hasMatch())
                continue;
            best_tag = build;
            best = AppRelease{tag, QUrl(asset.value("browser_download_url").toString()), digest.captured(1).toLower(),
                              asset.value("size").toInteger(), QUrl(release.value("html_url").toString())};
            break;
        }
    }
    return best;
}

QString installer_suffix() {
    const QString arch = QSysInfo::buildCpuArchitecture(); // the build's, not the machine's
#if defined(Q_OS_MACOS)
    return arch.contains(QLatin1String("arm64")) ? QStringLiteral("macos-arm64.dmg") : QString();
#elif defined(Q_OS_WIN)
    return arch.contains(QLatin1String("x86_64")) ? QStringLiteral("windows-x64-setup.exe") : QString();
#else
    Q_UNUSED(arch);
    return {};
#endif
}

namespace {
/// A Windows path with forward slashes and no trailing one. Not QDir::fromNativeSeparators, which
/// leaves backslashes alone off Windows (where this is unit-tested).
QString windows_path(QString path) {
    return QDir::cleanPath(path.replace(QLatin1Char('\\'), QLatin1Char('/')));
}
} // namespace

QString windows_user_install_dir(const QString& local_app_data) {
    return windows_path(local_app_data + QStringLiteral("/Programs/FinceptTerminal"));
}

UpdateMode windows_update_mode(const QString& app_dir, const QString& local_app_data, bool has_maintenance_tool) {
    // Without the maintenance tool, or without a user folder to install into.
    if (!has_maintenance_tool || local_app_data.trimmed().isEmpty())
        return UpdateMode::Manual;
    const QString here = windows_path(app_dir);
    const QString user_dir = windows_user_install_dir(local_app_data);
    // Windows paths compare without case.
    if (here.compare(user_dir, Qt::CaseInsensitive) == 0)
        return UpdateMode::SwapFolder;
    return UpdateMode::MoveToUserFolder;
}

QString update_asset_suffix(UpdateMode mode) {
    switch (mode) {
        case UpdateMode::SwapBundle:
            return QStringLiteral("macos-arm64.dmg");
        case UpdateMode::SwapFolder:
            return QStringLiteral("windows-x64.zip");
        case UpdateMode::MoveToUserFolder:
            return QStringLiteral("windows-x64-setup.exe");
        case UpdateMode::Manual:
            break;
    }
    return installer_suffix(); // this platform's installer, to open by hand
}

} // namespace fincept::krx_quant
