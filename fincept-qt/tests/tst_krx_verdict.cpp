// Tests for the KRX verdict screen's model (종합 판단, codeyoma/krx-quant#678): how it reads GET /v1/verdict
// and GET /v1/ranking (src/services/krx_quant/EngineData.h, parse_verdicts and parse_ranking, #699), what it shows of it (src/services/krx_quant/
// VerdictView.h) and when it reads it (src/services/krx_quant/KrxVerdictLive.h), the last against a stand-in
// engine on this computer. The settings (the engine's address, the symbol chosen) go to a temporary INI
// file: AppConfig is defined below, in place of core/config/AppConfig.cpp.

#include "core/config/AppConfig.h"
#include "services/krx_quant/EnginePoller.h"
#include "services/krx_quant/KrxQuantService.h"
#include "services/krx_quant/KrxVerdictLive.h"
#include "services/krx_quant/VerdictView.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

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
namespace view = fincept::krx_quant::judgment;

namespace {

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
}

QJsonObject odds(double up, double timeout, double down) {
    return {{"p_up_first", up}, {"p_timeout", timeout}, {"p_down_first", down}};
}

QJsonObject rules() {
    return {{"version", 1},     {"margin", 0.05},   {"lean_floor", 0.02}, {"moderate", 0.05},
            {"strong", 0.15},   {"families", QJsonObject{{"breiman", "breiman"}, {"breiman-15m", "breiman"}}},
            {"consensus", QJsonObject{}}};
}

QJsonObject horizon(const char* kind, int minutes, QJsonValue probabilities, QJsonValue net) {
    const bool members = probabilities.isObject();
    return {{"kind", kind},
            {"minutes", minutes},
            {"width_ticks", 12},
            {"probabilities", probabilities},
            {"families", members ? 1 : 0},
            {"members", members ? 1 : 0},
            {"lean", members ? QJsonValue("up") : QJsonValue()},
            {"strength", members ? QJsonValue("strong") : QJsonValue()},
            {"margin", members ? QJsonValue(0.5) : QJsonValue()},
            {"target", 82450.0},
            {"stop", 80050.0},
            {"expected_net", net},
            {"expected_net_ticks", net.isDouble() ? QJsonValue(net.toDouble() / 100) : QJsonValue()}};
}

QJsonArray side() {
    return {QJsonObject{{"name", "newton"},
                        {"kind", "direction"},
                        {"status", "active"},
                        {"output", QJsonObject{{"p_up", 0.4}, {"p_flat", 0.4}, {"p_down", 0.2}, {"expected_ticks", 0.3}}}},
            QJsonObject{{"name", "consensus-final"},
                        {"kind", "direction"},
                        {"status", QJsonValue()},
                        {"output", QJsonObject{{"p_up", 0.5}, {"p_flat", 0.3}, {"p_down", 0.2}}}},
            QJsonObject{{"name", "mandelbrot"},
                        {"kind", "range"},
                        {"status", "probation"},
                        {"output", QJsonObject{{"p_small", 0.2}, {"p_medium", 0.5}, {"p_large", 0.3}, {"q50", 2.0}}}}};
}

/// A buy at 15 minutes, its member on probation.
QJsonObject buy(const char* symbol = "005930") {
    return {{"symbol", symbol},
            {"bar_close", "2026-10-12T10:00:00+09:00"},
            {"version", 1},
            {"verdict", "buy"},
            {"meaning", QJsonValue()},
            {"reason", QJsonValue()},
            {"strength", "strong"},
            {"margin", 0.5},
            {"horizon", QJsonObject{{"kind", "barrier_15m"}, {"minutes", 15}}},
            {"deadline", "2026-10-12T10:15:00+09:00"},
            {"target", 82450.0},
            {"stop", 80050.0},
            {"held", false},
            {"horizons", QJsonArray{horizon("barrier", 4, QJsonValue(), QJsonValue()),
                                    horizon("barrier_15m", 15, odds(0.7, 0.1, 0.2), 361.35)}},
            {"members", QJsonArray{QJsonObject{{"name", "breiman-15m"},
                                               {"kind", "barrier_15m"},
                                               {"family", "breiman"},
                                               {"status", "probation"},
                                               {"evidence", 390},
                                               {"probabilities", odds(0.7, 0.1, 0.2)},
                                               {"lean", "up"},
                                               {"strength", "strong"},
                                               {"margin", 0.5},
                                               {"weight", 1.0},
                                               {"counted", true}}}},
            {"side", side()},
            {"rules", rules()}};
}

