// Tests for the KRX horizon and cost screen's model (codeyoma/krx-quant#307): how it reads GET
// /v1/cost-map (src/services/krx_quant/EngineData.h, parse_cost_map), what it shows of it
// (src/services/krx_quant/CostView.h) and when it reads it (src/services/krx_quant/KrxCostLive.h), the
// last against a stand-in engine on this computer. The settings (the engine's address, the choices) go
// to a temporary INI file: AppConfig is defined below, in place of core/config/AppConfig.cpp.

#include "core/config/AppConfig.h"
#include "services/krx_quant/CostView.h"
#include "services/krx_quant/KrxCostLive.h"
#include "services/krx_quant/KrxQuantService.h"

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
namespace view = fincept::krx_quant::cost;
using view::Cost;
using view::Role;
using view::Verdict;

namespace {

/// An answer as the demo engine gives it (trimmed): two symbols, the direction, a 15-minute barrier
/// and open to close, and two base-rate rows.
const char* kCostMap = R"json({
  "symbol": "all", "period": "20d", "special": "exclude", "as_of": "2026-10-12T10:30:00+09:00",
  "min_sample": 200, "fee_rate": 0.00015, "spread_ticks": 1,
  "symbols": [
    {"symbol": "000660", "name": "SK하이닉스", "price": 209000.0, "price_at": "2026-10-12T10:30:00+09:00", "live": true,
     "costs": {"tick": 500, "tick_bp": 23.92, "k": 1, "sell_tax_bp": 20.0, "fees_bp": 3.0, "spread_bp": 23.92,
               "limit": {"bp": 23.0, "ticks": 0.9614}, "market": {"bp": 46.92, "ticks": 1.9614},
               "inverse": {"leverage": 2, "listed": "2026-05-27", "bp": 1.5, "ticks": 0.0627, "omits": ["spread", "premium", "basis"]},
               "below": {"edge": 200000, "price": 199900, "tick": 100, "k": 5},
               "above": {"edge": 500000, "price": 500000, "tick": 1000, "k": 2}}},
    {"symbol": "009150", "name": "삼성전기", "price": 149250.0, "price_at": "2026-10-12T15:15:00+09:00", "live": false,
     "costs": {"tick": 100, "tick_bp": 6.70, "k": 4, "sell_tax_bp": 20.0, "fees_bp": 3.0, "spread_bp": 6.70,
               "limit": {"bp": 23.0, "ticks": 3.43}, "market": {"bp": 29.7, "ticks": 4.43},
               "inverse": null, "below": {"edge": 50000, "price": 49950, "tick": 50, "k": 3},
               "above": {"edge": 200000, "price": 200000, "tick": 500, "k": 1}}}
  ],
  "rows": [
    {"kind": "direction", "title": "방향", "horizon_minutes": 4, "rule": "4분 도달", "days": ["2026-10-08", "2026-10-12"],
     "unit": "ticks", "bar_closes": 606, "width": 2, "reference_price": 76350.0, "tick": 100,
     "cost": {"limit": 1.756, "market": 2.756},
     "breakeven": {"limit": {"c_over_w": 0.878, "p_star": 0.939}, "market": {"c_over_w": 1.378, "p_star": 1.189}},
     "breakeven_missing": null,
     "consensus": {"algorithm": "consensus-final", "rank": 2, "calls": 409, "precision": {"value": 0.633, "low": 0.584, "high": 0.68, "n": 409},
                   "edge": 0.284, "pnl": {"trades": 409, "per_trade": {"value": -4.79, "low": -5.78, "high": -3.81, "n": 409}},
                   "pnl_inverse": {"trades": 300, "per_trade": {"value": -1.2, "low": -2.0, "high": -0.4, "n": 300}},
                   "verdict": {"limit": "fail", "market": "fail"}},
     "top": null,
     "baseline": {"algorithm": "baseline-persist", "rank": null, "calls": 150, "precision": {"value": 0.525, "low": 0.45, "high": 0.6, "n": 150},
                  "edge": 0.105, "pnl": null, "pnl_inverse": null, "verdict": {"limit": "few", "market": "few"}}},
    {"kind": "barrier_15m", "title": "장벽 15분", "horizon_minutes": 15, "rule": "15분 장벽", "days": ["2026-10-12"],
     "unit": "ticks", "bar_closes": 501, "width": 12.0, "reference_price": 79750.0, "tick": 100,
     "cost": {"limit": 1.834, "market": 2.834},
     "breakeven": {"limit": {"c_over_w": 0.153, "p_star": 0.576}, "market": {"c_over_w": 0.236, "p_star": 0.618}},
     "breakeven_missing": null, "consensus": null,
     "top": {"algorithm": "breiman-15m", "rank": 1, "calls": 240, "precision": {"value": 0.66, "low": 0.6, "high": 0.72, "n": 240},
             "edge": 0.41, "pnl": {"trades": 240, "per_trade": {"value": 0.8, "low": -0.2, "high": 1.8, "n": 240}},
             "pnl_inverse": null, "verdict": {"limit": "pass", "market": "fail"}},
     "baseline": null},
    {"kind": "open_to_close", "title": "시가→종가", "horizon_minutes": null, "rule": "시가 → 종가", "days": [],
     "unit": "bp", "bar_closes": 57, "width": 51.4, "reference_price": 148550.0, "tick": null,
     "cost": {"limit": 23.0, "market": 23.0}, "breakeven": null, "breakeven_missing": "대칭 장벽이 아님",
     "consensus": null, "top": null,
     "baseline": {"algorithm": "otc-climate", "rank": null, "calls": 38, "precision": {"value": 0.474, "low": 0.32, "high": 0.63, "n": 38},
                  "edge": 0.158, "pnl": {"trades": 38, "per_trade": {"value": -17.77, "low": -55.7, "high": 20.17, "n": 38}},
                  "pnl_inverse": null, "verdict": null}}
  ],
  "regimes": {"computed_at": "2026-10-11T20:30:41+09:00", "first_day": "2025-10-13", "last_day": "2026-10-08",
              "rows": [{"symbol": "005930", "period": "상장 전(~05-26)", "from": null, "to": "2026-05-26", "days": 238, "windows": 89250,
                        "up": 0.056, "flat": 0.893, "down": 0.051, "k": 2, "persist": 0.933, "persist_windows": 88298},
                       {"symbol": "005930", "period": "애프터마켓 뒤(09-14~)", "from": "2026-09-14", "to": null, "days": 20, "windows": 7500,
                        "up": 0.259, "flat": 0.503, "down": 0.238, "k": 2, "persist": null, "persist_windows": 0}]},
  "regimes_missing": null
})json";

