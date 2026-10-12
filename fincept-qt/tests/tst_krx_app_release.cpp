// Unit tests for src/services/krx_quant/AppRelease.h
//
// A personal build updates itself from the fork's own Releases (codeyoma/krx-quant#92). It
// knows its tag (custom-v<upstream version>-krx.<n>), reads GitHub's list of releases, and
// offers the newest newer build that has this platform's installer with a sha256 digest.
// These tests pin which tags count, how builds are ordered and which release is offered.

#include "services/krx_quant/AppRelease.h"

#include <QJsonDocument>
#include <QTest>

using namespace fincept::krx_quant;

namespace {

const char* kMac = "macos-arm64.dmg";
const char* kWindows = "windows-x64-setup.exe";

// GitHub's list-releases answer, trimmed to what is read; latest first, as GitHub sends it.
// URLs escape their slashes (JSON's \/): moc reads // inside a raw string as a comment.
const char* kReleases = R"([
  {"tag_name": "custom-v4.5.0-krx.7", "draft": true, "prerelease": true,
   "html_url": "https:\/\/github.com/codeyoma/FinceptTerminal/releases/tag/custom-v4.5.0-krx.7",
   "assets": [{"name": "FinceptTerminal-custom-v4.5.0-krx.7-macos-arm64.dmg", "size": 9,
               "digest": "sha256:7777777777777777777777777777777777777777777777777777777777777777",
               "browser_download_url": "https:\/\/example.test/krx.7.dmg"}]},
  {"tag_name": "custom-v4.5.0-krx.6", "draft": false, "prerelease": true,
   "html_url": "https:\/\/github.com/codeyoma/FinceptTerminal/releases/tag/custom-v4.5.0-krx.6",
   "assets": [{"name": "FinceptTerminal-custom-v4.5.0-krx.6-windows-x64-setup.exe", "size": 6,
               "digest": "sha256:6666666666666666666666666666666666666666666666666666666666666666",
               "browser_download_url": "https:\/\/example.test/krx.6.exe"}]},
  {"tag_name": "custom-v4.5.0-krx.9-rc", "draft": false, "prerelease": true,
   "html_url": "https:\/\/github.com/codeyoma/FinceptTerminal/releases/tag/custom-v4.5.0-krx.9-rc",
   "assets": [{"name": "FinceptTerminal-custom-v4.5.0-krx.9-rc-macos-arm64.dmg", "size": 9,
               "digest": "sha256:9999999999999999999999999999999999999999999999999999999999999999",
               "browser_download_url": "https:\/\/example.test/krx.9-rc.dmg"}]},
  {"tag_name": "custom-v4.5.0-krx.8", "draft": false, "prerelease": true,
   "html_url": "https:\/\/github.com/codeyoma/FinceptTerminal/releases/tag/custom-v4.5.0-krx.8",
   "assets": [{"name": "FinceptTerminal-custom-v4.5.0-krx.8-macos-arm64.dmg", "size": 8,
               "digest": "sha1:8888888888888888888888888888888888888888",
               "browser_download_url": "https:\/\/example.test/krx.8.dmg"},
              {"name": "FinceptTerminal-custom-v4.5.0-krx.8-windows-x64-setup.exe", "size": 8,
               "digest": "sha256:8888",
               "browser_download_url": "https:\/\/example.test/krx.8.exe"}]},
  {"tag_name": "custom-v4.5.0-krx.5", "draft": false, "prerelease": true,
   "html_url": "https:\/\/github.com/codeyoma/FinceptTerminal/releases/tag/custom-v4.5.0-krx.5",
   "assets": [{"name": "FinceptTerminal-custom-v4.5.0-krx.5-macos-arm64.dmg", "size": 5,
               "digest": null, "browser_download_url": "https:\/\/example.test/krx.5.dmg"}]},
  {"tag_name": "v4.6.0", "draft": false, "prerelease": false,
   "html_url": "https:\/\/github.com/codeyoma/FinceptTerminal/releases/tag/v4.6.0",
   "assets": [{"name": "FinceptTerminal-4.6.0-macos-arm64.dmg", "size": 1,
               "digest": "sha256:1111111111111111111111111111111111111111111111111111111111111111",
               "browser_download_url": "https:\/\/example.test/official.dmg"}]},
  {"tag_name": "custom-v4.5.0-krx.4", "draft": false, "prerelease": true,
   "html_url": "https:\/\/github.com/codeyoma/FinceptTerminal/releases/tag/custom-v4.5.0-krx.4",
   "assets": [{"name": "FinceptTerminal-custom-v4.5.0-krx.4-macos-arm64.dmg", "size": 263925072,
               "digest": "sha256:05DF925F2FFBA75BF44B0AF9C5094D1BAD209414CCF439FF28EC1C6F0A6CB4FE",
               "browser_download_url": "https:\/\/example.test/krx.4.dmg"},
              {"name": "FinceptTerminal-custom-v4.5.0-krx.4-windows-x64-setup.exe", "size": 172669991,
               "digest": "sha256:5e3ae8be4e1f2613fdef16adbb7379b14c2f5e4a6ee9b5b64d19a958e31985c1",
               "browser_download_url": "https:\/\/example.test/krx.4.exe"}]},
  {"tag_name": "custom-v4.5.0-krx.3", "draft": false, "prerelease": true,
   "html_url": "https:\/\/github.com/codeyoma/FinceptTerminal/releases/tag/custom-v4.5.0-krx.3",
   "assets": [{"name": "FinceptTerminal-custom-v4.5.0-krx.3-macos-arm64.dmg", "size": 3,
               "digest": "sha256:3333333333333333333333333333333333333333333333333333333333333333",
               "browser_download_url": "https:\/\/example.test/krx.3.dmg"}]}
])";

