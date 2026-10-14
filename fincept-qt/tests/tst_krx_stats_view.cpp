// Unit tests for src/services/krx_quant/StatsView.h
//
// The KRX screen's leaderboard and an algorithm's detail window show the engine's stats:
// rates and means with their 95% intervals, the lift over each baseline with its test,
// the 30-minute slots and the calibration curve. These tests pin what is shown, including
// what was not measured or not tested.

#include "services/krx_quant/StatsView.h"

#include <QTest>

using namespace fincept::krx_quant;

namespace {

Lift lift(const char* baseline, double gain, std::optional<double> q, std::optional<bool> significant) {
    return {baseline, Interval{gain, gain - 0.03, gain + 0.03, 1128}, 0.01, q, significant, std::nullopt};
}

} // namespace

class TstKrxStatsView : public QObject {
    Q_OBJECT

  private slots:
    void a_rate_shows_as_a_percent_with_its_interval_aside() {
        const Shown rate = rate_shown(Interval{0.52, 0.49, 0.55, 1128});
        QCOMPARE(rate.text, QStringLiteral("52.0%"));
        QCOMPARE(rate.tip, QStringLiteral("95% 구간 49.0–55.0% (n 1128)"));
        QCOMPARE(rate_shown(std::nullopt).text, QStringLiteral("—"));
        QCOMPARE(rate_shown(Interval{1.0, std::nullopt, std::nullopt, 1}).tip, QString()); // one value: no interval
    }

    void a_mean_shows_with_its_decimals_and_interval_aside() {
        const Shown brier = mean_shown(Interval{0.58, 0.56, 0.6, 1128}, 3);
        QCOMPARE(brier.text, QStringLiteral("0.580"));
        QCOMPARE(brier.tip, QStringLiteral("95% 구간 0.560 – 0.600 (n 1128)"));
        QCOMPARE(mean_shown(Interval{-0.13, -0.6, 0.34, 96}, 2).text, QStringLiteral("-0.13"));
        QCOMPARE(mean_shown(std::nullopt, 2).text, QStringLiteral("—"));
    }

    void a_gain_shows_its_sign() {
        QCOMPARE(signed_text(0.01, 3), QStringLiteral("+0.010"));
        QCOMPARE(signed_text(-0.03, 3), QStringLiteral("-0.030"));
        QCOMPARE(signed_text(0.0, 3), QStringLiteral("0.000"));
        QCOMPARE(number_text(1128, 0), QStringLiteral("1,128"));
    }