CostMap parsed(const QByteArray& body = kCostMap) {
    auto m = parse_cost_map(QJsonDocument::fromJson(body));
    if (m.is_err())
        qFatal("%s", m.error().c_str());
    return m.value();
}

/// The engine as the cost screen reads it: GET /v1/symbols, /v1/kinds, /v1/algorithms and
/// /v1/cost-map, the paths asked kept.
class StandIn : public QObject {
  public:
    bool fail = false;
    int kinds_failures = 0;   ///< GET /v1/kinds fails this many times first
    bool stale_stats = false; ///< a predictor's stats answer another query's (every symbol)
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
        int status = 404;
        QByteArray bytes = R"({"error": "not found"})";
        if (path == "/v1/symbols") {
            status = 200;
            bytes =
                R"({"symbols": [{"symbol": "000660", "name": "SK하이닉스"}, {"symbol": "009150", "name": "삼성전기"}]})";
        } else if (path == "/v1/kinds" && kinds_failures > 0) {
            --kinds_failures;
            status = 500;
            bytes = R"({"error": "made-up failure"})";
        } else if (path == "/v1/kinds") {
            status = 200;
            bytes = R"({"kinds": [{"name": "direction", "title": "방향", "classes": ["up", "flat", "down"],
                        "class_titles": ["상승", "보합", "하락"], "action": ["up", "down"], "answer_version": 3,
                        "horizon_minutes": 4, "fields": {}, "baselines": [], "daily": null}]})";
        } else if (path == "/v1/algorithms") {
            status = 200;
            bytes = R"({"algorithms": [{"name": "alpha", "kind": "direction"}], "baselines": [], "consensus": []})";
        } else if (path.startsWith("/v1/algorithms/")) { // a predictor's stats, of the query asked
            status = 200;
            QJsonObject body{{"algorithm", path.section('/', 3, 3)}, {"overall", QJsonObject{}}};
            for (const auto& item : path.section('?', 1).split('&'))
                if (const QString key = item.section('=', 0, 0); key == "symbol" || key == "period" || key == "special")
                    body[key] = item.section('=', 1);
            if (stale_stats)
                body["symbol"] = "all";
            bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        } else if (path.startsWith("/v1/cost-map") && fail) {
            status = 500;
            bytes = R"({"error": "made-up failure"})";
        } else if (path.startsWith("/v1/cost-map")) {
            status = 200;
            QJsonObject body = QJsonDocument::fromJson(kCostMap).object();
            const QString query = path.section('?', 1);
            for (const auto& item : query.split('&')) // the answer echoes the query, as the engine's
                if (const QString key = item.section('=', 0, 0); key == "symbol" || key == "period" || key == "special")
                    body[key] = item.section('=', 1);
            bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        }
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                      "\r\nConnection: close\r\n\r\n" + bytes);
        socket->disconnectFromHost();
    }

    QTcpServer server_;
};

} // namespace

