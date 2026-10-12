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

} // namespace fincept::krx_quant