/// A sell that exits a held position at its time barrier.
QJsonObject sell(const char* symbol = "000660") {
    QJsonObject o = buy(symbol);
    o["verdict"] = "sell";
    o["meaning"] = "exit";
    o["strength"] = "moderate";
    o["margin"] = -0.08;
    o["target"] = QJsonValue();
    o["stop"] = QJsonValue();
    o["held"] = true;
    o["deadline"] = "2026-10-12T10:12:00+09:00";
    return o;
}

/// Today's live verdict: no barrier algorithm predicted.
QJsonObject no_members(const char* symbol = "009150") {
    QJsonObject o = buy(symbol);
    o["verdict"] = "wait";
    o["reason"] = "no_members";
    o["strength"] = "none";
    o["margin"] = QJsonValue();
    o["horizon"] = QJsonValue();
    o["deadline"] = QJsonValue();
    o["target"] = QJsonValue();
    o["stop"] = QJsonValue();
    o["horizons"] = QJsonArray{};
    o["members"] = QJsonArray{};
    o["side"] = QJsonArray{};
    return o;
}

QJsonObject day_odds(double up, double flat, double down) {
    return {{"p_up", up}, {"p_flat", flat}, {"p_down", down}};
}

QJsonObject forecaster(const char* name, const char* status, double up, double weight, bool ranked) {
    return {{"name", name},
            {"family", name},
            {"status", status},
            {"probabilities", day_odds(up, 0.3, 0.7 - up)},
            {"lean", "up"},
            {"strength", "weak"},
            {"margin", up - (0.7 - up)},
            {"weight", weight},
            {"counted", ranked},
            {"ranked", ranked}};
}

/// A version-2 buy: the 15-minute member ranked, a day call up and a week without members.
QJsonObject buy_v2(const char* symbol = "005930") {
    QJsonObject o = buy(symbol);
    o["version"] = 2;
    QJsonArray members = o["members"].toArray();
    QJsonObject ranked = members[0].toObject();
    ranked["ranked"] = true;
    QJsonObject unranked = ranked;
    unranked["name"] = "turing-15m";
    unranked["family"] = "turing";
    unranked["status"] = "shadow";
    unranked["ranked"] = false;
    unranked["counted"] = false;
    unranked["weight"] = 0.0;
    o["members"] = QJsonArray{ranked, unranked};
    o["advisory"] = QJsonArray{
        QJsonObject{{"kind", "open_to_close"},
                    {"sessions", 1},
                    {"call", "up"},
                    {"probabilities", day_odds(0.46, 0.3, 0.24)},
                    {"families", 1},
                    {"members", 1},
                    {"lean", "up"},
                    {"strength", "strong"},
                    {"margin", 0.22},
                    {"forecasters", QJsonArray{forecaster("cox", "active", 0.46, 1.0, true),
                                               forecaster("lorenz", "probation", 0.35, 0.0, false)}}},
        QJsonObject{{"kind", "open_to_close_5d"},
                    {"sessions", 5},
                    {"call", "wait"},
                    {"probabilities", QJsonValue()},
                    {"families", 0},
                    {"members", 0},
                    {"lean", QJsonValue()},
                    {"strength", QJsonValue()},
                    {"margin", QJsonValue()},
                    {"forecasters", QJsonArray{}}}};
    QJsonObject r = o["rules"].toObject();
    r["version"] = 2;
    r["ranked"] = true;
    o["rules"] = r;
    return o;
}