// A release with the Windows update ZIP next to the installer, and one from before the ZIP.
const char* kWithZip = R"([
  {"tag_name": "custom-v4.5.0-krx.10", "draft": false, "prerelease": true,
   "html_url": "https:\/\/github.com/codeyoma/FinceptTerminal/releases/tag/custom-v4.5.0-krx.10",
   "assets": [{"name": "FinceptTerminal-custom-v4.5.0-krx.10-windows-x64-setup.exe", "size": 10,
               "digest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
               "browser_download_url": "https:\/\/example.test/krx.10.exe"}]},
  {"tag_name": "custom-v4.5.0-krx.9", "draft": false, "prerelease": true,
   "html_url": "https:\/\/github.com/codeyoma/FinceptTerminal/releases/tag/custom-v4.5.0-krx.9",
   "assets": [{"name": "FinceptTerminal-custom-v4.5.0-krx.9-windows-x64-setup.exe", "size": 9,
               "digest": "sha256:8888888888888888888888888888888888888888888888888888888888888888",
               "browser_download_url": "https:\/\/example.test/krx.9.exe"},
              {"name": "FinceptTerminal-custom-v4.5.0-krx.9-windows-x64.zip", "size": 9,
               "digest": "sha256:9999999999999999999999999999999999999999999999999999999999999999",
               "browser_download_url": "https:\/\/example.test/krx.9.zip"}]}
])";

QJsonDocument releases() {
    return QJsonDocument::fromJson(kReleases);
}

} // namespace

class TstKrxAppRelease : public QObject {
    Q_OBJECT

  private slots:
    void a_personal_build_tag_reads_as_its_upstream_version_and_build() {
        const auto tag = parse_build_tag("custom-v4.5.0-krx.4");
        QVERIFY(tag);
        QCOMPARE(tag->major, 4);
        QCOMPARE(tag->minor, 5);
        QCOMPARE(tag->patch, 0);
        QCOMPARE(tag->build, 4);
    }

    void anything_else_is_not_a_personal_build() {
        QVERIFY(!parse_build_tag(""));       // a local build
        QVERIFY(!parse_build_tag("v4.6.0")); // an official release
        QVERIFY(!parse_build_tag("custom-v4.5.0"));
        QVERIFY(!parse_build_tag("custom-v4.5.0-krx.x"));
        QVERIFY(!parse_build_tag("custom-v4.5-krx.1"));
    }

    void builds_order_by_upstream_version_then_build_number() {
        QVERIFY(*parse_build_tag("custom-v4.5.0-krx.9") < *parse_build_tag("custom-v4.5.0-krx.10"));
        QVERIFY(*parse_build_tag("custom-v4.5.0-krx.9") < *parse_build_tag("custom-v4.6.0-krx.1"));
        QVERIFY(*parse_build_tag("custom-v4.5.1-krx.1") < *parse_build_tag("custom-v4.6.0-krx.1"));
    }

    void the_newest_newer_build_with_this_platforms_installer_is_offered() {
        // krx.9-rc is not a personal build's tag; krx.8's digest is not sha256; krx.7 is a draft;
        // krx.6 has no macOS installer; krx.5 has no digest; v4.6.0 is official.
        const auto found = newest_release(releases(), "custom-v4.5.0-krx.3", kMac);
        QVERIFY(found);
        QCOMPARE(found->tag, QStringLiteral("custom-v4.5.0-krx.4"));
        QCOMPARE(found->installer, QUrl("https://example.test/krx.4.dmg"));
        QCOMPARE(found->sha256, QStringLiteral("05df925f2ffba75bf44b0af9c5094d1bad209414ccf439ff28ec1c6f0a6cb4fe"));
        QCOMPARE(found->size, 263925072);
        QCOMPARE(found->page, QUrl("https://github.com/codeyoma/FinceptTerminal/releases/tag/custom-v4.5.0-krx.4"));
    }

