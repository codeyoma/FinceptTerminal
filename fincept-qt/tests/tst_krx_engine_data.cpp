// Unit tests for src/services/krx_quant/EngineData.h
//
// The KRX prediction screen shows what the prediction engine's API (v1) answers: its
// status, the symbols with their reference price, and each symbol's latest predictions.
// These tests pin how the answers are read, including what is missing: null values,
// predictions that were not made, a live-only field and failed requests. The fixtures
// hold only the fields the screen reads.

#include "services/krx_quant/EngineData.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QTest>

using namespace fincept::krx_quant;

namespace {

QJsonDocument json(const char* text) {
    return QJsonDocument::fromJson(QByteArray(text));
}

const char* kStatus = R"({
  "mode": "live",
  "now": "2026-10-08T10:30:05+09:00",
  "phase": "continuous",
  "last_bar_close": "2026-10-08T10:30:00+09:00",
  "pending_outcomes": 96,
  "special": [
    {"kind": "vi", "symbol": "000660", "start": "2026-10-08T09:40:10+09:00", "end": "2026-10-08T09:42:20+09:00"},
    {"kind": "sidecar", "symbol": null, "start": "2026-10-08T10:01:00+09:00", "end": "2026-10-08T10:06:00+09:00"}
  ],
  "session": {"open": "2026-10-08T09:00:00+09:00", "close": "2026-10-08T15:30:00+09:00"},
  "training": {"day": "2026-10-07", "applied": true, "reason": "applied", "promoted": {"alg-b": "alg-b-2"}},
  "kis": {"connected": true, "feeds": [
    {"feed": "H0UNCNT0", "key": "005930", "last_received_at": "2026-10-08T10:30:03+09:00", "seconds_since": 2.0, "gap": null},
    {"feed": "H0UNMBC0", "key": "000660", "last_received_at": "2026-10-08T10:14:05+09:00", "seconds_since": 960.0, "gap": "stale"},
    {"feed": "H0IFCNT0", "key": "A01612", "last_received_at": null, "seconds_since": null, "gap": "disconnected: closed"}
  ]},
  "usd_futures": {"price": 1395.2, "at": "2026-10-08T10:29:58+09:00", "previous_close": 1388.3, "change_pct": 0.497}
})";

const char* kMacro = R"json({
  "day": "2026-10-12", "decided_at": "2026-10-12T07:30:02+09:00", "regime": "risk_off",
  "reasons": ["SOX -1.4% (-1% 이하)"],
  "readings": [
    {"indicator": "sox", "label": "필라델피아 반도체지수", "source": "yahoo", "series": "^SOX",
     "as_of": "2026-10-09", "value": 12620.8, "change_pct": -1.4},
    {"indicator": "usdf", "label": "달러선물", "source": "kis", "series": "175",
     "as_of": "2026-10-09", "value": 1392.2, "change_pct": 0.3},
    {"indicator": "vix", "label": "VIX", "source": "yahoo", "series": "^VIX",
     "as_of": null, "value": null, "change_pct": null}
  ],
  "failed": {"fred": "HTTP 503"},
  "skipped": ["ecos"]
})json";

const char* kLatest = R"({
  "symbol": "005930",
  "bar_close": "2026-10-08T10:30:00+09:00",
  "predictions": [
    {"algorithm": "baseline-flat", "model_version": "v1", "status": "ok",
     "p_up": 0.0, "p_flat": 1.0, "p_down": 0.0, "expected_ticks": 0.0},
    {"algorithm": "alg-a", "model_version": "alg-a-1", "status": "ok",
     "p_up": 0.52, "p_flat": 0.10, "p_down": 0.38, "expected_ticks": 1.42},
    {"algorithm": "consensus-fast", "model_version": "w1", "status": "ok",
     "p_up": 0.47, "p_flat": 0.11, "p_down": 0.42, "expected_ticks": 0.47},
    {"algorithm": "alg-b", "model_version": "alg-b-1", "status": "timed_out",
     "p_up": null, "p_flat": null, "p_down": null, "expected_ticks": null},
    {"algorithm": "consensus-final", "model_version": "w1", "status": "ok",
     "p_up": 0.20, "p_flat": 0.70, "p_down": 0.10, "expected_ticks": 0.1}
  ]
})";

// The consensus scored on both bases, an algorithm scored on basis A only (the mid-point at
// its display time is unknown), and a prediction not made: no target, no outcome.
const char* kHistory = R"({
  "symbol": "005930",
  "predictions": [
    {"algorithm": "consensus-final", "model_version": "w1",
     "bar_close": "2026-10-08T10:26:00+09:00", "displayed_at": "2026-10-08T10:26:00.180000+09:00",
     "target_at": "2026-10-08T10:30:00+09:00", "status": "ok",
     "p_up": 0.62, "p_flat": 0.30, "p_down": 0.08, "expected_ticks": 1.2,
     "outcome": {"mid_close": 81250.0, "mid_display": 81250.0, "mid_target": 81550.0, "flat_ticks": 2,
                 "class_a": "up", "class_b": "up", "special": false, "unscorable": null,
                 "scored_at": "2026-10-08T10:30:00.003000+09:00"}},
    {"algorithm": "alg-a", "model_version": "alg-a-1",
     "bar_close": "2026-10-08T10:25:00+09:00", "displayed_at": "2026-10-08T10:25:00.090000+09:00",
     "target_at": "2026-10-08T10:29:00+09:00", "status": "ok",
     "p_up": 0.2, "p_flat": 0.5, "p_down": 0.3, "expected_ticks": -0.4,
     "outcome": {"mid_close": 81300.0, "mid_display": null, "mid_target": 81100.0, "flat_ticks": 2,
                 "class_a": "down", "class_b": null, "special": true, "unscorable": "missing mid: display",
                 "scored_at": "2026-10-08T10:29:00.002000+09:00"}},
    {"algorithm": "alg-b", "model_version": "alg-b-1",
     "bar_close": "2026-10-08T10:25:00+09:00", "displayed_at": "2026-10-08T10:25:00.001000+09:00",
     "target_at": null, "status": "no_book",
     "p_up": null, "p_flat": null, "p_down": null, "expected_ticks": null, "outcome": null}
  ]
})";

