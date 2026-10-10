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
  "now": "2026-10-12T10:30:05+09:00",
  "phase": "continuous",
  "last_bar_close": "2026-10-12T10:30:00+09:00",
  "pending_outcomes": 96,
  "special": [
    {"kind": "vi", "symbol": "000660", "start": "2026-10-12T09:40:10+09:00", "end": "2026-10-12T09:42:20+09:00"},
    {"kind": "sidecar", "symbol": null, "start": "2026-10-12T10:01:00+09:00", "end": "2026-10-12T10:06:00+09:00"}
  ],
  "training": {"day": "2026-10-09", "applied": true, "reason": "applied", "promoted": {"alg-b": "alg-b-2"}},
  "kis": {"connected": true}
})";

const char* kLatest = R"({
  "symbol": "005930",
  "bar_close": "2026-10-12T10:30:00+09:00",
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

Prediction forecast(double up, double flat, double down) {
    return {"alg-c", "alg-c-1", "ok", up, flat, down, 0.0};
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
        QCOMPARE(s.last_bar_close, QDateTime::fromString("2026-10-12T10:30:00+09:00", Qt::ISODate));
        QCOMPARE(s.pending_outcomes, 96);
        QCOMPARE(s.special.size(), 2);
        QCOMPARE(s.special[0].symbol, QStringLiteral("000660"));
        QVERIFY(s.special[1].symbol.isEmpty()); // the whole market
        QVERIFY(s.training.has_value());
        QCOMPARE(s.training->day, QStringLiteral("2026-10-09"));
        QVERIFY(s.training->applied);
        QCOMPARE(s.training->promoted.value("alg-b"), QStringLiteral("alg-b-2"));
        QCOMPARE(s.kis_connected, std::optional<bool>(true));
    }

    void status_without_a_training_kis_or_bar_close_yet() {
        const auto status = parse_status(json(R"({"mode": "demo", "now": "2026-10-12T08:10:00+09:00",
            "phase": "pre_open", "last_bar_close": null, "pending_outcomes": 0, "special": [], "training": null})"));
        QVERIFY(status.is_ok());
        QVERIFY(!status.value().last_bar_close.isValid());
        QVERIFY(!status.value().training.has_value());
        QVERIFY(!status.value().kis_connected.has_value()); // only a live engine reports KIS
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
        QCOMPARE(direction(timed_out), Direction::None);
        QCOMPARE(made[2].p_up, std::optional<double>(0.52));
    }

    void the_direction_is_the_likeliest_class() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        const auto& made = latest.value().predictions;
        QCOMPARE(direction(made[0]), Direction::Flat); // consensus-final: 0.70 flat
        QCOMPARE(direction(made[1]), Direction::Up);   // consensus-fast: 0.47 up
        QCOMPARE(direction(forecast(0.30, 0.10, 0.60)), Direction::Down);
        QCOMPARE(direction_label(Direction::Up), QStringLiteral("상승"));
        QCOMPARE(direction_label(Direction::None), QString());
    }

    void up_and_down_shown_as_the_same_percent_lean_neither_way() {
        // Shown as 36% / 28% / 36%: no direction, whatever the rounding underneath.
        QCOMPARE(direction(forecast(0.3600000001, 0.28, 0.3599999999)), Direction::None);
        QCOMPARE(direction(forecast(0.474, 0.06, 0.466)), Direction::None); // both 47%
        QCOMPARE(direction(forecast(0.30, 0.40, 0.30)), Direction::Flat);
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
};

QTEST_GUILESS_MAIN(TstKrxEngineData)
#include "tst_krx_engine_data.moc"
