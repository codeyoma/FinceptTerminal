// Tests for src/services/krx_quant/KrxLive.h, run against a stand-in engine on this computer.
//
// Before the open (no bar today yet: from midnight until the first trade, and on holidays) the
// minute views show the last trading day's 1-minute bars. A symbol whose chart was read while a
// candle view (day, week, month, year) was showing read none of them, and switching to a minute
// view read none either: the chart stayed empty until the screen was shown again. These tests
// pin that a minute view reads them however the screen got there, and that a symbol chosen in
// a minute view reads them once, and nothing before the day's bars while the day has some.
//
// A bar window (krx-quant #255) reads its bar close at once, then again each tick without the stream
// until nothing more is to come: an earlier day's once, today's while not scored, none once closed.
//
// The daily supply data (krx-quant #226) is read over the days of the bars held; an engine without it
// (404) is known as such, so the panel greys its indicators.
//
// The trader's buys and sells (krx-quant #398) are not read for this chart, not even the trader's symbol's:
// the auto trader screen's own chart reads and draws them (codeyoma/krx-quant#537, tests/tst_krx_trader.cpp).
//
// The SMT divergences (codeyoma/krx-quant#522) are read while the chart shows them, over the days of the bars
// held (31 at most), again with each chart read (a new bar close's), and once more after a read under way
// when asked meanwhile; none from an engine without them (404).
//
// The stand-in answers the routes these reads need (status, symbols, a symbol's bars, candles
// and predictions, and the daily supply data when a test gives it); every other route is 404, which
// the screen shows as not read. Built
// without Qt WebSockets, the stream never connects and the screen reads the API instead. The
// settings (the engine's address) go to a temporary INI file: AppConfig is defined below, in
// place of core/config/AppConfig.cpp, which keeps them in the app's own settings.

#include "core/config/AppConfig.h"
#include "services/krx_quant/KrxLive.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <algorithm>
#include <utility>

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

/// A KST time (ISO 8601, ``+09:00``) with its fraction to the microsecond, so that two compare as the
/// moments they are, as the engine compares them.
QString moment(const QString& at) {
    const qsizetype offset = at.indexOf('+');
    const QString clock = at.left(offset);
    const qsizetype dot = clock.indexOf('.');
    const QString whole = dot < 0 ? clock : clock.left(dot);
    const QString fraction = dot < 0 ? QString() : clock.mid(dot + 1);
    return whole + "." + fraction.leftJustified(6, '0') + at.mid(offset);
}

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

/// A live SMT divergence of 005930 and ``partner`` at ``bar_close``, ``direction`` "bearish" or "bullish", as
/// the engine sends it (codeyoma/krx-quant#521).
QJsonObject smt_json(const char* bar_close, const char* partner, const char* direction) {
    const QDateTime close = QDateTime::fromString(QString::fromLatin1(bar_close), Qt::ISODate);
    return {{"bar_close", close.toString(Qt::ISODate)},
            {"bar_start", close.addSecs(-60).toString(Qt::ISODate)},
            {"a", kSymbol},
            {"b", partner},
            {"direction", direction},
            {"minutes", 15},
            {"swept", kSymbol},
            {"other", partner},
            {"definition", "rolling_extreme"},
            {"version", 1},
            {"source", "live"}};
}

