#include "services/krx_quant/KrxStream.h"

#include "core/logging/Logger.h"

#include <QSignalBlocker>
#include <QUrl>

#include <algorithm>

namespace fincept::services {

namespace {

constexpr int kPingMs = 15000;
[[maybe_unused]] constexpr int kDeadMs = 45000; ///< no pong for this long: the connection is gone (with WebSockets)
constexpr int kMaxRetrySeconds = 30;

} // namespace

KrxStream::KrxStream(QObject* parent) : QObject(parent) {
    qRegisterMetaType<krx_quant::Event>();
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &KrxStream::connect_now);
    ping_.setInterval(kPingMs);
    connect(&ping_, &QTimer::timeout, this, &KrxStream::ping);
#ifdef HAS_QT_WEBSOCKETS
    socket_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(socket_, &QWebSocket::connected, this, [this]() {
        attempts_ = 0;
        last_pong_.restart();
        ping_.start();
        emit connected();
    });
    connect(socket_, &QWebSocket::disconnected, this,
            [this]() { lost(socket_->closeReason().isEmpty() ? tr("연결이 끊겼습니다") : socket_->closeReason()); });
    // A refused or failed connection may end without "disconnected": both lead to one retry.
    connect(socket_, &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { lost(socket_->errorString()); });
    connect(socket_, &QWebSocket::pong, this, [this](quint64, const QByteArray&) { last_pong_.restart(); });
    connect(socket_, &QWebSocket::textMessageReceived, this, [this](const QString& message) {
        last_pong_.restart(); // anything from the engine shows the connection is alive
        auto event = krx_quant::parse_event(message);
        if (event.is_err()) {
            LOG_WARN("KrxQuant", QString::fromStdString(event.error()));
            return;
        }
        emit event_received(event.value());
    });
#endif
}

bool KrxStream::available() {
#ifdef HAS_QT_WEBSOCKETS
    return true;
#else
    return false;
#endif
}

void KrxStream::open(const QString& base_url) {
    url_ = krx_quant::stream_url(base_url);
    wanted_ = !url_.isEmpty();
    attempts_ = 0;
    retry_.stop();
    if (!wanted_) {
        close();
        return;
    }
    if (!available()) {
        emit disconnected(tr("이 빌드에는 WebSocket이 없어 5초마다 API를 읽습니다"), -1);
        return;
    }
    connect_now();
}

void KrxStream::close() {
    wanted_ = false;
    retry_.stop();
    ping_.stop();
#ifdef HAS_QT_WEBSOCKETS
    const QSignalBlocker quiet(socket_);
    socket_->abort();
#endif
}

bool KrxStream::is_connected() const {
#ifdef HAS_QT_WEBSOCKETS
    return socket_->state() == QAbstractSocket::ConnectedState;
#else
    return false;
#endif
}

void KrxStream::connect_now() {
    if (!wanted_)
        return;
#ifdef HAS_QT_WEBSOCKETS
    {
        // Dropping a connection to the old address (or a half-dead one) is not a loss to retry.
        const QSignalBlocker quiet(socket_);
        socket_->abort();
    }
    socket_->open(QUrl(url_));
#endif
}

void KrxStream::lost(const QString& reason) {
    ping_.stop();
    if (!wanted_ || retry_.isActive())
        return; // closed on purpose, or a retry is already on its way
    const int seconds = std::min(kMaxRetrySeconds, 1 << std::min(attempts_, 5));
    ++attempts_;
    LOG_WARN("KrxQuant", QString("stream %1: %2; again in %3 s").arg(url_, reason).arg(seconds));
    retry_.start(seconds * 1000);
    emit disconnected(reason, seconds);
}

void KrxStream::ping() {
#ifdef HAS_QT_WEBSOCKETS
    if (last_pong_.elapsed() > kDeadMs) {
        {
            const QSignalBlocker quiet(socket_);
            socket_->abort();
        }
        lost(tr("엔진의 응답이 없습니다"));
        return;
    }
    socket_->ping();
#endif
}

} // namespace fincept::services
