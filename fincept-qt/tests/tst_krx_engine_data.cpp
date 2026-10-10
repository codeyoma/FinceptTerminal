// Unit tests for src/services/krx_quant/EngineData.h
//
// The KRX prediction screen shows what the engine's API (v1) answers: its status, the
// symbols with their reference price, and each symbol's latest predictions. These tests
// pin how the answers are read, including what is missing: null values, predictions that
// were not made, the engine's error answers and a live-only field.

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
  "now": "2026-10-12T10:30:05+09:00",
  "phase": "continuous",
  "session": {"open": "2026-10-12T09:00:00+09:00", "close": "2026-10-12T15:30:00+09:00"},
  "last_bar_close": "2026-10-12T10:30:00+09:00",
  "pending_outcomes": 96,
  "special": [
    {"kind": "vi", "symbol": "000660", "start": "2026-10-12T09:40:10+09:00", "end": "2026-10-12T09:42:20+09:00"},
    {"kind": "sidecar", "symbol": null, "start": "2026-10-12T10:01:00+09:00", "end": "2026-10-12T10:06:00+09:00"}
  ],
  "training": {"day": "2026-10-09", "finalized_at": "2026-10-09T21:31:40+09:00", "applied": true,
               "reason": "applied", "promoted": {"alg-b": "alg-b-20261002-8dd73a"}, "reports": []},
  "kis": {"connected": true, "feeds": []}
})";

const char* kLatest = R"({
  "symbol": "005930",
  "bar_close": "2026-10-12T10:30:00+09:00",
  "predictions": [
    {"algorithm": "baseline-flat", "model_version": "v1", "status": "ok",
     "p_up": 0.0, "p_flat": 1.0, "p_down": 0.0, "expected_ticks": 0.0, "outcome": null},
    {"algorithm": "alg-a", "model_version": "alg-a-default", "status": "ok",
     "p_up": 0.52, "p_flat": 0.10, "p_down": 0.38, "expected_ticks": 1.42, "outcome": null},
    {"algorithm": "consensus-fast", "model_version": "w1", "status": "ok",
     "p_up": 0.47, "p_flat": 0.11, "p_down": 0.42, "expected_ticks": 0.47, "outcome": null},
    {"algorithm": "alg-b", "model_version": "alg-b-default", "status": "timed_out",
     "p_up": null, "p_flat": null, "p_down": null, "expected_ticks": null, "outcome": null},
    {"algorithm": "consensus-final", "model_version": "w1", "status": "ok",
     "p_up": 0.20, "p_flat": 0.70, "p_down": 0.10, "expected_ticks": 0.1, "outcome": null}
  ]
})";

} // namespace

class TstKrxEngineData : public QObject {
    Q_OBJECT

  private slots:
    void status_is_read_with_its_special_situations_and_training() {
        const auto status = parse_status(json(kStatus));
        QVERIFY(status.is_ok());
        const auto& s = status.value();
        QCOMPARE(s.phase, QStringLiteral("continuous"));
        QCOMPARE(s.last_bar_close, QDateTime::fromString("2026-10-12T10:30:00+09:00", Qt::ISODate));
        QCOMPARE(s.pending_outcomes, 96);
        QCOMPARE(s.special.size(), 2);
        QCOMPARE(s.special[0].symbol, QStringLiteral("000660"));
        QVERIFY(s.special[1].symbol.isEmpty()); // the whole market
        QVERIFY(s.training.has_value());
        QCOMPARE(s.training->day, QStringLiteral("2026-10-09"));
        QVERIFY(s.training->applied);
        QCOMPARE(s.training->promoted.value("alg-b"), QStringLiteral("alg-b-20261002-8dd73a"));
        QCOMPARE(s.kis_connected, std::optional<bool>(true));
    }