/// The engine as these reads see it: each GET answered from its route, the paths asked kept.
class StandIn : public QObject {
  public:
    QString now;         ///< the engine's clock
    QJsonArray today;    ///< GET …/bars: the bars of the engine's day
    QJsonArray last_day; ///< GET …/bars?limit=N: the latest bars before now (none before them)
    /// GET …/supply?from=&to= (krx-quant #226): these days; none: 404, as an engine before it.
    std::optional<QJsonArray> supply;
    /// The supply reads fail (500); with ``hold_supply`` the first is answered only by release_supply.
    bool supply_fails = false;
    bool hold_supply = false;
    /// Another symbol the engine predicts (krx-quant #398), its bars ``today``'s; empty: none.
    QString other;
    /// GET /v1/symbols/005930/trader-marks (krx-quant #398): these, newest first, answered by ``before``
    /// and ``limit`` as the engine answers; none: 404, as an engine before it. This chart asks for none
    /// (codeyoma/krx-quant#537); they are here so that it would get them if it did.
    std::optional<QList<QJsonObject>> trader;
    /// GET /v1/symbols/005930/smt?from=&to= (codeyoma/krx-quant#521): these events whatever the days; none: 404,
    /// as an engine before it. With ``hold_smt`` the first read is answered only by release_smt, with the
    /// events as they were when it was asked.
    std::optional<QJsonArray> smt;
    bool hold_smt = false;
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
    qsizetype supply_reads() const {
        return std::count_if(asked.begin(), asked.end(), [](const QString& path) {
            return path.startsWith("/v1/symbols/" + kSymbol + "/supply?");
        });
    }
    qsizetype smt_reads() const {
        return std::count_if(asked.begin(), asked.end(),
                             [](const QString& path) { return path.startsWith("/v1/symbols/" + kSymbol + "/smt?"); });
    }
    /// Whether an SMT read is held (hold_smt).
    bool smt_held() const { return smt_held_ != nullptr; }
    /// Answer the SMT read held, as it would have been answered when asked.
    void release_smt() {
        if (QTcpSocket* socket = std::exchange(smt_held_, nullptr))
            send(socket, 200, smt_held_body_);
    }
    /// Whether a supply read is held (hold_supply).
    bool supply_held() const { return held_ != nullptr; }
    /// Answer the supply read held, as the others are answered now.
    void release_supply() {
        if (QTcpSocket* socket = std::exchange(held_, nullptr))
            respond(socket, held_path_);
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
        if (hold_supply && path.startsWith("/v1/symbols/" + kSymbol + "/supply?")) {
            hold_supply = false; // the first alone
            held_ = socket;
            held_path_ = path;
            return;
        }
        if (hold_smt && smt && path.startsWith("/v1/symbols/" + kSymbol + "/smt?")) {
            hold_smt = false; // the first alone
            smt_held_ = socket;
            int status = 200;
            smt_held_body_ = QJsonDocument(answer(path, &status)).toJson(QJsonDocument::Compact);
            return;
        }
        respond(socket, path);
    }

    void respond(QTcpSocket* socket, const QString& path) {
        int status = 200;
        const QByteArray body = QJsonDocument(answer(path, &status)).toJson(QJsonDocument::Compact);
        send(socket, status, body);
    }

