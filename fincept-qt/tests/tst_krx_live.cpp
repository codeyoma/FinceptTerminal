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
// The trader's buys and sells (krx-quant #398) are not read for this chart, not even the trader's symbol's:
// the auto trader screen's own chart reads and draws them (codeyoma/krx-quant#537, tests/tst_krx_trader.cpp).
//
// The SMT divergences (codeyoma/krx-quant#522) are read while the chart shows them, over the days of the bars
// held (31 at most), again with each chart read (a new bar close's), and once more after a read under way
// when asked meanwhile; none from an engine without them (404).
//
// The predictors' days (codeyoma/krx-quant#561) are read for the panel (the direction's) while the screen is
// shown and for each detail window's kind as it opens; an engine without them (404) is known as such, so the
// panel shows what it showed before.
//
// The signal alerts judge by the engine's signal rule from its status (codeyoma/krx-quant#617), the final
// consensus that came with it too, unless the user saved their own values; an engine without the rule
// leaves the app's own (60%, 2).
//
// The stand-in answers the routes these reads need (status, symbols, a symbol's bars, candles
// and predictions, and the SMT divergences when a test gives them); every other route is 404, which
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

/// A forecast of 005930's 09:31 bar close by ``algorithm``, as the engine sends it.
QJsonObject forecast_json(const char* algorithm, double up, double flat, double down) {
    return {{"algorithm", algorithm},
            {"model_version", "v1"},
            {"status", "ok"},
            {"bar_close", "2026-10-16T09:31:00+09:00"},
            {"target_at", "2026-10-16T09:35:00+09:00"},
            {"p_up", up},
            {"p_flat", flat},
            {"p_down", down},
            {"expected_ticks", 1.0},
            {"outcome", QJsonValue::Null}};
}

/// GET /v1/symbols/005930/latest at 09:31: a final consensus 65% up, both its members leaning up.
QJsonObject latest_up_65() {
    return {{"symbol", kSymbol},
            {"bar_close", "2026-10-16T09:31:00+09:00"},
            {"predictions", QJsonArray{forecast_json("gauss", 0.6, 0.3, 0.1), forecast_json("markov", 0.7, 0.2, 0.1),
                                       forecast_json("consensus-final", 0.65, 0.25, 0.1)}}};
}

/// The engine as these reads see it: each GET answered from its route, the paths asked kept.
class StandIn : public QObject {
  public:
    QString now;         ///< the engine's clock
    QJsonArray today;    ///< GET …/bars: the bars of the engine's day
    QJsonArray last_day; ///< GET …/bars?limit=N: the latest bars before now (none before them)
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
    /// GET /v1/leaderboard/days?kind=&limit= (codeyoma/krx-quant#561): this answer, of the kind asked; none:
    /// 404, as an engine before it.
    std::optional<QJsonObject> days;
    /// GET /v1/algorithms: this answer; none: 404 (the screen then shows them as not read).
    std::optional<QJsonObject> algorithms;
    /// /v1/status's ``signal_rule`` (codeyoma/krx-quant#617); none: left out, as an engine before it.
    std::optional<QJsonObject> signal_rule;
    /// GET /v1/symbols/005930/latest: this answer, and GET /v1/consensus its members (``consensus``); none: 404.
    std::optional<QJsonObject> latest;
    QStringList consensus;
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
        if (path == "/v1/status") {
            QJsonObject status{{"mode", "live"},
                               {"phase", "pre_open"},
                               {"now", now},
                               {"pending_outcomes", 0},
                               {"special", QJsonArray{}}};
            if (signal_rule)
                status.insert("signal_rule", *signal_rule);
            return status;
        }
        if (latest && path == "/v1/symbols/" + kSymbol + "/latest")
            return *latest;
        if (latest && path == "/v1/consensus")
            return {{"names", QJsonArray{"consensus-fast", "consensus-final"}},
                    {"members", QJsonArray::fromStringList(consensus)}};
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
        if (smt && path.startsWith("/v1/symbols/" + kSymbol + "/smt?"))
            return {{"symbol", kSymbol}, {"events", *smt}};
        if (days && path.startsWith("/v1/leaderboard/days?")) {
            QJsonObject answer = *days;
            answer.insert("kind", QUrlQuery(path.mid(path.indexOf('?') + 1)).queryItemValue("kind"));
            return answer;
        }
        if (algorithms && path == "/v1/algorithms")
            return *algorithms;
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

