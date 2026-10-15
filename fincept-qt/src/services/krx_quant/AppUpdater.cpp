#include "services/krx_quant/AppUpdater.h"

#include "core/logging/Logger.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace fincept::krx_quant {

namespace {

constexpr const char* kReleases = "https://api.github.com/repos/codeyoma/FinceptTerminal/releases?per_page=30";
// Seconds without a byte before a request gives up, so a stalled one cannot hold the updater.
constexpr int kStalledMs = 60 * 1000;
// How often an open app looks again for a new build, after the check at login.
constexpr int kRecheckMs = 6 * 60 * 60 * 1000;

/// The build's custom-v* tag, given to this file by CMake; empty for a local build.
QString running_tag() {
    return QStringLiteral(FINCEPT_CUSTOM_TAG);
}

QString user_agent() {
    return QStringLiteral("FinceptTerminal/%1").arg(running_tag());
}

/// The sha256 of the file at ``path`` as written to disk, in lowercase hex; empty if unreadable.
QString sha256_of(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString temp_path(const QString& name) {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(name);
}

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
/// The helper script from the app's resources, written where it can run after the app quits.
/// Only the platforms that run one (start_helper): elsewhere it would be an unused function,
/// an error under -Werror (codeyoma/krx-quant#136).
QString write_helper(const QString& name) {
    const QString path = temp_path(name);
    QFile::remove(path);
    if (!QFile::copy(QStringLiteral(":/krx/") + name, path))
        return {};
    QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}
#endif

} // namespace

AppUpdater::AppUpdater(QObject* parent) : QObject(parent) {
    net_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    // While the app stays open, look again now and then: a build can land hours after login.
    recheck_.setInterval(kRecheckMs);
    connect(&recheck_, &QTimer::timeout, this, [this] {
        if (busy_ || !ready_tag_.isEmpty())
            return;
        done_ = {};
        run_check(true);
    });
}

UpdateMode AppUpdater::mode() {
#if defined(Q_OS_MACOS)
    // The running bundle: <name>.app/Contents/MacOS/<binary>. Swapping it needs its folder writable.
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    dir.cdUp();
    const QString bundle = dir.absolutePath();
    if (bundle.endsWith(QLatin1String(".app")) && QFileInfo(QFileInfo(bundle).absolutePath()).isWritable())
        return UpdateMode::SwapBundle;
    return UpdateMode::Manual;
#elif defined(Q_OS_WIN)
    const QString root = QCoreApplication::applicationDirPath();
    const bool tool = QFileInfo::exists(QDir(root).filePath(QStringLiteral("FinceptMaintenanceTool.exe")));
    return windows_update_mode(root, qEnvironmentVariable("LOCALAPPDATA"), tool);
#else
    return UpdateMode::Manual;
#endif
}

void AppUpdater::check(bool silent, QWidget* parent, Done done) {
    if (!ready_tag_.isEmpty()) { // a build already waits for the app to quit
        if (!silent) {
            parent_ = parent;
            ask_to_restart();
        }
        if (done)
            done(true);
        return;
    }
    if (busy_) { // a check or a download under way: a check from the menu still gets an answer
        if (!silent)
            QMessageBox::information(parent, tr("앱 업데이트"), tr("업데이트를 이미 확인하거나 내려받는 중입니다."));
        if (done)
            done(false);
        return;
    }
    if (silent && silent_checked_) { // once per run; the updater checks again by itself
        if (done)
            done(false);
        return;
    }
    if (silent) {
        silent_checked_ = true;
        recheck_.start();
    }
    parent_ = parent;
    done_ = std::move(done);
    run_check(silent);
}

void AppUpdater::run_check(bool silent) {
    silent_ = silent;
    if (!parse_build_tag(running_tag())) {
        LOG_INFO("AppUpdater", "A local build without a custom-v* tag: no update check");
        recheck_.stop();
        if (!silent_)
            tell(tr("이 빌드는 개인 빌드 태그가 없는 로컬 빌드라 업데이트를 확인하지 않습니다."));
        finish(false);
        return;
    }
    if (installer_suffix().isEmpty()) {
        recheck_.stop();
        if (!silent_)
            tell(tr("이 플랫폼용 개인 빌드는 없습니다."));
        finish(false);
        return;
    }

    busy_ = true;
    QNetworkRequest request{QUrl(QString::fromLatin1(kReleases))};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader, user_agent());
    request.setTransferTimeout(kStalledMs);
    QNetworkReply* reply = net_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { on_releases(reply); });
}

