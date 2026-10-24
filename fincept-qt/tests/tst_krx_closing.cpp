// Tests for the KRX closing flow screen's model (codeyoma/krx-quant#305): how it reads GET
// /v1/symbols/{symbol}/closing-flow and …/closing-flow/days (src/services/krx_quant/EngineData.h,
// parse_closing_flow and parse_closing_days), what it shows of them (src/services/krx_quant/ClosingView.h)
// and when it reads them (src/services/krx_quant/KrxClosingLive.h), the last against a stand-in engine
// on this computer. The values are made up. The settings (the engine's address, the symbol chosen) go
// to a temporary INI file: AppConfig is defined below, in place of core/config/AppConfig.cpp.

#include "core/config/AppConfig.h"
#include "services/krx_quant/ClosingView.h"
#include "services/krx_quant/KrxClosingLive.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
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
namespace view = fincept::krx_quant::closing;
using view::Side;

namespace {

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
}

QJsonObject product(const char* code, int leverage, const char* structure, QJsonValue net_assets, QJsonValue demand,
                    const char* source = "kis") {
    const bool held = !net_assets.isNull();
    return {{"code", code},
            {"name", QStringLiteral("지어낸 상품 %1").arg(QString::fromLatin1(code))},
            {"leverage", leverage},
            {"structure", structure},
            {"coefficient", leverage * leverage - leverage},
            {"net_assets", net_assets},
            {"net_assets_date", held ? QJsonValue("2026-10-08") : QJsonValue()},
            {"net_assets_source", held ? QJsonValue(QString::fromLatin1(source)) : QJsonValue()},
            {"demand", demand}};
}

/// Today's answer for a symbol with products at 10:30 in continuous trading: one product without net
/// assets, the inverse one with its coefficient of 6; made-up values.
QJsonObject today_json(const QString& symbol = "005930", const char* source = "kis") {
    return {{"symbol", symbol},
            {"day", "2026-10-12"},
            {"as_of", "2026-10-12T10:30:20+09:00"},
            {"at", "2026-10-12T10:30:00+09:00"},
            {"source", "live"},
            {"price", 79250.0},
            {"previous_close", 81250.0},
            {"r", -0.0246},
            {"demand", -237873821539.0},
            {"spot_futures", -115462301538.0},
            {"futures", -122411520001.0},
            {"ratio", -1.5094},
            {"ratio_total", -3.1097},
            {"denominator", 76494829950.0},
            {"denominator_days", 15},
            {"denominator_reason", QJsonValue()},
            {"products", QJsonArray{product("0193W0", 2, "spot_futures", 215460000000.0, -10607261538.0, source),
                                    product("0198B0", 2, "futures", QJsonValue(), QJsonValue()),
                                    product("0193L0", -2, "futures", 513648000000.0, -75861858462.0)}},
            {"missing", QJsonArray{"0198B0"}},
            {"reason", QJsonValue()}};
}

/// The control symbol: no products, so no demand, with its reason; its denominator and r still given.
QJsonObject control_json() {
    QJsonObject o = today_json("009150");
    for (const char* key : {"demand", "spot_futures", "futures", "ratio", "ratio_total"})
        o[key] = QJsonValue();
    o["products"] = QJsonArray{};
    o["missing"] = QJsonArray{};
    o["reason"] = "no_products";
    return o;
}

QJsonObject stats(int days, QJsonValue agree, int agree_n) {
    return {{"days", days},
            {"auction_bp_median", 24.9},
            {"agree", agree},
            {"agree_n", agree_n},
            {"next_gap_opposite", 0.45},
            {"next_gap_n", agree_n},
            {"next_close_opposite", QJsonValue()},
            {"next_close_n", 0}};
}