    void the_alerts_follow_the_engine_s_signal_rule_unless_the_user_set_their_own() {
        // codeyoma/krx-quant#617. The engine's 70%: its 65% final consensus is no signal, judged by that rule
        // though it came in the same read as the status.
        const QDateTime close = QDateTime::fromString("2026-10-16T09:31:00+09:00", Qt::ISODate);
        StandIn engine;
        engine.now = "2026-10-16T09:31:10+09:00";
        engine.signal_rule = QJsonObject{{"min_probability", 0.7}, {"min_agree", 2}};
        engine.latest = latest_up_65();
        engine.consensus = {"gauss", "markov"};
        services::KrxLive follows;
        QSignalSpy follows_raised(&follows, &services::KrxLive::signal_raised);
        QSignalSpy rule_changed(&follows, &services::KrxLive::engine_rule_changed);
        follows.set_alerts(true, SavedRule{});
        follows.apply_base_url(engine.url());
        QTRY_COMPARE_WITH_TIMEOUT(follows.board().latest(kSymbol).bar_close, close, 10000);
        QCOMPARE(follows.engine_rule(), (std::optional<SignalRule>(SignalRule{0.7, 2})));
        QCOMPARE(follows.rule(), (SignalRule{0.7, 2}));
        QCOMPARE(rule_changed.count(), 1);
        QCOMPARE(follows_raised.count(), 0);

        // A threshold the user saved wins: the same consensus is a signal at 60%, N still the engine's.
        services::KrxLive saved;
        QSignalSpy saved_raised(&saved, &services::KrxLive::signal_raised);
        saved.set_alerts(true, SavedRule{0.6, std::nullopt});
        saved.apply_base_url(engine.url());
        QTRY_COMPARE_WITH_TIMEOUT(saved_raised.count(), 1, 10000);
        QCOMPARE(saved.rule(), (SignalRule{0.6, 2}));

        // An engine without the rule (before #617): the app's own 60%.
        StandIn old;
        old.now = engine.now;
        old.latest = latest_up_65();
        old.consensus = engine.consensus;
        services::KrxLive older;
        QSignalSpy older_raised(&older, &services::KrxLive::signal_raised);
        older.set_alerts(true, SavedRule{});
        older.apply_base_url(old.url());
        QTRY_COMPARE_WITH_TIMEOUT(older_raised.count(), 1, 10000);
        QVERIFY(!older.engine_rule().has_value());
        QCOMPARE(older.rule(), (SignalRule{0.6, 2}));
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

    void the_days_are_read_for_the_panel_and_a_detail_window_and_an_engine_without_them_is_known() {
        // An engine before codeyoma/krx-quant#561 answers 404: none, and the panel is as before.
        StandIn old;
        old.now = "2026-11-05T15:50:00+09:00";
        services::KrxLive live;
        live.apply_base_url(old.url());
        live.set_shown(true);
        QTRY_VERIFY_WITH_TIMEOUT(old.asked_for("/v1/leaderboard/days?kind=direction&limit=20"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.days_served(), 10000);
        QVERIFY(live.days().isEmpty());
        // An engine that gives them (made-up values): the direction's for the panel, shown.
        StandIn engine;
        engine.now = old.now;
        engine.days = QJsonObject{
            {"days", QJsonArray{"2026-11-05"}}, {"climate", "baseline-climate"}, {"min_sample", 200},
            {"algorithms", QJsonArray{}},       {"baselines", QJsonArray{}},     {"consensus", QJsonArray{}},
            {"members", QJsonArray{}}};
        engine.algorithms =
            QJsonObject{{"algorithms", QJsonArray{QJsonObject{{"name", "alg-r"},
                                                              {"kind", "range"},
                                                              {"state", "probation"},
                                                              {"status", QJsonObject{{"status", "probation"}}}}}},
                        {"baselines", QJsonArray{}},
                        {"consensus", QJsonArray{}}};
        services::KrxLive fresh;
        fresh.apply_base_url(engine.url());
        fresh.set_shown(true);
        QTRY_VERIFY_WITH_TIMEOUT(fresh.days().contains("direction"), 10000);
        QVERIFY(fresh.days_served());
        QCOMPARE(fresh.days().value("direction").days, QStringList{"2026-11-05"});
        QVERIFY(!engine.asked_for("/v1/leaderboard/days?kind=range"));
        // A detail window of the range's algorithm: its kind's, read as it opens.
        QTRY_VERIFY_WITH_TIMEOUT(fresh.algorithms().contains("alg-r"), 10000);
        QCOMPARE(fresh.kind_of("alg-r"), QStringLiteral("range"));
        QCOMPARE(fresh.states().value("alg-r"), QStringLiteral("probation"));
        fresh.set_details({"alg-r"});
        QTRY_VERIFY_WITH_TIMEOUT(fresh.days().contains("range"), 10000);
        QCOMPARE(fresh.days().value("range").kind, QStringLiteral("range"));
        // Another address: not known to be without them until that one answers.
        live.apply_base_url(engine.url());
        QVERIFY(live.days_served());
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