// Ranked: one beating every baseline after the luck correction, one not. Unranked: the
// consensus (no status) with nothing scored. A baseline has no lift and no status.
const char* kLeaderboard = R"({
  "symbol": "all", "period": "5d", "days": ["2026-10-07", "2026-10-08"], "basis": "b",
  "special": "exclude", "min_sample": 200,
  "ranked": [
    {"rank": 1, "algorithm": "alg-a", "status": "active", "model_versions": ["alg-a-2", "alg-a-1"], "n": 1128,
     "accuracy": {"value": 0.52, "low": 0.49, "high": 0.55, "n": 1128},
     "precision": {"value": 0.55, "low": 0.45, "high": 0.65, "n": 96},
     "coverage": {"value": 0.085, "low": 0.07, "high": 0.10, "n": 1128},
     "brier": {"value": 0.58, "low": 0.56, "high": 0.60, "n": 1128},
     "pnl": {"trades": 96, "total_ticks": 12.4, "per_trade": {"value": 0.13, "low": -0.2, "high": 0.46, "n": 96}},
     "lift": {"low": 0.01, "day_low": -0.01, "vs": {
       "baseline-flat": {"n": 1128, "value": 0.04, "low": 0.01, "high": 0.07, "p": 0.001, "q": 0.003, "significant": true,
                         "by_day": {"n": 5, "value": 0.04, "low": -0.01, "high": 0.09, "p": 0.06}},
       "baseline-persist": {"n": 1128, "value": 0.06, "low": 0.03, "high": 0.09, "p": 0.0001, "q": 0.0006, "significant": true,
                            "by_day": null}}}},
    {"rank": 2, "algorithm": "alg-b", "status": "probation", "model_versions": ["alg-b-1"], "n": 900,
     "accuracy": {"value": 0.50, "low": 0.47, "high": 0.53, "n": 900}, "precision": null,
     "coverage": {"value": 0.0, "low": 0.0, "high": 0.004, "n": 900},
     "brier": {"value": 0.61, "low": 0.59, "high": 0.63, "n": 900},
     "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null},
     "lift": {"low": -0.03, "vs": {
       "baseline-flat": {"n": 900, "value": 0.0, "low": -0.03, "high": 0.03, "p": 0.5, "q": 0.6, "significant": false}}}}
  ],
  "unranked": [
    {"rank": null, "algorithm": "consensus-final", "status": null, "model_versions": [], "n": 0,
     "accuracy": null, "precision": null, "coverage": null, "brier": null,
     "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}
  ],
  "baselines": [
    {"rank": null, "algorithm": "baseline-flat", "model_versions": ["v1"], "n": 1128,
     "accuracy": {"value": 0.51, "low": 0.48, "high": 0.54, "n": 1128}, "precision": null,
     "coverage": {"value": 0.0, "low": 0.0, "high": 0.003, "n": 1128},
     "brier": {"value": 0.98, "low": 0.95, "high": 1.01, "n": 1128},
     "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}
  ]
})";