    void send(QTcpSocket* socket, int status, const QByteArray& body) {
        const QByteArray reason = status == 200 ? " OK" : status == 404 ? " Not Found" : " Internal Server Error";
        socket->write("HTTP/1.1 " + QByteArray::number(status) + reason +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) +
                      "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    QJsonObject answer(const QString& path, int* status) const {
        if (path == "/v1/status")
            return {{"mode", "live"}, {"phase", "pre_open"}, {"now", now}, {"pending_outcomes", 0},
                    {"special", QJsonArray{}}};
        if (path == "/v1/symbols") {
            QJsonArray symbols{QJsonObject{{"symbol", kSymbol}}};
            if (!other.isEmpty())
                symbols.append(QJsonObject{{"symbol", other}});
            return {{"symbols", symbols}};
        }
        if (!other.isEmpty() && path == "/v1/symbols/" + other + "/bars")
            return {{"symbol", other}, {"bars", today}};
        if (!other.isEmpty() && path.startsWith("/v1/symbols/" + other + "/predictions"))
            return {{"symbol", other}, {"predictions", QJsonArray{}}};
        if (trader && path.startsWith("/v1/symbols/" + kSymbol + "/trader-marks?")) {
            // As the engine answers (krx-quant #398): to the microsecond, strictly before, and a page never
            // splits the marks of one moment.
            const QUrlQuery query(path.mid(path.indexOf('?') + 1));
            const QString before = query.queryItemValue("before", QUrl::FullyDecoded);
            const int limit = query.queryItemValue("limit").toInt();
            QJsonArray page;
            QString last; // the moment the page ends on
            for (const auto& m : *trader) {
                const QString at = moment(m.value("at").toString());
                if (!before.isEmpty() && at >= moment(before))
                    continue;
                if (page.size() >= limit && at != last)
                    break;
                page.append(m);
                last = at;
            }
            return {{"symbol", kSymbol}, {"marks", page}};
        }
        if (path == kBarsPath)
            return {{"symbol", kSymbol}, {"bars", today}};
        if (path.startsWith(kBarsPath + "?")) // with before=, before the first of them: none
            return {{"symbol", kSymbol}, {"bars", path.contains("before=") ? QJsonArray{} : last_day}};
        if (path.startsWith("/v1/symbols/" + kSymbol + "/predictions"))
            return {{"symbol", kSymbol}, {"predictions", QJsonArray{}}};
        if (supply_fails && path.startsWith("/v1/symbols/" + kSymbol + "/supply?")) {
            *status = 500;
            return {{"error", "made-up failure"}};
        }
        if (supply && path.startsWith("/v1/symbols/" + kSymbol + "/supply?"))
            return {{"symbol", kSymbol}, {"days", *supply}};
        if (smt && path.startsWith("/v1/symbols/" + kSymbol + "/smt?"))
            return {{"symbol", kSymbol}, {"events", *smt}};
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
    QTcpSocket* held_ = nullptr; ///< a supply read not answered yet (hold_supply)
    QString held_path_;
    QTcpSocket* smt_held_ = nullptr; ///< an SMT read not answered yet (hold_smt)
    QByteArray smt_held_body_;       ///< its answer, as it was when asked
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

    void rebinding_to_the_setting_s_address_reads_nothing_until_asked() {
        // codeyoma/krx-quant#302: a dashboard tile that reads the status alone takes a new address
        // without the snapshot restart() starts, whose failure would read as the status's.
        StandIn engine;
        engine.now = "2026-10-16T00:14:00+09:00";
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxQuantService service;
        QSignalSpy urls(&service, &services::KrxQuantService::base_url_changed);
        service.rebind();
        QCOMPARE(service.url(), engine.url());
        QCOMPARE(urls.count(), 1);
        QTest::qWait(300);
        QVERIFY(engine.asked.isEmpty());

        QSignalSpy status(&service, &services::KrxQuantService::status_ready);
        service.read_status();
        QTRY_COMPARE_WITH_TIMEOUT(status.count(), 1, 10000);
        QCOMPARE(engine.asked, QStringList{"/v1/status"});
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

    void the_daily_supply_data_of_the_days_held_is_read_and_an_engine_without_it_is_known() {
        // An engine before krx-quant #226 answers 404: none, and the supply indicators are greyed.
        StandIn old;
        old.now = "2026-10-16T09:20:00+09:00";
        old.today = bars_json("2026-10-16T09:00:00+09:00", 20);
        services::KrxLive live;
        live.set_view(1);
        live.apply_base_url(old.url());
        QTRY_VERIFY_WITH_TIMEOUT(!live.supply_served(), 10000);
        QVERIFY(old.asked_for("/v1/symbols/" + kSymbol + "/supply?from=2026-10-16&to="));
        QVERIFY(live.supply().isEmpty());
        // An engine that gives it: the days of the bars held (made-up values), not greyed.
        StandIn engine;
        engine.now = old.now;
        engine.today = old.today;
        engine.supply = QJsonArray{QJsonObject{{"date", "2026-10-16"}, {"short_volume", 1200}, {"foreign_net", -3}}};
        services::KrxLive fresh;
        fresh.set_view(1);
        fresh.apply_base_url(engine.url());
        QTRY_COMPARE_WITH_TIMEOUT(fresh.supply().size(), qsizetype(1), 10000);
        QVERIFY(fresh.supply_served());
        QCOMPARE(fresh.supply().first().value(SupplyField::ForeignNet), std::optional<double>(-3));
        QVERIFY(!fresh.supply().first().value(SupplyField::CreditBalance));
        // Another address: not known to be without it until that one answers.
        live.apply_base_url(engine.url());
        QVERIFY(live.supply_served());
    }

    void a_week_view_reads_the_supply_data_from_two_weeks_before_its_first_week() {
        // Its flows add up over each week only when the days read reach before it (krx-quant #226).
        StandIn engine;
        engine.now = "2026-10-16T09:20:00+09:00";
        engine.today = bars_json("2026-10-16T09:00:00+09:00", 20);
        engine.supply = QJsonArray{};
        services::KrxLive live;
        live.set_view(kWeekView);
        live.apply_base_url(engine.url());
        QTRY_VERIFY_WITH_TIMEOUT(engine.supply_reads() > 0, 10000);
        const QList<Bar> weeks = live.view_candles(kWeekView);
        QVERIFY(!weeks.isEmpty());
        const QDate first = weeks.first().start.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(9 * 3600)).date();
        QVERIFY2(
            engine.asked_for("/v1/symbols/" + kSymbol + "/supply?from=" + first.addDays(-14).toString(Qt::ISODate)),
            qPrintable(engine.asked.join(' ')));
    }

    void the_days_wanted_now_are_read_at_once_when_a_read_of_others_fails() {
        // The view changes while a supply read is under way (krx-quant #226): the read the new days
        // need is skipped, as one is under way. That one failing, the new days are read at once, not
        // 5 minutes later; the days wanted now failing are not asked again at once.
        StandIn engine;
        engine.now = "2026-10-16T09:20:00+09:00";
        engine.today = bars_json("2026-10-16T09:00:00+09:00", 20);
        engine.supply_fails = true;
        engine.hold_supply = true;
        services::KrxLive live;
        live.set_view(1);
        live.apply_base_url(engine.url());
        QTRY_VERIFY_WITH_TIMEOUT(engine.supply_held(), 10000); // the minute view's days, from the 16th
        QVERIFY(engine.asked_for("/v1/symbols/" + kSymbol + "/supply?from=2026-10-16&to="));
        live.set_view(kDayView); // its candles start on the 15th: other days
        QTRY_VERIFY_WITH_TIMEOUT(engine.asked_for("/v1/symbols/" + kSymbol + "/candles"), 10000);
        QTest::qWait(500); // the candles answered: the day view's days wanted, their read skipped
        QCOMPARE(engine.supply_reads(), 1);
        engine.release_supply(); // the minute view's days fail
        QTRY_VERIFY_WITH_TIMEOUT(engine.asked_for("/v1/symbols/" + kSymbol + "/supply?from=2026-10-15&to="), 3000);
        QTest::qWait(1000); // those fail too: not asked again at once
        QCOMPARE(engine.supply_reads(), 2);
        QVERIFY(live.supply().isEmpty());
        QVERIFY(live.supply_served()); // a failure, not an engine without them
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

    void the_trader_s_symbol_s_chart_reads_no_trader_marks() {
        // codeyoma/krx-quant#537: the trader's buys and sells are drawn on the auto trader screen's own chart
        // alone, which reads them (KrxTraderLive). This chart of 005930 asked for them with each chart read
        // and with each page of bars before (krx-quant #398); now it asks for none, made-up marks waiting.
        StandIn engine;
        engine.now = "2026-10-16T09:20:00+09:00";
        engine.today = bars_json("2026-10-16T09:00:00+09:00", 20);
        engine.trader = QList<QJsonObject>{QJsonObject{{"trade_id", 1}, {"side", "buy"},
                                                       {"at", "2026-10-16T09:05:02.500+09:00"},
                                                       {"price", 209000}, {"quantity", 4}, {"mode", "live"}}};
        services::KrxLive live;
        live.set_view(1);
        live.apply_base_url(engine.url());
        QTRY_COMPARE_WITH_TIMEOUT(live.bars().size(), qsizetype(20), 10000);
        QCOMPARE(live.selected(), kSymbol);
        // Scrolled back past the first bar: the bars before are read, and the engine has none.
        live.view_from(-40, 20);
        QTRY_VERIFY_WITH_TIMEOUT(live.reached_start(), 10000);
        QVERIFY(engine.asked_for(kBarsPath + "?"));
        QVERIFY2(!engine.asked_for("/v1/symbols/" + kSymbol + "/trader-marks"), qPrintable(engine.asked.join(' ')));
    }

    void the_smt_divergences_are_read_while_shown_and_again_with_each_chart_read() {
        // codeyoma/krx-quant#522, made-up events: not read until the chart shows them; then over the days of the
        // bars held, and again with each chart read, which a new bar close brings (the engine keeps that bar
        // close's events before the stream tells it). Turned off, those read stay and no more are read.
        StandIn engine;
        engine.now = "2026-10-16T09:20:00+09:00";
        engine.today = bars_json("2026-10-16T09:00:00+09:00", 20);
        engine.smt = QJsonArray{smt_json("2026-10-16T09:16:00+09:00", "000660", "bearish")};
        services::KrxLive live;
        live.set_view(1);
        live.apply_base_url(engine.url());
        QTRY_COMPARE_WITH_TIMEOUT(live.bars().size(), qsizetype(20), 10000);
        // The special situations of the days held are asked along with them (krx-quant #236).
        QTRY_VERIFY_WITH_TIMEOUT(engine.asked_for("/v1/symbols/" + kSymbol + "/special?"), 10000);
        QTest::qWait(300); // any other read under way asked
        QCOMPARE(engine.smt_reads(), 0);

        live.set_smt(true);
        QTRY_COMPARE_WITH_TIMEOUT(live.smt().size(), qsizetype(1), 10000);
        QVERIFY2(engine.asked_for("/v1/symbols/" + kSymbol + "/smt?from=2026-10-16&to=2026-10-16"),
                 qPrintable(engine.asked.join(' ')));
        QVERIFY(live.smt().first().bearish);
        QCOMPARE(live.smt().first().b, QStringLiteral("000660"));

        engine.smt->append(smt_json("2026-10-16T09:21:00+09:00", "KOSPI200F", "bullish")); // the 09:21 bar close's
        live.set_shown(true); // a chart read, as a new bar close's
        QTRY_COMPARE_WITH_TIMEOUT(live.smt().size(), qsizetype(2), 10000);
        QVERIFY(!live.smt().last().bearish);

        live.set_smt(false);
        const auto bar_reads = [&engine]() { return engine.asked.count(kBarsPath); };
        const auto chart_reads = bar_reads();
        const auto smt_reads = engine.smt_reads();
        live.set_shown(true);
        QTRY_VERIFY_WITH_TIMEOUT(bar_reads() > chart_reads, 10000);
        QTest::qWait(300);
        QCOMPARE(engine.smt_reads(), smt_reads);
        QCOMPARE(live.smt().size(), qsizetype(2));
    }

    void an_smt_read_asked_while_one_is_under_way_is_read_once_it_answers() {
        // codeyoma/krx-quant#522: a chart read (a new bar close's) while an SMT read is under way. That one was
        // asked before the new events; they are read once it answers, not a bar close later.
        StandIn engine;
        engine.now = "2026-10-16T09:20:00+09:00";
        engine.today = bars_json("2026-10-16T09:00:00+09:00", 20);
        engine.smt = QJsonArray{smt_json("2026-10-16T09:16:00+09:00", "000660", "bearish")};
        engine.hold_smt = true;
        services::KrxLive live;
        live.set_view(1);
        live.set_smt(true);
        live.apply_base_url(engine.url());
        QTRY_VERIFY_WITH_TIMEOUT(engine.smt_held(), 10000);
        engine.smt->append(smt_json("2026-10-16T09:21:00+09:00", "KOSPI200F", "bullish"));
        live.set_shown(true); // the chart read meanwhile
        engine.release_smt(); // the one under way: as it was, one event
        QTRY_COMPARE_WITH_TIMEOUT(live.smt().size(), qsizetype(2), 10000);
    }

    void the_smt_days_are_the_last_31_held_and_an_engine_without_them_has_none() {
        // codeyoma/krx-quant#521: the engine answers 31 days at most; bars held from 09-01 are asked from 09-16.
        // An engine before it answers 404: none, and no failure shown.
        StandIn old;
        old.now = "2026-10-16T09:20:00+09:00";
        old.today = bars_json("2026-09-01T09:00:00+09:00", 20);
        services::KrxLive live;
        live.set_view(1);
        live.set_smt(true);
        live.apply_base_url(old.url());
        QTRY_VERIFY2_WITH_TIMEOUT(old.asked_for("/v1/symbols/" + kSymbol + "/smt?from=2026-09-16&to=2026-10-16"),
                                  qPrintable(old.asked.join(' ')), 10000);
        QTest::qWait(300);
        QVERIFY(live.smt().isEmpty());
        QVERIFY(live.failure().isEmpty());
    }

    void a_bar_window_reads_its_bar_until_nothing_more_comes() {
        // krx-quant #255: a bar window opened reads its bar close's predictions, the algorithms'
        // statuses, the bars before its scoring time and that day's special situations at once. An
        // earlier day's bar is read once; today's, its four minutes not over, again each tick without
        // the stream; a window closed, no more.
        StandIn engine;
        engine.now = "2026-10-16T10:28:00+09:00";
        services::KrxLive live;
        live.apply_base_url(engine.url());
        live.start(); // the ticks
        QTRY_VERIFY_WITH_TIMEOUT(live.status().has_value(), 10000);
        QList<BarRead> reads;
        connect(&live, &services::KrxLive::bar_changed, &live, [&reads](const BarRead& read) { reads << read; });
        const QDateTime past = QDateTime::fromString("2026-10-15T10:27:00+09:00", Qt::ISODate);
        const QDateTime today = QDateTime::fromString("2026-10-16T10:27:00+09:00", Qt::ISODate);
        live.set_bars({{kSymbol, past}, {kSymbol, today}});
        QTRY_COMPARE_WITH_TIMEOUT(reads.size(), 2, 10000);
        const auto reads_of = [&engine](const QString& route, const char* day) {
            return std::count_if(engine.asked.begin(), engine.asked.end(), [&](const QString& path) {
                return path.startsWith("/v1/symbols/" + kSymbol + route) && path.contains(QLatin1String(day));
            });
        };
        QCOMPARE(reads_of("/predictions?bar_close=", "2026-10-15T10"), 1);
        QCOMPARE(reads_of("/bars?limit=10&before=", "2026-10-15T10"), 1); // before 10:31, its scoring time
        QVERIFY(engine.asked_for("/v1/symbols/" + kSymbol + "/bars?limit=10&before=2026-10-15T10%3A31"));
        QCOMPARE(reads_of("/special?from=2026-10-15&to=", "2026-10-15"), 1);
        QVERIFY(engine.asked_for("/v1/algorithms"));
        QCOMPARE(reads[0].symbol, kSymbol);

        // A tick later: today's again (not scored yet), not the past day's.
        QTRY_VERIFY_WITH_TIMEOUT(reads_of("/predictions?bar_close=", "2026-10-16T10") >= 2, 8000);
        QCOMPARE(reads_of("/predictions?bar_close=", "2026-10-15T10"), 1);

        // Both closed: neither is read again.
        live.set_bars({});
        const auto asked = reads_of("/predictions?bar_close=", "2026-10-16T10");
        QTest::qWait(5500);
        QCOMPARE(reads_of("/predictions?bar_close=", "2026-10-16T10"), asked);
    }
};

QTEST_GUILESS_MAIN(TstKrxLive)
#include "tst_krx_live.moc"