    void each_baseline_has_its_gain_interval_pairs_and_test() {
        Entry e;
        e.lifts = {lift("baseline-flat", 0.04, 0.003, true), lift("baseline-persist", -0.01, 0.6, false)};
        const auto rows = lift_rows(e);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].baseline, QStringLiteral("기준: 늘 보합"));
        QCOMPARE(rows[0].gain, QStringLiteral("+0.040"));
        QCOMPARE(rows[0].interval, QStringLiteral("+0.010 – +0.070"));
        QCOMPARE(rows[0].pairs, QStringLiteral("1,128"));
        QCOMPARE(rows[0].p, QStringLiteral("0.010"));
        QCOMPARE(rows[0].q, QStringLiteral("0.003"));
        QCOMPARE(rows[0].verdict, QStringLiteral("통과"));
        QCOMPARE(rows[1].gain, QStringLiteral("-0.010"));
        QCOMPARE(rows[1].verdict, QStringLiteral("미통과"));
    }

    void a_lift_by_trading_day_shows_its_interval_and_days() {
        Entry e;
        e.lifts = {lift("baseline-flat", 0.04, 0.003, true), lift("baseline-persist", 0.06, 0.001, true)};
        e.lifts[0].by_day = Interval{0.04, -0.012, 0.092, 5};
        const auto rows = lift_rows(e);
        QCOMPARE(rows[0].day_interval, QStringLiteral("-0.012 – +0.092"));
        QCOMPARE(rows[0].days, QStringLiteral("5"));
        QCOMPARE(rows[1].day_interval, QStringLiteral("—")); // fewer than two days
        QCOMPARE(rows[1].days, QStringLiteral("—"));
    }

    void a_lift_not_tested_with_the_others_has_no_q_and_no_verdict() {
        Entry e;
        e.lifts = {lift("baseline-random", 0.02, std::nullopt, std::nullopt)};
        const auto rows = lift_rows(e);
        QCOMPARE(rows[0].q, QStringLiteral("—"));
        QCOMPARE(rows[0].verdict, QStringLiteral("—"));
    }

    void a_baseline_with_no_bar_close_in_common_has_no_lift() {
        Entry e;
        e.lifts = {Lift{"baseline-flat", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}};
        const auto rows = lift_rows(e);
        QCOMPARE(rows[0].gain, QStringLiteral("—"));
        QCOMPARE(rows[0].interval, QStringLiteral("—"));
        QCOMPARE(rows[0].pairs, QStringLiteral("0"));
        QCOMPARE(rows[0].p, QStringLiteral("—"));
        QCOMPARE(rows[0].verdict, QStringLiteral("—")); // nothing was tested
        QCOMPARE(rows[0].day_interval, QStringLiteral("—"));
        QCOMPARE(rows[0].days, QStringLiteral("—"));
    }

    void a_tiny_p_still_reads() {
        Entry e;
        e.lifts = {lift("baseline-flat", 0.1, 0.00002, true)};
        e.lifts[0].p = 0.000004;
        QCOMPARE(lift_rows(e)[0].p, QStringLiteral("<0.001"));
        QCOMPARE(lift_rows(e)[0].q, QStringLiteral("<0.001"));
    }

    void a_slot_reads_as_the_bar_closes_it_holds() {
        QCOMPARE(slot_label("09:00"), QStringLiteral("09:01–09:30"));
        QCOMPARE(slot_label("09:30"), QStringLiteral("09:31–10:00"));
        QCOMPARE(slot_label("15:00"), QStringLiteral("15:01–15:30"));
        QCOMPARE(slot_label("later"), QStringLiteral("later")); // not a time: as it is
    }

    void the_calibration_plot_draws_the_classes_pooled_then_each_with_points() {
        AlgorithmStats stats;
        const CalibrationBin third{0.3, 0.4, 10, 1.0 / 3, Interval{1.0, 0.72, 1.0, 10}};
        stats.calibration = {CalibrationBin{0.3, 0.4, 30, 1.0 / 3, Interval{1.0 / 3, 0.19, 0.51, 30}}};
        stats.calibration_by_class = {{"up", {third}},
                                      {"flat", {CalibrationBin{0.3, 0.4, 10, 1.0 / 3, Interval{0.0, 0.0, 0.28, 10}}}},
                                      {"down", {CalibrationBin{0.0, 0.1, 0, std::nullopt, std::nullopt}}}};
        const auto series = calibration_series(stats);
        QCOMPARE(series.size(), 3); // down has no point
        QCOMPARE(series[0].key, QStringLiteral("all"));
        QCOMPARE(series[1].key, QStringLiteral("up"));
        QCOMPARE(series[2].key, QStringLiteral("flat"));
        QCOMPARE(series[1].points[0].observed, 1.0);
        QVERIFY(series[1].points[0].tip.startsWith(QStringLiteral("상승 · 확률 30–40%: 10건")));
        QVERIFY(series[0].points[0].tip.startsWith(QStringLiteral("전체 · ")));
    }

    void the_columns_a_name_could_mislead_say_what_they_are() {
        QVERIFY(
            column_tip(QStringLiteral("중간가 손익(틱)")).startsWith(QStringLiteral("거래당 평균, 연구용 값이다.")));
        QCOMPARE(column_tip(QStringLiteral("95% 구간")),
                 QStringLiteral("봉 마감마다 따로 센 설명용 구간이다. 같은 날의 4분 정답은 서로 겹쳐 실제보다 "
                                "확실해 보인다. p도 그렇다."));
        QCOMPARE(column_tip(QStringLiteral("거래일 95% 구간")),
                 QStringLiteral("하루를 값 하나로 본 구간이다. 상태(수습·정식·벤치)는 이것으로 정한다. "
                                "거래일이 2일보다 적으면 없다."));
        QCOMPARE(column_tip(QStringLiteral("q")),
                 QStringLiteral("순위표의 다른 lift와 함께 보정한 값이다. 순위 밖이거나 나눠 본 줄은 함께 검정하지 "
                                "않아 없다."));
        QCOMPARE(column_tip(QStringLiteral("운 보정")),
                 QStringLiteral("기준 예측 셋을 모두 이긴 것이 운으로 보기 어렵다는 뜻이다(q ≤ 0.05)."));
        QCOMPARE(column_tip(QStringLiteral("표본")), QString());
    }

    void the_lift_title_says_why_there_is_none_then_what_it_is() {
        const QString what = QStringLiteral("lift는 같은 종목·봉 마감의 기준 예측과 정답(1/0) 차이의 평균이다. 열 "
                                            "제목에 마우스를 올리면 각 구간의 뜻이 나온다.");
        AlgorithmStats stats;
        stats.algorithm = QStringLiteral("gauss");
        stats.overall.lifts = {lift("baseline-flat", 0.04, 0.003, true)};
        QCOMPARE(lift_tip(stats), what);
        stats.overall.lifts.clear();
        QCOMPARE(lift_tip(stats), QStringLiteral("기준 예측과 짝지을 채점된 예측이 없어 lift가 없다.\n") + what);
        stats.algorithm = QStringLiteral("baseline-flat");
        QCOMPARE(lift_tip(stats), QStringLiteral("기준 예측 자신이라 lift가 없다.\n") + what);
    }

    void the_calibration_title_says_what_the_plot_compares() {
        const QString what =
            QStringLiteral("확률을 10% 구간으로 나눠, 예측한 확률과 실제로 일어난 비율을 견준다.\n"
                           "전체는 세 클래스를 합친 것이라 방향별 오차를 숨길 수 있어, 상승·보합·하락을 따로 그린다.\n"
                           "점이 점선에 가까울수록 확률을 믿을 수 있다.");
        AlgorithmStats stats;
        QCOMPARE(calibration_tip(stats), QStringLiteral("채점된 예측이 없다.\n") + what);
        stats.calibration = {CalibrationBin{0.3, 0.4, 30, 1.0 / 3, Interval{1.0 / 3, 0.19, 0.51, 30}}};
        QCOMPARE(calibration_tip(stats), what);
    }

    void what_the_stats_left_out_reads_in_korean() {
        const Excluded some{2, 1, {{"timed_out", 3}, {"vi", 4}}};
        QCOMPARE(excluded_text(some, "exclude"),
                 QStringLiteral("제외: 특수 상황 2 · 채점 불가 1 · 시간 초과 3 · VI 4"));
        QCOMPARE(excluded_text(some, "include"),
                 QStringLiteral("특수 상황 포함 2 · 제외: 채점 불가 1 · 시간 초과 3 · VI 4"));
        QCOMPARE(excluded_text(Excluded{2, 0, {}}, "include"), QStringLiteral("특수 상황 포함 2 · 제외 없음"));
        QCOMPARE(excluded_text(Excluded{}, "exclude"), QStringLiteral("제외 없음"));
    }

    void the_calibration_plot_has_a_point_per_bin_with_forecasts_in_it() {
        const QList<CalibrationBin> bins = {
            {0.0, 0.1, 0, std::nullopt, std::nullopt},
            {0.3, 0.4, 412, 0.347, Interval{0.31, 0.27, 0.36, 412}},
        };
        const auto points = calibration_points(bins);
        QCOMPARE(points.size(), 1); // nothing forecast in the first bin
        QCOMPARE(points[0].forecast, 0.347);
        QCOMPARE(points[0].observed, 0.31);
        QCOMPARE(points[0].low, std::optional<double>(0.27));
        QCOMPARE(points[0].high, std::optional<double>(0.36));
        QCOMPARE(points[0].tip,
                 QStringLiteral("확률 30–40%: 412건\n평균 예측 34.7%, 실제 31.0% (95% 구간 27.0–36.0%)"));
    }

    void the_days_read_as_a_range() {
        QCOMPARE(days_text({}), QStringLiteral("예측이 있는 날 없음"));
        QCOMPARE(days_text({"2026-10-08"}), QStringLiteral("2026-10-08"));
        QCOMPARE(days_text({"2026-10-07", "2026-10-08", "2026-10-09"}),
                 QStringLiteral("2026-10-07 ~ 2026-10-09 (3일)"));
    }

    void the_ranking_rules_name_the_days_and_the_least_sample() {
        QCOMPARE(ranking_rules({"2026-10-07", "2026-10-08"}, 30),
                 QStringLiteral("2026-10-07 ~ 2026-10-08 (2일)\n"
                                "순위에는 채점 30건 이상, 기준 예측과의 짝도 그만큼 있어야 오른다.\n"
                                "순위는 가장 약한 lift의 하한 순이다.\n"
                                "운 보정 통과는 기준 예측 셋을 모두 이긴 것이 운으로 보기 어렵다는 뜻이다(q ≤ 0.05).\n"
                                "줄을 두 번 누르면 그 예측의 상세 통계가 열린다."));
    }

    void the_query_reads_in_korean() {
        AlgorithmStats s;
        s.symbol = "005930";
        s.period = "20d";
        s.basis = "a";
        s.special = "include";
        QCOMPARE(query_text(s), QStringLiteral("20일 · 봉 마감 (A) · 005930 · 특수 상황 포함"));
        s.symbol = "all";
        s.period = "today";
        s.basis = "b";
        s.special = "exclude";
        QCOMPARE(query_text(s), QStringLiteral("오늘 · 표시 시점 (B) · 전체 종목 · 특수 상황 제외"));
    }

    void the_weakest_lift_is_its_lower_bound_or_unknown() {
        Entry e;
        QCOMPARE(weakest_lift_text(e), QStringLiteral("—"));
        e.lift_low = -0.03;
        QCOMPARE(weakest_lift_text(e), QStringLiteral("-0.030"));
    }

    void the_about_section_says_what_the_engine_says_about_the_algorithm() {
        AlgorithmInfo info;
        info.name = "alpha";
        info.card = AlgorithmCard{"A. Person", "테스트 성격", "로직 글", {"f1"}, "학습 글"};
        info.features = {"f1", "f2"};
        info.training_days = 20;
        info.model_version = "alpha-1";
        info.params = {{"a", "3"}, {"b", "0.25"}};
        info.trained = "2026-09-01..2026-10-01, 100 samples, log loss 0.7";
        info.trained_until = "2026-10-01";
        info.status = "probation";
        info.since = QDateTime::fromString("2026-10-10T15:09:52+09:00", Qt::ISODate);
        info.history = {
            {QDateTime::fromString("2026-10-10T15:09:52+09:00", Qt::ISODate), {}, "probation", "registered"},
            {QDateTime::fromString("2026-10-23T15:40:00+09:00", Qt::ISODate), "probation", "active", ""}};

        const auto rows = about_rows(info);

        QStringList labels;
        for (const auto& row : rows)
            labels << row.first;
        QCOMPARE(labels, (QStringList{"성격", "예측 로직", "고유 피처", "입력 피처", "학습 방법", "학습 기간", "모델",
                                      "학습 결과", "파라미터", "상태", "상태 이력"}));
        QCOMPARE(rows[0].second, QStringLiteral("테스트 성격 (A. Person)"));
        QCOMPARE(rows[5].second, QStringLiteral("최근 20거래일"));
        QCOMPARE(rows[7].second,
                 QStringLiteral("2026-09-01..2026-10-01, 100 samples, log loss 0.7 · 2026-10-01까지의 데이터"));
        QCOMPARE(rows[8].second, QStringLiteral("a = 3, b = 0.25"));
        QCOMPARE(rows[9].second, QStringLiteral("수습 · 2026-10-10 15:09부터"));
        QCOMPARE(rows[10].second, QStringLiteral("10-10 15:09 수습 (registered)\n10-23 15:40 수습 → 정식"));
    }

    void a_model_not_trained_yet_says_so_and_a_predictor_without_a_card_says_the_engine_gives_none() {
        AlgorithmInfo info;
        info.name = "beta";
        info.model_version = "beta-default";
        const auto rows = about_rows(info);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[1].second, QStringLiteral("학습 기록 없음 (기본값)"));

        const auto none = about_rows(std::nullopt);
        QCOMPARE(none.size(), 1);
        QCOMPARE(none[0].second, QStringLiteral("엔진이 아직 설명을 주지 않음"));

        // A failed read says so, and shows nothing as the engine's, not even what was read before.
        const auto failed = about_rows(info, "엔진에 연결할 수 없습니다");
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed[0].second, QStringLiteral("엔진에서 설명을 읽지 못했습니다: 엔진에 연결할 수 없습니다"));
    }

    void the_card_says_how_it_forecasts_and_each_name_says_what_it_is() {
        // Made-up text: the fork holds none of the engine's own (krx-quant #164).
        AlgorithmInfo info;
        info.name = "alpha";
        info.card = AlgorithmCard{"A. Person", "테스트 성격", "로직 글", {}, "학습 글", "확률 글", "실패 글"};
        info.features = {"f1", "f2"};
        info.feature_meanings = {{"f1", {"뜻 1", "틱", "봉 마감"}}, {"f2", {"뜻 2", "", ""}}};
        info.model_version = "alpha-1";
        info.params = {{"a", "3"}, {"b", "0.25"}};
        info.param_meanings = {{"a", {"뜻 a", "", "밤 학습"}}}; // b's is missing: its name alone

        const auto rows = about_rows(info);

        QStringList labels;
        for (const auto& row : rows)
            labels << row.first;
        QCOMPARE(labels, (QStringList{"성격", "예측 로직", "예측 방식", "실패 조건", "입력 피처", "학습 방법", "모델",
                                      "학습 결과", "파라미터"}));
        QCOMPARE(rows[2].second, QStringLiteral("확률 글"));
        QCOMPARE(rows[3].second, QStringLiteral("실패 글"));
        QCOMPARE(rows[4].second, QStringLiteral("f1 — 뜻 1 (틱 · 봉 마감)\nf2 — 뜻 2"));
        QCOMPARE(rows[8].second, QStringLiteral("a = 3 — 뜻 a (밤 학습)\nb = 0.25"));

        // A newer engine that knows no meaning for these names: still one line each.
        info.feature_meanings = QMap<QString, Meaning>{};
        info.param_meanings = QMap<QString, Meaning>{};
        const auto bare = about_rows(info);
        QCOMPARE(bare[4].second, QStringLiteral("f1\nf2"));
        QCOMPARE(bare[8].second, QStringLiteral("a = 3\nb = 0.25"));
    }

    void a_baseline_or_the_consensus_shows_its_card_only() {
        AlgorithmInfo info;
        info.name = "baseline-x";
        info.kind = "baseline";
        info.card = AlgorithmCard{{}, "기준 성격", "기준 로직", {}, "배우지 않음", "기준 확률", "기준 실패"};

        const auto rows = about_rows(info);

        QStringList labels;
        for (const auto& row : rows)
            labels << row.first;
        QCOMPARE(labels, (QStringList{"성격", "예측 로직", "예측 방식", "실패 조건", "학습 방법"}));
        QCOMPARE(rows[0].second, QStringLiteral("기준 성격")); // no scientist
    }

    void the_learning_section_says_what_it_learned_from_and_how_last_night_went() {
        // Made-up values (krx-quant #165).
        AlgorithmInfo info;
        info.name = "alpha";
        info.training = TrainingInfo{"2026-09-08", "2026-10-07", 1200, 0.81234, 3, 17};
        info.validation = NightValidation{"2026-10-13",
                                          {"2026-10-12", "2026-10-13"},
                                          400,
                                          ValidatedModel{"alpha-2", 0.82, std::nullopt},
                                          ValidatedModel{"alpha-1", std::nullopt, true},
                                          true,
                                          "이유 글"};
        Stage stage;
        stage.status = "probation";
        stage.began = "2026-10-12";
        stage.days = 2;
        stage.next_review = 10;
        stage.next_review_on = "2026-10-23";
        stage.level = 0.983;
        stage.weakest = WeakestLift{"base-x", 2, -0.0123, -0.3867, 0.3621};
        info.stage = stage;

        const auto rows = learning_rows(info);

        QStringList labels;
        for (const auto& row : rows)
            labels << row.first;
        QCOMPARE(labels, (QStringList{"학습 데이터", "학습 손실", "지난밤 검증", "새 모델", "지금 모델", "승격",
                                      "수습 단계", "가장 약한 lift"}));
        QCOMPARE(rows[0].second, QStringLiteral("2026-09-08 – 2026-10-07 · 표본 1,200 · 녹화 호가 3일 · 1분봉 17일"));
        QCOMPARE(rows[1].second, QStringLiteral("0.8123 (학습 데이터로 잰 값)"));
        QCOMPARE(rows[2].second, QStringLiteral("2026-10-13 밤 · 검증일 2026-10-12, 2026-10-13 · 표본 400"));
        QCOMPARE(rows[3].second, QStringLiteral("alpha-2 · 검증 손실 0.8200"));
        QCOMPARE(rows[4].second, QStringLiteral("alpha-1 · 검증 손실 없음 · 검증일로 학습한 모델"));
        QCOMPARE(rows[5].second, QStringLiteral("승격함 · 이유 글"));
        QCOMPARE(rows[6].second, QStringLiteral("2026-10-12부터 · 2거래일 셈 · 다음 판정 10일째 (2026-10-23 예정)"));
        QCOMPARE(rows[7].second, QStringLiteral("base-x 대비 -0.012 · 거래일 하한 -0.387 (2일, 신뢰 98.3%)"));
    }

    void without_values_the_learning_section_says_so_and_a_baseline_has_none() {
        AlgorithmInfo info;
        info.name = "beta";
        const auto rows = learning_rows(info);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].second, QStringLiteral("엔진이 주지 않음 (기본값이거나 이전에 저장한 모델)"));
        QCOMPARE(rows[1].second, QStringLiteral("밤 학습 리포트 없음"));

        // A report without a reason: the verdict alone.
        info.validation = NightValidation{"2026-10-13", {}, std::nullopt, std::nullopt, std::nullopt, false, {}};
        QCOMPARE(learning_rows(info).last(), (QPair<QString, QString>("승격", "승격하지 않음")));

        info.kind = "baseline";
        QVERIFY(learning_rows(info).isEmpty());
        QVERIFY(learning_rows(std::nullopt).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstKrxStatsView)
#include "tst_krx_stats_view.moc"