QJsonObject ranking_row(QJsonValue rank, const char* algorithm, bool member, double low, int nights, int pending = 0) {
    const bool ranked = rank.isDouble();
    return {{"rank", rank},
            {"algorithm", algorithm},
            {"status", member ? "active" : "shadow"},
            {"source", "live"},
            {"days", ranked ? 20 : 3},
            {"n", 7400},
            {"skill", ranked ? QJsonValue(low + 0.004) : QJsonValue()},
            {"skill_low", ranked ? QJsonValue(low) : QJsonValue()},
            {"skill_high", ranked ? QJsonValue(low + 0.008) : QJsonValue()},
            {"precision", 0.58},
            {"calls", 900},
            {"call_rate", 0.12},
            {"longs", 450},
            {"net_ticks", 0.3},
            {"ranked", ranked},
            {"why", ranked ? QJsonValue() : QJsonValue("few_days")},
            {"qualifies", low > 0},
            {"member", member},
            {"nights", nights},
            {"pending", pending}};
}

QJsonObject ranking_kind(const char* kind, const QJsonArray& members, const QJsonArray& rows) {
    return {{"kind", kind},
            {"night", "2026-11-13"},
            {"version", 1},
            {"made_at", "2026-11-13T15:40:02+09:00"},
            {"through", "2026-11-13"},
            {"rules", QJsonObject{{"level", 0.95}, {"nights", 3}, {"window_days", 20}, {"min_days", 10}, {"min_calls", 30}}},
            {"settled", true},
            {"members", members},
            {"rows", rows}};
}

/// Two horizons: at 15 minutes the second-ranked a member still (its lower bound fell below 0 a night ago)
/// and the first not yet (two nights above 0 of three); at a day, none qualifies.
QJsonObject ranking_answer() {
    return {{"version", 1},
            {"kinds",
             QJsonArray{ranking_kind("barrier_15m", QJsonArray{"breiman-15m"},
                                     QJsonArray{ranking_row(1, "turing-15m", false, 0.003, 5, 2),
                                                ranking_row(2, "breiman-15m", true, -0.001, 4, 1),
                                                ranking_row(QJsonValue(), "friedman-15m", false, 0, 1)}),
                        ranking_kind("open_to_close", QJsonArray{},
                                     QJsonArray{ranking_row(1, "cox", false, -0.002, 2)})}}};
}

Ranking ranking_parsed(const QJsonObject& body) {
    auto r = parse_ranking(QJsonDocument(body));
    if (r.is_err())
        qFatal("%s", r.error().c_str());
    return r.value();
}

QJsonObject answer(const QJsonArray& verdicts) {
    return {{"version", 1}, {"verdicts", verdicts}};
}

Verdicts parsed(const QJsonObject& body) {
    auto v = parse_verdicts(QJsonDocument(body));
    if (v.is_err())
        qFatal("%s", v.error().c_str());
    return v.value();
}

/// The engine as the verdict screen reads it: GET /v1/symbols and /v1/verdict, the paths asked kept.
class StandIn : public QObject {
  public:
    int status = 200; ///< /v1/verdict's
    QJsonObject body = answer({buy(), sell(), no_members()});
    int ranking_status = 200; ///< /v1/ranking's
    QJsonObject ranking = ranking_answer();
    QStringList asked;

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

  private:
    void read(QTcpSocket* socket) {
        QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n"))
            return;
        const QString path = QString::fromLatin1(request.left(request.indexOf("\r\n")).split(' ').value(1));
        asked << path;
        int code = 404;
        QJsonObject reply{{"error", "not found"}};
        if (path == "/v1/symbols") {
            code = 200;
            reply = {{"symbols", QJsonArray{QJsonObject{{"symbol", "005930"}, {"name", "삼성전자"}},
                                            QJsonObject{{"symbol", "000660"}, {"name", "SK하이닉스"}}}}};
        } else if (path == "/v1/verdict") {
            code = status;
            reply = status == 200 ? body : QJsonObject{{"error", "made-up failure"}};
        } else if (path == "/v1/ranking") {
            code = ranking_status;
            reply = ranking_status == 200 ? ranking : QJsonObject{{"error", "made-up failure"}};
        }
        const QByteArray bytes = QJsonDocument(reply).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " Error") +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                      "\r\nConnection: close\r\n\r\n" + bytes);
        socket->disconnectFromHost();
    }

    QTcpServer server_;
};

} // namespace