// One algorithm's stats: its leaderboard line with the calibration curve (two of the ten
// bins; one with nothing in it), and its breakdowns, whose lifts were not tested together.
const char* kStats = R"({
  "algorithm": "alg-a", "symbol": "005930", "period": "20d", "days": ["2026-10-08"], "basis": "a",
  "special": "include", "min_sample": 200,
  "overall": {
    "rank": 1, "algorithm": "alg-a", "model_versions": ["alg-a-1"], "n": 300,
    "accuracy": {"value": 0.52, "low": 0.49, "high": 0.55, "n": 300}, "precision": null,
    "coverage": null, "brier": {"value": 0.58, "low": 0.56, "high": 0.60, "n": 300},
    "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null},
    "lift": {"low": 0.01, "vs": {
      "baseline-flat": {"n": 300, "value": 0.04, "low": 0.01, "high": 0.07, "p": 0.001, "q": 0.003, "significant": true}}},
    "calibration": [
      {"low": 0.0, "high": 0.1, "count": 0, "mean_p": null, "observed": null},
      {"low": 0.3, "high": 0.4, "count": 412, "mean_p": 0.347,
       "observed": {"value": 0.31, "low": 0.27, "high": 0.36, "n": 412}}
    ]
  },
  "by_symbol": {"005930": {"rank": null, "algorithm": "alg-a", "model_versions": ["alg-a-1"], "n": 300,
    "accuracy": {"value": 0.52, "low": 0.49, "high": 0.55, "n": 300}, "precision": null, "coverage": null,
    "brier": null, "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}},
  "by_slot": {
    "09:30": {"rank": null, "algorithm": "alg-a", "model_versions": ["alg-a-1"], "n": 120,
      "accuracy": {"value": 0.55, "low": 0.46, "high": 0.64, "n": 120}, "precision": null, "coverage": null,
      "brier": null, "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null},
      "lift": {"low": -0.05, "vs": {
        "baseline-flat": {"n": 120, "value": 0.03, "low": -0.05, "high": 0.11, "p": 0.24, "q": null, "significant": null}}}},
    "09:00": {"rank": null, "algorithm": "alg-a", "model_versions": ["alg-a-1"], "n": 180,
      "accuracy": {"value": 0.50, "low": 0.43, "high": 0.57, "n": 180}, "precision": null, "coverage": null,
      "brier": null, "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}
  },
  "by_model_version": {"alg-a-1": {"rank": null, "algorithm": "alg-a", "model_versions": ["alg-a-1"], "n": 300,
    "accuracy": {"value": 0.52, "low": 0.49, "high": 0.55, "n": 300}, "precision": null, "coverage": null,
    "brier": null, "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}}
})";

Prediction forecast(double up, double flat, double down) {
    Prediction p;
    p.algorithm = "alg-c";
    p.model_version = "alg-c-1";
    p.status = "ok";
    p.p_up = up;
    p.p_flat = flat;
    p.p_down = down;
    p.expected_ticks = 0.0;
    return p;
}

} // namespace

class TstKrxEngineData : public QObject {
    Q_OBJECT

  private slots:
    void status_is_read_with_its_special_situations_and_training() {
        const auto status = parse_status(json(kStatus));
        QVERIFY(status.is_ok());
        const auto& s = status.value();
        QCOMPARE(s.phase, QStringLiteral("continuous"));
        QCOMPARE(s.last_bar_close, QDateTime::fromString("2026-10-08T10:30:00+09:00", Qt::ISODate));
        QCOMPARE(s.pending_outcomes, 96);
        QCOMPARE(s.special.size(), 2);
        QCOMPARE(s.special[0].symbol, QStringLiteral("000660"));
        QVERIFY(s.special[1].symbol.isEmpty()); // the whole market
        QVERIFY(s.training.has_value());
        QCOMPARE(s.training->day, QStringLiteral("2026-10-07"));
        QVERIFY(s.training->applied);
        QCOMPARE(s.training->promoted.value("alg-b"), QStringLiteral("alg-b-2"));
        QCOMPARE(s.kis_connected, std::optional<bool>(true));
    }

    void status_has_the_usd_futures_now() {
        const auto status = parse_status(json(kStatus));
        QVERIFY(status.is_ok());
        const auto& usd = status.value().usd_futures;
        QVERIFY(usd.has_value());
        QCOMPARE(usd->price, 1395.2);
        QCOMPARE(usd->at, QDateTime::fromString("2026-10-08T10:29:58+09:00", Qt::ISODate));
        QCOMPARE(usd->change_pct, std::optional<double>(0.497));
        const auto unknown = parse_status(json(R"({"mode": "live", "usd_futures": {"price": 1390.0,
            "at": "2026-10-08T09:00:05+09:00", "previous_close": null, "change_pct": null}})"));
        QVERIFY(unknown.is_ok());
        QVERIFY(unknown.value().usd_futures.has_value());
        QVERIFY(!unknown.value().usd_futures->change_pct.has_value());
    }

    void the_usd_futures_show_live_or_as_read_in_the_morning() {
        UsdFutures live{1395.2, QDateTime::fromString("2026-10-08T10:29:58+09:00", Qt::ISODate), 0.497};
        const QList<Reading> morning = {
            {"sox", "필라델피아 반도체지수", "2026-10-07", 12620.8, -1.4},
            {"usdf", "달러선물 최근월물", "2026-10-07", 1388.3, 0.3},
        };
        // Named as the morning's reading, so the row reads the same before and after the first trade.
        QCOMPARE(usd_futures_text(live, morning), QStringLiteral("달러선물 최근월물 1,395.2 (+0.50%, 10:29)"));
        live.change_pct.reset();                                                          // no close to compare with
        QCOMPARE(usd_futures_text(live, {}), QStringLiteral("달러선물 1,395.2 (10:29)")); // and no reading
        // Before today's first trade: the morning's reading.
        QCOMPARE(usd_futures_text(std::nullopt, morning), QStringLiteral("달러선물 최근월물 1,388.3 (+0.30%)"));
        QCOMPARE(usd_futures_text(std::nullopt, {}), QString());
    }

    void status_without_a_training_kis_or_bar_close_yet() {
        const auto status = parse_status(json(R"({"mode": "demo", "now": "2026-10-08T08:10:00+09:00",
            "phase": "pre_open", "last_bar_close": null, "pending_outcomes": 0, "special": [], "training": null})"));
        QVERIFY(status.is_ok());
        QVERIFY(!status.value().last_bar_close.isValid());
        QVERIFY(!status.value().training.has_value());
        QVERIFY(!status.value().kis_connected.has_value()); // only a live engine reports KIS
        QVERIFY(!status.value().usd_futures.has_value());   // no trade today yet (or an older engine)
    }

    void an_answer_that_is_not_an_object_is_an_error_in_korean() {
        const auto status = parse_status(json("[1, 2]"));
        QVERIFY(status.is_err());
        QCOMPARE(QString::fromStdString(status.error()), QStringLiteral("엔진 응답이 JSON 객체가 아닙니다"));
        QVERIFY(parse_latest(QJsonDocument()).is_err());
    }

    void a_failed_request_says_why_in_korean() {
        // HttpClient: a transport failure carries no text; a body that is not JSON carries
        // Qt's parse error; an HTTP error carries the engine's {"error": ...}.
        QCOMPARE(failure_text(0, QString()),
                 QStringLiteral("엔진에 연결할 수 없습니다. 엔진 주소와 네트워크를 확인하세요."));
        QCOMPARE(failure_text(0, QStringLiteral("illegal value")),
                 QStringLiteral("엔진 응답을 읽을 수 없습니다: illegal value"));
        QCOMPARE(failure_text(404, QStringLiteral("unknown symbol 999999")),
                 QStringLiteral("엔진 오류 (HTTP 404): unknown symbol 999999"));
        QCOMPARE(failure_text(502, QString()), QStringLiteral("엔진 오류 (HTTP 502)"));
    }

    void symbols_keep_an_unknown_mid_point_unknown() {
        const auto symbols = parse_symbols(json(R"({"symbols": [
            {"symbol": "005930", "mid": 81250.0}, {"symbol": "000660", "mid": null}]})"));
        QVERIFY(symbols.is_ok());
        QCOMPARE(symbols.value().size(), 2);
        QCOMPARE(symbols.value()[0].mid, std::optional<double>(81250.0));
        QVERIFY(!symbols.value()[1].mid.has_value());
    }

    void latest_predictions_come_consensus_first_then_algorithms_then_baselines() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        QCOMPARE(latest.value().bar_close, QDateTime::fromString("2026-10-08T10:30:00+09:00", Qt::ISODate));
        QStringList order;
        for (const auto& p : latest.value().predictions)
            order << p.algorithm;
        QCOMPARE(order, (QStringList{"consensus-final", "consensus-fast", "alg-a", "alg-b", "baseline-flat"}));
    }

    void the_algorithms_keep_one_order_whatever_order_they_finished_in() {
        // The stream brings each algorithm when it finishes: the rows must not move about.
        Prediction late = forecast(0.3, 0.4, 0.3), early = forecast(0.3, 0.4, 0.3);
        late.algorithm = "alg-z";
        early.algorithm = "alg-a";
        Prediction persist = late, flat = late;
        persist.algorithm = "baseline-persist";
        flat.algorithm = "baseline-flat";
        QStringList order;
        for (const auto& p : in_screen_order({persist, late, flat, early}))
            order << p.algorithm;
        QCOMPARE(order, (QStringList{"alg-a", "alg-z", "baseline-flat", "baseline-persist"}));
    }

    void a_prediction_not_made_has_no_probabilities() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        const auto& made = latest.value().predictions;
        const auto& timed_out = made[3];
        QCOMPARE(timed_out.status, QStringLiteral("timed_out"));
        QVERIFY(!timed_out.p_up.has_value());
        QVERIFY(!timed_out.expected_ticks.has_value());
        QCOMPARE(shown_direction(timed_out), Direction::None);
        QCOMPARE(made[2].p_up, std::optional<double>(0.52));
    }

    void the_direction_shown_is_the_likeliest_class() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        const auto& made = latest.value().predictions;
        QCOMPARE(shown_direction(made[0]), Direction::Flat); // consensus-final: 0.70 flat
        QCOMPARE(shown_direction(made[1]), Direction::Up);   // consensus-fast: 0.47 up
        QCOMPARE(shown_direction(forecast(0.30, 0.10, 0.60)), Direction::Down);
        QCOMPARE(direction_label(Direction::Up), QStringLiteral("상승"));
        QCOMPARE(direction_label(Direction::None), QString());
    }

    void up_and_down_shown_as_the_same_percent_lean_neither_way() {
        // Shown as 36% / 28% / 36%: no direction, whatever the rounding underneath.
        QCOMPARE(shown_direction(forecast(0.3600000001, 0.28, 0.3599999999)), Direction::None);
        QCOMPARE(shown_direction(forecast(0.474, 0.06, 0.466)), Direction::None); // both 47%
        QCOMPARE(shown_direction(forecast(0.30, 0.40, 0.30)), Direction::Flat);
    }

    void names_and_codes_read_in_korean() {
        QCOMPARE(predictor_label("consensus-final"), QStringLiteral("합의 (최종)"));
        QCOMPARE(predictor_label("consensus-fast"), QStringLiteral("합의 (빠른)"));
        QCOMPARE(predictor_label("baseline-flat"), QStringLiteral("기준: 늘 보합"));
        QCOMPARE(predictor_label("alg-a"), QStringLiteral("alg-a")); // an algorithm keeps its name
        QVERIFY(is_consensus("consensus-fast"));
        QVERIFY(!is_consensus("alg-a"));
        QCOMPARE(phase_label("continuous"), QStringLiteral("연속매매"));
        QCOMPARE(phase_label("closed"), QStringLiteral("휴장"));
        QCOMPARE(status_label("vi"), QStringLiteral("VI 중"));
        QCOMPARE(status_label("timed_out"), QStringLiteral("시간 초과"));
        QCOMPARE(special_label("sidecar"), QStringLiteral("사이드카"));
    }

    void a_history_keeps_each_prediction_with_its_symbol_times_and_outcome() {
        const auto history = parse_predictions(json(kHistory));
        QVERIFY(history.is_ok());
        const auto& made = history.value();
        QCOMPARE(made.size(), 3);
        QCOMPARE(made[0].symbol, QStringLiteral("005930")); // from the answer, for every item
        QCOMPARE(made[0].bar_close, QDateTime::fromString("2026-10-08T10:26:00+09:00", Qt::ISODate));
        QCOMPARE(made[0].target_at, QDateTime::fromString("2026-10-08T10:30:00+09:00", Qt::ISODate));
        QVERIFY(made[0].outcome.has_value());
        QCOMPARE(made[0].outcome->class_a, Direction::Up);
        QCOMPARE(made[0].outcome->class_b, Direction::Up);
        QVERIFY(!made[0].outcome->special);
    }

    void an_outcome_unscorable_on_one_basis_has_no_class_there() {
        const auto history = parse_predictions(json(kHistory));
        QVERIFY(history.is_ok());
        const auto& outcome = history.value()[1].outcome;
        QVERIFY(outcome.has_value());
        QCOMPARE(outcome->class_a, Direction::Down);
        QCOMPARE(outcome->class_b, Direction::None);
        QCOMPARE(outcome->unscorable, QStringLiteral("missing mid: display"));
        QVERIFY(outcome->special);
    }

    void a_prediction_not_made_has_no_target_and_no_outcome() {
        const auto history = parse_predictions(json(kHistory));
        QVERIFY(history.is_ok());
        const auto& not_made = history.value()[2];
        QCOMPARE(not_made.status, QStringLiteral("no_book"));
        QVERIFY(!not_made.target_at.isValid());
        QVERIFY(!not_made.outcome.has_value());
        QCOMPARE(not_made.bar_close, QDateTime::fromString("2026-10-08T10:25:00+09:00", Qt::ISODate));
    }

    void the_latest_predictions_carry_their_symbol_and_bar_close_too() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        for (const auto& p : latest.value().predictions) {
            QCOMPARE(p.symbol, QStringLiteral("005930"));
            QCOMPARE(p.bar_close, latest.value().bar_close); // the answer's, where an item has none
        }
    }

    void bars_are_read_oldest_first() {
        const auto bars = parse_bars(json(R"({"symbol": "005930", "bars": [
            {"start": "2026-10-08T09:30:00+09:00", "open": 81000, "high": 81400, "low": 80900,
             "close": 81100, "volume": 19, "trades": 4},
            {"start": "2026-10-08T09:31:00+09:00", "open": 81100, "high": 81200, "low": 81000,
             "close": 81200, "volume": 7, "trades": 2}]})"));
        QVERIFY(bars.is_ok());
        QCOMPARE(bars.value().size(), 2);
        const auto& first = bars.value()[0];
        QCOMPARE(first.start, QDateTime::fromString("2026-10-08T09:30:00+09:00", Qt::ISODate));
        QCOMPARE(first.open, 81000.0);
        QCOMPARE(first.high, 81400.0);
        QCOMPARE(first.low, 80900.0);
        QCOMPARE(first.close, 81100.0);
        QCOMPARE(first.volume, 19.0);
        QVERIFY(parse_bars(json(R"({"error": "unknown symbol 999999"})")).is_err());
    }

    void the_leaderboard_keeps_its_query_and_three_lists() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        const auto& b = board.value();
        QCOMPARE(b.symbol, QStringLiteral("all"));
        QCOMPARE(b.period, QStringLiteral("5d"));
        QCOMPARE(b.basis, QStringLiteral("b"));
        QCOMPARE(b.special, QStringLiteral("exclude"));
        QCOMPARE(b.days, (QStringList{"2026-10-07", "2026-10-08"}));
        QCOMPARE(b.min_sample, 200);
        QCOMPARE(b.ranked.size(), 2);
        QCOMPARE(b.unranked.size(), 1);
        QCOMPARE(b.baselines.size(), 1);
    }

    void a_ranked_entry_has_its_rank_status_models_and_stats() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        const auto& first = board.value().ranked[0];
        QCOMPARE(first.rank, std::optional<int>(1));
        QCOMPARE(first.algorithm, QStringLiteral("alg-a"));
        QCOMPARE(first.status, QStringLiteral("active"));
        QCOMPARE(first.model_versions, (QStringList{"alg-a-2", "alg-a-1"}));
        QCOMPARE(first.n, 1128);
        QVERIFY(first.accuracy.has_value());
        QCOMPARE(first.accuracy->value, 0.52);
        QCOMPARE(first.accuracy->low, std::optional<double>(0.49));
        QCOMPARE(first.brier->value, 0.58);
        QCOMPARE(first.trades, 96);
        QCOMPARE(first.total_ticks, 12.4);
        QCOMPARE(first.per_trade->value, 0.13);
        QCOMPARE(first.lift_low, std::optional<double>(0.01));
    }

    void significant_means_every_baseline_beaten_after_the_luck_correction() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        QVERIFY(board.value().ranked[0].significant);
        QVERIFY(!board.value().ranked[1].significant); // beats none of them for sure
        QVERIFY(!board.value().baselines[0].significant);
    }

    void each_baselines_lift_has_its_interval_and_test() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        const auto& lifts = board.value().ranked[0].lifts;
        QCOMPARE(lifts.size(), 2);
        QCOMPARE(lifts[0].baseline, QStringLiteral("baseline-flat"));
        QCOMPARE(lifts[0].gain->value, 0.04);
        QCOMPARE(lifts[0].gain->low, std::optional<double>(0.01));
        QCOMPARE(lifts[0].gain->high, std::optional<double>(0.07));
        QCOMPARE(lifts[0].gain->n, 1128);
        QCOMPARE(lifts[0].p, std::optional<double>(0.001));
        QCOMPARE(lifts[0].q, std::optional<double>(0.003));
        QCOMPARE(lifts[0].significant, std::optional<bool>(true));
        QCOMPARE(lifts[1].baseline, QStringLiteral("baseline-persist"));
        QVERIFY(board.value().unranked[0].lifts.isEmpty()); // nothing to compare with
    }

    void a_lift_taken_by_trading_day_has_its_days_and_interval() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        const auto& lifts = board.value().ranked[0].lifts;
        QCOMPARE(lifts[0].by_day->n, 5); // days, not pairs
        QCOMPARE(lifts[0].by_day->value, 0.04);
        QCOMPARE(lifts[0].by_day->low, std::optional<double>(-0.01));
        QCOMPARE(lifts[0].by_day->high, std::optional<double>(0.09));
        QVERIFY(!lifts[1].by_day); // fewer than two days
        // An engine from before the lifts by trading day has none.
        QVERIFY(!board.value().ranked[1].lifts[0].by_day);
    }

    void an_algorithms_stats_keep_their_query_and_its_leaderboard_line() {
        const auto stats = parse_stats(json(kStats));
        QVERIFY(stats.is_ok());
        const auto& s = stats.value();
        QCOMPARE(s.algorithm, QStringLiteral("alg-a"));
        QCOMPARE(s.symbol, QStringLiteral("005930"));
        QCOMPARE(s.period, QStringLiteral("20d"));
        QCOMPARE(s.basis, QStringLiteral("a"));
        QCOMPARE(s.special, QStringLiteral("include"));
        QCOMPARE(s.days, QStringList{"2026-10-08"});
        QCOMPARE(s.min_sample, 200);
        QCOMPARE(s.overall.rank, std::optional<int>(1));
        QCOMPARE(s.overall.n, 300);
        QCOMPARE(s.overall.lifts.size(), 1);
        QVERIFY(s.overall.significant);
    }

    void the_calibration_curve_keeps_its_empty_bins_unknown() {
        const auto stats = parse_stats(json(kStats));
        QVERIFY(stats.is_ok());
        const auto& bins = stats.value().calibration;
        QCOMPARE(bins.size(), 2);
        QCOMPARE(bins[0].count, 0);
        QVERIFY(!bins[0].mean_p.has_value());
        QVERIFY(!bins[0].observed.has_value());
        QCOMPARE(bins[1].low, 0.3);
        QCOMPARE(bins[1].high, 0.4);
        QCOMPARE(bins[1].count, 412);
        QCOMPARE(bins[1].mean_p, std::optional<double>(0.347));
        QCOMPARE(bins[1].observed->value, 0.31);
        QCOMPARE(bins[1].observed->low, std::optional<double>(0.27));
    }

    void the_breakdowns_are_by_symbol_slot_and_model_version_in_order() {
        const auto stats = parse_stats(json(kStats));
        QVERIFY(stats.is_ok());
        const auto& s = stats.value();
        QCOMPARE(s.by_symbol.keys(), QStringList{"005930"});
        QCOMPARE(s.by_slot.keys(), (QStringList{"09:00", "09:30"}));
        const Entry slot = s.by_slot.value("09:30");
        QCOMPARE(slot.n, 120);
        QCOMPARE(s.by_model_version.keys(), QStringList{"alg-a-1"});
        // A breakdown's lift was not tested with the others: no q, no verdict.
        const Lift& lift = slot.lifts.first();
        QCOMPARE(lift.p, std::optional<double>(0.24));
        QVERIFY(!lift.q.has_value());
        QVERIFY(!lift.significant.has_value());
    }

    void stats_with_nothing_scored_are_empty_not_zero() {
        // A consensus over a period it did not predict in: the baselines are still there to
        // compare with, but no bar close pairs with them.
        const auto stats = parse_stats(json(R"({
          "algorithm": "consensus-final", "symbol": "all", "period": "today", "days": [], "basis": "b",
          "special": "exclude", "min_sample": 200,
          "overall": {"rank": null, "algorithm": "consensus-final", "model_versions": [], "n": 0,
            "accuracy": null, "precision": null, "coverage": null, "brier": null,
            "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null},
            "lift": {"low": null, "vs": {
              "baseline-flat": {"n": 0, "value": null, "low": null, "high": null, "p": null, "q": null, "significant": false}}},
            "calibration": [{"low": 0.0, "high": 0.1, "count": 0, "mean_p": null, "observed": null}]},
          "by_symbol": {}, "by_slot": {}, "by_model_version": {}
        })"));
        QVERIFY(stats.is_ok());
        const auto& s = stats.value();
        QCOMPARE(s.overall.n, 0);
        QVERIFY(!s.overall.lift_low.has_value());
        QVERIFY(!s.overall.significant);
        QCOMPARE(s.overall.lifts.size(), 1);
        QVERIFY(!s.overall.lifts[0].gain.has_value()); // no pairs: unknown, not a gain of 0
        QVERIFY(!s.overall.lifts[0].p.has_value());
        QVERIFY(s.by_symbol.isEmpty() && s.by_slot.isEmpty() && s.by_model_version.isEmpty());
        QVERIFY(s.days.isEmpty());
    }

    void stats_of_an_unknown_algorithm_are_an_error() {
        QVERIFY(parse_stats(json(R"({"error": "unknown algorithm alg-z"})")).is_err());
    }

    void what_was_not_measured_stays_unknown() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        const auto& consensus = board.value().unranked[0];
        QVERIFY(!consensus.rank.has_value());
        QVERIFY(consensus.status.isEmpty()); // the consensus has no status
        QVERIFY(!consensus.accuracy.has_value());
        QVERIFY(!consensus.per_trade.has_value());
        QVERIFY(!consensus.lift_low.has_value());
        QVERIFY(!board.value().ranked[1].precision.has_value());
        const auto& baseline = board.value().baselines[0];
        QVERIFY(baseline.status.isEmpty());
        QVERIFY(!baseline.lift_low.has_value());
    }

    void status_has_each_feeds_silence_and_gap() {
        const auto status = parse_status(json(kStatus));
        QVERIFY(status.is_ok());
        const auto& s = status.value();
        QCOMPARE(s.feeds.size(), 3);
        QCOMPARE(s.feeds[0].feed, QStringLiteral("H0UNCNT0"));
        QCOMPARE(s.feeds[0].key, QStringLiteral("005930"));
        QCOMPARE(s.feeds[0].seconds_since, std::optional<double>(2.0));
        QVERIFY(s.feeds[0].gap.isEmpty());
        QCOMPARE(s.feeds[1].gap, QStringLiteral("stale"));
        QVERIFY(!s.feeds[2].seconds_since.has_value()); // never received
    }

    void a_disconnected_engine_lists_no_feeds() {
        const auto status = parse_status(json(R"({"mode": "live", "now": "2026-10-09T10:00:00+09:00",
            "phase": "closed", "session": null, "last_bar_close": null, "pending_outcomes": 0, "special": [],
            "training": null, "kis": {"connected": false, "feeds": []}})"));
        QVERIFY(status.is_ok());
        QCOMPARE(status.value().kis_connected, std::optional<bool>(false));
        QVERIFY(status.value().feeds.isEmpty());
    }

    void the_macro_row_has_the_regime_its_reasons_and_readings() {
        const auto macro = parse_macro(json(kMacro));
        QVERIFY(macro.is_ok());
        const auto& m = macro.value();
        QCOMPARE(m.day, QStringLiteral("2026-10-12"));
        QCOMPARE(m.regime, QStringLiteral("risk_off"));
        QCOMPARE(m.reasons, (QStringList{QStringLiteral("SOX -1.4% (-1% 이하)")}));
        QCOMPARE(m.readings.size(), 3);
        QCOMPARE(m.readings[1].indicator, QStringLiteral("usdf"));
        QCOMPARE(m.readings[1].label, QStringLiteral("달러선물"));
        QCOMPARE(m.readings[1].value, std::optional<double>(1392.2));
        QCOMPARE(m.readings[1].change_pct, std::optional<double>(0.3));
        QVERIFY(!m.readings[2].value.has_value()); // missing, not 0
        QCOMPARE(m.failed.value("fred"), QStringLiteral("HTTP 503"));
        QCOMPARE(m.skipped, (QStringList{"ecos"}));
    }

    void no_macro_decided_yet_is_empty_not_an_error() {
        const auto macro = parse_macro(json(R"({"day": null, "decided_at": null, "regime": null,
            "reasons": [], "readings": [], "failed": {}, "skipped": []})"));
        QVERIFY(macro.is_ok());
        QVERIFY(macro.value().day.isEmpty());
        QVERIFY(macro.value().regime.isEmpty());
        QVERIFY(macro.value().readings.isEmpty());
    }

    void the_consensus_members_are_the_active_algorithms() {
        const auto members = parse_members(json(R"({"names": ["consensus-fast", "consensus-final"],
            "version": "w-20261012", "computed_at": "2026-10-12T15:40:00+09:00",
            "members": ["alg-a", "alg-b"], "fast_seconds": 2.0, "weights": {}})"));
        QVERIFY(members.is_ok());
        QCOMPARE(members.value(), (QStringList{"alg-a", "alg-b"}));
    }

    void a_prediction_event_brings_a_bar_closes_predictions_with_their_symbols() {
        const auto event =
            parse_event(QStringLiteral(R"({"type": "prediction", "at": "2026-10-12T09:31:00.090000+09:00",
            "bar_close": "2026-10-12T09:31:00+09:00", "predictions": [
              {"symbol": "000660", "algorithm": "alg-a", "model_version": "alg-a-1",
               "bar_close": "2026-10-12T09:31:00+09:00", "displayed_at": "2026-10-12T09:31:00.090000+09:00",
               "target_at": "2026-10-12T09:35:00+09:00", "status": "ok",
               "p_up": 0.5, "p_flat": 0.3, "p_down": 0.2, "expected_ticks": 0.6, "outcome": null}]})"));
        QVERIFY(event.is_ok());
        const auto& e = event.value();
        QCOMPARE(e.type, Event::Type::Prediction);
        QCOMPARE(e.at, QDateTime::fromString("2026-10-12T09:31:00.090+09:00", Qt::ISODateWithMs));
        QCOMPARE(e.bar_close, QDateTime::fromString("2026-10-12T09:31:00+09:00", Qt::ISODate));
        QCOMPARE(e.predictions.size(), 1);
        QCOMPARE(e.predictions[0].symbol, QStringLiteral("000660"));
        QCOMPARE(e.predictions[0].p_up, std::optional<double>(0.5));
        QVERIFY(!e.predictions[0].outcome.has_value());
    }

    void a_consensus_event_says_which_one() {
        const auto event = parse_event(QStringLiteral(R"({"type": "consensus", "at": "2026-10-12T09:31:02+09:00",
            "kind": "final", "bar_close": "2026-10-12T09:31:00+09:00", "predictions": [
              {"symbol": "005930", "algorithm": "consensus-final", "model_version": "w1",
               "bar_close": "2026-10-12T09:31:00+09:00", "displayed_at": "2026-10-12T09:31:02+09:00",
               "target_at": "2026-10-12T09:35:00+09:00", "status": "ok",
               "p_up": 0.64, "p_flat": 0.26, "p_down": 0.1, "expected_ticks": 1.1, "outcome": null}]})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Consensus);
        QCOMPARE(event.value().kind, QStringLiteral("final"));
        QCOMPARE(event.value().predictions[0].algorithm, QStringLiteral("consensus-final"));
    }

    void an_outcome_event_brings_the_scored_predictions() {
        const auto event = parse_event(QStringLiteral(R"({"type": "outcome", "at": "2026-10-12T09:35:00+09:00",
            "outcomes": [
              {"symbol": "005930", "algorithm": "consensus-final", "model_version": "w1",
               "bar_close": "2026-10-12T09:31:00+09:00", "displayed_at": "2026-10-12T09:31:02+09:00",
               "target_at": "2026-10-12T09:35:00+09:00", "status": "ok",
               "p_up": 0.64, "p_flat": 0.26, "p_down": 0.1, "expected_ticks": 1.1,
               "outcome": {"mid_close": 81250.0, "mid_display": 81250.0, "mid_target": 81050.0, "flat_ticks": 2,
                           "class_a": "down", "class_b": "down", "special": false, "unscorable": null,
                           "scored_at": "2026-10-12T09:35:00+09:00"}}]})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Outcome);
        QCOMPARE(event.value().predictions.size(), 1);
        QCOMPARE(event.value().predictions[0].outcome->class_b, Direction::Down);
    }

    void a_health_event_brings_the_kis_status() {
        const auto event = parse_event(QStringLiteral(R"({"type": "health", "at": "2026-10-12T09:40:00+09:00",
            "kis": {"connected": false, "feeds": [{"feed": "H0UNCNT0", "key": "005930",
              "last_received_at": "2026-10-12T09:39:00+09:00", "seconds_since": 60.0,
              "gap": "disconnected: closed"}]}})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Health);
        QVERIFY(!event.value().kis_connected);
        QCOMPARE(event.value().feeds.size(), 1);
        QCOMPARE(event.value().feeds[0].gap, QStringLiteral("disconnected: closed"));
    }

    void a_lifecycle_event_brings_an_algorithms_status_change() {
        const auto event = parse_event(QStringLiteral(R"({"type": "lifecycle", "at": "2026-10-12T15:40:00+09:00",
            "algorithm": "alg-b", "change": {"at": "2026-10-12T15:40:00+09:00", "from": "probation",
            "to": "active", "reason": "lift 0.02 > 0"}})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Lifecycle);
        QCOMPARE(event.value().algorithm, QStringLiteral("alg-b"));
        QCOMPARE(event.value().change.from, QStringLiteral("probation"));
        QCOMPARE(event.value().change.to, QStringLiteral("active"));
    }

    void an_event_of_a_newer_engine_is_passed_over_not_an_error() {
        const auto event = parse_event(QStringLiteral(R"({"type": "weather", "at": "2026-10-12T09:00:00+09:00"})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Other);
        const auto broken = parse_event(QStringLiteral("not json"));
        QVERIFY(broken.is_err());
        QCOMPARE(QString::fromStdString(broken.error()), QStringLiteral("엔진 스트림 메시지를 읽을 수 없습니다"));
    }

    void the_stream_is_at_the_engines_address_over_websocket() {
        QCOMPARE(stream_url("http://engine.local:8080"), QStringLiteral("ws://engine.local:8080/v1/stream"));
        QCOMPARE(stream_url("https://engine.example/"), QStringLiteral("wss://engine.example/v1/stream"));
        QCOMPARE(stream_url("http://127.0.0.1:8090"), QStringLiteral("ws://127.0.0.1:8090/v1/stream"));
        QVERIFY(stream_url("").isEmpty());
        QVERIFY(stream_url("engine.local:8080").isEmpty()); // no http(s): nothing to connect to
    }

    void the_predicted_class_is_the_likeliest_a_tie_going_to_flat_then_up() {
        // As the engine scores: unlike the direction shown, a near tie still has a class.
        QCOMPARE(most_likely(forecast(0.474, 0.06, 0.466)), Direction::Up);
        QCOMPARE(most_likely(forecast(0.40, 0.40, 0.20)), Direction::Flat);
        QCOMPARE(most_likely(forecast(0.40, 0.20, 0.40)), Direction::Up);
        QCOMPARE(most_likely(forecast(0.30, 0.20, 0.50)), Direction::Down);
        Prediction not_made;
        not_made.status = "timed_out";
        QCOMPARE(most_likely(not_made), Direction::None);
    }

    void why_last_nights_training_is_not_running_reads_in_korean() {
        QCOMPARE(training_reason_label("waiting for 08:00"), QStringLiteral("08:00 교체 대기"));
        QCOMPARE(training_reason_label("final at 10-10 07:41, after 07:30: yesterday's models run on"),
                 QStringLiteral("07:30 뒤에 확정되어 어제 모델로 돈다"));
        QCOMPARE(training_reason_label("not in the store: alg-a-20261010-abc123; yesterday's models run on"),
                 QStringLiteral("모델 파일이 저장소에 없어 어제 모델로 돈다"));
        QCOMPARE(training_reason_label("applied, then replaced: alg-a runs its defaults"),
                 QStringLiteral("적용 뒤 바뀜: alg-a runs its defaults"));
        QCOMPARE(training_reason_label("something new"), QStringLiteral("something new")); // as it is
    }

    void times_read_in_kst_and_prices_as_on_the_exchange() {
        const QDateTime utc = QDateTime::fromString("2026-10-12T00:31:05Z", Qt::ISODate);
        QCOMPARE(kst_text(utc, "HH:mm"), QStringLiteral("09:31"));
        QCOMPARE(kst_text(QDateTime(), "HH:mm"), QStringLiteral("—"));
        QCOMPARE(price_text(81250.0), QStringLiteral("81,250"));
        QCOMPARE(price_text(1392.2), QStringLiteral("1,392.2")); // a mid-point between ticks keeps a decimal
    }

    void the_consensus_and_the_baselines_are_told_by_their_names() {
        QVERIFY(is_consensus("consensus-final"));
        QVERIFY(is_baseline("baseline-random"));
        QVERIFY(!is_baseline("newton"));
        QVERIFY(!is_consensus("newton"));
    }

    void more_names_read_in_korean() {
        QCOMPARE(status_label("no_book"), QStringLiteral("호가 없음"));
        QCOMPARE(regime_label("risk_off"), QStringLiteral("위험회피"));
        QCOMPARE(regime_label("shock"), QStringLiteral("충격"));
        QCOMPARE(standing_label("probation"), QStringLiteral("수습"));
        QCOMPARE(standing_label("active"), QStringLiteral("정식"));
        QCOMPARE(standing_label("clone"), QStringLiteral("복제"));
        QCOMPARE(standing_label(""), QString()); // the consensus and the baselines have none
    }
};

QTEST_GUILESS_MAIN(TstKrxEngineData)
#include "tst_krx_engine_data.moc"
