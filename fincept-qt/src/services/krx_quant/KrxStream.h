#pragma once

#include "services/krx_quant/EngineData.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#ifdef HAS_QT_WEBSOCKETS
#    include <QWebSocket>
#endif

namespace fincept::services {

/// The KRX prediction engine's stream (GET /v1/stream) over a WebSocket, for the KRX
/// prediction screen, which owns it.
///
/// While it is open it keeps the connection: after a failure or a close it tries again 1,
/// 2, 4, … seconds later (at most 30 s apart, without end: the engine restarts every
/// morning), and it pings the engine every 15 s so a connection that died silently (a
/// laptop that slept) is dropped and made again within a minute. Built without Qt
/// WebSockets it never connects; the screen then reads the engine's API instead.
class KrxStream : public QObject {
    Q_OBJECT
  public:
    explicit KrxStream(QObject* parent = nullptr);

    /// Whether this build has Qt WebSockets.
    static bool available();

    /// Connect to the stream of the engine at ``base_url`` (http(s)://…), dropping any
    /// connection to another. An address without http(s) closes it.
    void open(const QString& base_url);
    void close();
    bool is_connected() const;

  signals:
    void connected();
    /// The connection failed or ended; ``retry_in`` seconds to the next try (-1: none).
    void disconnected(const QString& reason, int retry_in);
    void event_received(const fincept::krx_quant::Event& event);

  private:
    void connect_now();
    void lost(const QString& reason);
    void ping();

    QString url_;
    bool wanted_ = false;
    int attempts_ = 0;
    QTimer retry_;
    QTimer ping_;
    QElapsedTimer last_pong_;
#ifdef HAS_QT_WEBSOCKETS
    QWebSocket* socket_ = nullptr;
#endif
};

} // namespace fincept::services

Q_DECLARE_METATYPE(fincept::krx_quant::Event)