/// Three days, oldest first: before the recording (no denominator), one without net assets (compared
/// with the day's flow), and the last with its stored 15:20 demand and a VI, its next day not yet in.
QJsonObject days_json(const QString& symbol = "005930", QJsonValue computed_at = "2026-10-12T06:01:30+09:00") {
    const QJsonObject recordless{{"date", "2026-09-11"},
                                 {"period", "규제 뒤(07-31~09-13)"},
                                 {"demand", -28033566434.0},
                                 {"spot_futures", -14652680653.0},
                                 {"futures", -13380885781.0},
                                 {"demand_source", "bars"},
                                 {"ratio", QJsonValue()},
                                 {"ratio_total", QJsonValue()},
                                 {"denominator", QJsonValue()},
                                 {"denominator_days", 0},
                                 {"denominator_reason", "no_recorded_auction"},
                                 {"auction_value", QJsonValue()},
                                 {"auction_value_source", QJsonValue()},
                                 {"auction_bp", -46.8},
                                 {"auction_ticks", -2.0},
                                 {"flow_bp", -46.6},
                                 {"compared_with", "demand"},
                                 {"agree", true},
                                 {"next_gap_bp", -47.1},
                                 {"next_close_bp", -117.6},
                                 {"special", QJsonArray{}}};
    QJsonObject without = recordless;
    without["date"] = "2026-09-14";
    without["period"] = "애프터마켓 뒤(09-14~)";
    for (const char* key : {"demand", "spot_futures", "futures", "demand_source"})
        without[key] = QJsonValue();
    without["denominator_reason"] = QJsonValue();
    without["denominator"] = 44557992000.0;
    without["denominator_days"] = 1;
    without["compared_with"] = "flow";
    without["agree"] = QJsonValue();
    QJsonObject stored = recordless;
    stored["date"] = "2026-10-08";
    stored["period"] = "애프터마켓 뒤(09-14~)";
    stored["demand"] = 56898771859.0;
    stored["demand_source"] = "live";
    stored["ratio"] = 0.4241;
    stored["ratio_total"] = 0.8114;
    stored["denominator"] = 70120346000.0;
    stored["denominator_days"] = 14;
    stored["denominator_reason"] = QJsonValue();
    stored["auction_value"] = 56356782000.0;
    stored["auction_value_source"] = "recorded";
    stored["auction_bp"] = 24.9;
    stored["auction_ticks"] = 1.0;
    stored["agree"] = false;
    stored["next_gap_bp"] = QJsonValue();
    stored["next_close_bp"] = QJsonValue();
    stored["special"] = QJsonArray{"vi"};
    const QJsonObject summary{{"periods", QJsonArray{QJsonObject{{"period", "애프터마켓 뒤(09-14~)"},
                                                                 {"from", "2026-09-14"},
                                                                 {"to", QJsonValue()},
                                                                 {"all", stats(19, 0.1333, 15)},
                                                                 {"without_special", stats(18, 0.1333, 15)}}}},
                              {"tertiles", QJsonArray{QJsonObject{{"tertile", 1},
                                                                  {"ratio_from", 0.0},
                                                                  {"ratio_to", 0.42},
                                                                  {"all", stats(25, 0.55, 22)},
                                                                  {"without_special", stats(24, QJsonValue(), 0)}}}}};
    return {{"symbol", symbol},
            {"from", "2025-10-13"},
            {"to", "2026-10-12"},
            {"computed_at", computed_at},
            {"days", QJsonArray{recordless, without, stored}},
            {"summary", summary}};
}

/// The days as an engine since codeyoma/krx-quant#368 sends them: the day before any value says
/// no_auction_value, each value says where it came from (recorded, else KRX's bar, else the integrated
/// one), each denominator its days by source, and each demand whose net assets it read.
QJsonObject sourced_days_json() {
    QJsonObject o = days_json();
    QJsonArray days = o["days"].toArray();
    QJsonObject first = days[0].toObject();
    first["denominator_reason"] = "no_auction_value";
    first["denominator_sources"] = QJsonObject{{"recorded", 0}, {"krx_bar", 0}, {"integrated_bar", 0}};
    first["auction_value"] = 31200000000.0;
    first["auction_value_source"] = "integrated_bar";
    first["net_assets_source"] = "kis";
    QJsonObject second = days[1].toObject();
    second["denominator_sources"] = QJsonObject{{"recorded", 0}, {"krx_bar", 0}, {"integrated_bar", 1}};
    second["auction_value"] = 44557992000.0;
    second["auction_value_source"] = "krx_bar";
    second["net_assets_source"] = QJsonValue();
    QJsonObject third = days[2].toObject();
    third["denominator_sources"] = QJsonObject{{"recorded", 7}, {"krx_bar", 7}, {"integrated_bar", 0}};
    third["net_assets_source"] = "krx+kis";
    o["days"] = QJsonArray{first, second, third};
    return o;
}

ClosingFlow today(const QJsonObject& body = today_json()) {
    auto f = parse_closing_flow(QJsonDocument(body));
    if (f.is_err())
        qFatal("%s", f.error().c_str());
    return f.value();
}

ClosingDays days(const QJsonObject& body = days_json()) {
    auto d = parse_closing_days(QJsonDocument(body));
    if (d.is_err())
        qFatal("%s", d.error().c_str());
    return d.value();
}

/// The engine as the closing flow screen reads it: GET /v1/symbols, …/closing-flow and
/// …/closing-flow/days, the paths asked kept.
class StandIn : public QObject {
  public:
    bool fail = false;
    bool fail_days = false; ///< …/closing-flow/days alone fails
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
    qsizetype count(const QString& suffix) const {
        return std::count_if(asked.cbegin(), asked.cend(), [&suffix](const QString& p) { return p.endsWith(suffix); });
    }

