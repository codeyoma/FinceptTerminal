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
};

QTEST_GUILESS_MAIN(TstKrxStatsView)
#include "tst_krx_stats_view.moc"