void AppUpdater::on_releases(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        LOG_WARN("AppUpdater", QString("Could not read the releases: %1").arg(reply->errorString()));
        if (!silent_)
            tell(tr("업데이트를 확인하지 못했습니다.\n\n%1").arg(reply->errorString()));
        finish(false);
        return;
    }
    const QJsonDocument answer = QJsonDocument::fromJson(reply->readAll());
    if (!answer.isArray()) { // an error object or a broken answer: not "up to date"
        const QString message = answer.object().value("message").toString();
        LOG_WARN("AppUpdater", QString("Unexpected releases answer: %1").arg(message));
        if (!silent_)
            tell(tr("업데이트를 확인하지 못했습니다. GitHub의 답을 읽을 수 없습니다.\n\n%1").arg(message));
        finish(false);
        return;
    }
    const UpdateMode how = mode();
    const auto release = newest_release(answer, running_tag(), update_asset_suffix(how));
    if (!release) {
        LOG_INFO("AppUpdater", QString("Up to date: %1").arg(running_tag()));
        if (!silent_)
            tell(tr("최신 개인 빌드입니다 (%1).").arg(running_tag()));
        finish(false);
        return;
    }
    LOG_INFO("AppUpdater", QString("Update available: %1 -> %2").arg(running_tag(), release->tag));
    if (silent_) {
        // A silent check fetches the build without asking. An install that cannot update itself
        // hears of it only from a check from the menu, not from every scheduled check.
        if (how == UpdateMode::Manual)
            finish(true);
        else
            download(*release, true);
        return;
    }
    offer(*release);
}

void AppUpdater::offer(const AppRelease& release) {
    QMessageBox box(parent_);
    box.setWindowTitle(tr("앱 업데이트"));
    box.setIcon(QMessageBox::Information);
    const QString then = mode() == UpdateMode::Manual ? tr("받으면 확인한 뒤 설치 파일을 엽니다. 직접 설치해 주세요.")
                                                      : tr("받으면 확인한 뒤, 지금 재시작할지 묻습니다.");
    box.setText(tr("새 개인 빌드가 있습니다.\n\n지금: %1\n새 빌드: %2 (%3 MB)\n\n%4")
                    .arg(running_tag(), release.tag)
                    .arg(release.size / (1024 * 1024))
                    .arg(then));
    QPushButton* now = box.addButton(tr("받기"), QMessageBox::AcceptRole);
    QPushButton* page = box.addButton(tr("릴리스 보기"), QMessageBox::HelpRole);
    box.addButton(tr("나중에"), QMessageBox::RejectRole);
    box.setDefaultButton(now);
    box.exec();

    if (box.clickedButton() == now) {
        download(release, false);
        return;
    }
    if (box.clickedButton() == page)
        QDesktopServices::openUrl(release.page);
    finish(true);
}