  private:
    void read(QTcpSocket* socket) {
        QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n"))
            return;
        const QString path = QString::fromLatin1(request.left(request.indexOf("\r\n")).split(' ').value(1));
        asked << path;
        int status = 404;
        QJsonObject body{{"error", "not found"}};
        const QString symbol = path.section('/', 3, 3);
        if (path == "/v1/symbols") {
            status = 200;
            body = {{"symbols", QJsonArray{QJsonObject{{"symbol", "005930"}, {"name", "삼성전자"}},
                                           QJsonObject{{"symbol", "000660"}, {"name", "SK하이닉스"}},
                                           QJsonObject{{"symbol", "009150"}, {"name", "삼성전기"}}}}};
        } else if (path.contains("/closing-flow") && (fail || (fail_days && path.endsWith("/days")))) {
            status = 500;
            body = {{"error", "made-up failure"}};
        } else if (path.endsWith("/closing-flow/days")) {
            status = 200;
            body = days_json(symbol);
        } else if (path.endsWith("/closing-flow")) {
            status = 200;
            body = symbol == "009150" ? control_json() : today_json(symbol);
        }
        const QByteArray bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                      "\r\nConnection: close\r\n\r\n" + bytes);
        socket->disconnectFromHost();
    }

    QTcpServer server_;
};

/// The live object's own schedule, fired now (its next read, as the timer would start it).
void fire_schedule(services::KrxClosingLive& live) {
    auto* timer = live.findChild<QTimer*>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(timer);
    timer->start(0);
}

} // namespace

class TstKrxClosing : public QObject {
    Q_OBJECT

