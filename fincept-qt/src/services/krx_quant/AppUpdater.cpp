#include "services/krx_quant/AppUpdater.h"

#include "core/logging/Logger.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
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

/// The helper script from the app's resources, written where it can run after the app quits.
QString write_helper(const QString& name) {
    const QString path = temp_path(name);
    QFile::remove(path);
    if (!QFile::copy(QStringLiteral(":/krx/") + name, path))
        return {};
    QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

} // namespace

AppUpdater::AppUpdater(QObject* parent) : QObject(parent) {
    net_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

void AppUpdater::check(bool silent, QWidget* parent, Done done) {
    if (busy_) { // a check or a download under way: a check from the menu still gets an answer
        if (!silent) {
            QMessageBox::information(parent, tr("화면 업데이트"), tr("업데이트를 이미 확인하거나 내려받는 중입니다."));
            if (done)
                done(false);
        }
        return;
    }
    if (silent && silent_checked_)
        return;
    silent_checked_ = silent_checked_ || silent;
    silent_ = silent;
    parent_ = parent;
    done_ = std::move(done);

    if (!parse_build_tag(running_tag())) {
        LOG_INFO("AppUpdater", "A local build without a custom-v* tag: no update check");
        if (!silent_)
            tell(tr("이 빌드는 개인 빌드 태그가 없는 로컬 빌드라 업데이트를 확인하지 않습니다."));
        finish(false);
        return;
    }
    if (installer_suffix().isEmpty()) {
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
    const auto release = newest_release(QJsonDocument::fromJson(reply->readAll()), running_tag(), installer_suffix());
    if (!release) {
        LOG_INFO("AppUpdater", QString("Up to date: %1").arg(running_tag()));
        if (!silent_)
            tell(tr("최신 개인 빌드입니다 (%1).").arg(running_tag()));
        finish(false);
        return;
    }
    offer(*release);
}

void AppUpdater::offer(const AppRelease& release) {
    LOG_INFO("AppUpdater", QString("Update available: %1 -> %2").arg(running_tag(), release.tag));
    QMessageBox box(parent_);
    box.setWindowTitle(tr("화면 업데이트"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("새 개인 빌드가 있습니다.\n\n지금: %1\n새 빌드: %2 (%3 MB)\n\n"
                   "지금 업데이트하면 내려받아 확인한 뒤, 앱을 닫고 새 빌드를 설치해 다시 엽니다.")
                    .arg(running_tag(), release.tag)
                    .arg(release.size / (1024 * 1024)));
    QPushButton* now = box.addButton(tr("지금 업데이트"), QMessageBox::AcceptRole);
    QPushButton* page = box.addButton(tr("릴리스 보기"), QMessageBox::HelpRole);
    box.addButton(tr("나중에"), QMessageBox::RejectRole);
    box.setDefaultButton(now);
    box.exec();

    if (box.clickedButton() == now) {
        download(release);
        return;
    }
    if (box.clickedButton() == page)
        QDesktopServices::openUrl(release.page);
    finish(true);
}

void AppUpdater::download(const AppRelease& release) {
    pending_ = release;
    file_ = std::make_unique<QFile>(temp_path(QFileInfo(release.installer.path()).fileName()));
    file_->remove();
    if (!file_->open(QIODevice::WriteOnly)) {
        tell(tr("설치 파일을 저장할 수 없습니다.\n\n%1").arg(file_->errorString()));
        finish(true);
        return;
    }
    write_failed_ = false;

    QNetworkRequest request{release.installer};
    request.setHeader(QNetworkRequest::UserAgentHeader, user_agent());
    request.setTransferTimeout(kStalledMs);
    QNetworkReply* reply = net_.get(request);
    LOG_INFO("AppUpdater", QString("Downloading %1 to %2").arg(release.installer.toString(), file_->fileName()));

    progress_ = new QProgressDialog(tr("%1을(를) 내려받는 중…").arg(release.tag), tr("취소"), 0, 100, parent_);
    progress_->setWindowTitle(tr("화면 업데이트"));
    progress_->setWindowModality(Qt::WindowModal);
    progress_->setMinimumDuration(0);
    progress_->setAttribute(Qt::WA_DeleteOnClose);
    connect(progress_, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
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
        tell(tr("설치 파일을 다 저장하지 못했습니다(디스크 공간을 확인하세요).\n\n%1").arg(file_->errorString()));
        finish(true);
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        file_->remove();
        if (reply->error() != QNetworkReply::OperationCanceledError)
            tell(tr("새 빌드를 내려받지 못했습니다.\n\n%1").arg(reply->errorString()));
        finish(true);
        return;
    }
    const QString actual = sha256_of(path); // what is on disk, as the helper will read it
    if (actual != pending_.sha256) {
        LOG_ERROR("AppUpdater", QString("sha256 mismatch: expected %1, got %2").arg(pending_.sha256, actual));
        file_->remove();
        tell(tr("내려받은 파일이 GitHub의 sha256과 맞지 않아 지웠습니다. 다시 시도하거나 릴리스 페이지에서 받으세요."));
        finish(true);
        return;
    }
    LOG_INFO("AppUpdater", QString("sha256 verified: %1").arg(actual));
    install(path);
}

void AppUpdater::install(const QString& installer) {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    QString helper; // the platform's install helper, written out of the app's resources
    QString program;
    QStringList arguments;
#if defined(Q_OS_MACOS)
    // The running bundle: <name>.app/Contents/MacOS/<binary>. Swapping it needs its folder writable.
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    dir.cdUp();
    const QString bundle = dir.absolutePath();
    if (!bundle.endsWith(QLatin1String(".app")) || !QFileInfo(QFileInfo(bundle).absolutePath()).isWritable()) {
        install_by_hand(installer, tr("이 앱이 있는 폴더에 쓸 수 없습니다: %1").arg(bundle));
        return;
    }
    helper = write_helper(QStringLiteral("update-macos.sh"));
    program = QStringLiteral("/bin/bash");
    arguments = {helper, pid, installer, bundle};
#elif defined(Q_OS_WIN)
    const QString root = QCoreApplication::applicationDirPath();
    if (!QFileInfo::exists(QDir(root).filePath(QStringLiteral("FinceptMaintenanceTool.exe")))) {
        install_by_hand(installer,
                        tr("설치 프로그램으로 설치한 앱이 아닙니다: %1").arg(QDir::toNativeSeparators(root)));
        return;
    }
    helper = write_helper(QStringLiteral("update-windows.ps1"));
    program = QStringLiteral("powershell.exe");
    arguments = {QStringLiteral("-NoProfile"),
                 QStringLiteral("-ExecutionPolicy"),
                 QStringLiteral("Bypass"),
                 QStringLiteral("-WindowStyle"),
                 QStringLiteral("Hidden"),
                 QStringLiteral("-File"),
                 QDir::toNativeSeparators(helper),
                 QStringLiteral("-AppPid"),
                 pid,
                 QStringLiteral("-Setup"),
                 QDir::toNativeSeparators(installer),
                 QStringLiteral("-Root"),
                 QDir::toNativeSeparators(root)};
#endif
    if (helper.isEmpty()) {
        install_by_hand(installer, tr("자동 설치를 준비하지 못했습니다."));
        return;
    }
    // Said before the helper starts, which gives this run two minutes to quit.
    tell(tr("앱을 닫고 %1을(를) 설치합니다. 설치가 끝나면 새 빌드가 저절로 열립니다(1–2분).\n"
            "안 되면 설치 파일이 열리니 직접 설치하세요.")
             .arg(pending_.tag));
    if (!QProcess::startDetached(program, arguments)) {
        install_by_hand(installer, tr("자동 설치를 시작하지 못했습니다."));
        return;
    }
    LOG_INFO("AppUpdater", QString("Installing %1 after this run quits").arg(pending_.tag));
    finish(true);
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void AppUpdater::install_by_hand(const QString& installer, const QString& why) {
    LOG_WARN("AppUpdater", QString("In-place update not possible (%1); opening the installer").arg(why));
    QDesktopServices::openUrl(QUrl::fromLocalFile(installer));
    tell(tr("%1\n\n설치 파일을 열었습니다. 직접 설치해 주세요.").arg(why));
    finish(true);
}

void AppUpdater::finish(bool update_found) {
    busy_ = false;
    file_.reset();
    if (done_)
        done_(update_found);
}

void AppUpdater::tell(const QString& text) {
    QMessageBox::information(parent_, tr("화면 업데이트"), text);
}

} // namespace fincept::krx_quant