void AppUpdater::download(const AppRelease& release, bool in_background) {
    pending_ = release;
    background_ = in_background;
    file_ = std::make_unique<QFile>(temp_path(QFileInfo(release.installer.path()).fileName()));
    file_->remove();
    if (!file_->open(QIODevice::WriteOnly)) {
        fail(tr("새 빌드를 저장할 수 없습니다.\n\n%1").arg(file_->errorString()));
        return;
    }
    write_failed_ = false;

    QNetworkRequest request{release.installer};
    request.setHeader(QNetworkRequest::UserAgentHeader, user_agent());
    request.setTransferTimeout(kStalledMs);
    QNetworkReply* reply = net_.get(request);
    LOG_INFO("AppUpdater", QString("Downloading %1 to %2%3")
                               .arg(release.installer.toString(), file_->fileName(),
                                    in_background ? QStringLiteral(" in the background") : QString()));

    if (!in_background) {
        progress_ = new QProgressDialog(tr("%1을(를) 내려받는 중…").arg(release.tag), tr("취소"), 0, 100, parent_);
        progress_->setWindowTitle(tr("앱 업데이트"));
        progress_->setWindowModality(Qt::WindowModal);
        progress_->setMinimumDuration(0);
        progress_->setAttribute(Qt::WA_DeleteOnClose);
        connect(progress_, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    }
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray chunk = reply->readAll();
        if (file_->write(chunk) != chunk.size()) { // a full disk: stop here
            write_failed_ = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const qint64 size = total > 0 ? total : pending_.size;
        if (progress_ && size > 0)
            progress_->setValue(static_cast<int>(received * 100 / size));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { on_downloaded(reply); });
}

void AppUpdater::on_downloaded(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray rest = reply->readAll();
        write_failed_ = write_failed_ || file_->write(rest) != rest.size();
    }
    write_failed_ = write_failed_ || !file_->flush();
    file_->close();
    if (progress_) {
        disconnect(progress_, nullptr, reply, nullptr); // closing it emits canceled
        progress_->close();
    }
    const QString path = file_->fileName();

    if (write_failed_) {
        file_->remove();
        fail(tr("새 빌드를 다 저장하지 못했습니다(디스크 공간을 확인하세요).\n\n%1").arg(file_->errorString()));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        file_->remove();
        if (reply->error() == QNetworkReply::OperationCanceledError && !background_) {
            finish(true); // the user cancelled
            return;
        }
        fail(tr("새 빌드를 내려받지 못했습니다.\n\n%1").arg(reply->errorString()));
        return;
    }
    const QString actual = sha256_of(path); // what is on disk, as the helper will read it
    if (actual != pending_.sha256) {
        LOG_ERROR("AppUpdater", QString("sha256 mismatch: expected %1, got %2").arg(pending_.sha256, actual));
        file_->remove();
        fail(tr("내려받은 파일이 GitHub의 sha256과 맞지 않아 지웠습니다. 다시 시도하거나 릴리스 페이지에서 받으세요."));
        return;
    }
    LOG_INFO("AppUpdater", QString("sha256 verified: %1").arg(actual));
    prepare(path);
}

void AppUpdater::prepare(const QString& payload) {
    const UpdateMode how = mode();
    if (how == UpdateMode::Manual) {
        install_by_hand(payload, tr("이 위치에 설치된 앱은 스스로 업데이트할 수 없습니다."));
        return;
    }
    if (!start_helper(payload, how)) {
        if (background_) { // quiet, like any background failure; the next check tries again
            QFile::remove(payload);
            fail(tr("자동 업데이트를 시작하지 못했습니다."));
            return;
        }
        if (how == UpdateMode::SwapFolder) { // a ZIP is no installer: send the user to the release
            QDesktopServices::openUrl(pending_.page);
            fail(tr("자동 업데이트를 시작하지 못했습니다. 릴리스 페이지를 열었습니다. 설치 파일을 받아 직접 설치해 "
                    "주세요."));
            return;
        }
        install_by_hand(payload, tr("자동 업데이트를 시작하지 못했습니다."));
        return;
    }
    ready_tag_ = pending_.tag;
    LOG_INFO("AppUpdater", QString("%1 is ready; it goes in when this run quits").arg(ready_tag_));
    finish(true);
    ask_to_restart();
}

