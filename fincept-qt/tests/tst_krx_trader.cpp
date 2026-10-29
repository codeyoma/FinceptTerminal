// Tests for the KRX auto trader screen's model (codeyoma/krx-quant#397): how it reads GET /v1/trader,
// /v1/trader/days and /v1/trader/trades (src/services/krx_quant/EngineData.h, parse_trader and the
// others), what it shows of them (src/services/krx_quant/TraderView.h) and when it reads them and sends
// the screen's pause (src/services/krx_quant/KrxTraderLive.h), the last against a stand-in engine on
// this computer. The values are made up. The settings (the engine's address) go to a temporary INI
// file: AppConfig is defined below, in place of core/config/AppConfig.cpp.

#include "core/config/AppConfig.h"
#include "services/krx_quant/KrxQuantService.h"
#include "services/krx_quant/KrxTraderLive.h"
#include "services/krx_quant/TraderView.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QTimer>

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
namespace view = fincept::krx_quant::trader;
using view::Tone;

namespace {

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODateWithMs);
}

QJsonObject limit_json(double percent, double won, double used, QJsonValue reached = QJsonValue()) {
    return {{"percent", percent}, {"won", won}, {"used", used}, {"reached_at", reached}};
}

QJsonObject control_json(int id, const char* at, const char* action, const char* source, QJsonValue sell_now,
                         QJsonValue applied) {
    return {{"id", id},       {"at", at},           {"action", action},     {"source", source},
            {"sell_now", sell_now}, {"mode", QJsonValue()}, {"applied_at", applied}};
}

/// The trader running in paper at 10:06 with a position open since 10:05:02.5, its time barrier at
/// 10:09; a resume from the server and a pause from the screen before; made-up values.
QJsonObject state_json(const char* mode = "paper", const char* status = "running", bool alive = true,
                       bool position = true) {
    QJsonObject o{{"updated_at", "2026-10-15T10:06:00+09:00"},
                  {"alive", alive},
                  {"mode", mode},
                  {"status", status},
                  {"started_at", "2026-10-15T07:20:05+09:00"},
                  {"capital", 1000000},
                  {"cash", 14142.79},
                  {"position", QJsonValue()},
                  {"today",
                   QJsonObject{{"day", "2026-10-15"},
                               {"realized", 0.0},
                               {"unrealized", -5051.2},
                               {"net", -5051.2},
                               {"trades", 0},
                               {"worst", -5051.2}}},
                  {"cumulative",
                   QJsonObject{{"since", "2026-10-14T07:20:03+09:00"},
                               {"realized", -4310.0},
                               {"unrealized", -5051.2},
                               {"net", -9361.2},
                               {"worst", -9361.2}}},
                  {"limits",
                   QJsonObject{{"daily", limit_json(10, 100000, 0.0505)}, {"cumulative", limit_json(20, 200000, 0.0468)}}},
                  {"pause", QJsonValue()},
                  {"controls", QJsonArray{control_json(2, "2026-10-15T10:00:02.400+09:00", "resume", "cli", QJsonValue(),
                                                       "2026-10-15T10:00:02.600+09:00"),
                                          control_json(1, "2026-10-14T10:07:00+09:00", "pause", "api", false,
                                                       "2026-10-14T10:07:00.300+09:00")}}};
    if (position)
        o["position"] = QJsonObject{{"trade_id", 7},
                                    {"kind", "barrier"},
                                    {"predictor", "breiman"},
                                    {"quantity", 14},
                                    {"entry_price", 70100.0},
                                    {"entry_at", "2026-10-15T10:05:02.500000+09:00"},
                                    {"target", 70250.0},
                                    {"stop", 69850.0},
                                    {"time_barrier", "2026-10-15T10:09:00+09:00"},
                                    {"bid", 69900.0},
                                    {"bid_at", "2026-10-15T10:06:00+09:00"},
                                    {"unrealized", -5051.2},
                                    {"selling", false}};
    return o;
}

/// Before the trader first wrote its state: all null but alive and controls.
QJsonObject unwritten_json() {
    QJsonObject o;
    for (const char* key : {"updated_at", "mode", "status", "started_at", "capital", "cash", "position", "today",
                            "cumulative", "limits", "pause"})
        o[key] = QJsonValue();
    o["alive"] = false;
    o["controls"] = QJsonArray{};
    return o;
}

QJsonObject day_json(const char* day, const char* mode, double net, int trades, const char* status = "running",
                     QJsonValue halt = QJsonValue()) {
    return {{"day", day},
            {"mode", mode},
            {"net", net},
            {"trades", trades},
            {"daily", QJsonObject{{"limit", 100000.0}, {"worst", std::min(0.0, net)}, {"used", std::max(0.0, -net) / 100000}}},
            {"cumulative", QJsonObject{{"limit", 200000.0}, {"net", net}, {"worst", std::min(0.0, net)}, {"used", 0.01}}},
            {"status", status},
            {"halt", halt},
            {"updated_at", QStringLiteral("%1T15:29:59.750+09:00").arg(QString::fromLatin1(day))}};
}

QJsonObject days_json() {
    return {{"from", "2025-10-16"},
            {"to", "2026-10-15"},
            {"days", QJsonArray{day_json("2026-10-13", "paper", 1922.25, 1),
                                day_json("2026-10-14", "paper", -105000.0, 2, "daily_limit", "daily_limit"),
                                day_json("2026-10-15", "paper", 0.0, 0),
                                day_json("2026-10-16", "mock", -161.735, 1)}}};
}