class TstKrxVerdict : public QObject {
    Q_OBJECT

    QTemporaryDir settings_;

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        settings_file = settings_.filePath("settings.ini");
    }

    void a_buy_is_read_whole() {
        const Verdicts all = parsed(answer({buy()}));
        QCOMPARE(all.version, 1);
        QCOMPARE(all.verdicts.size(), 1);
        const SymbolVerdict& v = all.verdicts.first();
        QCOMPARE(v.symbol, QStringLiteral("005930"));
        QCOMPARE(v.verdict, QStringLiteral("buy"));
        QVERIFY(v.meaning.isEmpty());
        QCOMPARE(v.horizon_kind, QStringLiteral("barrier_15m"));
        QCOMPARE(v.horizon_minutes, 15);
        QCOMPARE(*v.target, 82450.0);
        QCOMPARE(v.horizons.size(), 2);
        QVERIFY(!v.horizons[0].odds); // a horizon without members
        QVERIFY(!v.horizons[0].expected_net);
        QCOMPARE(v.horizons[1].odds->up_first, 0.7);
        QCOMPARE(*v.horizons[1].expected_net, 361.35);
        QCOMPARE(v.members.size(), 1);
        QCOMPARE(v.members[0].evidence, 390);
        QCOMPARE(v.members[0].odds.down_first, 0.2);
        QVERIFY(v.members[0].counted);
        QCOMPARE(v.side.size(), 3);
        QVERIFY(v.side[1].status.isEmpty()); // the consensus has none
    }

    void an_engine_error_is_a_failure() {
        QVERIFY(parse_verdicts(QJsonDocument(QJsonObject{{"error", "unknown symbol"}})).is_err());
        QVERIFY(parse_verdicts(QJsonDocument(QJsonArray{})).is_err());
    }

    void a_buy_says_buy_with_its_holding_time_target_and_stop() {
        const SymbolVerdict v = parsed(answer({buy()})).verdicts.first();
        QCOMPARE(view::call(v), view::Call::Buy);
        QCOMPARE(view::word(v), QStringLiteral("사세요"));
        QCOMPARE(view::reason_text(v), QStringLiteral("강도 강함 · 마진 +0.50"));
        QCOMPARE(view::hold_text(v), QStringLiteral("최대 보유 15분 · 10:15까지"));
        QCOMPARE(view::levels_text(v), QStringLiteral("목표 82,450 · 손절 80,050"));
        const auto lines = view::horizon_lines(v);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].horizon, QStringLiteral("4분"));
        QCOMPARE(lines[0].odds, QStringLiteral("—"));
        QCOMPARE(lines[0].net, QStringLiteral("—"));
        QVERIFY(!lines[0].chosen);
        QCOMPARE(lines[1].odds, QStringLiteral("위 70% · 아래 20%"));
        QCOMPARE(lines[1].net, QStringLiteral("+361원 (+3.6틱)"));
        QVERIFY(lines[1].chosen && lines[1].positive);
    }

    void a_sell_says_sell_and_why() {
        const SymbolVerdict v = parsed(answer({sell()})).verdicts.first();
        QCOMPARE(view::call(v), view::Call::Sell);
        QCOMPARE(view::word(v), QStringLiteral("파세요"));
        QCOMPARE(view::reason_text(v), QStringLiteral("청산: 들고 있는 것을 파세요 · 강도 보통 · 마진 -0.08"));
        QCOMPARE(view::hold_text(v), QStringLiteral("보유 기한 10:12"));
        QVERIFY(view::levels_text(v).isEmpty());
        QCOMPARE(view::as_of_text(v), QStringLiteral("봉 마감 10-12 10:00 · 보유 중"));
        QJsonObject avoid = sell();
        avoid["meaning"] = "avoid";
        avoid["held"] = false;
        avoid["deadline"] = QJsonValue();
        const SymbolVerdict a = parsed(answer({avoid})).verdicts.first();
        QVERIFY(view::reason_text(a).startsWith(QStringLiteral("회피: 들고 있지 않으면 사지 마세요")));
        QVERIFY(view::hold_text(a).isEmpty());
    }

    void a_wait_without_members_says_none_took_part() {
        const SymbolVerdict v = parsed(answer({no_members()})).verdicts.first();
        QCOMPARE(view::call(v), view::Call::Wait);
        QCOMPARE(view::word(v), QStringLiteral("기다리세요"));
        QCOMPARE(view::reason_text(v), QStringLiteral("참여 알고리즘 없음 — 장벽 알고리즘 검증 대기"));
        QVERIFY(view::hold_text(v).isEmpty());
        QVERIFY(view::levels_text(v).isEmpty());
        QVERIFY(view::horizon_lines(v).isEmpty());
        QVERIFY(view::member_rows(v).isEmpty());
        QVERIFY(view::no_members_text(v).contains(QStringLiteral("장벽 알고리즘이 없습니다")));
        QVERIFY(view::side_rows(v).isEmpty());
        QJsonObject edge = no_members();
        edge["reason"] = "no_edge";
        QVERIFY(view::reason_text(parsed(answer({edge})).verdicts.first()).startsWith(QStringLiteral("우위 없음")));
    }

    void the_members_and_the_forecasts_beside_them() {
        const SymbolVerdict v = parsed(answer({buy()})).verdicts.first();
        const auto members = view::member_rows(v);
        QCOMPARE(members.size(), 1);
        QCOMPARE(members[0].name, QStringLiteral("breiman-15m"));
        QCOMPARE(members[0].kind, QStringLiteral("장벽 15분"));
        QCOMPARE(members[0].family, QStringLiteral("breiman"));
        QCOMPARE(members[0].status, QStringLiteral("검증 중"));
        QCOMPARE(members[0].evidence, QStringLiteral("390"));
        QCOMPARE(members[0].up, QStringLiteral("70%"));
        QCOMPARE(members[0].timeout, QStringLiteral("10%"));
        QCOMPARE(members[0].down, QStringLiteral("20%"));
        QCOMPARE(members[0].lean, QStringLiteral("위 · 강함"));
        QCOMPARE(view::status_text("active"), QStringLiteral("정식"));
        QCOMPARE(view::status_text(""), QStringLiteral("기록 없음"));
        QCOMPARE(view::kind_text("barrier"), QStringLiteral("장벽 4분"));
        QCOMPARE(view::kind_text("barrier_60m"), QStringLiteral("장벽 60분"));
        const auto side = view::side_rows(v);
        QCOMPARE(side.size(), 3);
        QCOMPARE(side[0].kind, QStringLiteral("방향 4분"));
        QCOMPARE(side[0].status, QStringLiteral("정식"));
        QCOMPARE(side[0].output, QStringLiteral("상승 40% · 보합 40% · 하락 20% · 기대 +0.3틱"));
        QVERIFY(side[1].status.isEmpty());
        QCOMPARE(side[2].kind, QStringLiteral("변동폭"));
        QCOMPARE(side[2].output, QStringLiteral("작음 20% · 중간 50% · 큼 30% · q50 2.0"));
    }

    void a_version_2_verdict_has_its_day_and_week_and_ranked_members() {
        const SymbolVerdict v = parsed(answer({buy_v2()})).verdicts.first();
        QCOMPARE(v.version, 2);
        QCOMPARE(v.advisory.size(), 2);
        QCOMPARE(v.advisory[0].kind, QStringLiteral("open_to_close"));
        QCOMPARE(v.advisory[0].call, QStringLiteral("up"));
        QCOMPARE(v.advisory[0].odds->up, 0.46);
        QCOMPARE(v.advisory[0].forecasters.size(), 2);
        QVERIFY(v.advisory[0].forecasters[0].ranked);
        QVERIFY(!v.advisory[0].forecasters[1].ranked);
        QVERIFY(!v.advisory[1].odds);
        QVERIFY(v.members[0].ranked);
        QVERIFY(!v.members[1].ranked);
        // Version 1 has neither: its members count as ranked and no day row shows.
        const SymbolVerdict old = parsed(answer({buy()})).verdicts.first();
        QVERIFY(old.advisory.isEmpty());
        QVERIFY(old.members[0].ranked);
        QVERIFY(view::advisory_lines(old).isEmpty());
    }

    void the_day_and_week_rows_say_their_call_and_that_nothing_trades() {
        const SymbolVerdict v = parsed(answer({buy_v2()})).verdicts.first();
        const auto lines = view::advisory_lines(v);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].horizon, QStringLiteral("하루"));
        QCOMPARE(lines[0].call, QStringLiteral("상승"));
        QCOMPARE(lines[0].odds, QStringLiteral("상승 46% · 보합 30% · 하락 24%"));
        QCOMPARE(lines[0].members, QStringLiteral("멤버 1"));
        QVERIFY(lines[0].up && !lines[0].down);
        QVERIFY(lines[0].tip.contains(view::advisory_label()));
        QVERIFY(lines[0].tip.contains(QStringLiteral("lorenz (검증 중)")));
        QVERIFY(lines[0].tip.contains(QStringLiteral("순위 멤버 아님")));
        QCOMPARE(lines[1].horizon, QStringLiteral("1주"));
        QCOMPARE(lines[1].call, QStringLiteral("대기"));
        QCOMPARE(lines[1].odds, QStringLiteral("—"));
        QCOMPARE(lines[1].members, QStringLiteral("멤버 0"));
        QCOMPARE(view::advisory_label(), QStringLiteral("참고(매매 안 함)"));
        QCOMPARE(view::day_call_text("down"), QStringLiteral("하락"));
        QCOMPARE(view::day_call_text("flat"), QStringLiteral("보합"));
        // A member outside the ranking is not counted, and says why.
        const auto members = view::member_rows(v);
        QCOMPARE(members[1].status, QStringLiteral("그림자"));
        QVERIFY(!members[1].counted);
        QVERIFY(members[1].tip.contains(QStringLiteral("순위의 멤버가 아니라")));
        // A version-2 wait without members: none better than the baseline.
        QJsonObject none = no_members();
        none["version"] = 2;
        const SymbolVerdict w = parsed(answer({none})).verdicts.first();
        QCOMPARE(view::reason_text(w), QStringLiteral("참여 알고리즘 없음 — 기준선보다 나은 알고리즘 없음"));
    }

    void a_ranking_is_read_whole() {
        const Ranking r = ranking_parsed(ranking_answer());
        QCOMPARE(r.version, 1);
        QCOMPARE(r.kinds.size(), 2);
        const RankingKind& k = r.kinds[0];
        QCOMPARE(k.kind, QStringLiteral("barrier_15m"));
        QCOMPARE(k.night, QDate(2026, 11, 13));
        QCOMPARE(k.nights, 3);
        QCOMPARE(*k.window_days, 20);
        QCOMPARE(k.members, QStringList{QStringLiteral("breiman-15m")});
        QCOMPARE(k.rows.size(), 3);
        QCOMPARE(*k.rows[0].rank, 1);
        QCOMPARE(*k.rows[0].skill_low, 0.003);
        QVERIFY(!k.rows[2].rank);
        QVERIFY(!k.rows[2].skill);
        QCOMPARE(k.rows[2].why, QStringLiteral("few_days"));
        QVERIFY(r.kinds[1].members.isEmpty());
        QVERIFY(parse_ranking(QJsonDocument(QJsonObject{{"error", "night must be a date"}})).is_err());
    }

    void the_ranking_shows_members_first_and_says_when_none_qualifies() {
        const auto panels = view::ranking_panels(ranking_parsed(ranking_answer()));
        QCOMPARE(panels.size(), 2);
        const auto& p = panels[0];
        QCOMPARE(p.title, QStringLiteral("순위 · 장벽 15분"));
        QCOMPARE(p.line, QStringLiteral("11-13 밤 · 멤버 breiman-15m"));
        QVERIFY(!p.no_members);
        QCOMPARE(p.rows.size(), 3);
        QCOMPARE(p.rows[0].algorithm, QStringLiteral("breiman-15m")); // the member, though ranked second
        QCOMPARE(p.rows[0].rank, QStringLiteral("2"));
        QCOMPARE(p.rows[0].member, QStringLiteral("예"));
        QCOMPARE(p.rows[0].status, QStringLiteral("정식"));
        QCOMPARE(p.rows[0].skill, QStringLiteral("+0.0030 (-0.0010 ~ +0.0070)"));
        QCOMPARE(p.rows[0].precision, QStringLiteral("58%"));
        QCOMPARE(p.rows[0].calls, QStringLiteral("900"));
        QCOMPARE(p.rows[0].nights, QStringLiteral("4밤 · 반대 1/3"));
        QCOMPARE(p.rows[1].algorithm, QStringLiteral("turing-15m"));
        QCOMPARE(p.rows[1].member, QStringLiteral("아니오"));
        QCOMPARE(p.rows[1].status, QStringLiteral("그림자"));
        QVERIFY(p.rows[1].qualifies);
        QCOMPARE(p.rows[2].rank, QStringLiteral("—"));
        QCOMPARE(p.rows[2].skill, QStringLiteral("날 부족 (3일 / 10일)"));
        QVERIFY(!p.rows[2].ranked);
        const auto& day = panels[1];
        QCOMPARE(day.title, QStringLiteral("순위 · 하루 · 참고(매매 안 함)"));
        QVERIFY(day.no_members);
        QVERIFY(day.line.contains(QStringLiteral("기준선보다 나은 알고리즘 없음")));
        QCOMPARE(view::no_qualifier_text(), QStringLiteral("기준선보다 나은 알고리즘 없음"));
        QVERIFY(view::ranking_panels(Ranking{}).isEmpty());
        QVERIFY(view::ranking_failure_text(QStringLiteral("엔진 오류 (HTTP 404): not found"))
                    .contains(QStringLiteral("GET /v1/ranking")));
    }

    void it_reads_often_while_krx_trades_and_seldom_otherwise() {
        QCOMPARE(view::poll_ms(kst("2026-10-12T08:58:59+09:00")), view::kSlowMs);
        QCOMPARE(view::poll_ms(kst("2026-10-12T08:59:00+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(kst("2026-10-12T15:39:59+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(kst("2026-10-12T15:40:00+09:00")), view::kSlowMs);
        QCOMPARE(view::poll_ms(kst("2026-10-11T10:00:00+09:00")), view::kSlowMs); // a Sunday
    }

    void without_an_address_it_says_where_to_set_one() {
        services::KrxQuantService::set_base_url(QString());
        services::KrxVerdictLive live(nullptr, [] { return kst("2026-10-12T10:00:30+09:00"); });
        live.start();
        QTRY_COMPARE_WITH_TIMEOUT(live.failure(), QString::fromUtf8(services::EnginePoller::kNoAddress), 10000);
        QVERIFY(!live.verdicts());
    }

    void the_screen_reads_every_symbol_s_verdict() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        AppConfig::instance().remove(services::KrxVerdictLive::kSymbolKey);
        services::KrxVerdictLive live(nullptr, [] { return kst("2026-10-12T10:00:30+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.verdicts().has_value(), 10000);
        QVERIFY(live.failure().isEmpty());
        QCOMPARE(live.verdicts()->verdicts.size(), 3);
        QCOMPARE(live.chosen()->symbol, QStringLiteral("005930")); // none chosen: the first verdict's
        QCOMPARE(live.next_ms(), view::kFastMs);
        QTRY_COMPARE_WITH_TIMEOUT(live.symbols().size(), 2, 10000); // the names, read beside it
        QTRY_VERIFY_WITH_TIMEOUT(live.ranking().has_value(), 10000); // read with the verdicts
        QCOMPARE(live.ranking()->kinds.size(), 2);
        QVERIFY(live.ranking_failure().isEmpty());

        live.set_symbol("009150");
        QCOMPARE(live.chosen()->reason, QStringLiteral("no_members"));
        QCOMPARE(AppConfig::instance().get(services::KrxVerdictLive::kSymbolKey).toString(), QStringLiteral("009150"));

        // A symbol without a verdict now: the first one's members are shown.
        engine.body = answer({sell()});
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(live.verdicts()->verdicts.size() == 1, 10000);
        QCOMPARE(live.chosen()->symbol, QStringLiteral("000660"));

        // No verdict yet (before the first predictions).
        engine.body = answer({});
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(live.verdicts()->verdicts.isEmpty(), 10000);
        QVERIFY(!live.chosen());

        engine.status = 500;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(!live.failure().isEmpty(), 10000);
        QVERIFY(live.verdicts().has_value()); // the last answer stays
        live.stop();
        QCOMPARE(live.next_ms(), 0);

        // The symbol kept is shown first next time.
        services::KrxVerdictLive again(nullptr, [] { return kst("2026-10-12T10:00:30+09:00"); });
        QCOMPARE(again.symbol(), QStringLiteral("009150"));
    }

    void an_engine_before_the_ranking_still_shows_its_verdicts() {
        StandIn engine;
        engine.ranking_status = 404;
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxVerdictLive live(nullptr, [] { return kst("2026-10-12T10:00:30+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(!live.ranking_failure().isEmpty() && live.verdicts().has_value(), 10000);
        QVERIFY(live.failure().isEmpty()); // the screen's failure is the verdicts'
        QVERIFY(!live.ranking());
        QVERIFY(view::ranking_failure_text(live.ranking_failure()).contains(QStringLiteral("GET /v1/ranking")));

        // The ranking comes once the engine gives it, and its failure goes; a later failure keeps it.
        engine.ranking_status = 200;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(live.ranking().has_value(), 10000);
        QVERIFY(live.ranking_failure().isEmpty());
        engine.ranking_status = 500;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(!live.ranking_failure().isEmpty(), 10000);
        QVERIFY(live.ranking().has_value());
        QVERIFY(live.failure().isEmpty());
    }

    void another_engine_drops_the_ranking() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxVerdictLive live(nullptr, [] { return kst("2026-10-12T10:00:30+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.ranking().has_value(), 10000);
        StandIn other;
        other.ranking = QJsonObject{{"version", 1}, {"kinds", QJsonArray{}}};
        services::KrxQuantService::set_base_url(other.url());
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(live.ranking().has_value() && live.ranking()->kinds.isEmpty(), 10000);
    }

    void an_engine_before_the_verdict_says_so() {
        StandIn engine;
        engine.status = 404;
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxVerdictLive live(nullptr, [] { return kst("2026-10-12T10:00:30+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(!live.failure().isEmpty(), 10000);
        QVERIFY(view::failure_text(live.failure()).contains(QStringLiteral("GET /v1/verdict")));
        QCOMPARE(view::failure_text(QStringLiteral("엔진에 연결할 수 없습니다.")), QStringLiteral("엔진에 연결할 수 없습니다."));
    }
};

QTEST_GUILESS_MAIN(TstKrxVerdict)
#include "tst_krx_verdict.moc"
