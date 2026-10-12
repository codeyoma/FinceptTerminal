#pragma once

#include "services/krx_quant/AppRelease.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>

class QNetworkReply;
class QProgressDialog;
class QWidget;

namespace fincept::krx_quant {

/// The personal build's self-update from the fork's own Releases (codeyoma/krx-quant#92, #130,
/// custom/README.md "앱 업데이트"). In a personal build UpdateService hands its checks here: an
/// official release never replaces it.
///
/// A check reads the fork's releases from the GitHub API and finds the newest newer build
/// (AppRelease.h). A silent check (after login, then every few hours) downloads it in the
/// background and checks it against GitHub's sha256 digest; a check from the menu asks first and
/// shows the download. Once the build is on disk, the platform's helper (update/update-macos.sh,
/// update/update-windows.ps1) starts and waits for the app to quit, and the app offers one button,
/// "지금 재시작". Pressed, the app quits and the helper swaps in the new build and reopens it; not
/// pressed, the helper swaps it in when the app quits some other time. Where the install cannot
/// update itself, the installer is opened for a manual install.
class AppUpdater : public QObject {
    Q_OBJECT

  public:
    using Done = std::function<void(bool update_found)>;

    explicit AppUpdater(QObject* parent = nullptr);

    /// Look for a newer build. A silent check runs once per run of the app (then the updater
    /// checks again by itself every few hours) and speaks only when a build is ready; a check
    /// from the menu always answers.
    void check(bool silent, QWidget* parent, Done done);

  private:
    void run_check(bool silent);
    void on_releases(QNetworkReply* reply);
    void offer(const AppRelease& release);
    void download(const AppRelease& release, bool in_background);
    void on_downloaded(QNetworkReply* reply);
    void prepare(const QString& payload);
    bool start_helper(const QString& payload, UpdateMode mode);
    void ask_to_restart();
    void restart_now();
    void install_by_hand(const QString& installer, const QString& why);
    void fail(const QString& text);
    void finish(bool update_found);
    void tell(const QString& text);

    /// How this install updates itself.
    static UpdateMode mode();

    QNetworkAccessManager net_;
    QTimer recheck_;
    QPointer<QWidget> parent_;
    Done done_;
    bool silent_ = true;
    bool busy_ = false;
    bool silent_checked_ = false;
    AppRelease pending_;
    std::unique_ptr<QFile> file_;
    bool write_failed_ = false;
    bool background_ = false;
    QPointer<QProgressDialog> progress_;
    QString ready_tag_;       ///< the build waiting for the app to quit; empty when none
    QString relaunch_marker_; ///< made when the user asks to restart: the helper then reopens the app
};

} // namespace fincept::krx_quant