/// A trade: closed with ``reason`` unless it is null (held).
QJsonObject trade_json(int id, QJsonValue reason, const char* mode = "paper", bool picked = true) {
    const bool held = reason.isNull();
    QJsonObject o{{"id", id},
                  {"mode", mode},
                  {"symbol", "005930"},
                  {"kind", "barrier"},
                  {"predictor", "breiman"},
                  {"role", picked ? QJsonValue("top") : QJsonValue()},
                  {"prediction_id", 42},
                  {"bar_close", "2026-10-14T10:00:00+09:00"},
                  {"probability", 0.7},
                  {"threshold", 0.6},
                  {"p_star", picked ? QJsonValue(0.63) : QJsonValue()},
                  {"precision_low", picked ? QJsonValue(0.7) : QJsonValue()},
                  {"kelly", picked ? QJsonValue(10.5) : QJsonValue()},
                  {"fraction", picked ? QJsonValue(1.0) : QJsonValue()},
                  {"policy", picked ? QJsonValue(1) : QJsonValue()},
                  {"anchor", 70050.0},
                  {"tick", 100.0},
                  {"width_ticks", 2},
                  {"target", 70250.0},
                  {"stop", 69850.0},
                  {"time_barrier", "2026-10-14T10:04:00+09:00"},
                  {"decided_at", "2026-10-14T10:00:02.300+09:00"},
                  {"entry", QJsonObject{{"at", "2026-10-14T10:00:02.500+09:00"}, {"price", 70100.0}, {"quantity", 14}}},
                  {"exit", QJsonValue()},
                  {"buy_fee", 147.21},
                  {"sell_fee", QJsonValue()},
                  {"tax", QJsonValue()},
                  {"cost", QJsonValue()},
                  {"gross", QJsonValue()},
                  {"net", QJsonValue()},
                  {"orders", QJsonArray{}}};
    if (!held) {
        const bool gain = reason.toString() == QLatin1String("take_profit");
        const double price = gain ? 70250.0 : 69800.0;
        o["exit"] = QJsonObject{{"at", "2026-10-14T10:02:00+09:00"}, {"price", price}, {"reason", reason}};
        o["sell_fee"] = 147.525;
        o["tax"] = 1967.0;
        o["cost"] = 2261.735;
        o["gross"] = (price - 70100.0) * 14;
        o["net"] = (price - 70100.0) * 14 - 2261.735;
    }
    return o;
}

QJsonObject trades_json() {
    return {{"trades", QJsonArray{trade_json(7, QJsonValue()), trade_json(6, "cumulative_limit"),
                                  trade_json(5, "daily_limit"), trade_json(4, "pause"), trade_json(3, "time"),
                                  trade_json(2, "stop_loss", "live"), trade_json(1, "take_profit", "paper", false)}}};
}

TraderState state(const QJsonObject& o = state_json()) {
    auto s = parse_trader(QJsonDocument(o));
    if (s.is_err())
        qFatal("%s", s.error().c_str());
    return s.value();
}

QList<TraderDay> days() {
    auto d = parse_trader_days(QJsonDocument(days_json()));
    if (d.is_err())
        qFatal("%s", d.error().c_str());
    return d.value();
}

QList<TraderTrade> trades() {
    auto t = parse_trader_trades(QJsonDocument(trades_json()));
    if (t.is_err())
        qFatal("%s", t.error().c_str());
    return t.value();
}

/// The engine as the auto trader screen reads it: GET /v1/trader, /v1/trader/days, /v1/trader/trades
/// and POST /v1/trader/pause, each request kept (its method, path, content type and body).
class StandIn : public QObject {
  public:
    struct Request {
        QString method;
        QString path;
        QString content_type;
        QByteArray body;
    };
    bool fail = false;         ///< every answer fails
    bool fail_pause = false;   ///< the pause alone
    bool without_trader = false; ///< an engine before codeyoma/krx-quant#396: 404
    QJsonObject state = state_json();
    QList<Request> asked;

    StandIn() {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = server_.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        server_.listen(QHostAddress::LocalHost);
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()); }
    qsizetype count(const QString& method, const QString& path) const {
        return std::count_if(asked.cbegin(), asked.cend(), [&](const Request& r) {
            return r.method == method && r.path.section('?', 0, 0) == path;
        });
    }

  private:
    void read(QTcpSocket* socket) {
        QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        const qsizetype head_end = request.indexOf("\r\n\r\n");
        if (head_end < 0)
            return;
        const QByteArray head = request.left(head_end);
        qsizetype length = 0;
        QString content_type;
        for (const QByteArray& line : head.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:"))
                length = trimmed.mid(15).trimmed().toLongLong();
            if (trimmed.toLower().startsWith("content-type:"))
                content_type = QString::fromLatin1(trimmed.mid(13).trimmed());
        }
        const QByteArray body = request.mid(head_end + 4);
        if (body.size() < length)
            return; // the rest of the body is on its way
        const QList<QByteArray> first = head.left(head.indexOf("\r\n")).split(' ');
        const Request r{QString::fromLatin1(first.value(0)), QString::fromLatin1(first.value(1)), content_type,
                        body.left(length)};
        asked << r;
        int status = 404;
        QJsonObject answer{{"error", "not found"}};
        const QString path = r.path.section('?', 0, 0);
        if (without_trader) {
        } else if (fail || (fail_pause && path == "/v1/trader/pause")) {
            status = 500;
            answer = {{"error", "made-up failure"}};
        } else if (r.method == "GET" && path == "/v1/trader") {
            status = 200;
            answer = state;
        } else if (r.method == "GET" && path == "/v1/trader/days") {
            status = 200;
            answer = days_json();
        } else if (r.method == "GET" && path == "/v1/trader/trades") {
            status = 200;
            answer = trades_json();
        } else if (r.method == "POST" && path == "/v1/trader/pause") {
            const QJsonObject asked_for = QJsonDocument::fromJson(r.body).object();
            status = 202;
            answer = {{"control", control_json(3, "2026-10-15T10:07:00+09:00", "pause", "api",
                                               asked_for.value("sell_now"), QJsonValue())}};
        }
        const QByteArray bytes = QJsonDocument(answer).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status < 300 ? " OK" : " Error") +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                      "\r\nConnection: close\r\n\r\n" + bytes);
        socket->disconnectFromHost();
    }

    QTcpServer server_;
};

