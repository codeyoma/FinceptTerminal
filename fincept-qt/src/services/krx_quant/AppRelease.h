#pragma once

#include <QJsonDocument>
#include <QString>
#include <QUrl>

#include <compare>
#include <optional>

/// Which personal build is newer, and the newest one on the fork's Releases page, for the
/// app's one-click update (codeyoma/krx-quant#92, custom/README.md). Qt Core only,
/// unit-tested on its own (tests/tst_krx_app_release.cpp).
namespace fincept::krx_quant {

/// A personal build's tag, custom-v<major>.<minor>.<patch>-krx.<n>: the upstream release it
/// is built on, then the fork's build number. Builds order by these, in this order.
struct BuildTag {
    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;
    auto operator<=>(const BuildTag&) const = default;
};

/// ``tag`` as a personal build's tag; none for anything else (a local build has no tag).
std::optional<BuildTag> parse_build_tag(const QString& tag);

/// A personal build to update to, and its installer for this platform.
struct AppRelease {
    QString tag;
    QUrl installer; ///< its download URL
    QString sha256; ///< lowercase hex, from GitHub's digest of the installer
    qint64 size = 0;
    QUrl page; ///< the release's page
};

/// The newest release in ``releases`` (GitHub's list-releases answer) newer than the running
/// build's tag ``running``, that is not a draft and has an installer named
/// FinceptTerminal-<tag>-<suffix> with a sha256 digest. None for a build without a personal
/// tag, or when there is none newer.
std::optional<AppRelease> newest_release(const QJsonDocument& releases, const QString& running, const QString& suffix);

/// This platform's installer suffix: "macos-arm64.dmg", "windows-x64-setup.exe"; empty where
/// the fork builds none.
QString installer_suffix();

/// How an installed personal build updates itself (codeyoma/krx-quant#130).
enum class UpdateMode {
    SwapBundle,       ///< macOS: the installed .app is swapped for the one in the new DMG
    SwapFolder,       ///< Windows, installed in the user's folder: the folder is swapped for the new ZIP's tree
    MoveToUserFolder, ///< Windows, installed elsewhere: the new setup.exe goes into the user's folder, the old install
                      ///< is removed
    Manual,           ///< neither possible: the installer is opened for a manual install
};

/// Windows: the per-user install folder, <%LOCALAPPDATA%>/Programs/FinceptTerminal. New
/// installs default to it, and updates move other installs there: it needs no administrator.
QString windows_user_install_dir(const QString& local_app_data);

/// Windows: how the build installed at ``app_dir`` updates. Manual without the installer's
/// maintenance tool next to it (not installed by setup.exe) or without %LOCALAPPDATA%,
/// SwapFolder in the per-user folder, MoveToUserFolder anywhere else (Program Files).
UpdateMode windows_update_mode(const QString& app_dir, const QString& local_app_data, bool has_maintenance_tool);

/// The release asset a mode downloads: "macos-arm64.dmg", "windows-x64.zip" (the installed
/// tree without the installer's own files), "windows-x64-setup.exe" (MoveToUserFolder), or this
/// platform's installer (Manual).
QString update_asset_suffix(UpdateMode mode);

} // namespace fincept::krx_quant