    void each_platform_looks_for_its_own_installer() {
        // krx.8's Windows digest is cut short.
        const auto found = newest_release(releases(), "custom-v4.5.0-krx.3", kWindows);
        QVERIFY(found);
        QCOMPARE(found->tag, QStringLiteral("custom-v4.5.0-krx.6"));
        QCOMPARE(found->installer, QUrl("https://example.test/krx.6.exe"));
    }

    void the_running_build_or_an_older_one_is_not_offered() {
        QVERIFY(!newest_release(releases(), "custom-v4.5.0-krx.4", kMac));
        QVERIFY(!newest_release(releases(), "custom-v4.5.0-krx.6", kWindows));
        QVERIFY(!newest_release(releases(), "custom-v4.6.0-krx.1", kWindows));
    }

    void a_build_without_a_tag_is_never_offered_an_update() {
        QVERIFY(!newest_release(releases(), "", kMac));
        QVERIFY(!newest_release(releases(), "4.5.0", kMac));
    }

    void windows_updates_by_swapping_the_folder_only_in_the_users_folder() {
        const QString local = QStringLiteral("C:\\Users\\A B\\AppData\\Local");
        QCOMPARE(windows_user_install_dir(local),
                 QStringLiteral("C:/Users/A B/AppData/Local/Programs/FinceptTerminal"));
        // Windows paths compare without case and with either separator.
        QCOMPARE(windows_update_mode("C:\\Users\\A B\\AppData\\Local\\Programs\\FinceptTerminal", local, true),
                 UpdateMode::SwapFolder);
        QCOMPARE(windows_update_mode("c:/users/a b/appdata/local/programs/finceptterminal/", local, true),
                 UpdateMode::SwapFolder);
        // Anywhere else, Program Files included, the next update moves it there.
        QCOMPARE(windows_update_mode("C:\\Program Files\\FinceptTerminal", local, true), UpdateMode::MoveToUserFolder);
        QCOMPARE(windows_update_mode("D:\\Apps\\FinceptTerminal", local, true), UpdateMode::MoveToUserFolder);
        // Not installed by setup.exe (no maintenance tool): a manual install.
        QCOMPARE(windows_update_mode("C:\\Users\\A B\\AppData\\Local\\Programs\\FinceptTerminal", local, false),
                 UpdateMode::Manual);
        // No %LOCALAPPDATA%: never a swap.
        QCOMPARE(windows_update_mode("/Programs/FinceptTerminal", QString(), true), UpdateMode::MoveToUserFolder);
    }

    void each_update_mode_downloads_its_own_asset() {
        QCOMPARE(update_asset_suffix(UpdateMode::SwapBundle), QStringLiteral("macos-arm64.dmg"));
        QCOMPARE(update_asset_suffix(UpdateMode::SwapFolder), QStringLiteral("windows-x64.zip"));
        QCOMPARE(update_asset_suffix(UpdateMode::MoveToUserFolder), QStringLiteral("windows-x64-setup.exe"));
        QCOMPARE(update_asset_suffix(UpdateMode::Manual), installer_suffix());
    }

    void the_windows_zip_is_found_like_any_other_asset() {
        const auto found = newest_release(QJsonDocument::fromJson(kWithZip), "custom-v4.5.0-krx.3", "windows-x64.zip");
        QVERIFY(found);
        QCOMPARE(found->tag, QStringLiteral("custom-v4.5.0-krx.9"));
        QCOMPARE(found->installer, QUrl("https://example.test/krx.9.zip"));
        QCOMPARE(found->sha256, QStringLiteral("9999999999999999999999999999999999999999999999999999999999999999"));
        // A release from before the ZIP existed has nothing to swap in.
        QVERIFY(!newest_release(QJsonDocument::fromJson(kWithZip), "custom-v4.5.0-krx.9", "windows-x64.zip"));
    }

    void an_answer_that_is_not_a_list_offers_nothing() {
        QVERIFY(!newest_release(QJsonDocument::fromJson(R"({"message": "API rate limit exceeded"})"),
                                "custom-v4.5.0-krx.3", kMac));
        QVERIFY(!newest_release(QJsonDocument(), "custom-v4.5.0-krx.3", kMac));
    }
};

QTEST_GUILESS_MAIN(TstKrxAppRelease)
#include "tst_krx_app_release.moc"
