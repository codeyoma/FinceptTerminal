// Tests for the KRX auto trader screen's model (codeyoma/krx-quant#397): how it reads GET /v1/trader,
// /v1/trader/days and /v1/trader/trades (src/services/krx_quant/EngineData.h, parse_trader and the
// others), what it shows of them (src/services/krx_quant/TraderView.h) and when it reads them and sends
// the screen's pause (src/services/krx_quant/KrxTraderLive.h), the last against a stand-in engine on
// this computer. The values are made up. The settings (the engine's address) go to a temporary INI
// file: AppConfig is defined below, in place of core/config/AppConfig.cpp.
//
// The screen's 1-minute chart of 005930 (codeyoma/krx-quant#537) reads the day's bars without predictions
// and the trader's buys and sells (krx-quant #398): the latest page with each chart read, then back to the
// first bar held, a page at a time; none from an engine without them (404). Hidden, it reads nothing; shown
// again, or the engine answering again after a failure, it reads at once.
//
// The decisions panel (codeyoma/krx-quant#601) reads a book's decisions of the chart's day (GET
// /v1/trader/decisions, krx-quant #584) with the chart, the day whole, page after page: the champion's from a
// payload the demo engine answered (10-12), the practice's made up with its skips, unfilled buys and answers.

#include "core/config/AppConfig.h"
#include "services/krx_quant/KrxQuantService.h"
#include "services/krx_quant/KrxTraderLive.h"
#include "services/krx_quant/TraderView.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QTimer>
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

/// ``o`` as a trader of codeyoma/krx-quant#533 writes it: ``today`` and ``cumulative`` what the loss limits
/// count, the mock practice's beside them (``practice`` won over ``trades`` trades exited today, ``before`` on
/// the days before) and the account's, both together.
QJsonObject split_json(QJsonObject o, double practice, int trades, double before) {
    QJsonObject today = o["today"].toObject();
    const double open = today["unrealized"].toDouble();
    today["practice"] = QJsonObject{{"realized", practice}, {"unrealized", 0.0}, {"net", practice}, {"trades", trades}};
    today["account"] = QJsonObject{{"realized", today["realized"].toDouble() + practice},
                                   {"unrealized", open},
                                   {"net", today["net"].toDouble() + practice},
                                   {"trades", today["trades"].toInt() + trades}};
    o["today"] = today;
    QJsonObject cumulative = o["cumulative"].toObject();
    const double all = practice + before;
    cumulative["practice"] = QJsonObject{{"realized", all}, {"unrealized", 0.0}, {"net", all}};
    cumulative["account"] = QJsonObject{{"realized", cumulative["realized"].toDouble() + all},
                                        {"unrealized", open},
                                        {"net", cumulative["net"].toDouble() + all}};
    o["cumulative"] = cumulative;
    return o;
}