    void status_without_a_training_kis_or_bar_close_yet() {
        const auto status = parse_status(json(R"({"mode": "demo", "now": "2026-10-12T08:10:00+09:00",
            "phase": "pre_open", "session": null, "last_bar_close": null, "pending_outcomes": 0,
            "special": [], "training": null})"));
        QVERIFY(status.is_ok());
        QVERIFY(!status.value().last_bar_close.isValid());
        QVERIFY(!status.value().training.has_value());
        QVERIFY(!status.value().kis_connected.has_value()); // only a live engine reports KIS
    }

    void the_engines_error_answer_is_an_error() {
        const auto latest = parse_latest(json(R"({"error": "unknown symbol 999999"})"));
        QVERIFY(latest.is_err());
        QCOMPARE(QString::fromStdString(latest.error()), QStringLiteral("unknown symbol 999999"));
        QVERIFY(parse_status(json("[1, 2]")).is_err());
        QVERIFY(parse_status(QJsonDocument()).is_err());
    }

    void symbols_keep_an_unknown_mid_point_unknown() {
        const auto symbols = parse_symbols(json(R"({"symbols": [
            {"symbol": "005930", "mid": 81250.0, "tick": 100, "flat_ticks": 2},
            {"symbol": "000660", "mid": null, "tick": null, "flat_ticks": null}]})"));
        QVERIFY(symbols.is_ok());
        QCOMPARE(symbols.value().size(), 2);
        QCOMPARE(symbols.value()[0].mid, std::optional<double>(81250.0));
        QVERIFY(!symbols.value()[1].mid.has_value());
    }

    void latest_predictions_come_consensus_first_then_algorithms_then_baselines() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        QCOMPARE(latest.value().bar_close, QDateTime::fromString("2026-10-12T10:30:00+09:00", Qt::ISODate));
        QStringList order;
        for (const auto& p : latest.value().predictions)
            order << p.algorithm;
        QCOMPARE(order, (QStringList{"consensus-final", "consensus-fast", "alg-a", "alg-b", "baseline-flat"}));
    }

    void a_prediction_not_made_has_no_probabilities() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        const auto& made = latest.value().predictions;
        const auto& timed_out = made[3];
        QCOMPARE(timed_out.status, QStringLiteral("timed_out"));
        QVERIFY(!timed_out.p_up.has_value());
        QVERIFY(!timed_out.expected_ticks.has_value());
        QCOMPARE(direction(timed_out), QString());
        QCOMPARE(made[2].p_up, std::optional<double>(0.52));
    }

    void the_direction_is_the_likeliest_class() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        const auto& made = latest.value().predictions;
        QCOMPARE(direction(made[0]), QStringLiteral("보합")); // consensus-final: 0.70 flat
        QCOMPARE(direction(made[1]), QStringLiteral("상승")); // consensus-fast: 0.47 up
    }

    void a_forecast_that_leans_neither_way_has_no_direction() {
        // A symmetric forecast: up and down the same but for rounding, the move unknown.
        Prediction even{"alg-c", "alg-c-default", "ok", 0.3600000001, 0.28, 0.3599999999, 0.0};
        QCOMPARE(direction(even), QString());
        Prediction flat{"alg-c", "alg-c-default", "ok", 0.30, 0.40, 0.30, 0.0};
        QCOMPARE(direction(flat), QStringLiteral("보합"));
        Prediction down{"alg-c", "alg-c-default", "ok", 0.30, 0.10, 0.60, -2.0};
        QCOMPARE(direction(down), QStringLiteral("하락"));
    }

    void phases_and_statuses_read_in_korean() {
        QCOMPARE(phase_label("continuous"), QStringLiteral("연속매매"));
        QCOMPARE(phase_label("closed"), QStringLiteral("휴장"));
        QCOMPARE(phase_label("something_new"), QStringLiteral("something_new"));
        QCOMPARE(status_label("vi"), QStringLiteral("VI 중"));
        QCOMPARE(status_label("timed_out"), QStringLiteral("시간 초과"));
    }
};

QTEST_GUILESS_MAIN(TstKrxEngineData)
#include "tst_krx_engine_data.moc"