    QTemporaryDir settings_;

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        settings_file = settings_.filePath("settings.ini");
    }

    void today_is_read_with_its_nulls_and_reasons() {
        const ClosingFlow f = today();
        QCOMPARE(f.symbol, QStringLiteral("005930"));
        QCOMPARE(f.day, QDate(2026, 10, 12));
        QCOMPARE(f.at, kst("2026-10-12T10:30:00+09:00"));
        QCOMPARE(f.source, QStringLiteral("live"));
        QCOMPARE(*f.demand, -237873821539.0); // beyond an int: kept whole
        QCOMPARE(*f.denominator, 76494829950.0);
        QCOMPARE(f.denominator_days, 15);
        QVERIFY(f.denominator_reason.isEmpty());
        QCOMPARE(f.products.size(), 3);
        QCOMPARE(f.products[0].net_assets_source, QStringLiteral("kis"));
        QCOMPARE(f.products[0].net_assets_date, QDate(2026, 10, 8));
        QVERIFY(!f.products[1].net_assets); // not kept: out of the sums
        QVERIFY(!f.products[1].demand);
        QVERIFY(f.products[1].net_assets_source.isEmpty());
        QCOMPARE(f.products[2].leverage, -2);
        QCOMPARE(f.products[2].coefficient, 6);
        QCOMPARE(f.missing, QStringList{"0198B0"});
        QVERIFY(f.reason.isEmpty());

        const ClosingFlow control = today(control_json());
        QVERIFY(!control.demand);
        QVERIFY(!control.ratio);
        QCOMPARE(control.reason, QStringLiteral("no_products"));
        QVERIFY(control.products.isEmpty());
        QVERIFY(control.denominator.has_value()); // still given, as r

        QJsonObject early = today_json();
        early["at"] = QJsonValue();
        early["source"] = QJsonValue();
        early["demand"] = QJsonValue();
        early["denominator"] = QJsonValue();
        early["denominator_reason"] = "not_computed";
        early["reason"] = "before_open";
        const ClosingFlow before = today(early);
        QVERIFY(!before.at.isValid());
        QVERIFY(before.source.isEmpty());
        QCOMPARE(before.denominator_reason, QStringLiteral("not_computed"));
        QCOMPARE(before.reason, QStringLiteral("before_open"));

        QVERIFY(parse_closing_flow(QJsonDocument::fromJson(R"({"error": "unknown symbol"})")).is_err());
    }

    void the_days_are_read_with_their_reasons() {
        const ClosingDays d = days();
        QCOMPARE(d.symbol, QStringLiteral("005930"));
        QCOMPARE(d.from, QDate(2025, 10, 13));
        QCOMPARE(d.computed_at, kst("2026-10-12T06:01:30+09:00"));
        QCOMPARE(d.days.size(), 3);
        const ClosingDay& recordless = d.days[0];
        QVERIFY(!recordless.ratio);
        QCOMPARE(recordless.denominator_reason, QStringLiteral("no_recorded_auction"));
        QCOMPARE(recordless.demand_source, QStringLiteral("bars"));
        QCOMPARE(*recordless.agree, true);
        const ClosingDay& without = d.days[1];
        QVERIFY(!without.demand);
        QVERIFY(!without.agree); // null: not a false
        QCOMPARE(without.compared_with, QStringLiteral("flow"));
        const ClosingDay& stored = d.days[2];
        QCOMPARE(*stored.agree, false);
        QVERIFY(!stored.next_gap_bp);
        QCOMPARE(stored.special, QStringList{"vi"});
        QCOMPARE(*stored.auction_value, 56356782000.0);
        QVERIFY(stored.net_assets_source.isEmpty()); // an engine before krx-quant#368 sends none
        QCOMPARE(d.periods.size(), 1);
        QVERIFY(!d.periods[0].to.isValid());
        QCOMPARE(d.periods[0].all.agree_n, 15);
        QCOMPARE(d.periods[0].without_special.days, 18);
        QVERIFY(!d.periods[0].all.next_close_opposite);
        QCOMPARE(d.tertiles.size(), 1);
        QCOMPARE(d.tertiles[0].ratio_to, 0.42);
        QVERIFY(!d.tertiles[0].without_special.agree);

        const ClosingDays fresh = days(days_json("005930", QJsonValue()));
        QVERIFY(!fresh.computed_at.isValid()); // not computed yet
        QVERIFY(parse_closing_days(QJsonDocument::fromJson(R"({"error": "from after to"})")).is_err());
    }

    void the_card_says_buy_or_sell_by_structure_and_the_ratio_s_upper_bound() {
        const ClosingFlow f = today();
        QCOMPARE(view::headline(f), QStringLiteral("매도 −2,378.7억원"));
        QCOMPARE(view::day_line(f), QStringLiteral("2026-10-12(월) · 10:30 지금 가격으로 계산"));
        const auto lines = view::card_lines(f);
        QCOMPARE(lines[0].label, QStringLiteral("현물+선물형"));
        QCOMPARE(lines[0].value, QStringLiteral("−1,154.6억원"));
        QCOMPARE(lines[0].side, Side::Sell);
        QCOMPARE(lines[1].label, QStringLiteral("선물형"));
        QCOMPARE(lines[2].value, QStringLiteral("−1.51배 (상한)"));
        QCOMPARE(lines[3].value, QStringLiteral("−3.11배"));
        QCOMPARE(lines[4].label, QStringLiteral("분모"));
        QCOMPARE(lines[4].value, QStringLiteral("764.9억원 (20거래일 중 15일)"));
        QVERIFY(lines[4].tip.startsWith(
            QStringLiteral("그날 전 20거래일의 종가 단일가 체결대금 중앙값. 날마다 녹화한 KRX 체결")));
        QVERIFY(!lines[4].tip.contains(QStringLiteral("그 날들"))); // an engine before krx-quant#368: no counts
        QCOMPARE(lines[5].value, QStringLiteral("−2.46% (79,250 ÷ 81,250)"));
        QCOMPARE(lines[6].value, QStringLiteral("10:30 (그 직전 기준가)"));
        const QStringList notes = view::notes(f);
        QVERIFY(notes[0].startsWith(QStringLiteral("공식으로 계산한 값")));
        QVERIFY(notes[1].startsWith(QStringLiteral("홍콩 상품·ETN 제외")));
        QCOMPARE(notes[2], QStringLiteral("순자산 없음 1종(0198B0): 합에서 뺐습니다."));

        ClosingFlow buying = f;
        buying.demand = 160163959925.0;
        QCOMPARE(view::headline(buying), QStringLiteral("매수 +1,601.6억원"));
        QCOMPARE(view::side_of(buying.demand), Side::Buy);
        QCOMPARE(view::won_text(5'000'000.0), QStringLiteral("+500만원"));
        QCOMPARE(view::won_text(0.0), QStringLiteral("0원"));
        QCOMPARE(view::won_text(std::nullopt), QStringLiteral("—"));

        ClosingFlow recordless = f;
        recordless.ratio.reset();
        recordless.ratio_total.reset();
        recordless.denominator.reset();
        recordless.denominator_days = 0;
        recordless.denominator_reason = "no_recorded_auction";
        const auto without = view::card_lines(recordless);
        QCOMPARE(without[2].value, QStringLiteral("녹화 없음"));
        QVERIFY(without[2].dim);
        QCOMPARE(without[4].value, QStringLiteral("녹화 없음"));

        ClosingFlow stored = f;
        stored.at = kst("2026-10-12T15:20:00+09:00");
        stored.as_of = kst("2026-10-12T16:00:00+09:00");
        stored.source = "stored";
        QCOMPARE(view::day_line(stored), QStringLiteral("2026-10-12(월) · 15:20 저장값"));
    }

    void the_control_symbol_has_its_reason_and_no_demand_lines() {
        const ClosingFlow control = today(control_json());
        QVERIFY(view::headline(control).contains(QStringLiteral("레버리지·인버스 ETF가 없습니다")));
        const auto lines = view::card_lines(control);
        QCOMPARE(lines.size(), 3); // the denominator, r and when
        QCOMPARE(lines[0].label, QStringLiteral("분모"));
        QVERIFY(view::notes(control).first().startsWith(QStringLiteral("대조군")));
        QVERIFY(view::product_rows(control).isEmpty());

        ClosingFlow before = control;
        before.reason = "before_open";
        QVERIFY(view::headline(before).startsWith(QStringLiteral("장 전")));
    }

    void the_product_table_marks_missing_products_and_the_inverse_coefficient() {
        const auto rows = view::product_rows(today());
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].leverage, QStringLiteral("2배"));
        QCOMPARE(rows[0].structure, QStringLiteral("현물+선물형"));
        QCOMPARE(rows[0].coefficient, QStringLiteral("2"));
        QCOMPARE(rows[0].net_assets, QStringLiteral("2,154.6억원"));
        QCOMPARE(rows[0].net_assets_date, QStringLiteral("10-08"));
        QCOMPARE(rows[0].source, QStringLiteral("KIS"));
        QCOMPARE(rows[0].demand, QStringLiteral("−106.1억원"));
        QCOMPARE(rows[1].net_assets, QStringLiteral("없음"));
        QCOMPARE(rows[1].demand, QStringLiteral("없음 (합에서 뺌)"));
        QVERIFY(rows[1].missing);
        QVERIFY(rows[1].tip.contains(QStringLiteral("합에서 뺐습니다")));
        QCOMPARE(rows[2].leverage, QStringLiteral("−2배"));
        QCOMPARE(rows[2].coefficient, QStringLiteral("6"));
        QVERIFY(rows[2].inverse);
        QVERIFY(rows[2].tip.contains(QStringLiteral("오른 날 마감 매수가 큽니다")));
    }

    void the_days_table_is_newest_first_and_says_why_a_ratio_is_missing() {
        const auto rows = view::day_rows(days(), true);
        QCOMPARE(rows.size(), 3);
        const auto& stored = rows[0];
        QCOMPARE(stored.date, QStringLiteral("2026-10-08(목)"));
        QCOMPARE(stored.demand, QStringLiteral("+569.0억원"));
        QCOMPARE(stored.source, QStringLiteral("15:20 저장"));
        QCOMPARE(stored.ratio, QStringLiteral("+0.42배 (상한)"));
        QCOMPARE(stored.auction, QStringLiteral("+24.9bp (+1틱)"));
        QCOMPARE(stored.agree, QStringLiteral("반대"));
        QCOMPARE(stored.next_gap, QStringLiteral("—"));
        QCOMPARE(stored.special, QStringLiteral("VI"));
        QVERIFY(stored.auction_tip.contains(QStringLiteral("563.6억원 (녹화한 KRX 체결(가격 × 수량))")));
        const auto& without = rows[1];
        QCOMPARE(without.demand, QStringLiteral("순자산 없음"));
        QCOMPARE(without.ratio, QStringLiteral("—"));
        QCOMPARE(without.agree, QStringLiteral("—"));
        QVERIFY(without.agree_tip.contains(QStringLiteral("그날 흐름")));
        const auto& recordless = rows[2];
        QCOMPARE(recordless.ratio, QStringLiteral("녹화 없음"));
        QVERIFY(recordless.ratio_missing);
        QVERIFY(recordless.ratio_tip.contains(QStringLiteral("2026-10-12")));
        QCOMPARE(recordless.source, QStringLiteral("1분봉 계산"));
        QCOMPARE(recordless.agree, QStringLiteral("같음"));
        QCOMPARE(recordless.auction, QStringLiteral("−46.8bp (−2틱)"));

        ClosingDays flowed = days();
        flowed.days[1].agree = true;
        QCOMPARE(view::day_rows(flowed, true)[1].agree, QStringLiteral("같음 (흐름)"));
        QCOMPARE(view::day_rows(days(), false)[1].demand, QStringLiteral("상품 없음")); // 009150
    }

    void the_summary_has_each_group_with_and_without_special_days_and_dims_thin_cells() {
        const ClosingDays d = days();
        const auto rows = view::summary_rows(d);
        QCOMPARE(rows.size(), 4); // a period and a tertile, each with and without special days
        QCOMPARE(view::summary_headers().size(), rows[0].cells.size());
        QCOMPARE(rows[0].group, QStringLiteral("애프터마켓 뒤(09-14~)"));
        QVERIFY(!rows[0].without_special);
        QVERIFY(rows[1].without_special);
        QCOMPARE(rows[0].cells[0].text, QStringLiteral("19일"));
        QVERIFY(rows[0].cells[0].thin); // 19 days: under 20
        QCOMPARE(rows[0].cells[1].text, QStringLiteral("24.9bp"));
        QCOMPARE(rows[0].cells[2].text, QStringLiteral("13.3% (15일)"));
        QVERIFY(rows[0].cells[2].thin);
        QCOMPARE(rows[0].cells[4].text, QStringLiteral("—"));
        QCOMPARE(rows[2].group, QStringLiteral("비율 3분위 1 (|비율| 0.00–0.42배)"));
        QVERIFY(!rows[2].cells[0].thin); // 25 days
        QCOMPARE(rows[2].cells[2].text, QStringLiteral("55.0% (22일)"));
        QVERIFY(!rows[2].cells[2].thin);
        QCOMPARE(rows[3].cells[2].text, QStringLiteral("—"));
        QCOMPARE(view::days_line(d), QStringLiteral("2026-10-12 06:01에 계산 · 2025-10-13 – 2026-10-12 · 3일"));
        QVERIFY(view::days_line(days(days_json("005930", QJsonValue()))).startsWith(QStringLiteral("엔진이 아직")));
    }

    void the_days_and_today_say_where_their_values_came_from() {
        // codeyoma/krx-quant#368: each denominator's days by source, each value's source, each day's net assets.
        const ClosingDays d = days(sourced_days_json());
        QCOMPARE(d.days[0].denominator_reason, QStringLiteral("no_auction_value"));
        QVERIFY(d.days[0].denominator_sources.has_value());
        QCOMPARE(d.days[0].denominator_sources->recorded, 0);
        QCOMPARE(d.days[0].auction_value_source, QStringLiteral("integrated_bar"));
        QCOMPARE(d.days[0].net_assets_source, QStringLiteral("kis"));
        QCOMPARE(d.days[1].denominator_sources->integrated_bar, 1);
        QCOMPARE(d.days[1].auction_value_source, QStringLiteral("krx_bar"));
        QVERIFY(d.days[1].net_assets_source.isEmpty()); // null: no demand that day
        QCOMPARE(d.days[2].denominator_sources->recorded, 7);
        QCOMPARE(d.days[2].denominator_sources->krx_bar, 7);
        QCOMPARE(d.days[2].auction_value_source, QStringLiteral("recorded"));
        QCOMPARE(d.days[2].net_assets_source, QStringLiteral("krx+kis"));
        // An engine before it: no counts.
        QVERIFY(!days().days[2].denominator_sources.has_value());
        QJsonObject body = today_json();
        body["denominator_sources"] = QJsonObject{{"recorded", 15}, {"krx_bar", 5}, {"integrated_bar", 0}};
        const ClosingFlow f = today(body);
        QCOMPARE(f.denominator_sources->recorded, 15);
        QCOMPARE(f.denominator_sources->krx_bar, 5);
        QVERIFY(!today().denominator_sources.has_value());
        QCOMPARE(view::denominator_sources_text(f.denominator_sources), QStringLiteral("녹화 15일 · KRX 봉 5일"));
        QVERIFY(view::denominator_sources_text(std::nullopt).isEmpty());
        QVERIFY(view::denominator_sources_text(ClosingSources{}).isEmpty());
        // The card's denominator says its days' sources; no value at all reads "체결대금 없음".
        const auto lines = view::card_lines(f);
        QCOMPARE(lines[4].value, QStringLiteral("764.9억원 (20거래일 중 15일)"));
        QVERIFY(lines[4].tip.endsWith(QStringLiteral("\n그 날들: 녹화 15일 · KRX 봉 5일")));
        ClosingFlow none = f;
        none.ratio.reset();
        none.ratio_total.reset();
        none.denominator.reset();
        none.denominator_days = 0;
        none.denominator_sources = ClosingSources{};
        none.denominator_reason = "no_auction_value";
        const auto without = view::card_lines(none);
        QCOMPARE(without[2].value, QStringLiteral("체결대금 없음"));
        QCOMPARE(without[4].value, QStringLiteral("체결대금 없음"));
        QVERIFY(without[4].tip.endsWith(QStringLiteral("그 20거래일에 종가 단일가 체결대금이 있는 날이 없습니다.")));
        QCOMPARE(view::denominator_reason_text("no_auction_value"), QStringLiteral("체결대금 없음"));
        QCOMPARE(view::denominator_reason_text("no_recorded_auction"), QStringLiteral("녹화 없음")); // older engines
        QCOMPARE(view::denominator_reason_text("not_computed"), QStringLiteral("지난날 표 계산 전"));
    }

    void the_days_table_shows_each_value_s_source_and_whose_net_assets() {
        const auto rows = view::day_rows(days(sourced_days_json()), true);
        const auto& stored = rows[0]; // 10-08: recorded, both net assets
        QCOMPARE(stored.auction_source, QStringLiteral("녹화"));
        QCOMPARE(stored.auction_source_tip,
                 QStringLiteral("그날 종가 단일가 체결대금 563.6억원: 녹화한 KRX 체결(가격 × 수량)"));
        QVERIFY(
            stored.auction_tip.endsWith(QStringLiteral("\n그날 종가 단일가 체결대금 563.6억원 (녹화한 KRX 체결(가격 × "
                                                       "수량))")));
        QCOMPARE(stored.source_tip, QStringLiteral("순자산 출처: 한국거래소 통계정보 · KIS"));
        QVERIFY(stored.ratio_tip.endsWith(QStringLiteral("\n분모의 날들: 녹화 7일 · KRX 봉 7일")));
        const auto& without = rows[1]; // 09-14: KRX's bar, no demand
        QCOMPARE(without.auction_source, QStringLiteral("KRX 봉"));
        QCOMPARE(without.auction_source_tip,
                 QStringLiteral("그날 종가 단일가 체결대금 445.6억원: KRX 1분봉의 단일가 봉(거래량 × 종가)"));
        QVERIFY(without.source_tip.isEmpty());
        const auto& first = rows[2]; // 09-11: the integrated bar, no value before it
        QCOMPARE(first.auction_source, QStringLiteral("통합 봉"));
        QCOMPARE(first.ratio, QStringLiteral("체결대금 없음"));
        QVERIFY(first.ratio_missing);
        QCOMPARE(first.ratio_tip,
                 QStringLiteral("그날 전 20거래일에 종가 단일가 체결대금이 있는 날이 없어 분모가 없습니다."));
        QCOMPARE(first.source_tip, QStringLiteral("순자산 출처: KIS"));
        // Without a value: "—", and the tooltip says neither the recording nor a bar had one.
        const auto& older = view::day_rows(days(), true)[2];
        QCOMPARE(older.auction_source, QStringLiteral("—"));
        QVERIFY(older.auction_source_tip.startsWith(QStringLiteral("그날 종가 단일가 체결대금이 없습니다")));
        QCOMPARE(view::auction_source_label("something_new"), QStringLiteral("something_new"));
    }

    void the_krx_label_shows_whenever_a_value_shown_is_krx_s() {
        QVERIFY(!view::shows_krx(today(), days())); // KIS's only
        QVERIFY(!view::shows_krx(std::nullopt, std::nullopt));
        QVERIFY(view::shows_krx(today(today_json("005930", "krx")), std::nullopt));
        QVERIFY(view::shows_krx(today(today_json("005930", "krx_openapi")), std::nullopt));
        ClosingDays filled = days();
        filled.days[0].net_assets_source = "krx";
        QVERIFY(view::shows_krx(std::nullopt, filled));
        QCOMPARE(view::source_label("kis"), QStringLiteral("KIS"));
        QCOMPARE(view::source_label("krx"), QString::fromUtf8(view::kKrxLabel));
        QCOMPARE(view::product_rows(today(today_json("005930", "krx")))[0].source, QString::fromUtf8(view::kKrxLabel));
        QVERIFY(view::source_label({}).isEmpty());
        // A day whose products read both (codeyoma/krx-quant#368) is KRX's too; the engine's days now say it.
        QCOMPARE(view::source_label("krx+kis"), QStringLiteral("한국거래소 통계정보 · KIS"));
        ClosingDays both = days();
        both.days[2].net_assets_source = "krx+kis";
        QVERIFY(view::shows_krx(today(), both));
        QVERIFY(view::shows_krx(today(), days(sourced_days_json())));
        ClosingDays kis = days(sourced_days_json());
        kis.days[2].net_assets_source = "kis";
        QVERIFY(!view::shows_krx(today(), kis)); // KIS's and none only
    }

    void it_reads_every_minute_in_continuous_trading_and_otherwise_at_the_open_the_evening_and_midnight() {
        const ClosingFlow live = today(); // the minute's demand
        QCOMPARE(view::poll_ms(live, days(), false, kst("2026-10-12T10:30:20+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(std::nullopt, std::nullopt, false, kst("2026-10-12T10:30:20+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(live, days(), true, kst("2026-10-12T22:00:00+09:00")), view::kFastMs); // failed

        ClosingFlow early = live; // before the open: at 09:00
        early.at = QDateTime();
        early.as_of = kst("2026-10-12T08:00:00+09:00");
        QCOMPARE(view::poll_ms(early, days(), false, kst("2026-10-12T08:00:00+09:00")), (3600 + 5) * 1000);
        QCOMPARE(view::poll_ms(early, days(), false, kst("2026-10-12T09:30:00+09:00")), view::kFastMs); // a late open

        ClosingFlow stored = live; // after 15:20: at 20:35, for the day's row
        stored.at = kst("2026-10-12T15:20:00+09:00");
        stored.as_of = kst("2026-10-12T16:00:00+09:00");
        stored.source = "stored";
        QCOMPARE(view::poll_ms(stored, days(), false, kst("2026-10-12T20:00:00+09:00")), 35 * 60 * 1000);
        QCOMPARE(view::poll_ms(stored, days(), false, kst("2026-10-12T20:40:00+09:00")), view::kSlowMs);
        QCOMPARE(view::poll_ms(stored, days(), false, kst("2026-10-12T23:55:00+09:00")), (5 * 60 + 5) * 1000);
        const ClosingDays computed = days(days_json("005930", "2026-10-12T20:41:12+09:00"));
        QCOMPARE(view::poll_ms(stored, computed, false, kst("2026-10-12T20:45:00+09:00")),
                 ((3 * 60 + 15) * 60 + 5) * 1000); // in: when the day changes
        // A day without trading shows the last trading day's: when the day changes.
        QCOMPARE(view::poll_ms(stored, days(), false, kst("2026-10-13T12:00:00+09:00")), (12 * 3600 + 5) * 1000);

        // The days: read again for another symbol, on another day, and in the evening until computed.
        const QDateTime morning = kst("2026-10-12T10:31:00+09:00");
        QVERIFY(view::days_due(std::nullopt, "005930", {}, live, morning));
        QVERIFY(!view::days_due(days(), "005930", morning, live, morning.addSecs(60)));
        QVERIFY(view::days_due(days(), "000660", morning, live, morning.addSecs(60)));
        QVERIFY(view::days_due(days(), "005930", morning, live, kst("2026-10-13T00:00:05+09:00")));
        QVERIFY(view::days_due(days(), "005930", morning, stored, kst("2026-10-12T20:35:00+09:00")));
        QVERIFY(!view::days_due(computed, "005930", kst("2026-10-12T20:45:00+09:00"), stored,
                                kst("2026-10-12T20:55:00+09:00")));
    }

    void the_screen_reads_the_symbols_then_today_and_the_days() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        AppConfig::instance().remove(services::KrxClosingLive::kSymbolKey);
        QDateTime clock = kst("2026-10-12T10:30:20+09:00");
        services::KrxClosingLive live(nullptr, [&clock] { return clock; });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.flow().has_value() && live.days().has_value(), 10000);
        QCOMPARE(engine.asked.first(), QStringLiteral("/v1/symbols")); // which symbol, and the names
        QCOMPARE(live.symbol(), QStringLiteral("005930"));             // none chosen: the engine's first
        QVERIFY(engine.asked.contains("/v1/symbols/005930/closing-flow"));
        QVERIFY(engine.asked.contains("/v1/symbols/005930/closing-flow/days"));
        QCOMPARE(live.symbols().size(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        QCOMPARE(live.next_ms(), view::kFastMs); // continuous trading

        // The next minute's read asks for today only: the days change once a day.
        clock = clock.addSecs(60);
        fire_schedule(live);
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("/005930/closing-flow"), 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        QCOMPARE(engine.count("/005930/closing-flow/days"), 1);
        // On a new day, the days too.
        clock = kst("2026-10-13T00:00:05+09:00");
        fire_schedule(live);
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("/005930/closing-flow/days"), 2, 10000);

        live.set_symbol("009150");
        QVERIFY(!live.flow().has_value());
        QVERIFY(!live.days().has_value());
        QTRY_VERIFY_WITH_TIMEOUT(live.flow().has_value() && live.flow()->symbol == "009150" &&
                                     live.days().has_value() && live.days()->symbol == "009150",
                                 10000);
        QCOMPARE(live.flow()->reason, QStringLiteral("no_products"));
        QCOMPARE(AppConfig::instance().get(services::KrxClosingLive::kSymbolKey).toString(), QStringLiteral("009150"));

        engine.fail = true;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(!live.failure().isEmpty(), 10000);
        QVERIFY(live.flow().has_value()); // the last answer stays
        QVERIFY(live.days().has_value());
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        QCOMPARE(live.next_ms(), view::kFastMs); // a failure is asked again in a minute
        live.stop();
        QCOMPARE(live.next_ms(), 0);

        // The symbol kept is read first next time.
        services::KrxClosingLive again;
        QCOMPARE(again.symbol(), QStringLiteral("009150"));
    }

    void a_symbol_changed_while_reading_drops_the_old_answers() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        AppConfig::instance().set(services::KrxClosingLive::kSymbolKey, QStringLiteral("005930"));
        services::KrxClosingLive live(nullptr, [] { return kst("2026-10-12T10:30:20+09:00"); });
        bool switched = false;
        // As soon as today and the days of 005930 are asked, another symbol is chosen.
        connect(&live, &services::KrxClosingLive::changed, this, [&live, &switched] {
            if (!switched && live.reading()) {
                switched = true;
                live.set_symbol("000660");
            }
        });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.flow().has_value() && live.days().has_value(), 10000);
        QCOMPARE(live.flow()->symbol, QStringLiteral("000660"));
        QCOMPARE(live.days()->symbol, QStringLiteral("000660"));
        QVERIFY(switched);
        QVERIFY(engine.asked.contains("/v1/symbols/005930/closing-flow")); // asked first, its answer dropped
        QVERIFY(engine.asked.contains("/v1/symbols/000660/closing-flow/days"));
        live.stop();
    }

    void failed_days_are_asked_again_with_the_next_read() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        AppConfig::instance().set(services::KrxClosingLive::kSymbolKey, QStringLiteral("005930"));
        const QDateTime clock = kst("2026-10-12T10:30:20+09:00");
        services::KrxClosingLive live(nullptr, [&clock] { return clock; });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.days().has_value() && !live.reading(), 10000);
        engine.fail_days = true;
        live.read_now(); // the days asked again, and they fail
        QTRY_VERIFY_WITH_TIMEOUT(live.failure().contains(QStringLiteral("지난날")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        QVERIFY(live.days().has_value()); // the last answer stays
        const qsizetype asked = engine.count("/005930/closing-flow/days");
        engine.fail_days = false;
        fire_schedule(live); // the same day's next read: the days only because they failed
        QTRY_COMPARE_WITH_TIMEOUT(engine.count("/005930/closing-flow/days"), asked + 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(live.failure().isEmpty(), 10000);
        live.stop();
    }

    void without_an_address_it_says_where_to_set_it() {
        services::KrxQuantService::set_base_url(QString());
        services::KrxClosingLive live;
        live.start();
        QVERIFY(live.failure().contains(QStringLiteral("엔진 주소")));
        live.stop();
    }
};

QTEST_GUILESS_MAIN(TstKrxClosing)
#include "tst_krx_closing.moc"