/// The mock practice on and practising a share of barrier · breiman (codeyoma/krx-quant#427).
QJsonObject practising_json() {
    return {{"enabled", true},
            {"active", true},
            {"cell", QJsonObject{{"kind", "barrier"},
                                 {"role", "top"},
                                 {"predictor", "breiman"},
                                 {"p_star", 0.63},
                                 {"low", 0.60},
                                 {"quantity", 1}}}};
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

/// A challenger's paper trades (codeyoma/krx-quant#400): policy 2's book, not the champion's.
QJsonObject challenger_trades_json() {
    QJsonArray trades;
    for (const int id : {11, 10}) {
        QJsonObject t = trade_json(id, "take_profit");
        t["policy"] = 2;
        t["champion"] = false;
        trades.append(t);
    }
    return {{"trades", trades}};
}

QJsonObject paper_json(QJsonValue since, QJsonValue last, int days, int trades, double net, QJsonValue mean,
                       QJsonValue low) {
    return {{"since", since}, {"last", last}, {"days", days}, {"trades", trades},
            {"net", net},     {"mean", mean}, {"low", low}};
}

QJsonObject cell_json(const char* kind, const char* role, const char* predictor, double threshold, double fraction) {
    return {{"kind", kind},
            {"role", role},
            {"predictor", predictor},
            {"threshold", threshold},
            {"kelly_fraction", fraction},
            {"fitted",
             QJsonObject{{"p_star", 0.63},
                         {"low", 0.7},
                         {"kelly", 10.5},
                         {"fraction", 1.0},
                         {"days", QJsonArray{"2026-10-07", "2026-10-08"}},
                         {"read", QJsonObject{{"read", 380}, {"rises", 120}}},
                         {"trades", 24},
                         {"wins", 14},
                         {"net", 8400.0},
                         {"growth", 0.0084}}}};
}

QJsonObject policy_json(int version, const char* status, const char* created, QJsonArray cells, QJsonValue admitted,
                        QJsonValue dropped, QJsonObject paper) {
    return {{"version", version},
            {"status", status},
            {"created_at", created},
            {"reason", QStringLiteral("%1 밤 맞추기(지어낸 값)").arg(QString::fromLatin1(created).left(10))},
            {"cells", cells},
            {"left_out", QJsonArray{}},
            {"fit", QJsonObject{}},
            {"admitted_at", admitted},
            {"dropped_at", dropped},
            {"paper", paper}};
}

QJsonObject promotion_json(int days, QJsonValue mean, QJsonValue low, double level, int challengers, bool passed,
                           QJsonValue why) {
    return {{"champion", 2},         {"days", days},     {"required", 10},
            {"until", 20},           {"mean", mean},     {"low", low},
            {"level", level},        {"challengers", challengers},
            {"passed", passed},      {"why", why}};
}

/// The first champion, policy 1, alone: no challenger yet (made-up values).
QJsonObject lone_policies_json() {
    return {{"champion", 1},
            {"policies",
             QJsonArray{policy_json(1, "champion", "2026-10-07T22:31:04+09:00",
                                    QJsonArray{cell_json("barrier", "top", "breiman", 0.6, 0.5)}, QJsonValue(),
                                    QJsonValue(),
                                    paper_json("2026-10-08", "2026-10-12", 2, 4, -10418.37, -5209.185, -26882.09))}},
            {"champions", QJsonArray{QJsonObject{{"at", "2026-10-07T22:31:04+09:00"},
                                                 {"version", 1},
                                                 {"reason", "처음 맞춘 정책(#399)"},
                                                 {"detail", QJsonValue()}}}}};
}

/// Policy 2 the champion since it beat policy 1 on 10-29, with three challengers (4, 5, 6), one waiting
/// (7, no cells) and one dropped (3); made-up values. The versions' paper records are a poll older than
/// the state's (policy 2's 14 days against its 15).
QJsonObject busy_policies_json() {
    const QJsonValue none;
    return {
        {"champion", 2},
        {"policies",
         QJsonArray{
             policy_json(7, "waiting", "2026-10-29T22:31:04+09:00", QJsonArray{}, none, none,
                         paper_json(none, none, 0, 0, 0.0, none, none)),
             policy_json(6, "challenger", "2026-10-28T22:31:04+09:00",
                         QJsonArray{cell_json("barrier_30m", "top", "gauss", 0.55, 0.25)}, "2026-10-28T22:31:04+09:00",
                         none, paper_json("2026-10-29", "2026-10-29", 1, 1, 500.0, 500.0, none)),
             policy_json(5, "challenger", "2026-10-15T22:31:04+09:00",
                         QJsonArray{cell_json("barrier", "top", "breiman", 0.7, 0.5)}, "2026-10-15T22:31:04+09:00", none,
                         paper_json("2026-10-12", "2026-10-29", 12, 20, -3000.0, -250.0, -900.0)),
             policy_json(4, "challenger", "2026-10-12T22:31:04+09:00",
                         QJsonArray{cell_json("barrier", "top", "breiman", 0.65, 1.0)}, "2026-10-12T22:31:04+09:00", none,
                         paper_json("2026-10-13", "2026-10-29", 13, 26, 15000.0, 1153.8, 120.0)),
             policy_json(3, "dropped", "2026-10-09T22:31:04+09:00",
                         QJsonArray{cell_json("direction", "consensus", "consensus-final", 0.6, 0.5)},
                         "2026-10-09T22:31:04+09:00", "2026-10-28T22:30:40+09:00",
                         paper_json("2026-10-12", "2026-10-28", 20, 31, -9000.0, -450.0, -1500.0)),
             policy_json(2, "champion", "2026-10-08T22:31:04+09:00",
                         QJsonArray{cell_json("barrier", "top", "breiman", 0.65, 1.0),
                                    cell_json("barrier_15m", "baseline", "barrier-persist", 0.7, 0.25)},
                         "2026-10-08T22:31:04+09:00", none,
                         paper_json("2026-10-12", "2026-10-28", 14, 28, 11200.0, 800.0, -150.0)),
             policy_json(1, "former", "2026-10-07T22:31:04+09:00",
                         QJsonArray{cell_json("barrier", "top", "breiman", 0.6, 0.5)}, none, none,
                         paper_json("2026-10-08", "2026-10-29", 16, 30, -4000.0, -250.0, -2000.0)),
         }},
        {"champions",
         QJsonArray{QJsonObject{{"at", "2026-10-29T22:30:40+09:00"},
                                {"version", 2},
                                {"reason", "도전 정책 2이 챔피언 정책 1을 모의로 이겼다(#400)"},
                                {"detail", QJsonObject{{"from", 1},
                                                       {"days", 10},
                                                       {"required", 10},
                                                       {"mean", 1100.0},
                                                       {"low", 63.35},
                                                       {"level", 0.95},
                                                       {"challengers", 1}}}},
                    QJsonObject{{"at", "2026-10-07T22:31:04+09:00"},
                                {"version", 1},
                                {"reason", "처음 맞춘 정책(#399)"},
                                {"detail", QJsonValue()}}}}};
}

/// The state's champion, gate and running policies for busy_policies_json: the gate not passed
/// (15/20 days, the bound below 0) unless ``passed`` (21 days, +393원).
QJsonObject busy_state_json(bool passed = false) {
    const QJsonValue none;
    QJsonObject o = state_json();
    o["champion"] = 2;
    o["gate"] = passed ? QJsonObject{{"policy", 2},    {"days", 21},  {"required", 20}, {"mean", 9300.0},
                                     {"low", 3930.0}, {"level", 0.95}, {"passed", true}}
                       : QJsonObject{{"policy", 2},     {"days", 15},  {"required", 20}, {"mean", 800.0},
                                     {"low", -3000.0}, {"level", 0.95}, {"passed", false}};
    o["policies"] = QJsonArray{
        QJsonObject{{"version", 2},
                    {"status", "champion"},
                    {"paper", paper_json("2026-10-12", "2026-10-29", 15, 30, 12000.0, 800.0, -3000.0)},
                    {"promotion", none}},
        QJsonObject{{"version", 4},
                    {"status", "challenger"},
                    {"paper", paper_json("2026-10-13", "2026-10-29", 13, 26, 15000.0, 1153.8, 120.0)},
                    {"promotion", promotion_json(12, 600.0, 63.35, 0.98333, 3, true, none)}},
        QJsonObject{{"version", 5},
                    {"status", "challenger"},
                    {"paper", paper_json("2026-10-12", "2026-10-29", 12, 20, -3000.0, -250.0, -900.0)},
                    {"promotion", promotion_json(12, -300.0, -244.0, 0.98333, 3, false, "not_above")}},
        QJsonObject{{"version", 6},
                    {"status", "challenger"},
                    {"paper", paper_json("2026-10-29", "2026-10-29", 1, 1, 500.0, 500.0, none)},
                    {"promotion", promotion_json(1, 300.0, none, 0.98333, 3, false, "few_days")}}};
    return o;
}

/// The state for lone_policies_json: champion 1, its gate at 2/20 days.
QJsonObject lone_state_json() {
    QJsonObject o = state_json();
    o["champion"] = 1;
    o["gate"] = QJsonObject{{"policy", 1},       {"days", 2},     {"required", 20}, {"mean", -5209.185},
                            {"low", -26882.09}, {"level", 0.95}, {"passed", false}};
    o["policies"] = QJsonArray{
        QJsonObject{{"version", 1},
                    {"status", "champion"},
                    {"paper", paper_json("2026-10-08", "2026-10-12", 2, 4, -10418.37, -5209.185, -26882.09)},
                    {"promotion", QJsonValue()}}};
    return o;
}

/// The cohort of review cycle 1 (codeyoma/krx-quant#473): policy 2 against policy 1, policy 3 passed over;
/// once ``judged``, policy 2 crowned on 11-06's night (made-up values).
QJsonObject cohort_json(bool judged) {
    QJsonObject o{{"evaluation", 2},
                  {"cycle", 1},
                  {"formed_at", "2026-10-08T22:31:04+09:00"},
                  {"start", "2026-10-12"},
                  {"review", "2026-11-06"},
                  {"champion", 1},
                  {"members", QJsonArray{2}},
                  {"passed_over", QJsonArray{QJsonObject{{"version", 3}, {"why", "older"}}}},
                  {"verdict", QJsonValue()}};
    if (judged)
        o["verdict"] = QJsonObject{{"judged_at", "2026-11-06T22:30:40+09:00"},
                                   {"champion", 1},
                                   {"outcome", "promoted"},
                                   {"promoted", 2},
                                   {"level", 0.95},
                                   {"members", QJsonArray{QJsonObject{{"version", 2},
                                                                      {"days", 18},
                                                                      {"mean", 1100.0},
                                                                      {"low", 63.35},
                                                                      {"verdict", "promoted"}}}}};
    return o;
}

/// A cell's calibration (codeyoma/krx-quant#475): 25 rises in its first bin, none in the others, 7 under it.
QJsonObject calibration_json() {
    QJsonArray bins;
    const double edges[] = {0.4, 0.5, 0.6, 0.7, 0.8, 1.0};
    for (int i = 0; i < 5; ++i) {
        const bool first = i == 0;
        bins.append(QJsonObject{{"from", edges[i]},
                                {"to", edges[i + 1]},
                                {"signals", first ? 25 : 0},
                                {"missing", 0},
                                {"answered", first ? 25 : 0},
                                {"outcomes", QJsonObject{{"up", first ? 10 : 0}, {"flat", first ? 6 : 0}, {"down", first ? 9 : 0}}},
                                {"hits", first ? 10 : 0},
                                {"first", first ? 9 : 0},
                                {"bought", first ? 23 : 0},
                                {"profitable", first ? 8 : 0}});
    }
    return {{"bins", bins}, {"below", 7}};
}

/// A policy fitted since codeyoma/krx-quant#472, #475: its cell without λ, entered on its replayed net per
/// signal's lower bound, with its calibration; two cells left out (no_edge, few_days); the cohort judged.
QJsonObject new_policies_json() {
    QJsonObject cell{{"kind", "barrier"},
                     {"role", "top"},
                     {"predictor", "breiman"},
                     {"threshold", 0.7},
                     {"fitted", QJsonObject{{"p_star", 0.63},
                                            {"low", 0.6},
                                            {"kelly", QJsonValue()},
                                            {"verdict", "fail"},
                                            {"days", QJsonArray{"2026-10-07", "2026-10-08"}},
                                            {"recorded_days", 20},
                                            {"read", QJsonObject{{"read", 380}, {"rises", 120}}},
                                            {"bound", QJsonObject{{"value", 0.31}, {"low", 0.04}, {"high", 0.58}, {"days", 20}, {"level", 0.99889}}},
                                            {"trades", 64},
                                            {"wins", 38},
                                            {"net", 21000.0},
                                            {"growth", 0.02},
                                            {"calibration", calibration_json()}}}};
    QJsonObject policy = policy_json(2, "champion", "2026-10-08T22:31:04+09:00", QJsonArray{cell},
                                     "2026-10-08T22:31:04+09:00", QJsonValue(),
                                     paper_json("2026-10-12", "2026-11-06", 18, 30, 12000.0, 666.7, 63.35));
    policy["left_out"] = QJsonArray{
        QJsonObject{{"kind", "direction"}, {"role", "consensus"}, {"predictor", "consensus-final"}, {"p_star", 0.58},
                    {"low", 0.52}, {"kelly", QJsonValue()}, {"verdict", "pass"}, {"why", "no_edge"},
                    {"recorded_days", 20},
                    {"best", QJsonObject{{"threshold", 0.6},
                                         {"bound", QJsonObject{{"value", -0.31}, {"low", -0.9}, {"high", 0.28}, {"days", 20}, {"level", 0.99889}}}}},
                    {"read", QJsonObject{{"read", 1640}}}, {"calibration", calibration_json()}},
        QJsonObject{{"kind", "barrier_15m"}, {"role", "baseline"}, {"predictor", "barrier-persist-15m"},
                    {"p_star", QJsonValue()}, {"low", QJsonValue()}, {"kelly", QJsonValue()}, {"why", "few_days"},
                    {"recorded_days", 12}, {"read", QJsonObject{}}}};
    return {{"champion", 2},
            {"policies", QJsonArray{policy_json(4, "waiting", "2026-11-05T22:31:04+09:00", QJsonArray{}, QJsonValue(),
                                                QJsonValue(), paper_json(QJsonValue(), QJsonValue(), 0, 0, 0.0, QJsonValue(), QJsonValue())),
                                    policy,
                                    policy_json(3, "dropped", "2026-10-07T22:41:00+09:00",
                                                QJsonArray{cell_json("barrier", "top", "breiman", 0.6, 0.5)}, QJsonValue(),
                                                "2026-10-08T22:31:04+09:00",
                                                paper_json(QJsonValue(), QJsonValue(), 0, 0, 0.0, QJsonValue(), QJsonValue())),
                                    policy_json(1, "former", "2026-10-07T22:31:04+09:00",
                                                QJsonArray{cell_json("barrier", "top", "breiman", 0.6, 0.5)}, QJsonValue(),
                                                QJsonValue(), paper_json("2026-10-08", "2026-11-06", 20, 30, -4000.0, -200.0, -900.0))}},
            {"champions", QJsonArray{QJsonObject{{"at", "2026-11-06T22:30:40+09:00"},
                                                 {"version", 2},
                                                 {"reason", "주기 1 묶음의 검토 날 판정(#473)"},
                                                 {"detail", QJsonObject{{"from", 1}, {"days", 18}, {"required", 10}, {"mean", 1100.0},
                                                                        {"low", 63.35}, {"level", 0.95}, {"challengers", 1},
                                                                        {"evaluation", 2}, {"cycle", 1}}}},
                                     QJsonObject{{"at", "2026-10-07T22:31:04+09:00"},
                                                 {"version", 1},
                                                 {"reason", "처음 맞춘 정책(#399)"},
                                                 {"detail", QJsonValue()}}}},
            {"cohorts", QJsonArray{cohort_json(true)}}};
}

TraderPolicies policies(const QJsonObject& o) {
    auto p = parse_trader_policies(QJsonDocument(o));
    if (p.is_err())
        qFatal("%s", p.error().c_str());
    return p.value();
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
/// The chart's reads (codeyoma/krx-quant#537).
const QString kChartBars = QStringLiteral("/v1/symbols/005930/bars");
const QString kChartMarks = QStringLiteral("/v1/symbols/005930/trader-marks");

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

/// ``count`` 1-minute bars from ``first`` on, as the engine sends them; made-up prices.
QJsonArray bars_json(const char* first, int count) {
    const QDateTime start = kst(first);
    QJsonArray out;
    for (int i = 0; i < count; ++i)
        out.append(QJsonObject{{"start", start.addSecs(60 * i).toString(Qt::ISODate)},
                               {"open", 70000},
                               {"high", 70300},
                               {"low", 69900},
                               {"close", 70000 + 100 * (i % 3)},
                               {"volume", 1000 + i},
                               {"trades", QJsonValue::Null}});
    return out;
}

/// ``count`` made-up trader marks a second apart, newest first, the newest ``count - 1`` seconds after
/// ``from``; trade ``first_trade`` on, a buy and its sell each.
QList<QJsonObject> marks_from(const QDateTime& from, int count, int first_trade = 1) {
    QList<QJsonObject> marks;
    for (int i = count - 1; i >= 0; --i)
        marks << QJsonObject{{"trade_id", first_trade + i / 2},
                             {"side", i % 2 ? "sell" : "buy"},
                             {"at", from.addSecs(i).toString(Qt::ISODate)},
                             {"price", 70000},
                             {"quantity", 4},
                             {"mode", "paper"},
                             {"policy", QJsonValue::Null},
                             {"kind", "barrier"},
                             {"predictor", "breiman"},
                             {"exit_reason", i % 2 ? "time" : QJsonValue()},
                             {"net", i % 2 ? QJsonValue(-120.5) : QJsonValue()}};
    return marks;
}

/// GET /v1/trader/decisions?day=2026-10-12&book=champion as the demo engine answered it (krx-quant #584): the
/// champion's two books of that morning (paper until 10:05, live after), buys alone, no θ, p* or answer.
const char* const kDemoChampionDecisions =
    R"({"day":"2026-10-12","book":"champion","books":[{"book":"champion","mode":"paper","policy":1,"totals":{"decisions":3,"bought":3,"waiting":0,"unfilled":{},"skipped":{}},"decisions":[{"id":5,"decided_at":"2026-10-12T09:55:02+09:00","symbol":"005930","kind":"barrier","predictor":"breiman","prediction_id":5,"bar_close":"2026-10-12T09:55:00+09:00","displayed_at":"2026-10-12T09:55:02+09:00","probability":0.68,"threshold":null,"p_star":null,"action":"buy","reason":null,"trade_id":5,"answer":null,"right":null},{"id":4,"decided_at":"2026-10-12T09:49:02+09:00","symbol":"005930","kind":"barrier","predictor":"breiman","prediction_id":4,"bar_close":"2026-10-12T09:49:00+09:00","displayed_at":"2026-10-12T09:49:02+09:00","probability":0.68,"threshold":null,"p_star":null,"action":"buy","reason":null,"trade_id":4,"answer":null,"right":null},{"id":3,"decided_at":"2026-10-12T09:43:02+09:00","symbol":"005930","kind":"barrier","predictor":"breiman","prediction_id":3,"bar_close":"2026-10-12T09:43:00+09:00","displayed_at":"2026-10-12T09:43:02+09:00","probability":0.68,"threshold":null,"p_star":null,"action":"buy","reason":null,"trade_id":3,"answer":null,"right":null}],"more":false},{"book":"champion","mode":"live","policy":1,"totals":{"decisions":4,"bought":4,"waiting":0,"unfilled":{},"skipped":{}},"decisions":[{"id":9,"decided_at":"2026-10-12T10:27:02+09:00","symbol":"005930","kind":"barrier","predictor":"breiman","prediction_id":9,"bar_close":"2026-10-12T10:27:00+09:00","displayed_at":"2026-10-12T10:27:02+09:00","probability":0.68,"threshold":null,"p_star":null,"action":"buy","reason":null,"trade_id":9,"answer":null,"right":null},{"id":8,"decided_at":"2026-10-12T10:18:02+09:00","symbol":"005930","kind":"barrier","predictor":"breiman","prediction_id":8,"bar_close":"2026-10-12T10:18:00+09:00","displayed_at":"2026-10-12T10:18:02+09:00","probability":0.68,"threshold":null,"p_star":null,"action":"buy","reason":null,"trade_id":8,"answer":null,"right":null},{"id":7,"decided_at":"2026-10-12T10:12:02+09:00","symbol":"005930","kind":"barrier","predictor":"breiman","prediction_id":7,"bar_close":"2026-10-12T10:12:00+09:00","displayed_at":"2026-10-12T10:12:02+09:00","probability":0.68,"threshold":null,"p_star":null,"action":"buy","reason":null,"trade_id":7,"answer":null,"right":null},{"id":6,"decided_at":"2026-10-12T10:06:02+09:00","symbol":"005930","kind":"barrier","predictor":"breiman","prediction_id":6,"bar_close":"2026-10-12T10:06:00+09:00","displayed_at":"2026-10-12T10:06:02+09:00","probability":0.68,"threshold":null,"p_star":null,"action":"buy","reason":null,"trade_id":6,"answer":null,"right":null}],"more":false}]})";

/// A decision as the engine gives it (krx-quant #584), made up: a barrier cell of breiman's unless said.
QJsonObject decision_json(qint64 id, const QString& at, const char* action, QJsonValue reason, QJsonValue probability,
                          QJsonValue threshold = 0.6, QJsonValue trade = QJsonValue(), QJsonValue answer = QJsonValue(),
                          QJsonValue right = QJsonValue(), const char* kind = "barrier",
                          const char* predictor = "breiman") {
    const QDateTime decided = QDateTime::fromString(at, Qt::ISODateWithMs);
    const QDateTime close(decided.date(), QTime(decided.time().hour(), decided.time().minute()), decided.timeZone());
    return {{"id", id},
            {"decided_at", at},
            {"symbol", "005930"},
            {"kind", kind},
            {"predictor", predictor},
            {"prediction_id", 4000 + id},
            {"bar_close", close.toString(Qt::ISODate)},
            {"displayed_at", decided.addMSecs(-200).toString(Qt::ISODateWithMs)},
            {"probability", probability},
            {"threshold", threshold},
            {"p_star", threshold.isNull() ? QJsonValue() : QJsonValue(0.58)},
            {"action", action},
            {"reason", reason},
            {"trade_id", trade},
            {"answer", answer},
            {"right", right}};
}

/// The practice's morning of 10-16, made up (codeyoma/krx-quant#601): a buy waiting for its quote, a skip
/// that missed a rise, a buy filled right, a buy unfilled (no quote), a skip of no rise, and a direction
/// cell's skip without its θ, p* or answer; the reasons' counts given in no order.
QJsonObject practice_json() {
    const QJsonArray decisions{
        decision_json(106, "2026-10-16T09:12:02.100+09:00", "buy", QJsonValue(), 0.7),
        decision_json(105, "2026-10-16T09:11:02.400+09:00", "skip", "holding", 0.66, 0.6, QJsonValue(), "up", true),
        decision_json(104, "2026-10-16T09:10:02.300+09:00", "buy", QJsonValue(), 0.71, 0.6, 41, "up", true),
        decision_json(103, "2026-10-16T09:09:02.250+09:00", "buy", "no_quote", 0.68, 0.6, QJsonValue(), "down", false),
        decision_json(102, "2026-10-16T09:08:02.200+09:00", "skip", "not_up", 0.41, 0.6, QJsonValue(), "flat", false),
        decision_json(101, "2026-10-16T09:07:02+09:00", "skip", "not_up", 0.2, QJsonValue(), QJsonValue(), QJsonValue(),
                      QJsonValue(), "direction", "gauss"),
    };
    return {{"day", "2026-10-16"},
            {"book", "practice"},
            {"books",
             QJsonArray{QJsonObject{{"book", "practice"},
                                    {"mode", "mock"},
                                    {"policy", QJsonValue()},
                                    {"totals", QJsonObject{{"decisions", 6},
                                                           {"bought", 1},
                                                           {"waiting", 1},
                                                           {"unfilled", QJsonObject{{"no_quote", 1}}},
                                                           {"skipped", QJsonObject{{"holding", 1}, {"not_up", 2}}}}},
                                    {"decisions", decisions},
                                    {"more", false}}}}};
}

/// ``count`` made-up skips a second apart, newest first, the newest ``count - 1`` seconds after ``from``,
/// ids from ``first_id`` on.
QJsonArray skips_from(const QDateTime& from, int count, qint64 first_id) {
    QJsonArray out;
    for (int i = count - 1; i >= 0; --i)
        out.append(decision_json(first_id + i, from.addSecs(i).toString(Qt::ISODateWithMs), "skip", "not_up", 0.4));
    return out;
}

/// A book of ``decisions`` as the engine keeps it for the stand-in: all of its day, newest first.
QJsonObject book_json(const char* book, const char* mode, QJsonValue policy, const QJsonArray& decisions) {
    return {{"book", book},
            {"mode", mode},
            {"policy", policy},
            {"totals", QJsonObject{{"decisions", decisions.size()},
                                   {"bought", 0},
                                   {"waiting", 0},
                                   {"unfilled", QJsonObject{}},
                                   {"skipped", QJsonObject{{"not_up", decisions.size()}}}}},
            {"decisions", decisions},
            {"more", false}};
}

TraderDecisions decisions_of(const QJsonObject& o) {
    auto d = parse_trader_decisions(QJsonDocument(o));
    if (d.is_err())
        qFatal("%s", d.error().c_str());
    return d.value();
}

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
    /// GET /v1/trader/trades is answered only on release() while this holds (codeyoma/krx-quant#401: an
    /// answer still under way when another policy is chosen).
    bool hold_trades = false;
    bool without_policies = false; ///< an engine before codeyoma/krx-quant#399: 404 for the versions
    QJsonObject state = state_json();
    QJsonObject policies = lone_policies_json();
    /// The chart's (codeyoma/krx-quant#537): GET /v1/symbols/005930/bars, the day's 1-minute bars; with
    /// ``limit=`` and no ``before=`` (the latest before now) ``last_day``; with ``before=``, none (nothing
    /// earlier).
    QJsonArray bars;
    QJsonArray last_day;
    /// GET /v1/symbols/005930/trader-marks: these, newest first, answered by ``before`` and ``limit`` as the
    /// engine answers; none: 404, as an engine before krx-quant #398.
    std::optional<QList<QJsonObject>> marks;
    /// GET /v1/trader/decisions (codeyoma/krx-quant#601): each book's day by its name, as the engine keeps it
    /// (its books with all their decisions, newest first), answered by ``before`` and ``limit`` as the engine
    /// answers (krx-quant #584), whatever the day asked; a book not here has none. None: 404, as an engine
    /// before #584.
    std::optional<QMap<QString, QJsonObject>> decisions;
    bool fail_decisions = false; ///< the decisions alone fail
    /// GET /v1/trader/decisions is answered only on release() while this holds.
    bool hold_decisions = false;
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
    /// Whether a request's path (with its query) starts with ``prefix``.
    bool asked_for(const QString& prefix) const {
        return std::any_of(asked.cbegin(), asked.cend(), [&](const Request& r) { return r.path.startsWith(prefix); });
    }
    /// The trades held answered now.
    void release() {
        const auto held = std::exchange(held_, {});
        for (const auto& [socket, r] : held)
            if (socket)
                respond(socket, r);
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
        if ((hold_trades && r.path.section('?', 0, 0) == "/v1/trader/trades") ||
            (hold_decisions && r.path.section('?', 0, 0) == "/v1/trader/decisions")) {
            held_.push_back({QPointer<QTcpSocket>(socket), r});
            return;
        }
        respond(socket, r);
    }

    void respond(QTcpSocket* socket, const Request& r) {
        int status = 404;
        QJsonObject answer{{"error", "not found"}};
        const QString path = r.path.section('?', 0, 0);
        if (without_trader) {
        } else if (fail || (fail_pause && path == "/v1/trader/pause") ||
                   (fail_decisions && path == "/v1/trader/decisions")) {
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
            answer = r.path.contains("policy=2") ? challenger_trades_json() : trades_json();
        } else if (r.method == "GET" && path == "/v1/trader/policies" && !without_policies) {
            status = 200;
            answer = policies;
        } else if (r.method == "GET" && path == kChartBars) {
            status = 200;
            const bool before = r.path.contains("before=");
            answer = {{"symbol", "005930"}, {"bars", !r.path.contains('?') ? bars : before ? QJsonArray{} : last_day}};
        } else if (r.method == "GET" && path == kChartMarks && marks) {
            // As the engine answers (krx-quant #398): to the microsecond, strictly before, and a page never
            // splits the marks of one moment.
            const QUrlQuery query(r.path.section('?', 1));
            const QString before = query.queryItemValue("before", QUrl::FullyDecoded);
            const int limit = query.queryItemValue("limit").toInt();
            QJsonArray page;
            QString last; // the moment the page ends on
            for (const auto& m : *marks) {
                const QString at = moment(m.value("at").toString());
                if (!before.isEmpty() && at >= moment(before))
                    continue;
                if (page.size() >= limit && at != last)
                    break;
                page.append(m);
                last = at;
            }
            status = 200;
            answer = {{"symbol", "005930"}, {"marks", page}};
        } else if (r.method == "GET" && path == "/v1/trader/decisions" && decisions) {
            // As the engine answers (krx-quant #584): each book its last ``limit`` decided strictly before
            // ``before`` (to the microsecond), and any more of the moment the page ends on.
            const QUrlQuery query(r.path.section('?', 1));
            const QString book = query.queryItemValue("book");
            const QString day = query.queryItemValue("day");
            const QString before = query.queryItemValue("before", QUrl::FullyDecoded);
            const int limit = query.queryItemValue("limit").toInt();
            QJsonArray books;
            for (const auto& v : decisions->value(book).value("books").toArray()) {
                QJsonObject b = v.toObject();
                QJsonArray page;
                QString last;
                bool more = false;
                for (const auto& d : b.value("decisions").toArray()) {
                    const QString at = moment(d.toObject().value("decided_at").toString());
                    if (!before.isEmpty() && at >= moment(before))
                        continue;
                    if (page.size() >= limit && at != last) {
                        more = true;
                        break;
                    }
                    page.append(d);
                    last = at;
                }
                if (page.isEmpty())
                    continue; // no decision of it before then
                b["decisions"] = page;
                b["more"] = more;
                books.append(b);
            }
            status = 200;
            // Asked without a day: the engine's today, the 17th, a day after the chart's bars.
            answer = {{"day", day.isEmpty() ? QStringLiteral("2026-10-17") : day}, {"book", book}, {"books", books}};
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
    QList<std::pair<QPointer<QTcpSocket>, Request>> held_;
};

/// The live object's own schedule, fired now (its next read, as the timer would start it).
void fire_schedule(services::KrxTraderLive& live) {
    auto* timer = live.findChild<QTimer*>(services::KrxTraderLive::kStateTimerName, Qt::FindDirectChildrenOnly);
    QVERIFY(timer);
    timer->start(0);
}

/// The chart's timer (codeyoma/krx-quant#537).
QTimer* chart_timer(services::KrxTraderLive& live) {
    return live.findChild<QTimer*>(services::KrxTraderLive::kChartTimerName, Qt::FindDirectChildrenOnly);
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

    void the_champion_and_the_live_gate_are_read_and_shown() {
        // codeyoma/krx-quant#400: the engine gives the champion's version and the gate on its paper record.
        QJsonObject o = state_json();
        o["champion"] = 1;
        o["gate"] = QJsonObject{{"policy", 1},  {"days", 12},     {"required", 20},  {"mean", 1200.0},
                                {"low", -3000.0}, {"level", 0.95}, {"passed", false}};
        const TraderState s = state(o);
        QCOMPARE(*s.champion, 1);
        QVERIFY(s.gate.has_value());
        QCOMPARE(s.gate->days, 12);
        QCOMPARE(*s.gate->low, -3000.0);
        QCOMPARE(view::gate_text(s), QStringLiteral("챔피언 정책 1 · 관문 모의 12/20일 · 하한 -0.3%"));
        QVERIFY(view::gate_tip(s).contains(QStringLiteral("20거래일")));
        QVERIFY(view::gate_tip(s).contains(QStringLiteral("서버")));
        // Passed: said so.
        o["gate"] = QJsonObject{{"policy", 1},  {"days", 21},     {"required", 20}, {"mean", 1200.0},
                                {"low", 500.0}, {"level", 0.95}, {"passed", true}};
        QCOMPARE(view::gate_text(state(o)), QStringLiteral("챔피언 정책 1 · 관문 모의 21/20일 · 하한 0.1% · 관문 넘음"));
        // Fewer than two days: no lower bound yet.
        o["gate"] = QJsonObject{{"policy", 1},          {"days", 1},      {"required", 20}, {"mean", -500.0},
                                {"low", QJsonValue()}, {"level", 0.95}, {"passed", false}};
        QCOMPARE(view::gate_text(state(o)), QStringLiteral("챔피언 정책 1 · 관문 모의 1/20일 · 하한 —"));
        // Before the first champion.
        o["champion"] = QJsonValue();
        o["gate"] = QJsonObject{{"policy", QJsonValue()}, {"days", 0},      {"required", 20}, {"mean", QJsonValue()},
                                {"low", QJsonValue()},    {"level", 0.95}, {"passed", false}};
        QCOMPARE(view::gate_text(state(o)), QStringLiteral("챔피언 정책 없음"));
        // An engine before #400: nothing.
        QVERIFY(!state().gate.has_value());
        QVERIFY(!state().champion.has_value());
        QVERIFY(view::gate_text(state()).isEmpty());
    }

    void the_policy_versions_are_read_with_their_cells_records_and_history() {
        // codeyoma/krx-quant#399, #400: GET /v1/trader/policies, and the state's running policies.
        QJsonObject o = busy_policies_json();
        QJsonArray versions = o["policies"].toArray();
        QJsonObject two = versions[5].toObject();
        two["left_out"] = QJsonArray{QJsonObject{{"kind", "direction"}, {"role", "consensus"}, {"predictor", "consensus"},
                                                 {"p_star", 0.6}, {"low", 0.62}, {"kelly", 3.0}, {"why", "few_trades"},
                                                 {"most_trades", 12}, {"read", QJsonObject{}}}};
        two["fit"] = QJsonObject{{"cost_map", QJsonObject{{"as_of", "2026-10-08T22:31:02+09:00"}, {"period", "20d"}, {"passed", 3}}},
                                 {"result", QJsonObject{{"trades", 64}, {"wins", 38}, {"net", 21000.0}, {"growth", 0.02}}}};
        versions[5] = two;
        o["policies"] = versions;
        const TraderPolicies p = policies(o);
        QCOMPARE(*p.champion, 2);
        QCOMPARE(p.policies.size(), 7);
        QCOMPARE(p.policies[0].version, 7); // newest first, as the engine sends them
        QCOMPARE(p.policies[0].status, QStringLiteral("waiting"));
        QVERIFY(p.policies[0].cells.isEmpty());
        QVERIFY(!p.policies[0].admitted_at.isValid());
        QVERIFY(!p.policies[0].paper.since.isValid());
        QVERIFY(!p.policies[0].paper.low.has_value());
        const TraderPolicy& champion = p.policies[5];
        QCOMPARE(champion.cells.size(), 2);
        QCOMPARE(champion.cells[1].kind, QStringLiteral("barrier_15m"));
        QCOMPARE(champion.cells[1].role, QStringLiteral("baseline"));
        QCOMPARE(champion.cells[1].threshold, 0.7);
        QCOMPARE(champion.cells[1].kelly_fraction, 0.25);
        QCOMPARE(*champion.cells[0].p_star, 0.63);
        QCOMPARE(champion.cells[0].fitted_days, 2);
        QCOMPARE(champion.cells[0].fitted_trades, 24);
        QCOMPARE(champion.cells[0].wins, 14);
        QCOMPARE(champion.left_out.size(), 1);
        QCOMPARE(champion.left_out[0].why, QStringLiteral("few_trades"));
        QCOMPARE(*champion.left_out[0].most_trades, 12);
        QCOMPARE(champion.cost_map_at, kst("2026-10-08T22:31:02+09:00"));
        QCOMPARE(*champion.passed, 3);
        QCOMPARE(*champion.simulated_trades, 64);
        QCOMPARE(champion.paper.days, 14);
        QCOMPARE(*champion.paper.low, -150.0);
        QCOMPARE(p.policies[4].dropped_at, kst("2026-10-28T22:30:40+09:00"));
        QVERIFY(!p.policies[6].cost_map_at.isValid()); // an empty fit: nothing assumed
        QVERIFY(!p.policies[6].simulated_trades.has_value());
        QCOMPARE(p.champions.size(), 2);
        QCOMPARE(p.champions[0].version, 2);
        QVERIFY(p.champions[0].detail.has_value());
        QCOMPARE(p.champions[0].detail->from, 1);
        QCOMPARE(*p.champions[0].detail->low, 63.35);
        QVERIFY(!p.champions[1].detail.has_value()); // the first champion
        // Before the first nightly fit: nothing.
        const TraderPolicies none =
            policies(QJsonObject{{"champion", QJsonValue()}, {"policies", QJsonArray{}}, {"champions", QJsonArray{}}});
        QVERIFY(!none.champion.has_value());
        QVERIFY(none.policies.isEmpty());
        QVERIFY(parse_trader_policies(QJsonDocument(QJsonObject{{"error", "bad limit"}})).is_err());

        // The state's running policies: the champion and the challengers, with their standing.
        const TraderState s = state(busy_state_json());
        QCOMPARE(s.policies.size(), 4);
        QCOMPARE(s.policies[0].status, QStringLiteral("champion"));
        QVERIFY(!s.policies[0].promotion.has_value());
        QCOMPARE(s.policies[0].paper.days, 15);
        QCOMPARE(s.policies[1].promotion->challengers, 3);
        QVERIFY(s.policies[1].promotion->passed);
        QVERIFY(s.policies[1].promotion->why.isEmpty());
        QCOMPARE(s.policies[2].promotion->why, QStringLiteral("not_above"));
        QVERIFY(!s.policies[3].promotion->low.has_value());
        QVERIFY(state().policies.isEmpty()); // an engine before #400

        // A trade says whose book made it; true from an engine before #400.
        const auto mine = parse_trader_trades(QJsonDocument(challenger_trades_json())).value();
        QVERIFY(!mine[0].champion);
        QCOMPARE(*mine[0].policy, 2);
        QVERIFY(trades()[0].champion);
    }

    void the_policy_table_without_a_challenger() {
        const TraderPolicies p = policies(lone_policies_json());
        const TraderState s = state(lone_state_json());
        const auto rows = view::policy_rows(p, s);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].version_text, QStringLiteral("정책 1"));
        QCOMPARE(rows[0].status, QStringLiteral("챔피언"));
        QCOMPARE(rows[0].status_tone, Tone::Positive);
        QCOMPARE(rows[0].created, QStringLiteral("10-07 22:31"));
        QCOMPARE(rows[0].cells, QStringLiteral("장벽 4분 · breiman (1위)"));
        QCOMPARE(rows[0].thresholds, QStringLiteral("60.0%"));
        QCOMPARE(rows[0].basis, QStringLiteral("λ 0.5"));
        QCOMPARE(rows[0].days, QStringLiteral("2일"));
        QCOMPARE(rows[0].trades, QStringLiteral("4개"));
        QCOMPARE(rows[0].net, QStringLiteral("-10,418원"));
        QCOMPARE(rows[0].net_tone, Tone::Negative);
        QCOMPARE(rows[0].mean, QStringLiteral("-5,209원"));
        QCOMPARE(rows[0].low, QStringLiteral("-26,882원"));
        QCOMPARE(rows[0].judgement, QStringLiteral("관문 2/20일 · 못 넘음"));
        QCOMPARE(rows[0].judgement_tone, Tone::Plain);
        QVERIFY(rows[0].paper_tip.contains(QStringLiteral("10-08(목) ~ 10-12(월), 2거래일")));
        QVERIFY(rows[0].paper_tip.contains(QStringLiteral("운용 상한의 -2.7%")));
        QVERIFY(rows[0].cells_tip.contains(QStringLiteral("p* 63.0% · 하한 70.0%")));
        QVERIFY(rows[0].cells_tip.contains(QStringLiteral("모의 매매 24개(이익 14)")));
        QVERIFY(rows[0].tip.contains(QStringLiteral("2026-10-07 밤 맞추기")));
        QCOMPARE(view::policies_line(p), QStringLiteral("챔피언 정책 1 · 도전 0개 · 대기 0개 · 전 챔피언 0개 · 내려짐 0개"));
        // The history: the first champion alone.
        const auto history = view::history_rows(p);
        QCOMPARE(history.size(), 1);
        QCOMPARE(history[0].what, QStringLiteral("처음 챔피언"));
        QCOMPARE(history[0].why, QStringLiteral("처음 맞춘 정책(#399)"));
        QCOMPARE(history[0].at, QStringLiteral("10-07 22:31"));
        // The trades' choices: the champion's, then policy 1's.
        const auto choices = view::trade_policy_choices(p, s);
        QCOMPARE(choices.size(), 2);
        QVERIFY(!choices[0].version.has_value());
        QCOMPARE(choices[0].text, QStringLiteral("챔피언의 매매"));
        QCOMPARE(*choices[1].version, 1);
        QCOMPARE(choices[1].text, QStringLiteral("정책 1 · 챔피언"));
        // Before the versions are read: the state's running ones; before either, the champion's alone.
        QCOMPARE(view::trade_policy_choices(std::nullopt, s).size(), 2);
        QCOMPARE(view::trade_policy_choices(std::nullopt, std::nullopt).size(), 1);
        // Before the first nightly fit.
        const TraderPolicies none =
            policies(QJsonObject{{"champion", QJsonValue()}, {"policies", QJsonArray{}}, {"champions", QJsonArray{}}});
        QVERIFY(view::policy_rows(none, s).isEmpty());
        QVERIFY(view::history_rows(none).isEmpty());
        QVERIFY(view::policies_line(none).contains(QStringLiteral("첫 밤 정책 맞추기 전")));
    }

    void the_policy_table_with_three_challengers_one_waiting_one_former_and_one_dropped() {
        const TraderPolicies p = policies(busy_policies_json());
        const TraderState s = state(busy_state_json());
        const auto rows = view::policy_rows(p, s);
        QCOMPARE(rows.size(), 7);
        // The champion, the challengers, the waiting, the former, the dropped; the newest first in each.
        QList<qint64> order;
        QStringList statuses;
        for (const auto& row : rows) {
            order << row.version;
            statuses << row.status;
        }
        QCOMPARE(order, (QList<qint64>{2, 6, 5, 4, 7, 1, 3}));
        QCOMPARE(statuses, (QStringList{"챔피언", "도전", "도전", "도전", "대기", "전 챔피언", "내려짐"}));
        // Two cells in rank order, each one's θ and λ in the same order.
        QCOMPARE(rows[0].cells, QStringLiteral("장벽 4분 · breiman (1위) / 장벽 15분 · 기준: 직전 4분 장벽 (기준)"));
        QCOMPARE(rows[0].thresholds, QStringLiteral("65.0% / 70.0%"));
        QCOMPARE(rows[0].basis, QStringLiteral("λ 1 / λ 0.25"));
        // A running one's record is the state's (read every few seconds), not the versions' older one.
        QCOMPARE(rows[0].days, QStringLiteral("15일"));
        QCOMPARE(rows[0].net, QStringLiteral("+12,000원"));
        QCOMPARE(rows[0].net_tone, Tone::Positive);
        // Each challenger's standing against the champion, over three challengers (Bonferroni).
        QCOMPARE(rows[3].judgement, QStringLiteral("이김 · 함께 12/10일 · 차이 하한 +63원"));
        QCOMPARE(rows[3].judgement_tone, Tone::Positive);
        QVERIFY(rows[3].judgement_tip.contains(QStringLiteral("98.33% 하한 +63원 (도전 정책 3개로 Bonferroni 보정)")));
        QVERIFY(rows[3].judgement_tip.contains(QStringLiteral("밤 작업")));
        QCOMPARE(rows[2].judgement, QStringLiteral("함께 12/10일 · 차이 하한 -244원 ≤ 0"));
        QCOMPARE(rows[2].judgement_tone, Tone::Warning);
        QVERIFY(rows[2].judgement_tip.contains(QStringLiteral("20일까지 이기지 못하면 그 밤에 내려집니다(남은 8일)")));
        QCOMPARE(rows[1].judgement, QStringLiteral("함께 1/10일 · 날 수 부족"));
        QCOMPARE(rows[1].low, QStringLiteral("—"));
        QCOMPARE(rows[1].low_tone, Tone::Dim);
        QCOMPARE(rows[3].low_tone, Tone::Positive);
        // The waiting one trades nothing yet, and has no record.
        QCOMPARE(rows[4].cells, QStringLiteral("칸 없음"));
        QCOMPARE(rows[4].thresholds, QStringLiteral("—"));
        QCOMPARE(rows[4].judgement, QStringLiteral("자리 기다림"));
        QCOMPARE(rows[4].days, QStringLiteral("0일"));
        QCOMPARE(rows[4].net_tone, Tone::Dim);
        QVERIFY(rows[4].paper_tip.contains(QStringLiteral("아직 모의로 돈 거래일이 없습니다")));
        // The former champion: beaten on 10-29 by policy 2; the dropped one: on 10-28.
        QCOMPARE(rows[5].judgement, QStringLiteral("10-29 정책 2에게 짐"));
        QCOMPARE(rows[5].days, QStringLiteral("16일")); // not running: the versions' record
        QCOMPARE(rows[6].judgement, QStringLiteral("10-28 내려짐"));
        QCOMPARE(rows[6].cells, QStringLiteral("방향 4분 · 합의 (최종) (합의)"));
        QCOMPARE(view::policies_line(p), QStringLiteral("챔피언 정책 2 · 도전 3개 · 대기 1개 · 전 챔피언 1개 · 내려짐 1개"));

        // The history, newest first: the promotion with its figures, the slots given, the drop.
        const auto history = view::history_rows(p);
        QStringList what;
        for (const auto& row : history)
            what << QStringLiteral("%1 %2 %3").arg(row.at, row.version, row.what);
        QCOMPARE(what, (QStringList{"10-29 22:30 정책 2 챔피언 (정책 1 → 2)", "10-28 22:31 정책 6 도전 자리 받음",
                                    "10-28 22:30 정책 3 내려짐", "10-15 22:31 정책 5 도전 자리 받음",
                                    "10-12 22:31 정책 4 도전 자리 받음", "10-09 22:31 정책 3 도전 자리 받음",
                                    "10-08 22:31 정책 2 도전 자리 받음", "10-07 22:31 정책 1 처음 챔피언"}));
        QCOMPARE(history[0].why, QStringLiteral("도전 정책 2이 챔피언 정책 1을 모의로 이겼다(#400)"));
        QVERIFY(history[0].tip.contains(QStringLiteral("함께 돈 거래일 10일(필요 10일)")));
        QVERIFY(history[0].tip.contains(QStringLiteral("95% 하한 +63원")));
        QStringList keys;
        for (const auto& row : history)
            keys << row.key;
        keys.removeDuplicates();
        QCOMPARE(keys.size(), history.size()); // each row's own key (the table keeps the reader's place by it)

        // The trades' choices follow the table's order.
        const auto choices = view::trade_policy_choices(p, s);
        QCOMPARE(choices.size(), 8);
        QCOMPARE(choices[1].text, QStringLiteral("정책 2 · 챔피언"));
        QCOMPARE(choices[2].text, QStringLiteral("정책 6 · 도전"));
        QCOMPARE(choices[7].text, QStringLiteral("정책 3 · 내려짐"));
        QCOMPARE(view::trades_line(std::nullopt, 11), QStringLiteral("챔피언의 매매 11개"));
        QCOMPARE(view::trades_line(qint64(2), 2), QStringLiteral("정책 2의 매매 2개"));
        // Without the state (not read yet): the versions' records, no standing.
        const auto unread = view::policy_rows(p, std::nullopt);
        QCOMPARE(unread[0].days, QStringLiteral("14일"));
        QCOMPARE(unread[0].judgement, QStringLiteral("—"));
        QCOMPARE(unread[3].judgement, QStringLiteral("—"));
    }

    void the_gate_passed_and_not_passed() {
        // Not passed: days short of 20 and the bound at or below 0, both said.
        const TraderState short_of = state(busy_state_json());
        QCOMPARE(view::gate_text(short_of), QStringLiteral("챔피언 정책 2 · 관문 모의 15/20일 · 하한 -0.3%"));
        QCOMPARE(view::gate_tone(short_of), Tone::Plain);
        QCOMPARE(view::gate_line(short_of),
                 QStringLiteral("실전 관문 · 챔피언 정책 2의 모의 기록 15/20일 · 날마다 평균 +800원 · 95% 하한 -3,000원 (운용 상한의 "
                                "-0.3%) · 못 넘음: 5일 더, 하한 ≤ 0"));
        QVERIFY(!view::gate_tip(short_of).contains(QStringLiteral("#401")));
        QVERIFY(view::gate_tip(short_of).contains(QStringLiteral("정책 버전 표")));
        // Passed: green, and the mode is still the server's to change.
        const TraderState passed = state(busy_state_json(true));
        QCOMPARE(view::gate_text(passed), QStringLiteral("챔피언 정책 2 · 관문 모의 21/20일 · 하한 0.4% · 관문 넘음"));
        QCOMPARE(view::gate_tone(passed), Tone::Positive);
        QCOMPARE(view::gate_line(passed),
                 QStringLiteral("실전 관문 · 챔피언 정책 2의 모의 기록 21/20일 · 날마다 평균 +9,300원 · 95% 하한 +3,930원 (운용 상한의 "
                                "0.4%) · 넘음: 실전 모드는 서버에서 켭니다"));
        const TraderPolicies p = policies(busy_policies_json());
        QCOMPARE(view::policy_rows(p, passed)[0].judgement, QStringLiteral("관문 넘음"));
        QCOMPARE(view::policy_rows(p, passed)[0].judgement_tone, Tone::Positive);
        QCOMPARE(view::policy_rows(p, short_of)[0].judgement, QStringLiteral("관문 15/20일 · 못 넘음"));
        QVERIFY(view::policy_rows(p, short_of)[0].judgement_tip.contains(QStringLiteral("5일 더")));
        // Enough days, the bound below 0: only the bound short.
        QJsonObject days_enough = busy_state_json();
        QJsonObject g = days_enough["gate"].toObject();
        g["days"] = 22;
        days_enough["gate"] = g;
        QVERIFY(view::gate_line(state(days_enough)).endsWith(QStringLiteral("못 넘음: 하한 ≤ 0")));
        // One day: no bound yet.
        QJsonObject one_day = lone_state_json();
        g = one_day["gate"].toObject();
        g["days"] = 1;
        g["low"] = QJsonValue();
        one_day["gate"] = g;
        QVERIFY(view::gate_line(state(one_day)).endsWith(QStringLiteral("95% 하한 — · 못 넘음: 19일 더, 하한은 2일부터")));
        // Before the first champion, and from an engine before #400.
        QJsonObject first = state_json();
        first["champion"] = QJsonValue();
        first["gate"] = QJsonObject{{"policy", QJsonValue()}, {"days", 0},      {"required", 20}, {"mean", QJsonValue()},
                                    {"low", QJsonValue()},    {"level", 0.95}, {"passed", false}};
        QVERIFY(view::gate_line(state(first)).contains(QStringLiteral("첫 챔피언 전")));
        QCOMPARE(view::gate_tone(state(first)), Tone::Plain);
        QVERIFY(view::gate_line(state()).isEmpty());
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

    void the_live_mode_waiting_an_order_failure_the_gate_s_checkpoints_and_a_stall_are_said() {
        // codeyoma/krx-quant#408: KRX_TRADER_MODE=live before the gate and the checks pass: it runs as paper.
        QJsonObject waiting = state_json("paper");
        waiting["live"] = QJsonObject{
            {"configured", true},
            {"ordering", false},
            {"unlocked", false},
            {"missing", QJsonArray{"gate", "mock_days"}},
            {"gate", "실전 관문: 챔피언 정책 1의 챔피언으로 돈 모의 기록 12일, 다음 판정은 모의 이력 20거래일째"},
            {"checks",
             QJsonArray{QJsonObject{{"name", "mock_days"}, {"passed", false}, {"value", 3}, {"required", 20},
                                    {"said", "KIS 모의 거래일 3/20일"}},
                        QJsonObject{{"name", "mock_trips"}, {"passed", true}, {"value", 40}, {"required", 30},
                                    {"said", "KIS 모의 왕복 매매 40/30건"}}}},
            {"period", QJsonObject{{"since", "2026-10-08"}, {"until", "2026-10-14"}, {"days", 3}}},
            {"ramp", QJsonValue()}};
        const TraderState w = state(waiting);
        QVERIFY(w.live.has_value() && w.live->configured && !w.live->ordering);
        QCOMPARE(w.live->missing, QStringList({"gate", "mock_days"}));
        const view::Badge badge = view::mode_badge(w);
        QCOMPARE(badge.text, QStringLiteral("실전 대기"));
        QCOMPARE(badge.tone, Tone::Warning);
        QVERIFY(badge.tip.contains(QStringLiteral("아직 실제 주문을 내지 않고 모의로 돕니다")));
        QVERIFY(badge.tip.contains(QStringLiteral("· 실전 관문: 챔피언 정책 1의 챔피언으로 돈 모의 기록 12일")));
        QVERIFY(badge.tip.contains(QStringLiteral("· KIS 모의 거래일 3/20일")));
        QVERIFY(!badge.tip.contains(QStringLiteral("왕복 매매 40/30건")));
        // Ordering on the live account, its first period's cap.
        QJsonObject ordering = state_json("live");
        QJsonObject live = waiting["live"].toObject();
        live["ordering"] = true;
        live["unlocked"] = true;
        live["missing"] = QJsonArray{};
        live["ramp"] = QJsonObject{{"cap", 250000}, {"lifted_at", QJsonValue()}, {"since", "2026-10-15T08:00:00+09:00"},
                                   {"days", 3}, {"required_days", 20}, {"trips", 4}, {"required_trips", 30},
                                   {"checks", QJsonArray{}}, {"breached_at", QJsonValue()}};
        ordering["live"] = live;
        const view::Badge real = view::mode_badge(state(ordering));
        QCOMPARE(real.text, QStringLiteral("실전"));
        QCOMPARE(real.tone, Tone::Live);
        QVERIFY(real.tip.contains(QStringLiteral("운용 상한 250,000원까지만")));
        // Not live: the paper badge as ever, whatever the live state says.
        live["configured"] = false;
        waiting["live"] = live;
        QCOMPARE(view::mode_badge(state(waiting)).text, QStringLiteral("모의"));

        // A KIS order failed today (codeyoma/krx-quant#405).
        const view::Badge failed = view::status_badge(state(state_json("mock", "order_failed")));
        QCOMPARE(failed.text, QStringLiteral("주문 실패"));
        QCOMPARE(failed.tone, Tone::Warning);
        QVERIFY(failed.tip.contains(QStringLiteral("다음 거래일에 저절로 다시 돕니다")));

        // The gate judged at checkpoints (codeyoma/krx-quant#408): the next one and its α.
        QJsonObject gated = state_json();
        gated["champion"] = 1;
        gated["gate"] = QJsonObject{{"policy", 1},
                                    {"days", 12},
                                    {"required", 20},
                                    {"mean", 1200.0},
                                    {"low", -3000.0},
                                    {"level", 0.9696},
                                    {"passed", false},
                                    {"history", 12},
                                    {"checkpoint", QJsonObject{{"k", 1}, {"day", 20}, {"every", 20}, {"alpha", 0.0152},
                                                               {"level", 0.9696}}},
                                    {"passed_at", QJsonValue()},
                                    {"stressed", false},
                                    {"last", QJsonValue()}};
        const TraderState g = state(gated);
        QVERIFY(g.gate->checkpoints);
        QCOMPARE(g.gate->next_day, 20);
        QCOMPARE(view::gate_text(g), QStringLiteral("챔피언 정책 1 · 관문 판정 이력 20일째(지금 12일) · 하한 -0.3%"));
        QVERIFY(view::gate_line(g).endsWith(
            QStringLiteral("못 넘음: 다음 판정 1번째, 이력 20일째(지금 12일, α 1.52%), 8일 더, 하한 ≤ 0")));
        QVERIFY(view::gate_line(g).contains(QStringLiteral("96.96% 하한 -3,000원")));
        // Its days as the champion alone (codeyoma/krx-quant#425's review).
        QVERIFY(view::gate_line(g).startsWith(QStringLiteral("실전 관문 · 챔피언 정책 1의 챔피언으로 돈 모의 기록 12/20일 · ")));
        QVERIFY(view::gate_tip(g).contains(QStringLiteral("도전 정책이었던 날은 세지 않습니다")));
        QVERIFY(view::gate_tip(g).contains(QStringLiteral("챔피언으로 돈 날 12일")));
        QVERIFY(view::gate_tip(g).contains(QStringLiteral("20일마다 한 번 판정합니다")));
        QVERIFY(view::gate_tip(g).contains(QStringLiteral("α 1.52%")));
        QJsonObject gate = gated["gate"].toObject();
        gate["passed"] = true;
        gate["days"] = 20;
        gate["history"] = 20;
        gate["low"] = 321.0;
        gate["passed_at"] = QJsonObject{{"k", 1}, {"day", "2026-10-13"}};
        gated["gate"] = gate;
        const TraderState passed = state(gated);
        QCOMPARE(view::gate_text(passed), QStringLiteral("챔피언 정책 1 · 관문 넘음(1번째 판정)"));
        QVERIFY(view::gate_line(passed).endsWith(QStringLiteral("넘음(1번째 판정, 10-13): 실전 모드는 서버에서 켭니다")));
        QCOMPARE(view::gate_tone(passed), Tone::Positive);

        // A trade open through a stall of the collector's quotes (codeyoma/krx-quant#407).
        QJsonObject stalled = trade_json(9, "time");
        stalled["stalls"] = QJsonArray{QJsonObject{{"began_at", "2026-10-14T10:01:00+09:00"},
                                                   {"last_quote_at", "2026-10-14T10:00:54+09:00"},
                                                   {"ended_at", "2026-10-14T10:02:00+09:00"},
                                                   {"effect", "waited"}}};
        const auto parsed = parse_trader_trades(QJsonDocument(QJsonObject{{"trades", QJsonArray{stalled}}}));
        QVERIFY(parsed.is_ok());
        QCOMPARE(parsed.value()[0].stalls, QStringList({"waited"}));
        const auto rows = view::trade_rows(parsed.value(), std::nullopt);
        QCOMPARE(rows[0].reason, QStringLiteral("시간 청산 · 호가 끊김"));
        QVERIFY(rows[0].tip.contains(QStringLiteral("수집기 호가 끊김: 호가가 돌아온 뒤 첫 호가로 청산")));
        QCOMPARE(trader_stall_label("rest"), QStringLiteral("끊긴 동안 KIS 호가로 매도"));
        QCOMPARE(trader_stall_label(""), QStringLiteral("청산에 영향 없음"));
    }

    void the_mock_practice_is_said_beside_the_mode_on_the_position_and_on_its_trades() {
        // codeyoma/krx-quant#427: no champion cell to trade, the mock book practises one share of a cell.
        QJsonObject o = state_json("mock");
        QJsonObject position = o["position"].toObject();
        position["quantity"] = 1;
        position["practice"] = true;
        o["position"] = position;
        o["practice"] = QJsonObject{{"enabled", true},
                                    {"active", true},
                                    {"cell", QJsonObject{{"kind", "barrier"},
                                                         {"role", "top"},
                                                         {"predictor", "breiman"},
                                                         {"p_star", 0.63},
                                                         {"low", 0.60},
                                                         {"priority", -0.03},
                                                         {"calls", 300},
                                                         {"width", 10.0},
                                                         {"cost", 2.6},
                                                         {"reference_price", 70050.0},
                                                         {"tick", 100.0},
                                                         {"kelly", -4.5},
                                                         {"fraction", 0.0},
                                                         {"threshold", QJsonValue()},
                                                         {"quantity", 1},
                                                         {"practice", true}}}};
        const TraderState s = state(o);
        QVERIFY(s.practice.has_value() && s.practice->enabled && s.practice->active);
        QCOMPARE(s.practice->kind, QStringLiteral("barrier"));
        QCOMPARE(s.practice->quantity, 1);
        QVERIFY(s.position.has_value() && s.position->practice);
        const auto badge = view::practice_badge(s);
        QVERIFY(badge.has_value());
        QCOMPARE(badge->text, QStringLiteral("연습"));
        QCOMPARE(badge->tone, Tone::Warning);
        QVERIFY(badge->tip.contains(QStringLiteral("연습 칸 장벽 4분 · breiman (1위)을 1주씩")));
        QVERIFY(badge->tip.contains(QStringLiteral("하한 60.0%, p* 63.0%")));
        QVERIFY(badge->tip.contains(QStringLiteral("연습 포지션을 들고 있습니다")));
        QVERIFY(badge->tip.contains(QStringLiteral("승격, 실전 관문, 정책의 모의 기록에는 세지 않고")));
        QCOMPARE(view::mode_badge(s).text, QStringLiteral("KIS 모의"));
        QVERIFY(view::mode_badge(s).tip.contains(QStringLiteral("KIS 모의 연습(KRX_TRADER_MOCK_PRACTICE)이 켜져 있습니다")));
        const view::Card card = view::cards(s, kst("2026-10-15T10:06:30+09:00"))[2];
        QVERIFY(card.lines.contains(QStringLiteral("장벽 4분 · breiman · 연습")));

        // A champion cell back: no practice, the practice position held still.
        QJsonObject back = o;
        back["practice"] = QJsonObject{{"enabled", true}, {"active", false}, {"cell", QJsonValue()}};
        const auto holding = view::practice_badge(state(back));
        QVERIFY(holding.has_value());
        QVERIFY(!holding->tip.contains(QStringLiteral("연습 칸")));
        QVERIFY(holding->tip.contains(QStringLiteral("연습 포지션을 들고 있습니다")));
        // Nothing practised, nothing held of it; a trader before #427.
        back["position"] = QJsonValue();
        QVERIFY(!view::practice_badge(state(back)).has_value());
        QVERIFY2(view::mode_badge(state(back)).tip.contains(QStringLiteral("KIS 모의 연습")), "the practice on still");
        const TraderState before = state(state_json("mock"));
        QVERIFY(!before.practice.has_value() && !before.position->practice);
        QVERIFY(!view::practice_badge(before).has_value());
        QVERIFY(!view::mode_badge(before).tip.contains(QStringLiteral("연습")));

        // Its trades: of no policy, said "연습".
        QJsonObject trade = trade_json(11, "take_profit", "mock");
        trade["policy"] = QJsonValue();
        trade["fraction"] = QJsonValue();
        trade["practice"] = true;
        const auto parsed = parse_trader_trades(QJsonDocument(QJsonObject{{"trades", QJsonArray{trade, trade_json(9, "time")}}}));
        QVERIFY(parsed.is_ok());
        QVERIFY(parsed.value()[0].practice && !parsed.value()[1].practice);
        const auto rows = view::trade_rows(parsed.value(), std::nullopt);
        QCOMPARE(rows[0].policy, QStringLiteral("연습"));
        QCOMPARE(rows[0].policy_tone, Tone::Warning);
        QVERIFY(rows[0].tip.contains(QStringLiteral("KIS 모의 연습 매매입니다")));
        QCOMPARE(rows[1].policy, QStringLiteral("정책 1"));
        QVERIFY(!rows[1].tip.contains(QStringLiteral("연습")));
    }

    void the_risk_rules_and_the_review_cycle_are_cards_and_an_older_engine_has_neither() {
        // codeyoma/krx-quant#475: three net losses in a row, the cooldown to 10:32:30; #469: day 4 of cycle 1.
        QJsonObject o = state_json();
        o["risk"] = QJsonObject{{"day", "2026-10-15"},         {"budget_percent", 0.5}, {"budget", 5000.0},
                                {"buys", 3},                   {"daily_buys", 20},      {"losses", 3},
                                {"loss_streak", 3},            {"cooldown_minutes", 30.0}, {"cooldowns", 1},
                                {"cooldown_until", "2026-10-15T10:32:30+09:00"}, {"held", "cooldown"}};
        o["cycle"] = QJsonObject{{"evaluation", 2}, {"since", "2026-10-12"}, {"number", 1}, {"start", "2026-10-12"},
                                 {"review", "2026-11-06"}, {"day", 4}, {"days", 20}};
        o["cohort"] = cohort_json(false);
        const TraderState s = state(o);
        QVERIFY(s.risk && s.cycle && s.cohort);
        QCOMPARE(s.risk->cooldown_until, kst("2026-10-15T10:32:30+09:00"));
        QCOMPARE(s.cohort->members, QList<qint64>{2});

        const QList<view::Card> cooling = view::cards(s, kst("2026-10-15T10:06:30+09:00"));
        QCOMPARE(cooling.size(), view::kCards);
        QCOMPARE(cooling[7].title, QStringLiteral("위험 규칙"));
        QCOMPARE(cooling[7].value, QStringLiteral("쉬는 중"));
        QCOMPARE(cooling[7].tone, Tone::Warning);
        QCOMPARE(cooling[7].lines,
                 (QStringList{QStringLiteral("10:32:30까지 새로 사지 않음"), QStringLiteral("오늘 매수 주문 3/20"),
                              QStringLiteral("연달은 순손실 3/3 · 쉬면 30분"), QStringLiteral("오늘 쉼 1번"),
                              QStringLiteral("손실 예산 5,000원 (0.5%)")}));
        QVERIFY(cooling[7].tip.contains(QStringLiteral("마지막 청산부터 30분")));
        const QList<view::Card> later = view::cards(s, kst("2026-10-15T10:40:00+09:00"));
        QCOMPARE(later[7].value, QStringLiteral("매수 3/20"));
        QCOMPARE(later[7].tone, Tone::Plain);

        QCOMPARE(cooling[8].title, QStringLiteral("검토 주기"));
        QCOMPARE(cooling[8].value, QStringLiteral("4/20일째"));
        QCOMPARE(cooling[8].lines, (QStringList{QStringLiteral("주기 1 · 10-12(월) – 11-06(금)"),
                                               QStringLiteral("검토 날 11-06(금) 장 마감 뒤"), QStringLiteral("묶음: 정책 2")}));

        // An engine before #469 and #475: the two cards say nothing, nothing else changes.
        const QList<view::Card> older = view::cards(state(), kst("2026-10-15T10:06:30+09:00"));
        QCOMPARE(older.size(), view::kCards);
        QCOMPARE(older[7].value, QStringLiteral("—"));
        QCOMPARE(older[7].tone, Tone::Dim);
        QCOMPARE(older[8].value, QStringLiteral("—"));
        QVERIFY(!state().risk && !state().cycle && !state().cohort);
        QCOMPARE(view::cohort_line(std::nullopt, state()).text, QString());
    }

    void the_policies_say_what_each_cell_entered_on_its_calibration_and_the_cohort_s_verdict() {
        // codeyoma/krx-quant#472, #473, #475.
        const TraderPolicies p = policies(new_policies_json());
        QCOMPARE(p.cohorts.size(), 1);
        const TraderPolicy& fitted = p.policies[1];
        QVERIFY(!fitted.cells[0].kelly_fraction);
        QCOMPARE(fitted.cells[0].bound_low, std::optional<double>(0.04));
        QCOMPARE(fitted.cells[0].calibration.size(), 5);
        QCOMPARE(fitted.cells[0].calibration[0].rises, 25);
        QCOMPARE(fitted.left_out[0].best_low, std::optional<double>(-0.9));

        const QList<view::PolicyRow> rows = view::policy_rows(p, std::nullopt);
        QCOMPARE(rows[0].version, 2);
        QCOMPARE(rows[0].basis, QStringLiteral("하한 +0.04틱"));
        QVERIFY(rows[0].cells_tip.contains(QStringLiteral(
            "장벽 4분 · breiman (1위): θ 70.0%, 1주씩 따로 잰 신호당 +0.31틱(하한 +0.04틱, 99.89% 수준, 녹화한 날 20일)")));
        QVERIFY(rows[0].cells_tip.contains(QStringLiteral("판정 fail(기록일 뿐)")));
        QVERIFY(!rows[0].cells_tip.contains(QStringLiteral("λ ")));
        QVERIFY(rows[0].cells_tip.contains(QStringLiteral(
            "  보정: [0.4, 0.5) 25건 · 상승 40% · 보합 24% · 하락 36% · 목표 먼저 36% · 순이익 35%(체결 23)")));
        QVERIFY(rows[0].cells_tip.contains(QStringLiteral("  보정: 0.4 아래 7건(어느 θ도 사지 않는다)")));
        QVERIFY(rows[0].cells_tip.contains(QStringLiteral(
            "(합의): 신호당 순손익의 하한이 0을 넘는 θ가 없음(가장 높은 θ 60.0%: 하한 -0.90틱) · 비용 지도 판정 pass")));
        QVERIFY(rows[0].cells_tip.contains(QStringLiteral("녹화한 날이 모자람(12일)")));
        const auto waiting = std::find_if(rows.begin(), rows.end(), [](const auto& r) { return r.version == 4; });
        QCOMPARE(waiting->judgement, QStringLiteral("다음 묶음 기다림"));
        const auto former = std::find_if(rows.begin(), rows.end(), [](const auto& r) { return r.version == 1; });
        QCOMPARE(former->basis, QStringLiteral("λ 0.5")); // a policy fitted before #471 keeps its λ

        const view::CohortLine line = view::cohort_line(p, std::nullopt);
        QCOMPARE(line.text, QStringLiteral("도전 정책 묶음 · 주기 1(10-12–11-06) · 정책 2 · 챔피언 정책 1과 견줌 · 판정 11-06: 정책 2 "
                                           "챔피언이 됨"));
        QCOMPARE(line.tone, Tone::Positive);
        QVERIFY(line.tip.contains(QStringLiteral("정책 2: 챔피언이 됨 · 함께 18일 · 평균 +1,100원 · 하한 +63원 (95%)")));
        QVERIFY(line.tip.contains(QStringLiteral("묶음에 들지 못해 내린 정책 3(older)")));
        QCOMPARE(view::cohort_line(p, state()).text, line.text); // the state of an older trader: the versions'

        const QList<view::HistoryRow> history = view::history_rows(p);
        QStringList whats;
        for (const auto& r : history)
            whats << r.what + QStringLiteral(" · ") + r.why;
        QCOMPARE(whats.mid(0, 2), (QStringList{QStringLiteral("챔피언 (정책 1 → 2) · 주기 1 묶음의 검토 날 판정(#473)"),
                                              QStringLiteral("묶음 판정 (주기 1) · 정책 2이 챔피언 정책 1을 이겼다")}));
        QVERIFY(whats.contains(QStringLiteral("묶음에 듦 (주기 1) · 10-12(월)부터 검토 날 11-06(금)까지 챔피언 옆에서 모의로 돈다")));
        QVERIFY(whats.contains(QStringLiteral("내려짐 · 주기 1의 묶음에 들지 못했다")));

        // A challenger's standing on a cohort engine: its review day, judged once.
        QJsonObject o = busy_state_json();
        QJsonArray running = o["policies"].toArray();
        QJsonObject challenger = running[1].toObject();
        QJsonObject promotion = challenger["promotion"].toObject();
        promotion["review"] = "2026-11-06";
        challenger["promotion"] = promotion;
        running[1] = challenger;
        o["policies"] = running;
        const QList<view::PolicyRow> busy = view::policy_rows(policies(busy_policies_json()), state(o));
        const auto four = std::find_if(busy.begin(), busy.end(), [](const auto& r) { return r.version == 4; });
        QCOMPARE(four->judgement, QStringLiteral("이김 · 함께 12/10일 · 차이 하한 +63원 · 검토 11-06"));
        QVERIFY(four->judgement_tip.contains(QStringLiteral("검토 날(11-06(금)) 장이 끝난 뒤 한 번만 판정합니다")));
        QVERIFY(!four->judgement_tip.contains(QStringLiteral("20일까지 이기지 못하면")));
    }

    void a_trade_s_execution_cost_is_in_its_tooltip_and_an_older_trade_has_none() {
        // codeyoma/krx-quant#475
        QJsonObject t = trade_json(1, "take_profit");
        t["execution"] = QJsonObject{
            {"clock", "trader"},
            {"entry", QJsonObject{{"ticks", 0.5}, {"placed", 0.2}, {"filled", 0.84}}},
            {"exit", QJsonObject{{"reason", "take_profit"}, {"reference", "target"}, {"ticks", -0.5}, {"placed", 0.3}, {"filled", QJsonValue()}}}};
        QJsonObject stopped = trade_json(2, "stop_loss");
        stopped["execution"] = QJsonObject{
            {"clock", "quote"},
            {"entry", QJsonObject{{"ticks", QJsonValue()}, {"placed", 0.2}, {"filled", 0.2}}},
            {"exit", QJsonObject{{"reason", "stop_loss"}, {"reference", "mid"}, {"ticks", 0.5}, {"placed", 0.0}, {"filled", 0.0}}}};
        auto parsed = parse_trader_trades(QJsonDocument(QJsonObject{{"trades", QJsonArray{t, stopped, trade_json(3, "time")}}}));
        QVERIFY(parsed.is_ok());
        const QList<TraderTrade>& trades = parsed.value();
        QCOMPARE(trades[0].clock, QStringLiteral("trader"));
        QCOMPARE(trades[0].exit_reference, QStringLiteral("target"));
        QVERIFY(!trades[0].exit_filled);
        const auto rows = view::trade_rows(trades, std::nullopt);
        QVERIFY(rows[0].tip.contains(QStringLiteral(
            "실행: 매수 +0.50틱(결정 때 중간가 대비) · 결정 뒤 주문 0.2초, 체결 0.8초 · 익절 -0.50틱(목표가 대비) · 매수 체결 뒤 "
            "주문 0.3초")));
        QVERIFY(rows[0].tip.contains(QStringLiteral("trader가 KIS 주문 체결 조회에서 체결을 읽은 때")));
        QVERIFY(rows[1].tip.contains(QStringLiteral(
            "실행: 매수 ?(결정 때 중간가 대비) · 결정 뒤 주문 0.2초, 체결 0.2초 · 매도 +0.50틱(결정 때 중간가 대비) · 결정 뒤 "
            "주문 0.0초, 체결 0.0초")));
        QVERIFY(rows[1].tip.contains(QStringLiteral("체결한 호가를 받은 때")));
        QVERIFY(!rows[2].tip.contains(QStringLiteral("실행")));
    }

    void the_practice_cell_frozen_for_the_cycle_and_why_it_does_not_practise_are_said() {
        // codeyoma/krx-quant#474
        QJsonObject frozen{{"evaluation", 2}, {"cycle", 1}, {"chosen_at", "2026-10-09T22:41:00+09:00"},
                           {"kind", "barrier"}, {"role", "top"}, {"predictor", "breiman"}, {"threshold", 0.6},
                           {"per_signal", QJsonObject{{"value", -0.2}, {"low", -0.6}, {"high", 0.2}, {"days", 20}}},
                           {"signals", 120}, {"bought", 100}, {"wins", 48}, {"measured", 45}, {"eligible", 30},
                           {"gone_at", QJsonValue()}};
        QJsonObject cell{{"kind", "barrier"}, {"role", "top"}, {"predictor", "breiman"}, {"p_star", 0.63}, {"low", 0.6},
                         {"threshold", 0.6}, {"quantity", 1}, {"practice", true}};
        QJsonObject o = state_json("mock", "running", true, false);
        o["practice"] = QJsonObject{{"enabled", true}, {"active", true}, {"cell", cell}, {"frozen", frozen}, {"why", QJsonValue()}};
        const TraderState practising = state(o);
        QCOMPARE(practising.practice->frozen_cycle, 1);
        QCOMPARE(practising.practice->threshold, std::optional<double>(0.6));
        const auto badge = view::practice_badge(practising);
        QVERIFY(badge);
        QVERIFY(badge->tip.contains(QStringLiteral("1주씩, 문턱 θ 60.0%로")));
        QVERIFY(badge->tip.contains(QStringLiteral("검토 주기 1에 고정한 칸입니다(10-09 22:41에 밤 작업이 고름")));

        o["practice"] = QJsonObject{{"enabled", true}, {"active", false}, {"cell", QJsonValue()}, {"frozen", QJsonValue()},
                                    {"why", "not_frozen"}};
        const TraderState waiting = state(o);
        QVERIFY(!view::practice_badge(waiting));
        QVERIFY(view::mode_badge(waiting).tip.contains(
            QStringLiteral("지금 연습하지 않는 까닭: 이 검토 주기의 연습 칸을 아직 고르지 않았다(밤 작업이 고른다).")));
    }

    void the_practice_and_the_account_p_and_l_are_beside_the_limits_and_an_older_engine_has_neither() {
        // codeyoma/krx-quant#533: the mock practice made 20 trades for -9,140 won today (-4,600 before); the loss
        // limits count the champion's alone, the account both.
        const QDateTime now = kst("2026-10-15T10:06:30+09:00");
        QJsonObject o = split_json(state_json("mock"), -9140.0, 20, -4600.0);
        o["practice"] = practising_json();
        const TraderState s = state(o);
        QVERIFY(s.today->practice && s.today->account && s.cumulative->practice && s.cumulative->account);
        QCOMPARE(s.today->net, -5051.2);
        QCOMPARE(*s.today->practice, (TraderPnlPart{-9140.0, 0.0, -9140.0, 20}));
        QCOMPARE(s.today->account->realized, -9140.0);
        QCOMPARE(s.today->account->unrealized, -5051.2);
        QCOMPARE(s.today->account->net, -14191.2);
        QCOMPARE(s.today->account->trades, 20);
        QCOMPARE(*s.cumulative->practice, (TraderPnlPart{-13740.0, 0.0, -13740.0, 0}));
        QCOMPARE(s.cumulative->account->net, -23101.2);

        // The cards keep what the limits count, the practice's and the account's in a line under it.
        const QList<view::Card> cards = view::cards(s, now);
        QCOMPARE(cards.size(), view::kCards);
        QCOMPARE(cards[3].value, QStringLiteral("-5,051원"));
        QCOMPARE(cards[3].tone, Tone::Negative);
        QCOMPARE(cards[3].lines, (QStringList{QStringLiteral("실현 0원 · 매매 0"), QStringLiteral("평가 -5,051원"),
                                             QStringLiteral("가장 낮을 때 -5,051원"),
                                             QStringLiteral("연습 -9,140원 · 계좌 -14,191원")}));
        QVERIFY(cards[3].tip.contains(QStringLiteral("위 값은 손실 한도가 세는 손익(챔피언의 매매)입니다")));
        QVERIFY(cards[3].tip.contains(QStringLiteral("연습: 실현 -9,140원 · 매매 20 · 평가 0원 · 합 -9,140원")));
        QVERIFY(cards[3].tip.contains(
            QStringLiteral("계좌(KIS, 한도가 세는 것과 연습의 합): 실현 -9,140원 · 매매 20 · 평가 -5,051원 · 합 -14,191원")));
        QCOMPARE(cards[4].value, QStringLiteral("-9,361원"));
        QCOMPARE(cards[4].lines.last(), QStringLiteral("연습 -13,740원 · 계좌 -23,101원"));
        QVERIFY(cards[4].tip.contains(QStringLiteral("연습: 실현 -13,740원 · 평가 0원 · 합 -13,740원")));
        // The limits' use is the engine's, of what they count; the cards say the practice is not counted.
        QCOMPARE(cards[5].value, QStringLiteral("5.1%"));
        QCOMPARE(cards[6].value, QStringLiteral("4.7%"));
        for (const int i : {5, 6}) {
            QCOMPARE(cards[i].lines.last(), QStringLiteral("연습은 한도에 세지 않음"));
            QVERIFY(cards[i].tip.contains(QStringLiteral("KIS 모의 연습 매매의 손익은 한도에 세지 않습니다")));
        }
        QVERIFY(cards[6].tip.contains(QStringLiteral("누적 순손익 카드의 값이")));
        // The practice badge and a practice trade say the limits leave it out.
        const auto badge = view::practice_badge(s);
        QVERIFY(badge.has_value());
        QVERIFY(badge->tip.contains(QStringLiteral("정책의 모의 기록과 손실 한도(codeyoma/krx-quant#533)에는 세지 않고")));
        QVERIFY(badge->tip.contains(QStringLiteral("위험 규칙(그날 매수 주문 수, 연달은 순손실)")));
        QJsonObject trade = trade_json(11, "take_profit", "mock");
        trade["policy"] = QJsonValue();
        trade["practice"] = true;
        const auto parsed = parse_trader_trades(QJsonDocument(QJsonObject{{"trades", QJsonArray{trade}}}));
        QVERIFY(parsed.is_ok());
        QVERIFY(view::trade_rows(parsed.value(), s)[0].tip.contains(QStringLiteral("손실 한도에도 세지 않습니다")));
        // A practice trade exited reads the trades again, though the limits' count of trades stays.
        QVERIFY(view::history_key(state(split_json(state_json("mock"), -9500.0, 21, -4600.0))) != view::history_key(s));

        // The practice on, nothing practised yet: no line, the limits still say it is not counted.
        QJsonObject quiet = split_json(state_json("mock"), 0.0, 0, 0.0);
        quiet["practice"] = practising_json();
        const QList<view::Card> waiting = view::cards(state(quiet), now);
        QCOMPARE(waiting[3].lines.size(), 3);
        QCOMPARE(waiting[4].lines.size(), 4);
        QVERIFY(!waiting[3].tip.contains(QStringLiteral("연습")));
        QCOMPARE(waiting[5].lines.last(), QStringLiteral("연습은 한도에 세지 않음"));

        // A practice position open, nothing of it exited: its open P&L is the practice's and the account's, not
        // the limits'.
        QJsonObject holding = state_json("mock");
        holding["today"] = QJsonObject{
            {"day", "2026-10-15"},
            {"realized", 0.0},
            {"unrealized", 0.0},
            {"net", 0.0},
            {"trades", 0},
            {"worst", 0.0},
            {"practice", QJsonObject{{"realized", 0.0}, {"unrealized", -500.0}, {"net", -500.0}, {"trades", 0}}},
            {"account", QJsonObject{{"realized", 0.0}, {"unrealized", -500.0}, {"net", -500.0}, {"trades", 0}}}};
        const view::Card opened = view::cards(state(holding), now)[3];
        QCOMPARE(opened.value, QStringLiteral("0원"));
        QCOMPARE(opened.lines.last(), QStringLiteral("연습 -500원 · 계좌 -500원"));

        // Paper on a trader of #533 (its practice 0), and a trader before #533 practising: as before, nothing
        // of the practice's P&L.
        const QList<view::Card> paper = view::cards(state(split_json(state_json(), 0.0, 0, 0.0)), now);
        QJsonObject older = state_json("mock");
        older["practice"] = practising_json();
        const TraderState before = state(older);
        QVERIFY(!before.today->practice && !before.today->account && !before.cumulative->practice);
        const QList<view::Card> old = view::cards(before, now);
        const QList<view::Card> plain = view::cards(state(), now);
        for (const int i : {3, 4, 5, 6}) {
            QCOMPARE(paper[i].lines, plain[i].lines);
            QCOMPARE(paper[i].tip, plain[i].tip);
            QCOMPARE(old[i].lines, plain[i].lines);
            QCOMPARE(old[i].tip, plain[i].tip);
        }
        QVERIFY(view::practice_badge(before)->tip.contains(QStringLiteral("손실 한도와 실전의 실행 점검(KIS 대조")));
        QVERIFY(view::trade_rows(parsed.value(), before)[0].tip.contains(QStringLiteral("손실 한도와 실전의 실행 점검에는 셉니다")));
        QCOMPARE(view::history_key(before), view::history_key(state(state_json("mock"))));
    }

    void the_days_say_the_practice_s_and_the_account_s_and_a_day_before_them_neither() {
        // codeyoma/krx-quant#533: a day's net is what the limits count; the practice's and the account's beside it,
        // null on a day written before #533, absent from an engine before it.
        const QJsonObject older = day_json("2026-10-13", "mock", 1922.25, 1);
        QJsonObject before = day_json("2026-10-14", "mock", -161.735, 1);
        before["practice"] = QJsonValue();
        before["account"] = QJsonValue();
        QJsonObject quiet = day_json("2026-10-15", "mock", 0.0, 0);
        quiet["practice"] = QJsonObject{{"net", 0.0}, {"trades", 0}, {"cumulative", 0.0}};
        quiet["account"] = QJsonObject{{"net", 0.0}, {"trades", 0}, {"cumulative", 0.0}};
        QJsonObject split = day_json("2026-10-16", "mock", -5000.0, 2);
        split["practice"] = QJsonObject{{"net", -9140.0}, {"trades", 20}, {"cumulative", -13740.0}};
        split["account"] = QJsonObject{{"net", -14140.0}, {"trades", 22}, {"cumulative", -18740.0}};
        const auto parsed = parse_trader_days(QJsonDocument(QJsonObject{
            {"from", "2025-10-17"}, {"to", "2026-10-16"}, {"days", QJsonArray{older, before, quiet, split}}}));
        QVERIFY(parsed.is_ok());
        const QList<TraderDay>& d = parsed.value();
        QCOMPARE(d.size(), 4);
        QVERIFY(!d[0].practice && !d[0].account);
        QVERIFY(!d[1].practice && !d[1].account);
        QCOMPARE(*d[2].practice, (TraderDayPnl{0.0, 0, 0.0}));
        QCOMPARE(*d[3].practice, (TraderDayPnl{-9140.0, 20, -13740.0}));
        QCOMPARE(*d[3].account, (TraderDayPnl{-14140.0, 22, -18740.0}));

        const auto points = view::day_points(d, "mock");
        QCOMPARE(points.size(), 4);
        QCOMPARE(points[3].net, -5000.0); // the bars and the sum: the limits' count
        QCOMPARE(points[3].sum, 1922.25 - 161.735 - 5000.0);
        QVERIFY(points[3].tip.contains(QStringLiteral("순손익 -5,000원 · 매매 2 (한도가 세는 것)\n"
                                                      "연습 -9,140원 · 매매 20 · 누적 -13,740원 (한도에 세지 않음)\n"
                                                      "계좌(KIS) -14,140원 · 매매 22 · 누적 -18,740원\n")));
        // An engine before #533, a day before it and a day without practice: the tooltip as before.
        QCOMPARE(points[0].tip, QStringLiteral("10-13(화) KIS 모의\n순손익 +1,922원 · 매매 1\n합 +1,922원 (보이는 첫날부터)\n"
                                               "하루 가장 낮을 때 0원 (한도의 0.0%)\n누적 +1,922원 (가장 낮을 때 0원, 한도의 1.0%)\n"
                                               "그날 마지막 상태: 도는 중"));
        for (const int i : {1, 2}) {
            QVERIFY(!points[i].tip.contains(QStringLiteral("연습")));
            QVERIFY(!points[i].tip.contains(QStringLiteral("계좌")));
            QVERIFY(!points[i].tip.contains(QStringLiteral("한도가 세는 것")));
        }
    }

    void the_cards_show_the_position_with_its_time_left() {
        const TraderState s = state();
        const QDateTime now = kst("2026-10-15T10:06:30+09:00");
        const QList<view::Card> cards = view::cards(s, now);
        QCOMPARE(cards.size(), view::kCards);
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
        // Whose trade: the champion's under its version, a challenger's paper trade, one before the first champion.
        QCOMPARE(rows[0].policy, QStringLiteral("정책 1"));
        QCOMPARE(rows[0].policy_tone, Tone::Plain);
        QCOMPARE(rows[6].policy, QStringLiteral("—"));
        QCOMPARE(rows[6].policy_tone, Tone::Dim);
        const auto challenger =
            view::trade_rows(parse_trader_trades(QJsonDocument(challenger_trades_json())).value(), state());
        QCOMPARE(challenger[0].policy, QStringLiteral("정책 2 · 도전"));
        QCOMPARE(challenger[0].policy_tone, Tone::Simulated);
        QVERIFY(challenger[0].tip.contains(QStringLiteral("도전 정책의 모의 매매")));
        QVERIFY(!rows[0].tip.contains(QStringLiteral("도전 정책의 모의 매매")));
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

    void the_chart_is_read_again_as_each_minute_turns() {
        // codeyoma/krx-quant#537: kChartSettleMs past the minute's turn, when the bar just over is the engine's.
        QCOMPARE(view::chart_ms(kst("2026-10-15T10:06:00+09:00")), view::kChartSettleMs);
        QCOMPARE(view::chart_ms(kst("2026-10-15T10:06:02.500+09:00")), 500);
        QCOMPARE(view::chart_ms(kst("2026-10-15T10:06:03+09:00")), 60'000);
        QCOMPARE(view::chart_ms(kst("2026-10-15T10:06:30+09:00")), 33'000);
        QCOMPARE(view::chart_ms(kst("2026-10-15T10:06:59.999+09:00")), 3'001);
        // Whatever the zone the moment is written in.
        QCOMPARE(view::chart_ms(QDateTime(QDate(2026, 10, 15), QTime(1, 6, 30), QTimeZone::UTC)), 33'000);
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
        auto* timer = live.findChild<QTimer*>(services::KrxTraderLive::kStateTimerName, Qt::FindDirectChildrenOnly);
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

    void the_policy_versions_are_read_with_the_days_and_trades() {
        StandIn engine;
        engine.state = busy_state_json();
        engine.policies = busy_policies_json();
        services::KrxQuantService::set_base_url(engine.url());
        QDateTime clock = kst("2026-10-15T10:06:30+09:00");
        services::KrxTraderLive live(nullptr, [&clock] { return clock; });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.policies().has_value() && live.state().has_value() && !live.reading(), 10000);
        QCOMPARE(live.policies()->policies.size(), 7);
        QCOMPARE(engine.count("GET", "/v1/trader/policies"), 1);
        QVERIFY(live.failure().isEmpty());
        // The next poll with nothing changed: the state alone.
        clock = clock.addSecs(2);
        fire_schedule(live);
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("GET", "/v1/trader"), 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        QCOMPARE(engine.count("GET", "/v1/trader/policies"), 1);
        // A challenger's record moved (a trade of its exited): the versions again, with the days and trades.
        QJsonObject moved = busy_state_json();
        QJsonArray running = moved["policies"].toArray();
        QJsonObject six = running[3].toObject();
        six["paper"] = paper_json("2026-10-29", "2026-10-30", 2, 2, 900.0, 450.0, -300.0);
        running[3] = six;
        moved["policies"] = running;
        engine.state = moved;
        clock = clock.addSecs(2);
        fire_schedule(live);
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("GET", "/v1/trader/policies"), 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        // The champion replaced: the same.
        QJsonObject promoted = moved;
        promoted["champion"] = 4;
        engine.state = promoted;
        clock = clock.addSecs(2);
        fire_schedule(live);
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("GET", "/v1/trader/policies"), 3, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        // An engine without the versions (before codeyoma/krx-quant#399): said so, the rest kept and shown.
        engine.without_policies = true;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(live.failure().startsWith(QStringLiteral("정책: ")) && !live.reading(), 10000);
        QVERIFY(live.state().has_value() && live.days().has_value() && live.trades().has_value());
        QVERIFY(live.policies().has_value()); // the last versions read stay
        live.stop();
    }

    void the_trades_of_a_policy_chosen_are_read_at_once_and_a_late_answer_for_another_is_not_kept() {
        StandIn engine;
        engine.state = busy_state_json();
        engine.policies = busy_policies_json();
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-15T10:06:30+09:00"); });
        QCOMPARE(live.trades_policy(), std::optional<qint64>()); // the champion's at first
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.trades().has_value() && !live.reading(), 10000);
        QCOMPARE(live.trades()->size(), 7);
        QVERIFY(std::all_of(engine.asked.cbegin(), engine.asked.cend(),
                            [](const StandIn::Request& r) { return !r.path.contains("policy="); }));

        // Policy 2 chosen: its trades at once, the champion's dropped meanwhile.
        live.set_trades_policy(2);
        QCOMPARE(*live.trades_policy(), 2);
        QVERIFY(!live.trades().has_value());
        QTRY_VERIFY_WITH_TIMEOUT(live.trades().has_value() && !live.reading(), 10000);
        QVERIFY(std::any_of(engine.asked.cbegin(), engine.asked.cend(), [](const StandIn::Request& r) {
            return r.path == "/v1/trader/trades?limit=100&policy=2";
        }));
        QCOMPARE(live.trades()->size(), 2);
        QVERIFY(!live.trades()->first().champion);
        // A read of all three keeps the choice.
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        QCOMPARE(live.trades()->size(), 2);

        // Chosen again while the trades are read: the answer under way is the old choice's, not kept;
        // the new choice's is asked once it is in.
        engine.hold_trades = true;
        const qsizetype asked = engine.count("GET", "/v1/trader/trades");
        live.read_now();
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("GET", "/v1/trader/trades"), asked + 1, 10000); // policy 2's, held
        live.set_trades_policy(std::nullopt);
        engine.release(); // policy 2's answer comes in now, after the champion's were chosen
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("GET", "/v1/trader/trades"), asked + 2, 10000); // the champion's, held
        QVERIFY(!live.trades().has_value()); // policy 2's answer was not kept for the champion's
        const auto last_trades = std::find_if(engine.asked.crbegin(), engine.asked.crend(), [](const StandIn::Request& r) {
            return r.path.startsWith("/v1/trader/trades");
        });
        QCOMPARE(last_trades->path, QStringLiteral("/v1/trader/trades?limit=100"));
        engine.hold_trades = false;
        engine.release();
        QTRY_VERIFY_WITH_TIMEOUT(live.trades().has_value() && !live.reading(), 10000);
        QCOMPARE(live.trades()->size(), 7);
        QVERIFY(live.trades()->first().champion);
        QVERIFY(live.failure().isEmpty());

        // Another engine: back to the champion's.
        live.set_trades_policy(2);
        StandIn other;
        services::KrxQuantService::set_base_url(other.url());
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(live.state().has_value() && other.count("GET", "/v1/trader") > 0 && !live.reading(), 10000);
        QVERIFY(!live.trades_policy().has_value());
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

    void a_pause_confirmed_after_the_engine_changed_while_asking_is_not_sent() {
        // Codex review on codeyoma/FinceptTerminal#108: the confirm window runs its own event loop, so a
        // poll may read a new address's engine while it is open; its yes was given for the engine shown.
        StandIn first;
        services::KrxQuantService::set_base_url(first.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-15T10:06:30+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.state().has_value() && !live.reading(), 10000);
        StandIn second;
        bool asked = false;
        QVERIFY(!live.pause(true, [&](const view::Question&) {
            asked = true;
            services::KrxQuantService::set_base_url(second.url());
            live.read_now(); // a poll while the window is open: the new engine is read
            return QTest::qWaitFor(
                [&] { return second.count("GET", "/v1/trader") > 0 && live.state().has_value() && !live.reading(); },
                10000);
        }));
        QVERIFY(asked);
        QVERIFY(!live.sending());
        QVERIFY(live.pause_failed());
        QVERIFY(live.pause_note().contains(QStringLiteral("보내지 않았습니다")));
        QCOMPARE(first.count("POST", "/v1/trader/pause"), 0);
        QCOMPARE(second.count("POST", "/v1/trader/pause"), 0);
        // Asked again now, of the engine shown, it is sent there.
        QVERIFY(live.pause(true, [](const view::Question&) { return true; }));
        QTRY_COMPARE_WITH_TIMEOUT(second.count("POST", "/v1/trader/pause"), 1, 10000);
        QCOMPARE(first.count("POST", "/v1/trader/pause"), 0);
        live.stop();
    }

    void the_chart_reads_the_day_s_bars_and_the_trader_s_marks_back_to_the_first_bar() {
        // codeyoma/krx-quant#537, made-up values: 1,100 marks a second apart from 09:00. The latest page holds
        // 1,000, back to 09:01:40, after the first bar (09:00); the page before it holds the 100 left, fewer
        // than a page, so no more is read.
        StandIn engine;
        engine.bars = bars_json("2026-10-16T09:00:00+09:00", 20);
        const QDateTime nine = kst("2026-10-16T09:00:00+09:00");
        engine.marks = marks_from(nine, 1100);
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-16T09:20:04+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.marks().size() == 1100 && !live.chart_reading(), 10000);
        QCOMPARE(live.chart_bars().size(), qsizetype(20));
        QVERIFY(engine.asked_for(kChartMarks + "?limit=1000"));
        // A millisecond past the earliest read: that millisecond's marks again (codeyoma/FinceptTerminal#107).
        QVERIFY2(engine.asked_for(kChartMarks + "?limit=1000&before=2026-10-16T09%3A01%3A40.001"),
                 qPrintable(engine.asked.constLast().path));
        for (const auto& r : engine.asked)
            if (r.path.startsWith(kChartMarks))
                QVERIFY2(r.path == kChartMarks + "?limit=1000" ||
                             r.path.startsWith(kChartMarks + "?limit=1000&before=2026-10-16T09%3A01%3A40"),
                         qPrintable(r.path));
        QCOMPARE(live.marks().first().at, nine.addSecs(1099)); // newest first
        QCOMPARE(live.marks().last().at, nine);
        QCOMPARE(live.marks().first().net, std::optional<double>(-120.5));
        // The day's bars alone: no prediction, and nothing the KRX 4-minute screen reads.
        QVERIFY(engine.asked_for(kChartBars));
        QVERIFY(std::none_of(engine.asked.cbegin(), engine.asked.cend(), [](const StandIn::Request& r) {
            return r.path.contains("/predictions") || r.path.startsWith("/v1/status") ||
                   r.path.startsWith("/v1/leaderboard") || r.path == "/v1/symbols";
        }));
        QVERIFY(live.chart_failure().isEmpty());
        QVERIFY(!live.reached_start());
        // Read again as the minute turns: 09:21:03, 59 s on.
        QVERIFY(chart_timer(live) && chart_timer(live)->isActive());
        QCOMPARE(chart_timer(live)->interval(), 59'000);
        live.stop();
        QVERIFY(!chart_timer(live)->isActive());
    }

    void the_marks_of_one_millisecond_at_a_page_s_end_are_all_read() {
        // codeyoma/FinceptTerminal#107 review: the engine's times are to the microsecond, this screen's to the
        // millisecond. The latest page ends on two marks of 09:01:40.000400 (the engine keeps a moment whole);
        // one of 09:01:40.000200 is just before them, in the same millisecond. Asked before 09:01:40.000, it
        // would be missed; asked a millisecond past, it comes with the page before.
        StandIn engine;
        engine.bars = bars_json("2026-10-16T09:00:00+09:00", 20);
        const QDateTime nine = kst("2026-10-16T09:00:00+09:00");
        QList<QJsonObject> marks = marks_from(nine.addSecs(101), 999, 100); // 09:01:41 on
        for (const auto& [trade, at] :
             {std::pair{98, "2026-10-16T09:01:40.000400+09:00"}, std::pair{97, "2026-10-16T09:01:40.000400+09:00"},
              std::pair{96, "2026-10-16T09:01:40.000200+09:00"}})
            marks << QJsonObject{{"trade_id", trade}, {"side", "buy"}, {"at", at},
                                 {"price", 70000},    {"quantity", 4}, {"mode", "live"}};
        marks += marks_from(nine, 100, 1); // up to 09:01:39
        engine.marks = marks;
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-16T09:20:04+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.marks().size() == 1102 && !live.chart_reading(), 10000);
        const auto traded = [&live](qint64 trade) {
            return std::any_of(live.marks().begin(), live.marks().end(),
                               [trade](const TraderMark& m) { return m.trade_id == trade; });
        };
        QVERIFY(traded(98) && traded(97) && traded(96));
        live.stop();
    }

    void a_latest_page_that_does_not_reach_those_held_reads_them_again_back_to_the_first_bar() {
        // codeyoma/FinceptTerminal#107 review: more came in since the last read than a page holds (the screen
        // hidden for days). The latest page does not reach the marks held: they are read again from it back.
        StandIn engine;
        engine.bars = bars_json("2026-10-16T09:00:00+09:00", 20);
        const QDateTime nine = kst("2026-10-16T09:00:00+09:00");
        engine.marks = marks_from(nine, 100);
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-16T09:20:04+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.marks().size() == 100 && !live.chart_reading(), 10000);
        live.stop();
        engine.marks = marks_from(nine.addSecs(100), 1500, 51) + *engine.marks; // 1,500 more since
        live.start();                                                           // shown again: read at once
        QTRY_VERIFY_WITH_TIMEOUT(live.marks().size() == 1600 && !live.chart_reading(), 10000);
        QCOMPARE(live.marks().first().at, nine.addSecs(1599));
        QCOMPARE(live.marks().last().at, nine);
        live.stop();
    }

    void an_engine_without_trader_marks_shows_the_bars_and_no_failure() {
        // An engine before krx-quant #398 answers 404: no marks, and the chart is not "읽지 못함".
        StandIn old;
        old.bars = bars_json("2026-10-16T09:00:00+09:00", 20);
        services::KrxQuantService::set_base_url(old.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-16T09:20:04+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(old.asked_for(kChartMarks) && !live.chart_reading() && !live.reading(), 10000);
        QCOMPARE(live.chart_bars().size(), qsizetype(20));
        QVERIFY(live.marks().isEmpty());
        QVERIFY(live.chart_failure().isEmpty());
        QVERIFY(live.older_failure().isEmpty());
        QVERIFY(live.failure().isEmpty());
        live.stop();
    }

    void before_the_open_the_chart_shows_the_last_day_and_the_bars_before_as_it_scrolls_back() {
        // No bar today yet (before the open, a holiday): the latest bars before now. Scrolled back past the
        // first of them: the bars before it, of which this engine has none.
        StandIn engine;
        engine.last_day = bars_json("2026-10-15T19:00:00+09:00", 60);
        engine.marks = QList<QJsonObject>{QJsonObject{{"trade_id", 1},
                                                      {"side", "buy"},
                                                      {"at", "2026-10-15T19:10:02.500+09:00"},
                                                      {"price", 70000},
                                                      {"quantity", 4},
                                                      {"mode", "mock"}}};
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-16T07:30:04+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.chart_bars().size() == 60 && live.marks().size() == 1 && !live.chart_reading(),
                                 10000);
        QCOMPARE(live.chart_bars().first().start, kst("2026-10-15T19:00:00+09:00"));
        QVERIFY(engine.asked_for(kChartBars + "?limit=1000"));
        QVERIFY(!live.reached_start());
        live.view_from(-40, 20);
        QTRY_VERIFY_WITH_TIMEOUT(live.reached_start() && !live.chart_reading(), 10000);
        QVERIFY2(engine.asked_for(kChartBars + "?limit=1000&before=2026-10-15T19%3A00%3A00"),
                 qPrintable(engine.asked.constLast().path));
        QCOMPARE(live.chart_bars().size(), qsizetype(60));
        QVERIFY(live.older_failure().isEmpty());
        live.stop();
    }

    void the_bars_before_are_kept_only_while_they_reach_the_day() {
        // Codex review on codeyoma/FinceptTerminal#116: read before the open, the bars held are the last
        // trading day's. Read on all along, the day that opens next follows them. Shown again after days
        // hidden, it may not: they are dropped and read again from the new day back. Made-up bars.
        StandIn engine;
        engine.last_day = bars_json("2026-10-15T19:00:00+09:00", 60);
        engine.marks = QList<QJsonObject>{};
        services::KrxQuantService::set_base_url(engine.url());
        const auto before_the_open = [] { return kst("2026-10-16T07:30:04+09:00"); };

        // Read on (not hidden): the open's first bars follow the last day's.
        services::KrxTraderLive on(nullptr, before_the_open);
        on.start();
        QTRY_VERIFY_WITH_TIMEOUT(on.chart_bars().size() == 60 && !on.chart_reading(), 10000);
        engine.bars = bars_json("2026-10-16T08:00:00+09:00", 5);
        on.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(on.chart_bars().size() == 65 && !on.chart_reading(), 10000);
        QCOMPARE(on.chart_bars().first().start, kst("2026-10-15T19:00:00+09:00"));
        on.stop();

        // Hidden over a whole trading day: the 19th opens, the 15th's bars held do not reach it.
        engine.bars = QJsonArray{};
        services::KrxTraderLive hidden(nullptr, before_the_open);
        hidden.start();
        QTRY_VERIFY_WITH_TIMEOUT(hidden.chart_bars().size() == 60 && !hidden.chart_reading(), 10000);
        hidden.stop();
        engine.bars = bars_json("2026-10-19T08:00:00+09:00", 3);
        hidden.start();
        QTRY_VERIFY_WITH_TIMEOUT(hidden.chart_bars().size() == 3 && !hidden.chart_reading(), 10000);
        QCOMPARE(hidden.chart_bars().first().start, kst("2026-10-19T08:00:00+09:00"));
        hidden.stop();

        // Before the open again, shown days later: the last trading day is another one, read again.
        engine.bars = QJsonArray{};
        services::KrxTraderLive early(nullptr, before_the_open);
        early.start();
        QTRY_VERIFY_WITH_TIMEOUT(early.chart_bars().size() == 60 && !early.chart_reading(), 10000);
        early.stop();
        engine.last_day = bars_json("2026-10-19T19:00:00+09:00", 30);
        early.start();
        QTRY_VERIFY_WITH_TIMEOUT(early.chart_bars().size() == 30 && !early.chart_reading(), 10000);
        QCOMPARE(early.chart_bars().first().start, kst("2026-10-19T19:00:00+09:00"));
        early.stop();
    }

    void the_chart_reads_nothing_while_hidden_and_at_once_when_shown_or_the_engine_is_back() {
        // codeyoma/krx-quant#537: the chart's next read is 59 s away on this clock; each read below is the
        // screen shown again, the engine answering again after a failure, or a new address, not that one.
        StandIn engine;
        engine.bars = bars_json("2026-10-15T09:00:00+09:00", 20);
        const auto buy = [](int trade, const char* at) {
            return QJsonObject{{"trade_id", trade}, {"side", "buy"}, {"at", at},
                               {"price", 70000},    {"quantity", 4}, {"mode", "paper"}};
        };
        engine.marks = QList<QJsonObject>{buy(1, "2026-10-15T09:05:02.500+09:00")};
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-15T10:06:04+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.marks().size() == 1 && !live.chart_reading() && !live.reading(), 10000);

        // Hidden: nothing scheduled, and the view moved reads nothing.
        live.stop();
        QVERIFY(!chart_timer(live)->isActive());
        live.view_from(-40, 20);
        QVERIFY(!live.chart_reading()); // a read asks at once: none was asked
        // Shown again: at once, the trade since among them.
        engine.marks = QList<QJsonObject>{buy(2, "2026-10-15T09:10:02.500+09:00")} + *engine.marks;
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.marks().size() == 2 && !live.chart_reading(), 10000);
        QVERIFY(!engine.asked_for(kChartBars + "?")); // the view moved while hidden read nothing before

        // The engine out of reach, then back: its chart at once, with the state's answer.
        engine.fail = true;
        fire_schedule(live);
        QTRY_VERIFY_WITH_TIMEOUT(!live.failure().isEmpty() && !live.reading(), 10000);
        QCOMPARE(live.marks().size(), qsizetype(2)); // what was read stays
        engine.fail = false;
        engine.marks = QList<QJsonObject>{buy(3, "2026-10-15T09:15:02.500+09:00")} + *engine.marks;
        QVERIFY(chart_timer(live)->remainingTime() > 30'000); // not the chart's own read
        fire_schedule(live);
        QTRY_VERIFY_WITH_TIMEOUT(live.marks().size() == 3 && !live.chart_reading(), 10000);
        QVERIFY(live.failure().isEmpty());

        // Another address: the new engine's chart at once, the old one's dropped.
        StandIn other;
        other.bars = bars_json("2026-10-15T09:00:00+09:00", 5);
        other.marks = QList<QJsonObject>{};
        services::KrxQuantService::set_base_url(other.url());
        fire_schedule(live);
        QTRY_VERIFY_WITH_TIMEOUT(live.chart_bars().size() == 5 && other.asked_for(kChartMarks) && !live.chart_reading(),
                                 10000);
        QVERIFY(live.marks().isEmpty());
        live.stop();
    }

    // ---- The decisions (codeyoma/krx-quant#601) ---------------------------------------------------------------

    void the_decisions_are_read_from_the_demo_engine_s_answer() {
        // krx-quant #584 as the demo engine answered it: the champion's paper book and its live book that morning.
        const TraderDecisions d = decisions_of(QJsonDocument::fromJson(kDemoChampionDecisions).object());
        QCOMPARE(d.day, QDate(2026, 10, 12));
        QCOMPARE(d.book, QStringLiteral("champion"));
        QCOMPARE(d.books.size(), qsizetype(2));
        QCOMPARE(d.books[0].mode, QStringLiteral("paper"));
        QCOMPARE(d.books[1].mode, QStringLiteral("live"));
        QCOMPARE(d.books[1].policy, std::optional<qint64>(1));
        QCOMPARE(d.books[1].totals.decisions, 4);
        QCOMPARE(d.books[1].totals.bought, 4);
        QVERIFY(d.books[1].totals.skipped.isEmpty());
        QVERIFY(!d.books[1].more);
        const TraderDecision& newest = d.books[1].decisions.first();
        QCOMPARE(newest.id, qint64(9));
        QCOMPARE(newest.decided_at, kst("2026-10-12T10:27:02+09:00"));
        QCOMPARE(newest.bar_close, kst("2026-10-12T10:27:00+09:00"));
        QCOMPARE(newest.probability, std::optional<double>(0.68));
        QVERIFY(!newest.threshold && !newest.p_star && !newest.right);
        QVERIFY(newest.buy && newest.reason.isEmpty() && newest.answer.isEmpty());
        QCOMPARE(newest.trade_id, std::optional<qint64>(9));
        // The panel: each book's line, its rows newest first over both books.
        const auto lines = view::totals_lines(d);
        QCOMPARE(lines.size(), qsizetype(2));
        QCOMPARE(lines[0].text, QStringLiteral("챔피언 정책 1 · 모의 · 결정 3 · 산 것 3"));
        QCOMPARE(lines[1].text, QStringLiteral("챔피언 정책 1 · 실전 · 결정 4 · 산 것 4"));
        const auto rows = view::decision_rows(d);
        QCOMPARE(rows.size(), qsizetype(7));
        QCOMPARE(rows.first().id, qint64(9));
        QCOMPARE(rows.last().id, qint64(3));
        QCOMPARE(rows.first().book, QStringLiteral("챔피언 정책 1 · 실전"));
        QCOMPARE(rows.first().action, QStringLiteral("매수"));
        QCOMPARE(rows.first().threshold, QStringLiteral("—"));
        QCOMPARE(rows.first().right, QStringLiteral("—"));
        QCOMPARE(view::decisions_line(d, true, false), QStringLiteral("10-12(월) · 결정 7개"));
        // Every call bought: no hollow mark.
        QVERIFY(view::skip_icons(d).isEmpty());
        QVERIFY(view::with_skip_lines({}, d).isEmpty());
    }

    void a_book_s_totals_rows_and_hollow_marks_say_why_each_call_bought_or_not() {
        const TraderDecisions d = decisions_of(practice_json());
        QCOMPARE(d.books.size(), qsizetype(1));
        // Commonest first, whatever order the object kept them in.
        QCOMPARE(d.books[0].totals.skipped,
                 (QList<std::pair<QString, int>>{{QStringLiteral("not_up"), 2}, {QStringLiteral("holding"), 1}}));
        const auto lines = view::totals_lines(d);
        QCOMPARE(lines.size(), qsizetype(1));
        QCOMPARE(lines[0].text,
                 QStringLiteral("연습 · KIS 모의 · 결정 6 · 산 것 1 · 호가 기다림 1 · 못 산 것 1(호가 없음 1) · "
                                "건너뜀 3(오름 아님 2 · 보유 중 1) · 정답 난 것: 산 것 1/1 맞음 · 건너뛴 것 "
                                "1/2 놓친 오름"));
        QVERIFY(lines[0].tip.contains(QStringLiteral("건너뛴 이유 · 보유 중 1: 들고 있는 포지션이나")));
        const auto rows = view::decision_rows(d);
        QStringList shown;
        for (const auto& row : rows)
            shown << QStringList{row.at,     row.cell,   row.probability, row.threshold,
                                 row.action, row.reason, row.answer,      row.right}
                         .join(QStringLiteral(" | "));
        QCOMPARE(
            shown,
            (QStringList{
                QStringLiteral("09:12:02 | 장벽 4분 · breiman | 70.0% | 60.0% | 호가 기다림 | — | — | —"),
                QStringLiteral(
                    "09:11:02 | 장벽 4분 · breiman | 66.0% | 60.0% | 건너뜀 | 보유 중 | 위 먼저 | 놓친 오름"),
                QStringLiteral("09:10:02 | 장벽 4분 · breiman | 71.0% | 60.0% | 매수 | — | 위 먼저 | 맞음"),
                QStringLiteral(
                    "09:09:02 | 장벽 4분 · breiman | 68.0% | 60.0% | 매수 못 함 | 호가 없음 | 아래 먼저 | 틀림"),
                QStringLiteral("09:08:02 | 장벽 4분 · breiman | 41.0% | 60.0% | 건너뜀 | 오름 아님 | 시간 만료 | 맞음"),
                QStringLiteral("09:07:02 | 방향 4분 · gauss | 20.0% | — | 건너뜀 | 오름 아님 | — | —"),
            }));
        QCOMPARE(rows[1].right_tone, Tone::Warning); // a rise missed
        QCOMPARE(rows[2].right_tone, Tone::Positive);
        QCOMPARE(rows[3].right_tone, Tone::Dim); // not filled: no trade
        QCOMPARE(rows[2].action_tone, Tone::Positive);
        QCOMPARE(rows[1].book, QStringLiteral("연습 · KIS 모의"));
        QVERIFY(rows[1].reason_tip.contains(QStringLiteral("(holding)")));
        QVERIFY(rows[2].tip.contains(QStringLiteral("매매 41")));

        // On the chart: a hollow mark on each bar of a call that traded nothing (not the buy filled, which is a
        // trade's, nor the one waiting), each call's line in its bar's box.
        const auto bar = [](const char* start) { return chart_time(kst(start)); };
        QCOMPARE(view::skip_icons(d), (QList<SkipIcon>{{bar("2026-10-16T09:07:00+09:00"), false},
                                                       {bar("2026-10-16T09:08:00+09:00"), false},
                                                       {bar("2026-10-16T09:09:00+09:00"), false},
                                                       {bar("2026-10-16T09:11:00+09:00"), false}}));
        BarHover kept;
        kept.time = bar("2026-10-16T09:08:00+09:00");
        kept.lines << QStringLiteral("자동 매매 매수 70,100원 × 1주 · KIS 모의 · 연습");
        const auto hovers = view::with_skip_lines({kept}, d);
        QMap<qint64, QStringList> boxes;
        for (const auto& h : hovers)
            boxes.insert(h.time, h.lines);
        QCOMPARE(boxes.size(), qsizetype(4));
        QCOMPARE(boxes.value(bar("2026-10-16T09:07:00+09:00")),
                 QStringList{QStringLiteral("연습 건너뜀 · 방향 4분 · gauss · 20.0% · 오름 아님")});
        QCOMPARE(boxes.value(bar("2026-10-16T09:08:00+09:00")),
                 (QStringList{QStringLiteral("자동 매매 매수 70,100원 × 1주 · KIS 모의 · 연습"),
                              QStringLiteral(
                                  "연습 건너뜀 · 장벽 4분 · breiman · 41.0% (θ 60.0%) · 오름 아님 · 정답 시간 만료")}));
        QCOMPARE(boxes.value(bar("2026-10-16T09:09:00+09:00")),
                 QStringList{QStringLiteral(
                     "연습 매수 못 함 · 장벽 4분 · breiman · 68.0% (θ 60.0%) · 호가 없음 · 정답 아래 먼저")});
        QCOMPARE(boxes.value(bar("2026-10-16T09:11:00+09:00")),
                 QStringList{QStringLiteral(
                     "연습 건너뜀 · 장벽 4분 · breiman · 66.0% (θ 60.0%) · 보유 중 · 정답 위 먼저 · 놓친 오름")});
        // A live book's mark is the live account's colour.
        QJsonObject live = practice_json();
        QJsonArray books = live["books"].toArray();
        QJsonObject book = books[0].toObject();
        book["book"] = "champion";
        book["mode"] = "live";
        books[0] = book;
        live["books"] = books;
        QVERIFY(view::skip_icons(decisions_of(live)).first().live);
        QCOMPARE(view::decisions_line(d, true, false), QStringLiteral("10-16(금) · 결정 6개"));
    }

    void each_reason_has_its_korean_name_and_meaning_and_an_unknown_one_stays() {
        for (const char* reason : {"not_picked",
                                   "not_ok",
                                   "blocked",
                                   "special",
                                   "stale",
                                   "expired",
                                   "not_up",
                                   "below_threshold",
                                   "no_reference",
                                   "closed",
                                   "before_entry",
                                   "past_last_exit",
                                   "holding",
                                   "reconciling",
                                   "outranked",
                                   "quotes_stalled",
                                   "entries_paused",
                                   "paused",
                                   "daily_limit",
                                   "cumulative_limit",
                                   "order_failed",
                                   "buy_limit",
                                   "cooldown",
                                   "outside_held",
                                   "target_reached",
                                   "no_quote",
                                   "no_shares",
                                   "bad_stop",
                                   "restarted",
                                   "unfilled",
                                   "?"}) {
            const QString key = QString::fromLatin1(reason);
            QVERIFY2(view::reason_label(key) != key, reason);
            QVERIFY2(!view::reason_tip(key).isEmpty(), reason);
        }
        QCOMPARE(view::reason_label(QStringLiteral("new_reason")), QStringLiteral("new_reason"));
        QVERIFY(view::reason_tip(QStringLiteral("new_reason")).isEmpty());
        QCOMPARE(view::book_choices().first().book, QString::fromLatin1(view::kDefaultBook)); // the practice's first
        QCOMPARE(view::book_label(QStringLiteral("shadow")), QStringLiteral("그림자"));
        // Before any answer, from an engine without them, an empty day.
        QCOMPARE(view::decisions_line(std::nullopt, true, true), QStringLiteral("읽는 중"));
        QCOMPARE(view::decisions_line(std::nullopt, false, false), QStringLiteral("엔진이 아직 지원하지 않음"));
        TraderDecisions empty;
        empty.day = QDate(2026, 10, 17);
        QCOMPARE(view::decisions_line(empty, true, false), QStringLiteral("10-17(토) · 결정 없음"));
        QVERIFY(view::totals_lines(empty).isEmpty());
    }

    void a_day_s_pages_join_until_every_book_is_read_back_to_its_first() {
        // Two challengers (codeyoma/krx-quant#601): the latest page asks back from the later of their earliest, a
        // millisecond past it; a book whose page says no more is done, and one asked for that comes no earlier
        // stops there (a millisecond holding more than a page).
        const auto page = [](QList<QJsonObject> books) {
            QJsonArray array;
            for (const auto& b : books)
                array.append(b);
            return decisions_of(QJsonObject{{"day", "2026-10-16"}, {"book", "challenger"}, {"books", array}});
        };
        const auto more = [](QJsonObject b) {
            b["more"] = true;
            return b;
        };
        const QDateTime nine = kst("2026-10-16T09:00:00+09:00");
        auto pages = view::decision_pages(
            page({more(book_json("challenger", "paper", 2, skips_from(nine.addSecs(10), 3, 10))),
                  more(book_json("challenger", "paper", 3, skips_from(nine.addSecs(20), 2, 30)))}));
        QCOMPARE(pages.done, (QList<bool>{false, false}));
        QCOMPARE(view::decisions_before(pages), std::optional<QDateTime>(nine.addSecs(20).addMSecs(1)));
        // Policy 3's page back is all it had; policy 2's is a repeat of the same, not done yet.
        view::join_decisions(pages,
                             page({more(book_json("challenger", "paper", 2, skips_from(nine.addSecs(10), 3, 10))),
                                   book_json("challenger", "paper", 3, skips_from(nine.addSecs(18), 3, 28))}),
                             nine.addSecs(20).addMSecs(1));
        QCOMPARE(pages.done, (QList<bool>{false, true}));
        QCOMPARE(pages.read.books[1].decisions.size(), qsizetype(4)); // 28–31, 30 and 31 not doubled
        QCOMPARE(pages.read.books[1].decisions.first().id, qint64(31));
        QCOMPARE(pages.read.books[1].decisions.last().id, qint64(28));
        QCOMPARE(view::decisions_before(pages), std::optional<QDateTime>(nine.addSecs(10).addMSecs(1)));
        // Asked for policy 2, its page comes no earlier: done, and nothing more to read.
        view::join_decisions(pages,
                             page({more(book_json("challenger", "paper", 2, skips_from(nine.addSecs(10), 1, 10)))}),
                             nine.addSecs(10).addMSecs(1));
        QCOMPARE(pages.done, (QList<bool>{true, true}));
        QVERIFY(!view::decisions_before(pages));
        QCOMPARE(pages.read.books[0].decisions.size(), qsizetype(3));
        // A book left out of a page back has nothing before it.
        auto left = view::decision_pages(page({more(book_json("challenger", "paper", 2, skips_from(nine, 2, 1)))}));
        view::join_decisions(left, page({}), nine.addMSecs(1));
        QCOMPARE(left.done, QList<bool>{true});
        // An empty day: nothing to read back.
        QVERIFY(!view::decisions_before(view::decision_pages(page({}))));
    }

    void the_decisions_of_the_chart_s_day_are_read_with_the_chart_and_another_book_at_once() {
        StandIn engine;
        engine.bars = bars_json("2026-10-16T09:00:00+09:00", 20);
        engine.marks = QList<QJsonObject>{};
        engine.decisions = QMap<QString, QJsonObject>{{QStringLiteral("practice"), practice_json()}};
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-16T09:20:04+09:00"); });
        QCOMPARE(live.decisions_book(), QStringLiteral("practice")); // the default
        live.start();
        const QString asked = QStringLiteral("/v1/trader/decisions?book=practice&limit=1000&day=2026-10-16");
        QTRY_VERIFY_WITH_TIMEOUT(engine.asked_for(asked) && live.decisions() &&
                                     live.decisions()->day == QDate(2026, 10, 16) && !live.decisions_reading(),
                                 10000);
        QCOMPARE(live.decisions()->books.size(), qsizetype(1));
        QCOMPARE(live.decisions()->books[0].decisions.size(), qsizetype(6));
        QVERIFY(live.decisions_served());
        QVERIFY(live.decisions_failure().isEmpty());

        // Another book: those read are not shown under it, and its own are read at once.
        live.set_decisions_book(QStringLiteral("champion"));
        QVERIFY(!live.decisions());
        QTRY_VERIFY_WITH_TIMEOUT(engine.asked_for(QStringLiteral("/v1/trader/decisions?book=champion&limit=1000&day="
                                                                 "2026-10-16")) &&
                                     live.decisions() && !live.decisions_reading(),
                                 10000);
        QCOMPARE(live.decisions()->book, QStringLiteral("champion"));
        QVERIFY(live.decisions()->books.isEmpty()); // none that day
        // Chosen again while the other's is read: that answer is not kept, the one chosen last is read.
        engine.hold_decisions = true;
        const qsizetype held = engine.count("GET", "/v1/trader/decisions");
        live.set_decisions_book(QStringLiteral("shadow"));
        QTRY_VERIFY_WITH_TIMEOUT(engine.count("GET", "/v1/trader/decisions") > held, 10000);
        live.set_decisions_book(QStringLiteral("practice"));
        engine.hold_decisions = false;
        engine.release();
        QTRY_VERIFY_WITH_TIMEOUT(live.decisions() && !live.decisions_reading(), 10000);
        QCOMPARE(live.decisions()->book, QStringLiteral("practice"));
        QCOMPARE(live.decisions()->books.size(), qsizetype(1));
        live.stop();
    }

    void a_book_s_day_is_read_whole_page_after_page() {
        // Two challengers' days of 1,100 and 2,500 calls, a second apart from 09:00: the latest page holds each
        // one's last 1,000; the page back asks from the later of their earliest (policy 3's 09:25:00), a
        // millisecond past it, then from policy 3's 09:08:21, until neither has more.
        StandIn engine;
        engine.bars = bars_json("2026-10-16T09:00:00+09:00", 20);
        engine.marks = QList<QJsonObject>{};
        const QDateTime nine = kst("2026-10-16T09:00:00+09:00");
        engine.decisions = QMap<QString, QJsonObject>{
            {QStringLiteral("challenger"),
             QJsonObject{{"books", QJsonArray{book_json("challenger", "paper", 2, skips_from(nine, 1100, 1)),
                                              book_json("challenger", "paper", 3, skips_from(nine, 2500, 10'001))}}}}};
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-16T09:20:04+09:00"); });
        live.set_decisions_book(QStringLiteral("challenger"));
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.decisions() && live.decisions()->books.size() == 2 &&
                                     live.decisions()->books[0].decisions.size() == 1100 &&
                                     live.decisions()->books[1].decisions.size() == 2500 && !live.decisions_reading(),
                                 10000);
        QVERIFY2(
            engine.asked_for(QStringLiteral("/v1/trader/decisions?book=challenger&limit=1000&day=2026-10-16&before="
                                            "2026-10-16T09%3A25%3A00.001")),
            qPrintable(engine.asked.constLast().path));
        QVERIFY(engine.asked_for(QStringLiteral("/v1/trader/decisions?book=challenger&limit=1000&day=2026-10-16&before="
                                                "2026-10-16T09%3A08%3A21.001")));
        QCOMPARE(live.decisions()->books[1].decisions.first().id, qint64(12'500)); // newest first
        QCOMPARE(live.decisions()->books[1].decisions.last().id, qint64(10'001));
        live.stop();
    }

    void an_engine_without_decisions_shows_none_and_no_failure() {
        // An engine before krx-quant #584 answers 404: nothing to show, "엔진이 아직 지원하지 않음", not a failure.
        StandIn old;
        old.bars = bars_json("2026-10-16T09:00:00+09:00", 20);
        old.marks = QList<QJsonObject>{};
        services::KrxQuantService::set_base_url(old.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-16T09:20:04+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(old.asked_for(QStringLiteral("/v1/trader/decisions")) && !live.decisions_served() &&
                                     !live.decisions_reading() && !live.reading() && !live.chart_reading(),
                                 10000);
        QVERIFY(!live.decisions());
        QVERIFY(live.decisions_failure().isEmpty());
        QVERIFY(live.failure().isEmpty());
        QVERIFY(live.chart_failure().isEmpty());
        QCOMPARE(view::decisions_line(live.decisions(), live.decisions_served(), live.decisions_reading()),
                 QStringLiteral("엔진이 아직 지원하지 않음"));
        // A failure is one, and keeps what was read.
        StandIn failing;
        failing.bars = old.bars;
        failing.marks = QList<QJsonObject>{};
        failing.decisions = QMap<QString, QJsonObject>{{QStringLiteral("practice"), practice_json()}};
        services::KrxQuantService::set_base_url(failing.url());
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(live.decisions() && live.decisions_served() && !live.decisions_reading(), 10000);
        failing.fail_decisions = true;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(!live.decisions_failure().isEmpty() && !live.decisions_reading(), 10000);
        QCOMPARE(live.decisions()->books[0].decisions.size(), qsizetype(6));
        failing.fail_decisions = false;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(live.decisions_failure().isEmpty() && !live.decisions_reading(), 10000);
        live.stop();
    }

    void the_decisions_are_not_read_while_hidden_and_read_at_once_when_shown_each_minute_or_the_engine_back() {
        // The chart's next read is 59 s away on this clock: each read below is the screen shown again, the minute
        // turning (its timer fired), the engine answering again after a failure, not that one.
        StandIn engine;
        engine.bars = bars_json("2026-10-16T09:00:00+09:00", 20);
        engine.marks = QList<QJsonObject>{};
        engine.decisions = QMap<QString, QJsonObject>{{QStringLiteral("practice"), practice_json()}};
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxTraderLive live(nullptr, [] { return kst("2026-10-16T09:20:04+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.decisions() && live.decisions()->day == QDate(2026, 10, 16) &&
                                     !live.decisions_reading() && !live.chart_reading() && !live.reading(),
                                 10000);
        const auto reads = [&engine] { return engine.count("GET", "/v1/trader/decisions"); };

        // Hidden: nothing is asked, now or as a minute turns.
        live.stop();
        qsizetype before = reads();
        live.read_now();
        live.set_decisions_book(QStringLiteral("champion"));
        QVERIFY(!live.decisions_reading()); // a read asks at once: none was asked
        live.set_decisions_book(QStringLiteral("practice"));
        QVERIFY(!live.decisions_reading());
        QCOMPARE(reads(), before);
        // Shown again: at once.
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(reads() > before && live.decisions() && !live.decisions_reading(), 10000);
        // The minute turns: once more.
        QTRY_VERIFY_WITH_TIMEOUT(!live.chart_reading() && !live.decisions_reading(), 10000);
        before = reads();
        chart_timer(live)->start(0);
        QTRY_VERIFY_WITH_TIMEOUT(reads() > before && !live.decisions_reading(), 10000);
        // The engine out of reach, then back: at once, with the state's answer.
        engine.fail = true;
        fire_schedule(live);
        QTRY_VERIFY_WITH_TIMEOUT(!live.failure().isEmpty() && !live.reading(), 10000);
        engine.fail = false;
        QTRY_VERIFY_WITH_TIMEOUT(!live.decisions_reading(), 10000);
        before = reads();
        QVERIFY(chart_timer(live)->remainingTime() > 30'000); // not the chart's own read
        fire_schedule(live);
        QTRY_VERIFY_WITH_TIMEOUT(reads() > before && live.failure().isEmpty() && !live.decisions_reading(), 10000);
        QVERIFY(live.decisions_failure().isEmpty());
        // Another address: the old engine's dropped, the new one's read at once.
        StandIn other;
        other.bars = engine.bars;
        other.marks = QList<QJsonObject>{};
        services::KrxQuantService::set_base_url(other.url());
        fire_schedule(live);
        QTRY_VERIFY_WITH_TIMEOUT(other.asked_for(QStringLiteral("/v1/trader/decisions")) && !live.decisions_served() &&
                                     !live.decisions_reading(),
                                 10000);
        QVERIFY(!live.decisions());
        QCOMPARE(live.decisions_book(), QStringLiteral("practice")); // the choice is kept
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
