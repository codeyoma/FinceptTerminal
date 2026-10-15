// Tests for src/services/krx_quant/KrxLive.h, run against a stand-in engine on this computer.
//
// Before the open (no bar today yet: from midnight until the first trade, and on holidays) the
// minute views show the last trading day's 1-minute bars. A symbol whose chart was read while a
// candle view (day, week, month, year) was showing read none of them, and switching to a minute
// view read none either: the chart stayed empty until the screen was shown again. These tests
// pin that a minute view reads them however the screen got there, and that a symbol chosen in
// a minute view reads them once, and nothing before the day's bars while the day has some.
//
// The stand-in answers the routes these reads need (status, symbols, a symbol's bars, candles
// and predictions); every other route is 404, which the screen shows as not read. Built
// without Qt WebSockets, the stream never connects and the screen reads the API instead. The
// settings (the engine's address) go to a temporary INI file: AppConfig is defined below, in
// place of core/config/AppConfig.cpp, which keeps them in the app's own settings.

#include "core/config/AppConfig.h"
#include "services/krx_quant/KrxLive.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

namespace {

QString settings_file; ///< set before the first AppConfig::instance()

} // namespace

namespace fincept {

AppConfig& AppConfig::instance() {
    static AppConfig s;
    return s;
}

AppConfig::AppConfig() : settings_(settings_file, QSettings::IniFormat) {}

QVariant AppConfig::get(const QString& key, const QVariant& default_val) const {
    return settings_.value(key, default_val);
}

void AppConfig::set(const QString& key, const QVariant& value) {
    settings_.setValue(key, value);
}

void AppConfig::remove(const QString& key) {
    settings_.remove(key);
}

} // namespace fincept

using namespace fincept;
using namespace fincept::krx_quant;

namespace {

const QString kSymbol = QStringLiteral("005930");
const QString kBarsPath = "/v1/symbols/" + kSymbol + "/bars";

/// ``count`` 1-minute bars from ``first`` on, as the engine sends them.
QJsonArray bars_json(const char* first, int count) {
    const QDateTime start = QDateTime::fromString(QString::fromLatin1(first), Qt::ISODate);
    QJsonArray out;
    for (int i = 0; i < count; ++i)
        out.append(QJsonObject{{"start", start.addSecs(60 * i).toString(Qt::ISODate)},
                               {"open", 209000},
                               {"high", 209500},
                               {"low", 208500},
                               {"close", 209000 + 50 * (i % 3)},
                               {"volume", 1000 + i},
                               {"trades", QJsonValue::Null}});
    return out;
}

/// The engine as these reads see it: each GET answered from its route, the paths asked kept.
class StandIn : public QObject {
  public:
    QString now;         ///< the engine's clock
    QJsonArray today;    ///< GET …/bars: the bars of the engine's day
    QJsonArray last_day; ///< GET …/bars?limit=N: the latest bars before now (none before them)
    QStringList asked;

    StandIn() {
        connect(&server_, &QTcpServer::newConnection, this, &StandIn::accept);
        server_.listen(QHostAddress::LocalHost);
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()); }
    /// The reads of the bars before the day's (KrxQuantService::read_older).
    qsizetype older_reads() const {
        return std::count_if(asked.begin(), asked.end(),
                             [](const QString& path) { return path.startsWith(kBarsPath + "?"); });
    }
    bool asked_for(const QString& prefix) const {
        return std::any_of(asked.begin(), asked.end(), [&](const QString& path) { return path.startsWith(prefix); });
    }