bool AppUpdater::start_helper(const QString& payload, UpdateMode how) {
    relaunch_marker_ = payload + QStringLiteral(".relaunch");
    QFile::remove(relaunch_marker_);
    const QString pid = QString::number(QCoreApplication::applicationPid());
    QString helper; // the platform's update helper, written out of the app's resources
    QString program;
    QStringList arguments;
#if defined(Q_OS_MACOS)
    Q_UNUSED(how)
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    dir.cdUp();
    helper = write_helper(QStringLiteral("update-macos.sh"));
    program = QStringLiteral("/bin/bash");
    arguments = {helper, pid, payload, dir.absolutePath(), relaunch_marker_};
#elif defined(Q_OS_WIN)
    helper = write_helper(QStringLiteral("update-windows.ps1"));
    // Windows PowerShell by its full path: PATH can lack it (codeyoma/krx-quant#101).
    const QString powershell = QDir(qEnvironmentVariable("SystemRoot"))
                                   .filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
    program = QFileInfo::exists(powershell) ? QDir::toNativeSeparators(powershell) : QStringLiteral("powershell.exe");
    const QString root = QCoreApplication::applicationDirPath();
    arguments = {QStringLiteral("-NoProfile"),
                 QStringLiteral("-ExecutionPolicy"),
                 QStringLiteral("Bypass"),
                 QStringLiteral("-WindowStyle"),
                 QStringLiteral("Hidden"),
                 QStringLiteral("-File"),
                 QDir::toNativeSeparators(helper),
                 QStringLiteral("-AppPid"),
                 pid,
                 QStringLiteral("-Relaunch"),
                 QDir::toNativeSeparators(relaunch_marker_)};
    if (how == UpdateMode::SwapFolder) {
        arguments << QStringLiteral("-Mode") << QStringLiteral("Swap") << QStringLiteral("-Zip")
                  << QDir::toNativeSeparators(payload) << QStringLiteral("-Root") << QDir::toNativeSeparators(root);
    } else {
        arguments << QStringLiteral("-Mode") << QStringLiteral("Move") << QStringLiteral("-Setup")
                  << QDir::toNativeSeparators(payload) << QStringLiteral("-OldRoot") << QDir::toNativeSeparators(root)
                  << QStringLiteral("-Root")
                  << QDir::toNativeSeparators(windows_user_install_dir(qEnvironmentVariable("LOCALAPPDATA")));
    }
#else
    Q_UNUSED(how)
    Q_UNUSED(pid)
#endif
    if (helper.isEmpty())
        return false;
    // In the helper's own folder, not the app's: the Start menu shortcut opens the app in its
    // install folder, and a process standing in a folder keeps Windows from moving it.
    return QProcess::startDetached(program, arguments, QFileInfo(helper).absolutePath());
}

void AppUpdater::ask_to_restart() {
    auto* box = new QMessageBox(parent_);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(tr("앱 업데이트"));
    box->setIcon(QMessageBox::Information);
    QString text = tr("새 개인 빌드 %1이(가) 준비됐습니다.\n\n"
                      "지금 재시작하면 몇 초 뒤 새 빌드로 다시 열립니다. 나중에 하면 앱을 끌 때 바뀌고, "
                      "다음에 켜면 새 빌드가 열립니다.")
                       .arg(ready_tag_);
#if defined(Q_OS_WIN)
    if (mode() == UpdateMode::MoveToUserFolder)
        text += tr("\n\n이번 한 번은 설치 위치를 %1(으)로 옮깁니다. 예전 설치를 지울 때 관리자 확인이 뜨면 "
                   "허용해 주세요.")
                    .arg(QDir::toNativeSeparators(windows_user_install_dir(qEnvironmentVariable("LOCALAPPDATA"))));
#endif
    box->setText(text);
    QPushButton* now = box->addButton(tr("지금 재시작"), QMessageBox::AcceptRole);
    box->addButton(tr("나중에"), QMessageBox::RejectRole);
    box->setDefaultButton(now);
    connect(box, &QMessageBox::buttonClicked, this, [this, now](QAbstractButton* clicked) {
        if (clicked == now)
            restart_now();
    });
    box->setModal(false);
    box->show();
}

void AppUpdater::restart_now() {
    QFile marker(relaunch_marker_);
    if (!marker.open(QIODevice::WriteOnly)) {
        tell(tr("재시작을 준비하지 못했습니다. 앱을 직접 껐다 켜 주세요.\n\n%1").arg(marker.errorString()));
        return;
    }
    marker.close();
    LOG_INFO("AppUpdater", QString("Restarting into %1").arg(ready_tag_));
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void AppUpdater::install_by_hand(const QString& installer, const QString& why) {
    LOG_WARN("AppUpdater", QString("Self-update not possible (%1); opening the installer").arg(why));
    QDesktopServices::openUrl(QUrl::fromLocalFile(installer));
    tell(tr("%1\n\n설치 파일을 열었습니다. 직접 설치해 주세요.").arg(why));
    finish(true);
}

void AppUpdater::fail(const QString& text) {
    LOG_WARN("AppUpdater", text);
    if (!background_) // a background download fails quietly; the next check tries again
        tell(text);
    finish(true);
}

void AppUpdater::finish(bool update_found) {
    busy_ = false;
    file_.reset();
    if (done_)
        done_(update_found);
    done_ = {};
}

void AppUpdater::tell(const QString& text) {
    QMessageBox::information(parent_, tr("앱 업데이트"), text);
}

} // namespace fincept::krx_quant