/// The live object's own schedule, fired now (its next read, as the timer would start it).
void fire_schedule(services::KrxTraderLive& live) {
    auto* timer = live.findChild<QTimer*>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(timer);
    timer->start(0);
}

} // namespace

class TstKrxTrader : public QObject {
    Q_OBJECT

    QTemporaryDir settings_;

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        settings_file = settings_.filePath("settings.ini");
    }

    void the_state_is_read_with_its_position_limits_pause_and_controls() {
        QJsonObject o = state_json("live", "entries_paused");
        o["pause"] = QJsonObject{{"at", "2026-10-14T10:07:00+09:00"}, {"sell_now", false}, {"source", "api"}};
        const TraderState s = state(o);
        QVERIFY(s.written());
        QVERIFY(s.alive);
        QCOMPARE(s.mode, QStringLiteral("live"));
        QCOMPARE(s.status, QStringLiteral("entries_paused"));
        QCOMPARE(*s.capital, 1000000.0);
        QCOMPARE(*s.cash, 14142.79);
        QVERIFY(s.position.has_value());
        QCOMPARE(s.position->trade_id, 7);
        QCOMPARE(s.position->quantity, 14);
        QCOMPARE(s.position->time_barrier, kst("2026-10-15T10:09:00+09:00"));
        QCOMPARE(*s.position->bid, 69900.0);
        QCOMPARE(s.today->day, QDate(2026, 10, 15));
        QCOMPARE(s.today->net, -5051.2);
        QCOMPARE(s.cumulative->since, kst("2026-10-14T07:20:03+09:00"));
        QCOMPARE(s.daily_limit->won, 100000.0);
        QCOMPARE(s.cumulative_limit->used, 0.0468);
        QVERIFY(!s.daily_limit->reached_at.isValid());
        QCOMPARE(s.pause->source, QStringLiteral("api"));
        QCOMPARE(s.controls.size(), 2);
        QCOMPARE(s.controls[0].action, QStringLiteral("resume"));
        QVERIFY(!s.controls[0].sell_now.has_value()); // a resume's is null
        QCOMPARE(*s.controls[1].sell_now, false);
        QVERIFY(s.controls[1].applied_at.isValid());

        // A position before its first quote since the buy: no bid yet.
        QJsonObject fresh = state_json();
        QJsonObject p = fresh["position"].toObject();
        p["bid"] = QJsonValue();
        p["bid_at"] = QJsonValue();
        fresh["position"] = p;
        QVERIFY(!state(fresh).position->bid.has_value());
    }

    void a_state_the_trader_never_wrote_has_only_alive_and_controls() {
        const TraderState s = state(unwritten_json());
        QVERIFY(!s.written());
        QVERIFY(!s.alive);
        QVERIFY(s.mode.isEmpty());
        QVERIFY(!s.capital.has_value());
        QVERIFY(!s.position.has_value());
        QVERIFY(!s.today.has_value());
        QVERIFY(!s.daily_limit.has_value());
        QVERIFY(s.controls.isEmpty());
    }

    void the_days_and_trades_are_read_with_their_nulls() {
        const QList<TraderDay> d = days();
        QCOMPARE(d.size(), 4);
        QCOMPARE(d[1].halt, QStringLiteral("daily_limit"));
        QCOMPARE(d[1].daily_used, 1.05);
        QVERIFY(d[0].halt.isEmpty());
        QCOMPARE(d[3].mode, QStringLiteral("mock"));

        const QList<TraderTrade> t = trades();
        QCOMPARE(t.size(), 7);
        QVERIFY(t[0].held());
        QVERIFY(!t[0].net.has_value());
        QVERIFY(!t[0].exit_price.has_value());
        QCOMPARE(t[1].exit_reason, QStringLiteral("cumulative_limit"));
        QCOMPARE(*t[1].cost, 2261.735);
        QCOMPARE(*t[0].policy, 1);
        QVERIFY(!t[6].p_star.has_value()); // entered before the cost map picked the cells (#395)
        QVERIFY(!t[6].policy.has_value());
        QCOMPARE(t[6].role, QString());
        QCOMPARE(*t[6].probability, 0.7);
    }

    void the_engine_s_error_and_a_pause_s_answer_are_read() {
        const QJsonDocument error(QJsonObject{{"error", "the body must be application/json"}});
        QVERIFY(parse_trader(error).is_err());
        QVERIFY(parse_trader_days(error).is_err());
        QVERIFY(parse_trader_trades(error).is_err());
        QVERIFY(parse_trader_pause(error).is_err());
        const auto control = parse_trader_pause(QJsonDocument(QJsonObject{
            {"control", control_json(3, "2026-10-15T10:07:00+09:00", "pause", "api", true, QJsonValue())}}));
        QVERIFY(control.is_ok());
        QCOMPARE(control.value().id, 3);
        QCOMPARE(*control.value().sell_now, true);
        QVERIFY(!control.value().applied_at.isValid());
    }

    void the_header_says_the_mode_and_the_status() {
        QCOMPARE(view::mode_badge(state(state_json("paper"))).text, QStringLiteral("모의"));
        QCOMPARE(view::mode_badge(state(state_json("paper"))).tone, Tone::Simulated);
        QCOMPARE(view::mode_badge(state(state_json("mock"))).text, QStringLiteral("KIS 모의"));
        QCOMPARE(view::mode_badge(state(state_json("live"))).text, QStringLiteral("실전"));
        QCOMPARE(view::mode_badge(state(state_json("live"))).tone, Tone::Live);
        QVERIFY(view::mode_badge(state(state_json("live"))).tip.contains(QStringLiteral(".env")));

        const QList<std::pair<const char*, std::pair<QString, Tone>>> statuses{
            {"running", {QStringLiteral("도는 중"), Tone::Positive}},
            {"entries_paused", {QStringLiteral("새 진입 정지"), Tone::Warning}},
            {"paused", {QStringLiteral("정지"), Tone::Warning}},
            {"daily_limit", {QStringLiteral("하루 한도"), Tone::Negative}},
            {"cumulative_limit", {QStringLiteral("누적 한도"), Tone::Negative}},
        };
        for (const auto& [status, shown] : statuses) {
            const view::Badge badge = view::status_badge(state(state_json("paper", status)));
            QCOMPARE(badge.text, shown.first);
            QCOMPARE(badge.tone, shown.second);
        }
        QVERIFY(view::status_badge(state(state_json("paper", "entries_paused"))).tip.contains("krx-quant trade resume"));
        QVERIFY(view::status_badge(state(state_json("paper", "cumulative_limit"))).tip.contains("krx-quant trade rearm"));

        // A trader that has not written its state for a while: its last values, said to be so.
        const view::Badge stopped = view::status_badge(state(state_json("paper", "running", false)));
        QCOMPARE(stopped.text, QStringLiteral("응답 없음"));
        QCOMPARE(stopped.tone, Tone::Negative);
        QVERIFY(stopped.tip.contains(QStringLiteral("10-15 10:06:00")));
        QVERIFY(stopped.tip.contains(QStringLiteral("도는 중")));
        // Never written: nothing to show but that.
        QCOMPARE(view::status_badge(state(unwritten_json())).text, QStringLiteral("상태 없음"));
        QCOMPARE(view::mode_badge(state(unwritten_json())).text, QStringLiteral("—"));

        QCOMPARE(view::header_line(state()), QStringLiteral("상태 10-15 10:06:00 · 시작 10-15 07:20"));
        QJsonObject paused = state_json("paper", "paused");
        paused["pause"] = QJsonObject{{"at", "2026-10-15T10:05:30+09:00"}, {"sell_now", true}, {"source", "api"}};
        QCOMPARE(view::header_line(state(paused)),
                 QStringLiteral("상태 10-15 10:06:00 · 시작 10-15 07:20 · 정지 10-15 10:05 화면(정지하고 즉시 매도)"));
        QCOMPARE(view::header_line(state(unwritten_json())), QStringLiteral("trader가 아직 상태를 쓰지 않았습니다"));
    }

    void the_cards_show_the_position_with_its_time_left() {
        const TraderState s = state();
        const QDateTime now = kst("2026-10-15T10:06:30+09:00");
        const QList<view::Card> cards = view::cards(s, now);
        QCOMPARE(cards.size(), 7);
        QCOMPARE(cards[0].title, QStringLiteral("운용 상한"));
        QCOMPARE(cards[0].value, QStringLiteral("1,000,000원"));
        QCOMPARE(cards[1].value, QStringLiteral("14,143원"));
        QCOMPARE(cards[2].title, QStringLiteral("포지션"));
        QCOMPARE(cards[2].value, QStringLiteral("14주 × 70,100원"));
        QVERIFY(cards[2].lines.contains(QStringLiteral("목표가 70,250원 · 손절가 69,850원")));
        QVERIFY(cards[2].lines.contains(QStringLiteral("시간 장벽 10:09:00 · 2분 30초 남음")));
        QVERIFY(cards[2].lines.contains(QStringLiteral("매수 1호가 69,900원 · 평가 -5,051원")));
        QVERIFY(cards[2].lines.contains(QStringLiteral("장벽 4분 · breiman")));
        QCOMPARE(cards[3].title, QStringLiteral("오늘 순손익"));
        QCOMPARE(cards[3].value, QStringLiteral("-5,051원"));
        QCOMPARE(cards[3].tone, Tone::Negative);
        QVERIFY(cards[3].lines.contains(QStringLiteral("실현 0원 · 매매 0")));
        QCOMPARE(cards[4].value, QStringLiteral("-9,361원"));
        QVERIFY(cards[4].lines.contains(QStringLiteral("2026-10-14 07:20부터")));
        QCOMPARE(cards[5].title, QStringLiteral("하루 한도 사용"));
        QCOMPARE(cards[5].value, QStringLiteral("5.1%"));
        QVERIFY(cards[5].lines.contains(QStringLiteral("한도 -100,000원 (운용 상한의 10%)")));
        QCOMPARE(cards[5].tone, Tone::Plain);
        QCOMPARE(cards[6].value, QStringLiteral("4.7%"));

        // The time left counts down on the trader's clock, and says when it is past.
        QCOMPARE(view::remaining_text(kst("2026-10-15T10:09:00+09:00"), kst("2026-10-15T10:08:48+09:00")),
                 QStringLiteral("12초 남음"));
        QCOMPARE(view::remaining_text(kst("2026-10-15T10:09:00+09:00"), kst("2026-10-15T10:09:01+09:00")),
                 QStringLiteral("지남"));
        const QDateTime answered = kst("2026-10-15T19:00:00+09:00"); // the screen's clock, another than the trader's
        QCOMPARE(view::trader_now(s, answered, answered.addSecs(3)), kst("2026-10-15T10:06:03+09:00"));
        QCOMPARE(view::trader_now(state(unwritten_json()), answered, answered.addSecs(3)), answered.addSecs(3));

        // Being sold by a pause or a limit.
        QJsonObject selling = state_json("paper", "paused");
        QJsonObject p = selling["position"].toObject();
        p["selling"] = true;
        selling["position"] = p;
        const view::Card sold = view::cards(state(selling), now)[2];
        QCOMPARE(sold.tone, Tone::Warning);
        QVERIFY(sold.lines.contains(QStringLiteral("매도 중: 다음 호가의 매수 1호가에 팝니다")));
    }

    void without_a_position_the_card_says_none_and_a_limit_reached_is_red() {
        QJsonObject o = state_json("paper", "daily_limit", true, false);
        QJsonObject limits = o["limits"].toObject();
        limits["daily"] = limit_json(10, 100000, 1.03, "2026-10-15T10:04:10+09:00");
        limits["cumulative"] = limit_json(20, 200000, 0.6);
        o["limits"] = limits;
        const QList<view::Card> cards = view::cards(state(o), kst("2026-10-15T10:06:30+09:00"));
        QCOMPARE(cards[2].value, QStringLiteral("없음"));
        QCOMPARE(cards[2].tone, Tone::Dim);
        QVERIFY(cards[2].lines.isEmpty());
        QCOMPARE(cards[5].value, QStringLiteral("103.0%"));
        QCOMPARE(cards[5].tone, Tone::Negative);
        QVERIFY(cards[5].lines.contains(QStringLiteral("닿음 10-15 10:04:10")));
        QCOMPARE(cards[6].tone, Tone::Warning); // over half of it
        // Before the trader first wrote its state: dashes.
        for (const auto& card : view::cards(state(unwritten_json()), kst("2026-10-15T10:06:30+09:00")))
            QCOMPARE(card.value, QStringLiteral("—"));
    }

    void the_trades_table_says_each_exit_its_costs_and_why_it_was_bought() {
        const auto rows = view::trade_rows(trades(), state());
        QCOMPARE(rows.size(), 7);
        // Held: the position's P&L at the bid, the buy's commission alone.
        QVERIFY(rows[0].held);
        QCOMPARE(rows[0].entry, QStringLiteral("10-14 10:00:02 · 70,100"));
        QCOMPARE(rows[0].exit, QStringLiteral("보유 중"));
        QCOMPARE(rows[0].reason, QStringLiteral("—"));
        QCOMPARE(rows[0].cost, QStringLiteral("147원 (매수)"));
        QCOMPARE(rows[0].net, QStringLiteral("평가 -5,051원"));
        QCOMPARE(rows[0].net_tone, Tone::Dim);
        QCOMPARE(rows[0].quantity, QStringLiteral("14주"));
        QCOMPARE(rows[0].cell, QStringLiteral("장벽 4분 · breiman (1위)"));
        QCOMPARE(rows[0].probability, QStringLiteral("70.0% (θ 60.0%)"));
        QCOMPARE(rows[0].p_star, QStringLiteral("63.0%"));
        QCOMPARE(rows[0].low, QStringLiteral("70.0%"));
        QVERIFY(rows[0].why_tip.contains(QStringLiteral("Kelly f* 10.50 → 산 비율 100.0%")));
        QVERIFY(rows[0].tip.contains(QStringLiteral("목표가 70,250 · 손절가 69,850 · 시간 장벽 10:04:00")));
        QVERIFY(rows[0].tip.contains(QStringLiteral("정책 버전 1")));
        // Each exit's reason, its colour, its costs and net.
        const QList<std::pair<QString, Tone>> reasons{{QStringLiteral("누적 한도"), Tone::Negative},
                                                      {QStringLiteral("하루 한도"), Tone::Negative},
                                                      {QStringLiteral("정지 매도"), Tone::Warning},
                                                      {QStringLiteral("시간 청산"), Tone::Plain},
                                                      {QStringLiteral("손절"), Tone::Negative},
                                                      {QStringLiteral("익절"), Tone::Positive}};
        for (qsizetype i = 0; i < reasons.size(); ++i) {
            QCOMPARE(rows[i + 1].reason, reasons[i].first);
            QCOMPARE(rows[i + 1].reason_tone, reasons[i].second);
        }
        QCOMPARE(rows[1].exit, QStringLiteral("10:02:00 · 69,800"));
        QCOMPARE(rows[1].cost, QStringLiteral("2,262원"));
        QVERIFY(rows[1].cost_tip.contains(QStringLiteral("매수 수수료 147원 + 매도 수수료 148원 + 매도세 1,967원")));
        QCOMPARE(rows[1].net, QStringLiteral("-6,462원"));
        QCOMPARE(rows[1].net_tone, Tone::Negative);
        QCOMPARE(rows[6].net, QStringLiteral("-162원"));
        QCOMPARE(rows[5].mode, QStringLiteral("실전"));
        QVERIFY(rows[5].live);
        QVERIFY(!rows[6].live);
        // Before the cost map picked the cells (#395): no p*, lower bound nor role.
        QCOMPARE(rows[6].p_star, QStringLiteral("—"));
        QCOMPARE(rows[6].low, QStringLiteral("—"));
        QCOMPARE(rows[6].cell, QStringLiteral("장벽 4분 · breiman"));
        QVERIFY(rows[6].why_tip.contains(QStringLiteral("#395")));
        // Held, but not the position the state has (the state not read yet): no P&L to show.
        const auto unread = view::trade_rows(trades(), std::nullopt);
        QCOMPARE(unread[0].net, QStringLiteral("—"));
        // Held while a pause sells it.
        QJsonObject selling = state_json("paper", "paused");
        QJsonObject p = selling["position"].toObject();
        p["selling"] = true;
        selling["position"] = p;
        QCOMPARE(view::trade_rows(trades(), state(selling))[0].reason, QStringLiteral("매도 중"));
        // An exit on another day than its entry has its date.
        QJsonObject later = trade_json(8, "time");
        QJsonObject out = later["exit"].toObject();
        out["at"] = "2026-10-15T09:05:00+09:00";
        later["exit"] = out;
        const auto next_day = parse_trader_trades(QJsonDocument(QJsonObject{{"trades", QJsonArray{later}}})).value();
        QCOMPARE(view::trade_rows(next_day, std::nullopt)[0].exit, QStringLiteral("10-15 09:05:00 · 69,800"));
        QCOMPARE(view::kind_label("barrier_30m"), QStringLiteral("장벽 30분"));
        QCOMPARE(view::kind_label("direction"), QStringLiteral("방향 4분"));
    }

    void the_days_curve_is_one_mode_s_days_with_their_running_sum() {
        const QList<TraderDay> d = days();
        QCOMPARE(view::day_modes(d), (QStringList{"mock", "paper"})); // the latest day's first, then others
        QCOMPARE(view::default_mode(d, state()), QStringLiteral("paper")); // the trader's now
        QCOMPARE(view::default_mode(d, std::nullopt), QStringLiteral("mock"));
        QCOMPARE(view::default_mode(d, state(state_json("live"))), QStringLiteral("mock")); // live has no days
        const auto points = view::day_points(d, "paper");
        QCOMPARE(points.size(), 3);
        QCOMPARE(points[0].day, QDate(2026, 10, 13));
        QCOMPARE(points[1].sum, 1922.25 - 105000.0);
        QVERIFY(points[1].halted);
        QVERIFY(points[1].tip.contains(QStringLiteral("10-14(수) 모의")));
        QVERIFY(points[1].tip.contains(QStringLiteral("하루 한도에 닿은 날")));
        QVERIFY(points[1].tip.contains(QStringLiteral("그날 마지막 상태: 하루 한도")));
        QCOMPARE(view::days_line(points, "paper"), QStringLiteral("모의 3일 · 순손익 합 -103,078원 · 이익 1일 · 손실 1일"));
        QCOMPARE(view::days_line({}, "live"), QStringLiteral("실전 거래일이 없습니다"));
        QCOMPARE(view::days_line({}, QString()), QStringLiteral("trader가 돈 거래일이 아직 없습니다"));
    }

    void the_recent_requests_say_what_who_and_whether_the_trader_read_them() {
        QJsonObject o = state_json();
        QJsonArray controls = o["controls"].toArray();
        controls.prepend(control_json(3, "2026-10-15T10:07:00+09:00", "pause", "api", true, QJsonValue()));
        QJsonObject rearm = control_json(0, "2026-10-13T08:00:00+09:00", "rearm", "cli", QJsonValue(), "2026-10-13T08:00:00.500+09:00");
        rearm["mode"] = "paper";
        controls.append(rearm);
        o["controls"] = controls;
        const auto rows = view::control_rows(state(o));
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[0].what, QStringLiteral("정지하고 즉시 매도"));
        QCOMPARE(rows[0].who, QStringLiteral("화면"));
        QCOMPARE(rows[0].applied, QStringLiteral("아직 읽지 않음"));
        QVERIFY(rows[0].waiting);
        QCOMPARE(rows[1].what, QStringLiteral("재개"));
        QCOMPARE(rows[1].who, QStringLiteral("서버"));
        QCOMPARE(rows[1].applied, QStringLiteral("읽음 10:00:02"));
        QCOMPARE(rows[2].what, QStringLiteral("새 진입 정지"));
        QCOMPARE(rows[3].what, QStringLiteral("누적 한도 다시 켜기 (모의)"));
    }

    void a_pause_button_asks_first_and_says_resuming_is_the_server_s() {
        const TraderState s = state();
        const view::Question entries = view::pause_question(false, s);
        QCOMPARE(entries.title, QStringLiteral("새 진입 정지"));
        QCOMPARE(entries.accept, QStringLiteral("새 진입 정지"));
        QVERIFY(entries.text.contains(QStringLiteral("들고 있는 14주(장벽 4분 · breiman)는 원래 규칙")));
        QVERIFY(entries.text.contains(QStringLiteral("krx-quant trade resume")));
        QVERIFY(entries.text.contains(QStringLiteral("재개 버튼이 없습니다")));
        const view::Question sell = view::pause_question(true, state(state_json("live")));
        QCOMPARE(sell.title, QStringLiteral("정지하고 즉시 매도"));
        QVERIFY(sell.text.contains(QStringLiteral("들고 있는 14주(장벽 4분 · breiman, 평가 -5,051원)를 다음 호가의 매수 "
                                                  "1호가에 바로 팝니다")));
        QVERIFY(sell.text.contains(QStringLiteral("실전 계좌의 매도 주문")));
        QVERIFY(view::pause_question(true, state(state_json("paper", "running", true, false)))
                    .text.contains(QStringLiteral("들고 있는 포지션은 없습니다")));
        QVERIFY(view::resume_note().contains(QStringLiteral("krx-quant trade resume")));

        // Both can be pressed while the trader runs, none before the state is read.
        QVERIFY(view::pause_action(false, s).enabled);
        QVERIFY(view::pause_action(true, s).enabled);
        QVERIFY(!view::pause_action(true, std::nullopt).enabled);
        // Pausing new entries again does nothing; selling at once still does.
        QJsonObject paused = state_json("paper", "entries_paused");
        paused["pause"] = QJsonObject{{"at", "2026-10-15T10:05:30+09:00"}, {"sell_now", false}, {"source", "api"}};
        QVERIFY(!view::pause_action(false, state(paused)).enabled);
        QVERIFY(view::pause_action(false, state(paused)).tip.contains(QStringLiteral("krx-quant trade resume")));
        QVERIFY(view::pause_action(true, state(paused)).enabled);
        paused["pause"] = QJsonObject{{"at", "2026-10-15T10:05:30+09:00"}, {"sell_now", true}, {"source", "api"}};
        QVERIFY(!view::pause_action(true, state(paused)).enabled);
        // Asked for and not read yet: not again.
        QJsonObject waiting = state_json();
        QJsonArray controls = waiting["controls"].toArray();
        controls.prepend(control_json(3, "2026-10-15T10:07:00+09:00", "pause", "api", false, QJsonValue()));
        waiting["controls"] = controls;
        QVERIFY(!view::pause_action(false, state(waiting)).enabled);
        QVERIFY(view::pause_action(false, state(waiting)).tip.contains(QStringLiteral("아직 읽지 않았습니다")));
        QVERIFY(view::pause_action(true, state(waiting)).enabled); // a pause of new entries does not sell
    }

    void it_reads_every_2s_with_a_position_5s_in_the_session_else_5_minutes() {
        const QDateTime session = kst("2026-10-15T10:06:30+09:00"); // a Thursday
        QCOMPARE(view::poll_ms(state(), session), view::kHeldMs);
        QCOMPARE(view::poll_ms(state(), kst("2026-10-15T20:00:00+09:00")), view::kHeldMs); // held: whenever
        QCOMPARE(view::poll_ms(state(state_json("paper", "running", true, false)), session), view::kSessionMs);
        QCOMPARE(view::poll_ms(std::nullopt, session), view::kSessionMs);
        // A position of a trader not running: as without one.
        QCOMPARE(view::poll_ms(state(state_json("paper", "running", false)), kst("2026-10-15T20:00:00+09:00")),
                 view::kIdleMs);
        QCOMPARE(view::poll_ms(std::nullopt, kst("2026-10-15T08:59:59+09:00")), view::kIdleMs);
        QCOMPARE(view::poll_ms(std::nullopt, kst("2026-10-15T15:20:00+09:00")), view::kIdleMs);
        QCOMPARE(view::poll_ms(std::nullopt, kst("2026-10-17T10:00:00+09:00")), view::kIdleMs); // a Saturday
        QCOMPARE(view::poll_ms(std::nullopt, QDateTime(QDate(2026, 10, 15), QTime(1, 0), QTimeZone::UTC)),
                 view::kSessionMs); // 10:00 KST
    }

    void the_screen_reads_the_state_with_its_days_and_trades_and_then_the_state_on_schedule() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        QDateTime clock = kst("2026-10-15T10:06:30+09:00");
        services::KrxTraderLive live(nullptr, [&clock] { return clock; });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.state().has_value() && live.days().has_value() && live.trades().has_value(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        QCOMPARE(engine.count("GET", "/v1/trader/days"), 1);
        QCOMPARE(engine.count("GET", "/v1/trader/trades"), 1);
        QVERIFY(std::any_of(engine.asked.cbegin(), engine.asked.cend(),
                            [](const StandIn::Request& r) { return r.path == "/v1/trader/trades?limit=100"; }));
        QCOMPARE(live.trades()->size(), 7);
        QCOMPARE(live.next_ms(), view::kHeldMs); // a position open
        QVERIFY(live.failure().isEmpty());
        QCOMPARE(live.answered_at(), clock);

        // The next read asks for the state alone while nothing it hangs on changed.
        clock = clock.addSecs(2);
        fire_schedule(live);
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("GET", "/v1/trader"), 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        QCOMPARE(engine.count("GET", "/v1/trader/trades"), 1);
        // A trade exited: the days and trades again.
        QJsonObject exited = state_json("paper", "running", true, false);
        QJsonObject today = exited["today"].toObject();
        today["trades"] = 1;
        exited["today"] = today;
        engine.state = exited;
        clock = clock.addSecs(2);
        fire_schedule(live);
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("GET", "/v1/trader/trades"), 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        QCOMPARE(engine.count("GET", "/v1/trader/days"), 2);
        QCOMPARE(live.next_ms(), view::kSessionMs); // no position now
        // Five minutes on, the days and trades again whatever the state says.
        clock = clock.addSecs(view::kIdleMs / 1000);
        fire_schedule(live);
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("GET", "/v1/trader/trades"), 3, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        // Asked to read now: all three.
        live.read_now();
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("GET", "/v1/trader/days"), 4, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);

        // Stopped (the screen hidden): nothing more is read, nor scheduled.
        live.stop();
        QCOMPARE(live.next_ms(), 0);
        live.read_now();
        QVERIFY(!live.reading()); // a read asks at once: none was asked
        auto* timer = live.findChild<QTimer*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(timer && !timer->isActive());
        // Nothing here writes but the pause.
        QVERIFY(std::all_of(engine.asked.cbegin(), engine.asked.cend(),
                            [](const StandIn::Request& r) { return r.method == "GET"; }));
    }

    void a_pause_is_sent_as_json_only_after_a_yes() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-15T10:06:30+09:00"); });
        // Before the state is read the buttons cannot send, and nothing is asked.
        bool asked_before = false;
        QVERIFY(!live.pause(true, [&asked_before](const view::Question&) { return asked_before = true; }));
        QVERIFY(!asked_before);
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.state().has_value() && !live.reading(), 10000);

        // No: nothing is sent.
        QString asked_title;
        QVERIFY(!live.pause(true, [&asked_title](const view::Question& q) {
            asked_title = q.title;
            return false;
        }));
        QCOMPARE(asked_title, QStringLiteral("정지하고 즉시 매도"));
        QVERIFY(!live.sending()); // a pause is sent at once once confirmed: none was
        QVERIFY(live.pause_note().isEmpty());

        // Yes: POST /v1/trader/pause with {"sell_now": true} as JSON, and the state read again.
        const qsizetype states = engine.count("GET", "/v1/trader");
        QVERIFY(live.pause(true, [](const view::Question&) { return true; }));
        QVERIFY(live.sending());
        QVERIFY(!live.pause(true, [](const view::Question&) { return true; })); // not twice while it is sent
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("POST", "/v1/trader/pause"), 1, 10000);
        const auto post = *std::find_if(engine.asked.cbegin(), engine.asked.cend(),
                                        [](const StandIn::Request& r) { return r.method == "POST"; });
        QVERIFY(post.content_type.startsWith(QStringLiteral("application/json")));
        QCOMPARE(QJsonDocument::fromJson(post.body).object(), (QJsonObject{{"sell_now", true}}));
        QTRY_VERIFY_WITH_TIMEOUT(!live.sending(), 10000);
        QVERIFY(!live.pause_failed());
        QVERIFY(live.pause_note().contains(QStringLiteral("정지하고 즉시 매도 요청을 보냈습니다(10:07:00)")));
        QTRY_VERIFY_WITH_TIMEOUT(engine.count("GET", "/v1/trader") > states, 10000);

        // New entries alone: false.
        QVERIFY(live.pause(false, [](const view::Question&) { return true; }));
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("POST", "/v1/trader/pause"), 2, 10000);
        const auto second = *std::find_if(engine.asked.crbegin(), engine.asked.crend(),
                                          [](const StandIn::Request& r) { return r.method == "POST"; });
        QCOMPARE(QJsonDocument::fromJson(second.body).object(), (QJsonObject{{"sell_now", false}}));
        QTRY_VERIFY_WITH_TIMEOUT(!live.sending(), 10000);

        // The engine refuses it: said so.
        engine.fail_pause = true;
        QVERIFY(live.pause(false, [](const view::Question&) { return true; }));
        QTRY_VERIFY_WITH_TIMEOUT(live.pause_failed(), 10000);
        QVERIFY(live.pause_note().contains(QStringLiteral("새 진입 정지 요청을 보내지 못했습니다")));
        live.stop();
    }

    void a_failed_read_keeps_what_was_read_and_says_why() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-15T10:06:30+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.state().has_value() && live.trades().has_value() && !live.reading(), 10000);
        engine.fail = true;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(!live.failure().isEmpty() && !live.reading(), 10000);
        QVERIFY(live.state().has_value()); // the last answers stay
        QVERIFY(live.trades().has_value());
        const qsizetype trades = engine.count("GET", "/v1/trader/trades");
        // The failure is asked again with the next read, and clears once answered.
        engine.fail = false;
        fire_schedule(live);
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("GET", "/v1/trader/trades"), trades + 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(live.failure().isEmpty() && !live.reading(), 10000);
        live.stop();

        // An engine before the trader's state (codeyoma/krx-quant#396): a failure, nothing shown.
        StandIn old;
        old.without_trader = true;
        services::KrxQuantService::set_base_url(old.url());
        services::KrxTraderLive before(nullptr, [] { return kst("2026-10-15T10:06:30+09:00"); });
        before.start();
        QTRY_VERIFY_WITH_TIMEOUT(!before.failure().isEmpty() && !before.reading(), 10000);
        QVERIFY(!before.state().has_value());
        QVERIFY(!view::pause_action(true, before.state()).enabled);
        before.stop();
    }

    void a_new_address_drops_what_was_read() {
        StandIn first;
        services::KrxQuantService::set_base_url(first.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-15T10:06:30+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.state().has_value() && !live.reading(), 10000);
        StandIn second;
        second.state = unwritten_json();
        services::KrxQuantService::set_base_url(second.url());
        // A pause confirmed now would go to another engine than the one shown: refused, and read again.
        QVERIFY(!live.pause(false, [](const view::Question&) { return true; }));
        QVERIFY(live.pause_note().contains(QStringLiteral("엔진 주소가 바뀌었습니다")));
        QTRY_VERIFY_WITH_TIMEOUT(live.state().has_value() && !live.state()->written() && !live.reading(), 10000);
        QCOMPARE(second.count("POST", "/v1/trader/pause"), 0);
        QCOMPARE(first.count("POST", "/v1/trader/pause"), 0);
        live.stop();
    }

    void without_an_address_it_says_where_to_set_it() {
        services::KrxQuantService::set_base_url(QString());
        services::KrxTraderLive live;
        live.start();
        QVERIFY(live.failure().contains(QStringLiteral("엔진 주소")));
        live.stop();
    }
};

QTEST_GUILESS_MAIN(TstKrxTrader)
#include "tst_krx_trader.moc"