class TstKrxCost : public QObject {
    Q_OBJECT

    QTemporaryDir settings_;

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        settings_file = settings_.filePath("settings.ini");
    }

    void the_map_is_read_part_by_part() {
        const CostMap m = parsed();
        QCOMPARE(m.period, QStringLiteral("20d"));
        QCOMPARE(m.min_sample, 200);
        QCOMPARE(m.symbols.size(), 2);
        const CostSymbol& hynix = m.symbols[0];
        QVERIFY(hynix.live && hynix.price && *hynix.price == 209000);
        QVERIFY(hynix.costs && hynix.costs->inverse);
        QCOMPARE(hynix.costs->inverse->omits, (QStringList{"spread", "premium", "basis"}));
        QCOMPARE(hynix.costs->below->k, 5);
        QVERIFY(!m.symbols[1].costs->inverse);
        QCOMPARE(m.rows.size(), 3);
        const CostRow& direction = m.rows[0];
        QCOMPARE(direction.days.size(), 2);
        QVERIFY(direction.breakeven_market && direction.breakeven_market->p_star > 1);
        QVERIFY(!direction.top);
        QCOMPARE(direction.consensus->rank, std::optional<int>(2));
        QVERIFY(!direction.baseline->pnl);
        const CostRow& otc = m.rows[2];
        QVERIFY(!otc.horizon_minutes && !otc.breakeven_limit && !otc.tick);
        QCOMPARE(otc.breakeven_missing, QStringLiteral("대칭 장벽이 아님"));
        QVERIFY(otc.baseline->verdict_limit.isEmpty());
        QVERIFY(m.regimes && m.regimes->rows.size() == 2);
        QVERIFY(!m.regimes->rows[0].from.isValid() && !m.regimes->rows[1].persist);
    }

    void the_cards_say_each_cost_and_where_k_cannot_pay() {
        const CostMap m = parsed();
        const auto hynix = view::card_lines(m.symbols[0]);
        QCOMPARE(view::card_title(m.symbols[0]), QStringLiteral("SK하이닉스 000660"));
        QCOMPARE(hynix[0].value, QStringLiteral("209,000원 (10:30 실시간)"));
        QCOMPARE(hynix[1].value, QStringLiteral("500원 = 23.9bp"));
        QCOMPARE(hynix[6].value, QStringLiteral("46.9bp = 1.96틱"));
        QVERIFY(hynix[7].warn); // k 1 under the market round trip's 1.96 ticks
        QCOMPARE(hynix[8].value, QStringLiteral("1.5bp = 0.06틱 (하한)"));
        QVERIFY(hynix[8].tip.contains(QStringLiteral("basis")));
        QCOMPARE(hynix[9].value, QStringLiteral("200,000원 → 틱 100원, k 5"));
        const auto electro = view::card_lines(m.symbols[1]);
        QVERIFY(electro[0].value.contains(QStringLiteral("마지막 봉 마감")));
        QVERIFY(electro[7].warn); // k 4 under its market round trip's 4.43 ticks too
        QCOMPARE(electro[8].value, QStringLiteral("거래 수단 없음"));
        CostSymbol none;
        none.symbol = "005930";
        QCOMPARE(view::card_lines(none).size(), 1);
    }

    void the_rows_show_the_breakeven_and_each_predictor_by_its_lower_bound() {
        const CostMap m = parsed();
        const CostRow& direction = m.rows[0];
        QCOMPARE(view::horizon_text(direction), QStringLiteral("방향 4분"));
        QCOMPARE(view::horizon_text(m.rows[1]), QStringLiteral("장벽 15분"));
        QCOMPARE(view::horizon_text(m.rows[2]), QStringLiteral("시가→종가 (하루)"));
        QCOMPARE(view::width_text(direction), QStringLiteral("2틱"));
        QCOMPARE(view::width_text(m.rows[2]), QStringLiteral("51.4bp"));
        QCOMPARE(view::cost_text(direction), QStringLiteral("지정가 1.76 · 시장가 2.76틱"));
        QCOMPARE(view::cost_text(m.rows[2]), QStringLiteral("23.0bp (단일가)"));
        QCOMPARE(view::breakeven_text(direction, Cost::Limit), QStringLiteral("p* 93.9% · C/W 0.88"));
        QCOMPARE(view::breakeven_text(direction, Cost::Market), QStringLiteral("p* 118.9% · C/W 1.38 (불가능)"));
        QCOMPARE(view::breakeven_text(m.rows[2], Cost::Market), QStringLiteral("해당 없음"));
        QVERIFY(view::breakeven_tip(m.rows[2]).contains(QStringLiteral("대칭 장벽이 아님")));
        QVERIFY(view::breakeven_tip(direction).contains(QStringLiteral("606")));

        const auto consensus = view::predictor_cell(direction, Role::Consensus, Cost::Market, m.min_sample);
        QCOMPARE(consensus.verdict, Verdict::Fail);
        QCOMPARE(consensus.text, QStringLiteral("consensus-final\n63.3% (하한 58.4%)\nedge +0.28 · 신호 409\n"
                                                "손익 -4.79틱 (연구용)"));
        QVERIFY(consensus.tip.contains(QStringLiteral("인버스")));
        const auto top = view::predictor_cell(direction, Role::Top, Cost::Market, m.min_sample);
        QCOMPARE(top.verdict, Verdict::None);
        QCOMPARE(top.text, QStringLiteral("순위에 든 알고리즘 없음"));
        const auto few = view::predictor_cell(direction, Role::Baseline, Cost::Limit, m.min_sample);
        QCOMPARE(few.verdict, Verdict::Few);
        QVERIFY(few.tip.contains(QStringLiteral("150개 < 최소 표본 200")));
        // The barrier passes at the limit's p* and fails at the market's.
        QCOMPARE(view::predictor_cell(m.rows[1], Role::Top, Cost::Limit, 200).verdict, Verdict::Pass);
        QCOMPARE(view::predictor_cell(m.rows[1], Role::Top, Cost::Market, 200).verdict, Verdict::Fail);
        QCOMPARE(view::predictor_cell(m.rows[1], Role::Consensus, Cost::Market, 200).text, QStringLiteral("합의 없음"));
        QCOMPARE(view::predictor_cell(m.rows[2], Role::Baseline, Cost::Market, 200).verdict, Verdict::None);
        QVERIFY(view::predictor_cell(m.rows[2], Role::Baseline, Cost::Market, 200).text.contains("-17.77bp"));
    }

    void the_base_rates_and_the_lines_above_the_tables() {
        const CostMap m = parsed();
        const auto lines = view::regime_lines(*m.regimes);
        QCOMPARE(lines[0].windows, QStringLiteral("89,250"));
        QCOMPARE(lines[0].flat, QStringLiteral("89.3%"));
        QCOMPARE(lines[0].persist, QStringLiteral("93.3% (n 88,298)"));
        QCOMPARE(lines[1].persist, QStringLiteral("—"));
        QCOMPARE(view::regimes_line(m), QStringLiteral("계산 10-11 20:30 · 1분봉 2025-10-13 – 2026-10-08"));
        CostMap missing = m;
        missing.regimes.reset();
        missing.regimes_missing = QStringLiteral("기간 기준율을 아직 계산하지 않았습니다");
        QCOMPARE(view::regimes_line(missing), missing.regimes_missing);
        QCOMPARE(view::as_of_line(m),
                 QStringLiteral("2026-10-12 10:30 기준 · 20일 · 최소 표본 200 · 스프레드 1틱 가정"));
    }

    void it_reads_every_minute_while_a_price_is_live_and_seldom_otherwise() {
        QCOMPARE(view::poll_ms(std::nullopt), view::kFastMs);
        CostMap m = parsed();
        QCOMPARE(view::poll_ms(m), view::kFastMs);
        m.symbols[0].live = false;
        QCOMPARE(view::poll_ms(m), view::kSlowMs);
    }

    void the_screen_reads_the_symbols_then_the_map_of_the_choice() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        for (const char* key : {services::KrxCostLive::kSymbolKey, services::KrxCostLive::kPeriodKey,
                                services::KrxCostLive::kSpecialKey, services::KrxCostLive::kCostKey})
            AppConfig::instance().remove(key);
        services::KrxCostLive live;
        QCOMPARE(live.cost(), Cost::Market); // by default: the market's round trip
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.map().has_value(), 10000);
        // The symbols first (their names, and whether the choice is known), with the kinds and algorithms.
        const auto first_map = std::find_if(engine.asked.cbegin(), engine.asked.cend(),
                                            [](const QString& path) { return path.startsWith("/v1/cost-map"); });
        QVERIFY(engine.asked.indexOf("/v1/symbols") < first_map - engine.asked.cbegin());
        QVERIFY(engine.asked.contains("/v1/kinds"));
        QVERIFY(engine.asked.contains("/v1/cost-map?symbol=all&period=today&special=exclude"));
        QCOMPARE(live.symbols().size(), 2);

        auto query = live.query();
        query.symbol = "000660";
        query.period = "20d";
        query.special = true;
        live.set_query(query);
        QVERIFY(!live.map().has_value());
        QTRY_VERIFY_WITH_TIMEOUT(live.map().has_value() && live.map()->symbol == "000660", 10000);
        QVERIFY(engine.asked.contains("/v1/cost-map?symbol=000660&period=20d&special=include"));
        live.set_cost(Cost::Limit);

        engine.fail = true;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(!live.failure().isEmpty(), 10000);
        QVERIFY(live.map().has_value()); // the last answer stays
        live.stop();
        QCOMPARE(live.next_ms(), 0);

        // The choices kept are read first next time.
        services::KrxCostLive again;
        QCOMPARE(again.query().symbol, QStringLiteral("000660"));
        QCOMPARE(again.query().period, QStringLiteral("20d"));
        QVERIFY(again.query().special);
        QCOMPARE(again.cost(), Cost::Limit);
    }

    void a_symbol_the_engine_does_not_predict_falls_back_to_every_symbol() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        AppConfig::instance().set(services::KrxCostLive::kSymbolKey, QStringLiteral("123456"));
        services::KrxCostLive live;
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.map().has_value(), 10000);
        QCOMPARE(live.query().symbol, QStringLiteral("all"));
        QVERIFY(!engine.asked.join(' ').contains("123456"));
        live.stop();
    }

    void the_kinds_are_asked_again_until_they_are_in() {
        StandIn engine;
        engine.kinds_failures = 1;
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxCostLive live;
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.map().has_value(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(engine.asked.count("/v1/kinds"), 1, 10000);
        QVERIFY(live.kinds().isEmpty());
        live.read_now(); // the next read asks again
        QTRY_VERIFY_WITH_TIMEOUT(!live.kinds().isEmpty(), 10000);
        QVERIFY(live.kind_info("direction").has_value());
        // The map asked with them answered first: a read asked while it is under way joins it.
        QTRY_VERIFY_WITH_TIMEOUT(!live.reading(), 10000);
        const qsizetype asked = engine.asked.count("/v1/kinds");
        const auto maps = [&engine] {
            return std::count_if(engine.asked.cbegin(), engine.asked.cend(),
                                 [](const QString& path) { return path.startsWith("/v1/cost-map"); });
        };
        const auto before = maps();
        live.read_now(); // in: not asked again
        QTRY_VERIFY_WITH_TIMEOUT(maps() > before, 10000);
        QTest::qWait(200);
        QCOMPARE(engine.asked.count("/v1/kinds"), asked);
        live.stop();
    }

    void an_open_detail_window_follows_the_query_and_drops_an_older_one_s_stats() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        AppConfig::instance().remove(services::KrxCostLive::kSymbolKey);
        services::KrxCostLive live;
        QList<AlgorithmStats> shown;
        connect(&live, &services::KrxCostLive::stats_ready, &live,
                [&shown](const AlgorithmStats& stats) { shown << stats; });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.map().has_value(), 10000);
        live.set_details({"alpha"});
        QTRY_COMPARE_WITH_TIMEOUT(shown.size(), 1, 10000);
        QCOMPARE(shown[0].algorithm, QStringLiteral("alpha"));

        engine.stale_stats = true; // answers for every symbol while 000660 is asked
        auto query = live.query();
        query.symbol = "000660";
        live.set_query(query);
        const QString asked =
            "/v1/algorithms/alpha/stats?symbol=000660&period=" + query.period + "&special=" + query.special_value();
        QTRY_VERIFY_WITH_TIMEOUT(live.map().has_value() && live.map()->symbol == "000660", 10000);
        QTRY_VERIFY_WITH_TIMEOUT(engine.asked.contains(asked), 10000); // the window follows the query
        QTest::qWait(300);
        QCOMPARE(shown.size(), 1); // the older query's answers are dropped

        engine.stale_stats = false;
        live.read_stats("alpha");
        QTRY_VERIFY_WITH_TIMEOUT(shown.size() >= 2, 10000);
        for (qsizetype i = 1; i < shown.size(); ++i)
            QCOMPARE(shown[i].symbol, QStringLiteral("000660"));
        live.set_details({});
        live.stop();
    }

    void without_an_address_it_says_where_to_set_it() {
        services::KrxQuantService::set_base_url(QString());
        services::KrxCostLive live;
        live.start();
        QVERIFY(live.failure().contains(QStringLiteral("엔진 주소")));
        live.stop();
    }
};

QTEST_GUILESS_MAIN(TstKrxCost)
#include "tst_krx_cost.moc"
