#pragma once

#include "services/krx_quant/AppRelease.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class QNetworkReply;
class QProgressDialog;
class QWidget;

namespace fincept::krx_quant {

/// The personal build's one-click update from the fork's own Releases (codeyoma/krx-quant#92,
/// custom/README.md). In a personal build UpdateService hands its checks here: an official
/// release never replaces it.
///
/// A check reads the fork's releases from the GitHub API and offers the newest newer build
/// (AppRelease.h). "지금 업데이트" downloads its installer, checks it against GitHub's sha256
/// digest, starts the platform's helper (update/update-macos.sh, update/update-windows.ps1) and
/// quits; the helper installs the new build in place and opens it. Where that cannot be done,
/// the installer is opened for a manual install.
class AppUpdater : public QObject {
    Q_OBJECT

  public:
    using Done = std::function<void(bool update_found)>;

    explicit AppUpdater(QObject* parent = nullptr);

    /// Look for a newer build. A silent check runs once per run of the app and speaks only
    /// when there is one; a check from the menu always answers.
    void check(bool silent, QWidget* parent, Done done);

  private:
    void on_releases(QNetworkReply* reply);
    void offer(const AppRelease& release);
    void download(const AppRelease& release);
    void on_downloaded(QNetworkReply* reply);
    void install(const QString& installer);
    void install_by_hand(const QString& installer, const QString& why);
    void finish(bool update_found);
    void tell(const QString& text);

    QNetworkAccessManager net_;
    QPointer<QWidget> parent_;
    Done done_;
    bool silent_ = true;
    bool busy_ = false;
    bool silent_checked_ = false;
    AppRelease pending_;
    std::unique_ptr<QFile> file_;
    bool write_failed_ = false;
    QPointer<QProgressDialog> progress_;
};

} // namespace fincept::krx_quant