  private:
    void accept() {
        while (QTcpSocket* socket = server_.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { read(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    void read(QTcpSocket* socket) {
        QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n"))
            return; // the rest of the head is on its way
        const QString path = QString::fromLatin1(request.left(request.indexOf("\r\n")).split(' ').value(1));
        asked << path;
        int status = 200;
        const QByteArray body = QJsonDocument(answer(path, &status)).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Not Found") +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) +
                      "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    QJsonObject answer(const QString& path, int* status) const {
        if (path == "/v1/status")
            return {{"mode", "live"}, {"phase", "pre_open"}, {"now", now}, {"pending_outcomes", 0},
                    {"special", QJsonArray{}}};
        if (path == "/v1/symbols")
            return {{"symbols", QJsonArray{QJsonObject{{"symbol", kSymbol}}}}};
        if (path == kBarsPath)
            return {{"symbol", kSymbol}, {"bars", today}};
        if (path.startsWith(kBarsPath + "?")) // with before=, before the first of them: none
            return {{"symbol", kSymbol}, {"bars", path.contains("before=") ? QJsonArray{} : last_day}};
        if (path.startsWith("/v1/symbols/" + kSymbol + "/predictions"))
            return {{"symbol", kSymbol}, {"predictions", QJsonArray{}}};
        if (path.startsWith("/v1/symbols/" + kSymbol + "/candles")) {
            const QJsonArray candles{QJsonObject{{"date", "2026-10-15"}, {"open", 210000}, {"high", 211000},
                                                 {"low", 206000}, {"close", 207500}, {"volume", 15000000}}};
            return {{"symbol", kSymbol},
                    {"venue", "UN"},
                    {"candles", path.contains("before=") ? QJsonArray{} : candles}};
        }
        *status = 404;
        return {{"error", QJsonObject{{"code", "not_found"}, {"message", path}}}};
    }

    QTcpServer server_;
};

} // namespace

class TstKrxLive : public QObject {
    Q_OBJECT

    QTemporaryDir settings_;

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        settings_file = settings_.filePath("settings.ini");
    }

    void before_the_open_a_minute_view_after_a_day_view_reads_the_last_days_bars() {
        StandIn engine;
        engine.now = "2026-10-16T00:14:00+09:00";
        engine.last_day = bars_json("2026-10-15T19:00:00+09:00", 60);
        services::KrxLive live;
        live.set_view(kDayView); // the symbol's own view, as the screen sets it
        live.apply_base_url(engine.url());
        // The chart read in the day view: no bars today, then the day view's candles.
        QTRY_VERIFY_WITH_TIMEOUT(engine.asked_for("/v1/symbols/" + kSymbol + "/candles"), 10000);
        QCOMPARE(live.selected(), kSymbol);
        QVERIFY(live.bars().isEmpty());
        QCOMPARE(engine.older_reads(), 0);

        live.set_view(1); // the 1-minute view chosen on the toolbar
        QTRY_COMPARE_WITH_TIMEOUT(live.bars().size(), qsizetype(60), 10000);
        QCOMPARE(live.bars().first().start, QDateTime::fromString("2026-10-15T19:00:00+09:00", Qt::ISODate));
        QCOMPARE(engine.older_reads(), 1);
    }

    void before_the_open_a_symbol_chosen_in_a_minute_view_reads_the_last_days_bars_once() {
        StandIn engine;
        engine.now = "2026-10-16T00:14:00+09:00";
        engine.last_day = bars_json("2026-10-15T19:00:00+09:00", 60);
        services::KrxLive live;
        live.set_view(1);
        // As the screen does on a symbol chosen (show_symbol_prefs), before its chart is read.
        connect(&live, &services::KrxLive::selected_changed, &live, [&live]() { live.set_view(1); });
        live.apply_base_url(engine.url());
        QTRY_COMPARE_WITH_TIMEOUT(live.bars().size(), qsizetype(60), 10000);
        QTest::qWait(300); // any other read under way answered
        QCOMPARE(engine.older_reads(), 1);
    }

    void a_symbol_chosen_in_a_minute_view_while_the_day_has_bars_reads_none_before_them() {
        StandIn engine;
        engine.now = "2026-10-16T09:20:00+09:00";
        engine.today = bars_json("2026-10-16T09:00:00+09:00", 20);
        engine.last_day = bars_json("2026-10-15T19:00:00+09:00", 60);
        services::KrxLive live;
        live.set_view(1);
        connect(&live, &services::KrxLive::selected_changed, &live, [&live]() { live.set_view(1); });
        live.apply_base_url(engine.url());
        QTRY_COMPARE_WITH_TIMEOUT(live.bars().size(), qsizetype(20), 10000);
        QTest::qWait(300);
        // No indicator wants bars before them, and the view has not been scrolled back.
        QCOMPARE(engine.older_reads(), 0);
    }
};

QTEST_GUILESS_MAIN(TstKrxLive)
#include "tst_krx_live.moc"
