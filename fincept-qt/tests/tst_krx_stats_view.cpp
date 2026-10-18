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

/// The range kind as GET /v1/kinds describes it (krx-quant #180, #182).
KindInfo range_kind() {
    KindInfo k;
    k.name = "range";
    k.title = QStringLiteral("변동폭");
    k.classes = {"small", "medium", "large"};
    k.class_titles = {QStringLiteral("작음"), QStringLiteral("보통"), QStringLiteral("큼")};
    k.action = {"small", "large"};
    k.answer_version = 1;
    k.baselines = {{"range-climate", std::nullopt}, {"range-persist", std::nullopt}};
    return k;
}

/// The barrier kind as GET /v1/kinds describes it (krx-quant #183, #185).
KindInfo barrier_kind() {
    KindInfo k;
    k.name = "barrier";
    k.title = QStringLiteral("장벽");
    k.classes = {"up_first", "timeout", "down_first"};
    k.class_titles = {QStringLiteral("위 먼저"), QStringLiteral("시간 만료"), QStringLiteral("아래 먼저")};
    k.action = {"up_first", "down_first"};
    k.answer_version = 1;
    k.baselines = {{"barrier-climate", std::nullopt}, {"barrier-persist", std::nullopt}};
    return k;
}

/// Made-up barrier P&L: 240 scored, 121 trades — 60 longs, 61 shorts —, as the demo engine's
/// barrier-persist (its numbers rounded).
BarrierStats barrier_stats() {
    BarrierStats b;
    b.n = 240;
    b.pnl = BarrierPnl{121, -378.38, Interval{-3.127, -3.856, -2.399, 121}};
    b.longs = BarrierPnl{60, -214.87, Interval{-3.581, -4.564, -2.599, 60}};
    b.shorts = BarrierPnl{61, -163.51, Interval{-2.681, -3.75, -1.611, 61}};
    return b;
}

/// Made-up quantile stats: 240 scored, q50's pinball also by day, against range-climate on q10.
RangeStats range_stats() {
    RangeStats r;
    r.n = 240;
    r.pinball = {{"q10", RangeMean{Interval{0.4, 0.35, 0.45, 240}, std::nullopt}},
                 {"q50", RangeMean{Interval{1.2, 1.0, 1.4, 240}, Interval{1.25, 0.9, 1.6, 2}}},
                 {"q90", RangeMean{std::nullopt, std::nullopt}}};
    r.interval_coverage = Interval{0.78, 0.723, 0.829, 240};
    r.q50_abs_error = Interval{2.4, 2.1, 2.7, 240};
    r.vs_climate =
        QMap<QString, RangeMean>{{"q10", RangeMean{Interval{-0.1, -0.2, 0.0, 240}, Interval{-0.12, -0.3, 0.06, 2}}},
                                 {"q50", RangeMean{Interval{0.05, -0.1, 0.2, 240}, std::nullopt}},
                                 {"q90", RangeMean{std::nullopt, std::nullopt}}};
    return r;
}
/// open_to_close as GET /v1/kinds describes it (krx-quant #264): predicted once a trading day.
KindInfo daily_kind() {
    KindInfo k;
    k.name = "open_to_close";
    k.title = QStringLiteral("시가→종가");
    k.classes = {"up", "flat", "down"};
    k.class_titles = {QStringLiteral("상승"), QStringLiteral("보합"), QStringLiteral("하락")};
    k.action = {"up", "down"};
    k.answer_version = 1;
    k.horizon_minutes = 0;
    k.baselines = {{"otc-flat", std::nullopt}, {"otc-climate", std::nullopt}, {"otc-persist", std::nullopt}};
    DailySchedule day;
    day.predict_at = "08:50";
    day.from_title = QStringLiteral("시가 단일가");
    day.to_title = QStringLiteral("종가 단일가");
    day.answer_from = "20:35";
    day.probation_days = {60, 120, 180};
    day.bench_days = {60, 120, 180};
    day.recent_days = 60;
    k.daily = day;
    return k;
}

/// Made-up open-to-close P&L, basis points: 57 scored, 35 trades — 24 longs, 11 shorts —, as the
/// demo engine's cox (its numbers rounded).
OpenToCloseStats daily_stats() {
    OpenToCloseStats o;
    o.n = 57;
    o.pnl = DailyPnl{35, -1690.2, Interval{-48.29, -85.5, -11.09, 35}};
    o.longs = DailyPnl{24, -922.9, Interval{-38.45, -73.79, -3.12, 24}};
    o.shorts = DailyPnl{11, -767.3, Interval{-69.76, -161.69, 22.18, 11}};
    o.pnl_inverse = DailyPnl{32, -1161.3, Interval{-36.29, -75.19, 2.61, 32}};
    o.shorts_inverse = DailyPnl{8, -238.4, Interval{-29.8, -150.1, 90.5, 8}};
    return o;
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
        QVERIFY(column_tip(QStringLiteral("닿음 손익(틱, 연구용)"))
                    .startsWith(QStringLiteral("거래당 평균, 연구용 값이다.")));
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
        s.basis = "a"; // asked by an older screen: the touch target is the only one (krx-quant #170)
        s.special = "include";
        QCOMPARE(query_text(s), QStringLiteral("20일 · 005930 · 특수 상황 포함"));
        s.symbol = "all";
        s.period = "today";
        s.basis.clear();
        s.special = "exclude";
        QCOMPARE(query_text(s), QStringLiteral("오늘 · 전체 종목 · 특수 상황 제외"));
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
        info.card = AlgorithmCard{"A. Person", "테스트 성격", "로직 글", {"f1"}, "학습 글", {}, {}};
        info.features = {"f1", "f2"};
        info.training_days = 20;
        info.model_version = "alpha-1";
        info.params = {{"a", "3"}, {"b", "0.25"}};
        info.trained = "2026-09-01..2026-10-01, 100 samples, log loss 0.7";
        info.trained_until = "2026-10-01";
        info.status = "probation";
        info.since = QDateTime::fromString("2026-10-10T15:09:52+09:00", Qt::ISODate);
        info.history = {
            {QDateTime::fromString("2026-10-10T15:09:52+09:00", Qt::ISODate), {}, "probation", "registered", false},
            {QDateTime::fromString("2026-10-23T15:40:00+09:00", Qt::ISODate), "probation", "active", "", false},
            // A simulated probation's review at once (krx-quant #298) is marked before its reason.
            {QDateTime::fromString("2026-10-23T23:10:00+09:00", Qt::ISODate), "active", "bench", "이유", true}};

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
        QCOMPARE(rows[10].second, QStringLiteral("10-10 15:09 수습 (registered)\n10-23 15:40 수습 → 정식\n"
                                                 "10-23 23:10 정식 → 벤치 (시뮬레이션 판정 · 이유)"));
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
        stage.counted_from = "2026-10-12";
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

        // Waiting for a model trained on the touch target (krx-quant #170): nothing counted yet.
        info.stage->counted_from.clear();
        info.stage->waiting = true;
        info.stage->days = 0;
        QCOMPARE(learning_rows(info)[6].second,
                 QStringLiteral("2026-10-12부터 · 0거래일 셈 · 새 정답으로 학습한 모델을 기다림"));

        // A simulated probation (krx-quant #298): its days are counted first, so nothing waits,
        // and its replay and review at once get a row of their own.
        info.stage->days = 20;
        info.stage->simulated = 20;
        info.stage->next_review = 40;
        info.stage->next_review_on = "2026-11-17";
        Simulation sim;
        sim.cadence = "daily";
        sim.days = 20;
        sim.first_day = "2026-09-21";
        sim.last_day = "2026-10-19";
        sim.review_day = 20;
        sim.status = "probation";
        info.simulation = sim;
        const auto simulated = learning_rows(info);
        QCOMPARE(simulated[6].second, QStringLiteral("2026-10-12부터 · 20거래일 셈 (시뮬레이션 20일 포함) · 다음 판정 "
                                                     "40일째 (2026-11-17 예정)"));
        QCOMPARE(simulated.last().first, QStringLiteral("수습 시뮬레이션"));
        QCOMPARE(simulated.last().second, QStringLiteral("2026-09-21 – 2026-10-19 · 20일 · 날마다 그 전날까지로 다시 학습 "
                                                         "· 20일째 판정: 통과하지 못해 수습이 이어짐"));
        sim.cadence = "bar_close";
        sim.days = 10;
        sim.review_day = 10;
        sim.status = "active";
        info.simulation = sim;
        info.stage.reset(); // active: no stage
        QCOMPARE(learning_rows(info).last().second,
                 QStringLiteral("2026-09-21 – 2026-10-19 · 10일 · 첫날 전까지로 한 번 학습 · 10일째 판정: 정식이 됨"));
        info.simulation = Simulation{QString(), 0, QString(), QString(), std::nullopt, QString(), "nothing to learn"};
        QCOMPARE(learning_rows(info).last().second, QStringLiteral("하지 못함: nothing to learn"));
    }

    void a_range_leaderboard_has_its_action_classes_coverage_and_q50_error_and_no_touch_p_and_l() {
        const auto columns = kind_stats_columns(range_kind());
        QStringList headers;
        for (const auto& c : columns)
            headers << c.header;
        QCOMPARE(headers, (QStringList{"표본", "정답률", "작음·큼 정밀도", "커버리지", "Brier", "구간 적중률",
                                       "q50 오차(틱)", "가장 약한 lift 하한"}));
        QCOMPARE(columns[2].tip, QStringLiteral("작음·큼(이)라고 한 예측 중 맞은 비율이다."));
        QVERIFY(columns[5].tip.startsWith(QStringLiteral("실제 변동폭이 q10~q90 안에 든 비율이다.")));

        Entry e;
        e.n = 240;
        e.accuracy = Interval{0.5, 0.44, 0.56, 240};
        e.brier = Interval{0.6, 0.58, 0.62, 240};
        e.lift_low = -0.02;
        e.range = range_stats();
        const auto cells = kind_stats_cells(e, range_kind());
        QCOMPARE(cells.size(), columns.size());
        QCOMPARE(cells[0].text, QStringLiteral("240"));
        QCOMPARE(cells[1].text, QStringLiteral("50.0%"));
        QCOMPARE(cells[2].text, QStringLiteral("—")); // nothing called small or large
        QCOMPARE(cells[5].text, QStringLiteral("78.0%"));
        QCOMPARE(cells[5].tip, QStringLiteral("95% 구간 72.3–82.9% (n 240)"));
        QCOMPARE(cells[6].text, QStringLiteral("2.40"));
        QCOMPARE(cells[6].tip, QStringLiteral("95% 구간 2.10 – 2.70 (n 240)"));
        QCOMPARE(cells[7].text, QStringLiteral("-0.020"));
        // Another kind without quantiles (one this screen does not know): no range columns; without
        // its stats, unknown.
        KindInfo other = range_kind();
        other.name = "weather";
        QCOMPARE(kind_stats_columns(other).size(), 6);
        e.range.reset();
        QCOMPARE(kind_stats_cells(e, range_kind())[5].text, QStringLiteral("—"));
    }

    void the_quantile_stats_have_each_pinball_and_how_it_did_against_climate() {
        const auto rows = quantile_rows(range_stats());
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].quantile, QStringLiteral("q10"));
        QCOMPARE(rows[0].pinball, QStringLiteral("0.40"));
        QCOMPARE(rows[0].interval, QStringLiteral("0.35 – 0.45"));
        QCOMPARE(rows[0].day_interval, QStringLiteral("—")); // fewer than two days
        QCOMPARE(rows[0].days, QStringLiteral("—"));
        QCOMPARE(rows[0].vs_climate, QStringLiteral("-0.10"));
        QCOMPARE(rows[0].vs_climate_interval, QStringLiteral("-0.30 – +0.06"));
        QCOMPARE(rows[1].day_interval, QStringLiteral("0.90 – 1.60"));
        QCOMPARE(rows[1].days, QStringLiteral("2"));
        QCOMPARE(rows[1].vs_climate, QStringLiteral("+0.05"));
        QCOMPARE(rows[2].pinball, QStringLiteral("—"));
        // range-climate itself: nothing against itself.
        RangeStats climate = range_stats();
        climate.vs_climate.reset();
        QCOMPARE(quantile_rows(climate)[0].vs_climate, QStringLiteral("—"));
        QCOMPARE(quantile_rows(climate)[0].vs_climate_interval, QStringLiteral("—"));
    }

    void a_baseline_without_the_window_before_says_so_in_korean() {
        // range-persist (and the direction's baseline-persist) make none without the window before.
        QCOMPARE(excluded_text(Excluded{4, 0, {{"no_previous", 12}}}, "exclude"),
                 QStringLiteral("제외: 특수 상황 4 · 직전 창 없음 12"));
    }

    void the_quantile_summary_says_coverage_q50_error_and_samples() {
        QCOMPARE(range_summary(range_stats()),
                 QStringLiteral("구간 적중률(q10~q90) 78.0% (95% 72.3–82.9%, 이상 80%) · q50 오차 2.40틱 (95% 2.10 – "
                                "2.70) · 표본 240"));
        QCOMPARE(range_summary(RangeStats{}), QStringLiteral("채점된 예측 없음"));
    }

    void a_range_predictor_s_calibration_draws_its_classes() {
        AlgorithmStats stats;
        stats.kind = "range";
        const CalibrationBin bin{0.3, 0.4, 10, 0.35, Interval{0.4, 0.2, 0.6, 10}};
        stats.calibration = {bin};
        stats.calibration_by_class = {{"small", {bin}}, {"large", {bin}}, {"medium", {}}};
        const auto series = calibration_series(stats, range_kind());
        QCOMPARE(series.size(), 3); // medium has no point
        QCOMPARE(series[0].key, QStringLiteral("all"));
        QCOMPARE(series[1].key, QStringLiteral("small"));
        QCOMPARE(series[1].name, QStringLiteral("작음"));
        QCOMPARE(series[2].key, QStringLiteral("large"));
        QVERIFY(series[2].points[0].tip.startsWith(QStringLiteral("큼 · 확률 30–40%")));
        QVERIFY(calibration_tip(stats, range_kind()).contains(QStringLiteral("작음·보통·큼을 따로 그린다")));
        // The direction's as before.
        QVERIFY(calibration_tip(stats).contains(QStringLiteral("상승·보합·하락을 따로 그린다")));
    }

    void a_range_baseline_has_no_lift_of_its_own() {
        AlgorithmStats stats;
        stats.kind = "range";
        stats.algorithm = "range-climate";
        QVERIFY(lift_tip(stats, range_kind()).startsWith(QStringLiteral("기준 예측 자신이라 lift가 없다.")));
        stats.algorithm = "alg-r";
        QVERIFY(lift_tip(stats, range_kind())
                    .startsWith(QStringLiteral("기준 예측과 짝지을 채점된 예측이 없어 lift가 없다.")));
    }

    void a_range_leaderboard_s_rules_say_what_it_is_scored_on_and_against() {
        const QString rules = ranking_rules({"2026-10-12"}, 200, range_kind());
        QCOMPARE(rules.split('\n')[1], QStringLiteral("변동폭 예측을 변동폭 정답으로 센다. 정밀도와 커버리지는 "
                                                      "작음·큼(으)로 센다."));
        QCOMPARE(luck_tip(range_kind()),
                 QStringLiteral("운 보정 통과는 기준 예측(기준: 변동폭 시간대 기후, 기준: 직전 4분 변동폭)을 모두 이긴 "
                                "것이 운으로 보기 어렵다는 뜻이다(q ≤ 0.05)."));
        QVERIFY(rules.contains(luck_tip(range_kind())));
        // The direction's rules are as before.
        QCOMPARE(ranking_rules({"2026-10-12"}, 200), ranking_rules({"2026-10-12"}, 200, std::nullopt));
        QVERIFY(ranking_rules({"2026-10-12"}, 200).contains(QStringLiteral("기준 예측 셋을 모두 이긴")));
    }

    void a_barrier_leaderboard_has_up_and_down_first_precision_and_the_barrier_p_and_l() {
        // krx-quant #185: the kind's columns, its barrier P&L (its extra) before the weakest lift.
        const auto columns = kind_stats_columns(barrier_kind());
        QStringList headers;
        for (const auto& c : columns)
            headers << c.header;
        QCOMPARE(headers, (QStringList{"표본", "정답률", "위 먼저·아래 먼저 정밀도", "커버리지", "Brier",
                                       "장벽 손익(틱, 연구용)", "가장 약한 lift 하한"}));
        QCOMPARE(columns[2].tip, QStringLiteral("위 먼저·아래 먼저(이)라고 한 예측 중 맞은 비율이다."));
        QVERIFY(columns[5].tip.startsWith(QStringLiteral("거래당 평균, 연구용 값이다. 위 먼저는 봉 마감의 매도 "
                                                         "1호가에 사서 위 장벽 U에 먼저 닿으면 U에")));
        QCOMPARE(column_tip(QStringLiteral("장벽 손익(틱, 연구용)")), columns[5].tip);
        KindInfo rung = barrier_kind(); // the barrier's horizon ladder (krx-quant #263) has it too
        rung.name = QStringLiteral("barrier_15m");
        QCOMPARE(kind_stats_columns(rung)[5].header, QStringLiteral("장벽 손익(틱, 연구용)"));

        Entry e;
        e.n = 240;
        e.accuracy = Interval{0.4125, 0.352, 0.476, 240};
        e.precision = Interval{0.281, 0.209, 0.367, 121};
        e.lift_low = -0.05;
        e.barrier = barrier_stats();
        const auto cells = kind_stats_cells(e, barrier_kind());
        QCOMPARE(cells.size(), columns.size());
        QCOMPARE(cells[2].text, QStringLiteral("28.1%"));
        QCOMPARE(cells[5].text, QStringLiteral("-3.13"));
        QCOMPARE(cells[5].tip, QStringLiteral("거래 121건, 합계 -378.4틱\n95% 구간 -3.86 – -2.40 (n 121)\n"
                                              "롱(위 먼저) 60건 평균 -3.58틱 · 숏(아래 먼저, 주식 공매도) 61건 평균 -2.68틱"));
        QCOMPARE(cells[6].text, QStringLiteral("-0.050"));
        // barrier-climate's default (1/3 each) calls every bar close "timeout": no trade.
        e.barrier = BarrierStats{252, {}, {}, {}};
        QCOMPARE(kind_stats_cells(e, barrier_kind())[5].text, QStringLiteral("—"));
        QCOMPARE(kind_stats_cells(e, barrier_kind())[5].tip,
                 QStringLiteral("거래 0건, 합계 0.0틱\n롱(위 먼저) 0건 · 숏(아래 먼저, 주식 공매도) 0건"));
        // An engine that gives no barrier P&L: unknown.
        e.barrier.reset();
        QCOMPARE(kind_stats_cells(e, barrier_kind())[5].text, QStringLiteral("—"));
        QVERIFY(kind_stats_cells(e, barrier_kind())[5].tip.isEmpty());
    }

    void the_barrier_p_and_l_has_all_its_trades_then_each_side() {
        const auto rows = barrier_pnl_rows(barrier_stats());
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].side, QStringLiteral("전체"));
        QCOMPARE(rows[0].trades, QStringLiteral("121"));
        QCOMPARE(rows[0].total, QStringLiteral("-378.4"));
        QCOMPARE(rows[0].per_trade, QStringLiteral("-3.13"));
        QCOMPARE(rows[0].interval, QStringLiteral("-3.86 – -2.40"));
        QCOMPARE(rows[1].side, QStringLiteral("롱(위 먼저)"));
        QCOMPARE(rows[1].trades, QStringLiteral("60"));
        QCOMPARE(rows[2].side, QStringLiteral("숏(아래 먼저)"));
        QCOMPARE(rows[2].per_trade, QStringLiteral("-2.68"));
        // A side without a trade: no mean.
        BarrierStats longs_only = barrier_stats();
        longs_only.shorts = BarrierPnl{};
        QCOMPARE(barrier_pnl_rows(longs_only)[2].trades, QStringLiteral("0"));
        QCOMPARE(barrier_pnl_rows(longs_only)[2].per_trade, QStringLiteral("—"));
        QCOMPARE(barrier_pnl_rows(longs_only)[2].interval, QStringLiteral("—"));
    }

    void the_touch_p_and_l_tip_has_the_inverse_product_s_beside_the_stock_short() {
        // krx-quant #267: the cell is the stock short's; its tooltip adds the inverse product's.
        Entry e;
        e.trades = 96;
        e.total_ticks = 12.4;
        e.per_trade = Interval{0.13, -0.2, 0.46, 96};
        Shown shown = pnl_shown(e);
        QCOMPARE(shown.text, QStringLiteral("0.13"));
        QVERIFY(shown.tip.startsWith(QStringLiteral("거래 96건, 합계 12.4틱 (하락은 주식 공매도로 친 값)")));
        QVERIFY(!shown.tip.contains(QStringLiteral("인버스 상품")));
        e.inverse = BarrierPnl{80, 49.6, Interval{0.62, 0.11, 1.13, 80}};
        shown = pnl_shown(e);
        QVERIFY(shown.tip.contains(QStringLiteral("하락을 인버스 상품으로 친 값: 거래 80건 평균 0.62틱 (합계 49.6틱)")));
        QVERIFY(shown.tip.contains(QStringLiteral("삼성전기")));
        QCOMPARE(pnl_shown(Entry{}).text, QStringLiteral("—"));
        // The column tips no longer say an inverse product is assumed for the stock short.
        for (const QString& header : {QStringLiteral("닿음 손익(틱, 연구용)"), QString::fromUtf8(kBarrierPnlHeader)}) {
            QVERIFY2(!column_tip(header).contains(QStringLiteral("인버스를 가정")), qPrintable(header));
            QVERIFY(column_tip(header).contains(QStringLiteral("주식 공매도")));
        }
    }

    void the_barrier_p_and_l_adds_the_shorts_through_the_inverse_product() {
        BarrierStats b = barrier_stats();
        QCOMPARE(barrier_pnl_rows(b).size(), 3); // an engine before krx-quant #267
        b.pnl_inverse = BarrierPnl{100, -190.2, Interval{-1.902, -2.6, -1.2, 100}};
        b.shorts_inverse = BarrierPnl{40, 24.67, Interval{0.617, -0.4, 1.6, 40}};
        const auto rows = barrier_pnl_rows(b);
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[3].side, QStringLiteral("숏(인버스 상품)"));
        QCOMPARE(rows[3].trades, QStringLiteral("40"));
        QCOMPARE(rows[3].per_trade, QStringLiteral("0.62"));
        const QString tip = barrier_pnl_shown(b).tip;
        QVERIFY(tip.contains(QStringLiteral("숏(아래 먼저, 주식 공매도)")));
        QVERIFY(tip.contains(QStringLiteral("숏을 인버스 상품으로 친 값: 전체 100건 평균 -1.90틱 (합계 -190.2틱) · 숏 40건")));
    }

    void the_barrier_summary_says_the_trades_their_mean_and_total() {
        QCOMPARE(barrier_summary(barrier_stats()),
                 QStringLiteral("채점 240개 중 거래 121건 (롱 60 · 숏 61) · 거래당 -3.13틱 (95% -3.86 – -2.40) · "
                                "합계 -378.4틱"));
        QCOMPARE(barrier_summary(BarrierStats{252, {}, {}, {}}),
                 QStringLiteral("채점 252개 · 거래 없음 (모두 시간 만료 예측)"));
        QCOMPARE(barrier_summary(BarrierStats{}), QStringLiteral("채점된 예측 없음"));
    }

    void a_barrier_predictor_s_calibration_draws_its_classes_and_its_baselines_have_no_lift() {
        AlgorithmStats stats;
        stats.kind = "barrier";
        stats.algorithm = "barrier-persist";
        const CalibrationBin bin{0.9, 1.0, 40, 1.0, Interval{0.3, 0.2, 0.4, 40}};
        stats.calibration = {bin};
        stats.calibration_by_class = {{"up_first", {bin}}, {"timeout", {bin}}, {"down_first", {bin}}};
        const auto series = calibration_series(stats, barrier_kind());
        QCOMPARE(series.size(), 4);
        QCOMPARE(series[1].key, QStringLiteral("up_first"));
        QCOMPARE(series[2].name, QStringLiteral("시간 만료"));
        QCOMPARE(series[3].key, QStringLiteral("down_first"));
        QVERIFY(calibration_tip(stats, barrier_kind()).contains(QStringLiteral("위 먼저·시간 만료·아래 먼저를 따로")));
        QVERIFY(lift_tip(stats, barrier_kind()).startsWith(QStringLiteral("기준 예측 자신이라 lift가 없다.")));
        QCOMPARE(ranking_rules({"2026-10-12"}, 200, barrier_kind()).split('\n')[1],
                 QStringLiteral("장벽 예측을 장벽 정답으로 센다. 정밀도와 커버리지는 위 먼저·아래 먼저(으)로 센다."));
        QCOMPARE(luck_tip(barrier_kind()),
                 QStringLiteral("운 보정 통과는 기준 예측(기준: 장벽 시간대 기후, 기준: 직전 4분 장벽)을 모두 이긴 "
                                "것이 운으로 보기 어렵다는 뜻이다(q ≤ 0.05)."));
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

    void the_skill_has_its_seven_cells_each_with_what_it_is() {
        // krx-quant #262: the skill against the class rates, beside the overall line's accuracy.
        Skill s;
        s.n = 1128;
        s.accuracy = 0.52;
        s.expected_accuracy = 0.491;
        s.kappa = 0.057;
        s.gerrity = -0.012;
        s.balanced_accuracy = 0.384;
        s.chi2 = 14.234;
        s.df = 4;
        s.p = 0.0066;
        s.kappa_by_day = Interval{0.0412, -0.0105, 0.0929, 5};
        s.kappa_by_day_p = 0.048;
        const auto cells = skill_cells(s);
        QStringList headers, values;
        for (const auto& c : cells) {
            headers << c.header;
            values << c.value;
            QVERIFY2(!c.tip.isEmpty(), qPrintable(c.header));
        }
        QCOMPARE(headers, (QStringList{"기대 정답률", "기대 대비", "kappa", "거래일 kappa", "Gerrity", "균형 정답률",
                                       "독립성 검정"}));
        QCOMPARE(values, (QStringList{"49.1%", "+2.9%p", "+0.057", "+0.041 (-0.011 – +0.093)", "-0.012", "38.4%",
                                      "p 0.007 (χ² 14.23, 자유도 4)"}));
        QVERIFY(cells[3].tip.endsWith(QStringLiteral("\n5일 · 한쪽 p 0.048")));
        QVERIFY(cells[6].tip.contains("p가 실제보다 작다"));

        s.p = 0.0004;
        s.accuracy = 0.45;
        QCOMPARE(skill_cells(s)[6].value, QStringLiteral("p <0.001 (χ² 14.23, 자유도 4)"));
        QCOMPARE(skill_cells(s)[1].value, QStringLiteral("-4.1%p"));
        s.gerrity = -0.0001; // rounds to nothing: no "-0.000"
        QCOMPARE(skill_cells(s)[4].value, QStringLiteral("0.000"));
        QVERIFY(skill_tip().contains("lift"));
        // The headers are the same whatever the skill: the window builds its columns from an empty one.
        QCOMPARE(skill_cells({}).size(), 7);
        QCOMPARE(skill_cells({})[0].header, QStringLiteral("기대 정답률"));
    }

    void a_skill_that_cannot_be_told_says_why() {
        Skill s;
        s.n = 50;
        s.accuracy = s.expected_accuracy = 0.6;
        s.balanced_accuracy = 1.0 / 3;
        s.df = 0; // one class called: no test
        auto cells = skill_cells(s);
        QCOMPARE(cells[1].value, QStringLiteral("0.0%p"));
        QCOMPARE(cells[2].value, QStringLiteral("—"));
        QVERIFY(cells[2].tip.contains("한 클래스라 낼 수 없다"));
        QCOMPARE(cells[3].value, QStringLiteral("—")); // today alone, or an engine before krx-quant #278
        QVERIFY(cells[3].tip.contains("2일보다 적거나"));
        QCOMPARE(cells[4].value, QStringLiteral("—"));
        QVERIFY(cells[4].tip.contains("한 번도 일어나지 않아"));
        QCOMPARE(cells[5].value, QStringLiteral("33.3%"));
        QCOMPARE(cells[6].value, QStringLiteral("—"));
        QVERIFY(cells[6].tip.contains("한 클래스뿐이라 검정할 수 없다"));

        s.df = 4; // a used cell expecting fewer than five
        cells = skill_cells(s);
        QCOMPARE(cells[6].value, QStringLiteral("—"));
        QVERIFY(cells[6].tip.contains("Cochran"));

        s.kappa_by_day = Interval{0.0, 0.0, 0.0, 3}; // always flat, day after day: no spread
        s.kappa_by_day_p = 1.0;
        QCOMPARE(skill_cells(s)[3].value, QStringLiteral("0.000 (0.000 – 0.000)"));
    }

    void the_volatility_terciles_read_in_korean_with_each_symbol_s_cuts() {
        QCOMPARE(volatility_label("low"), QStringLiteral("낮음"));
        QCOMPARE(volatility_label("mid"), QStringLiteral("중간"));
        QCOMPARE(volatility_label("high"), QStringLiteral("높음"));
        QCOMPARE(volatility_label("other"), QStringLiteral("other"));
        QCOMPARE(kVolatilityTerciles, (QStringList{"low", "mid", "high"}));

        AlgorithmStats stats;
        stats.volatility_cuts.insert("005930", {12.34, 18.0});
        stats.volatility_cuts.insert("000660", {20.5, 31.25});
        const QString tip = volatility_tip(stats);
        QVERIFY(tip.contains("bp"));
        QVERIFY(tip.contains(QStringLiteral("\n000660: 낮음 ≤ 20.5bp < 중간 ≤ 31.3bp < 높음")));
        QVERIFY(tip.contains(QStringLiteral("\n005930: 낮음 ≤ 12.3bp < 중간 ≤ 18.0bp < 높음")));
        QVERIFY(volatility_tip(AlgorithmStats{}).endsWith(QStringLiteral("나누지 못했다.")));
    }

    // ---- A kind predicted once a trading day (krx-quant #264) ----

    void a_daily_leaderboard_counts_a_symbol_s_days_and_has_its_open_to_close_p_and_l() {
        const auto columns = kind_stats_columns(daily_kind());
        QStringList headers;
        for (const auto& c : columns)
            headers << c.header;
        QCOMPARE(headers, (QStringList{"표본(종목·일)", "정답률", "상승·하락 정밀도", "커버리지", "Brier",
                                       "시가→종가 손익(bp, 연구용)", "가장 약한 lift 하한"}));
        QCOMPARE(columns[0].tip,
                 QStringLiteral("채점된 예측 수다. 하루 한 번 예측이라 종목마다 하루에 하나다(종목 × 날)."));
        QVERIFY(columns[5].tip.startsWith(QStringLiteral("거래당 평균, 시가 대비 bp, 연구용 값이다. 상승 예측은 시가 "
                                                         "단일가에 사서 종가 단일가에 판다고 친다.")));
        QCOMPARE(columns[5].tip, daily_pnl_tip());
        QCOMPARE(daily_pnl_header(daily_kind()), columns[5].header);
        // A bar-close kind's samples stay "표본".
        QCOMPARE(kind_stats_columns(range_kind())[0].header, QStringLiteral("표본"));

        Entry e;
        e.n = 57;
        e.accuracy = Interval{0.263, 0.166, 0.39, 57};
        e.lift_low = -0.313;
        e.open_to_close = daily_stats();
        e.barrier = barrier_stats(); // a barrier's P&L is not the daily kind's column
        const auto cells = kind_stats_cells(e, daily_kind());
        QCOMPARE(cells.size(), columns.size());
        QCOMPARE(cells[0].text, QStringLiteral("57"));
        QCOMPARE(cells[5].text, QStringLiteral("-48.29"));
        QCOMPARE(cells[5].tip,
                 QStringLiteral("거래 35건, 합계 -1,690.2bp\n95% 구간 -85.50 – -11.09 (n 35)\n"
                                "롱(상승) 24건 평균 -38.45bp · 숏(하락, 주식 공매도) 11건 평균 -69.76bp\n"
                                "하락을 인버스 상품으로 친 값: 전체 32건 평균 -36.29bp (합계 -1,161.3bp) · "
                                "숏 8건 평균 -29.80bp (합계 -238.4bp)"));
        QCOMPARE(cells[6].text, QStringLiteral("-0.313"));
        // "늘 보합" never trades; an engine without the stats: unknown.
        e.open_to_close = OpenToCloseStats{57, {}, {}, {}};
        QCOMPARE(kind_stats_cells(e, daily_kind())[5].text, QStringLiteral("—"));
        QCOMPARE(kind_stats_cells(e, daily_kind())[5].tip,
                 QStringLiteral("거래 0건, 합계 0.0bp\n롱(상승) 0건 · 숏(하락, 주식 공매도) 0건"));
        e.open_to_close.reset();
        QCOMPARE(kind_stats_cells(e, daily_kind())[5].text, QStringLiteral("—"));
        QVERIFY(kind_stats_cells(e, daily_kind())[5].tip.isEmpty());
    }

    void the_daily_p_and_l_has_all_its_trades_each_side_and_the_inverse_product_s() {
        const auto rows = daily_pnl_rows(daily_stats());
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[0].side, QStringLiteral("전체"));
        QCOMPARE(rows[0].trades, QStringLiteral("35"));
        QCOMPARE(rows[0].total, QStringLiteral("-1,690.2"));
        QCOMPARE(rows[0].per_trade, QStringLiteral("-48.29"));
        QCOMPARE(rows[0].interval, QStringLiteral("-85.50 – -11.09"));
        QCOMPARE(rows[1].side, QStringLiteral("롱(상승)"));
        QCOMPARE(rows[2].side, QStringLiteral("숏(하락)"));
        QCOMPARE(rows[3].side, QStringLiteral("숏(인버스 상품)"));
        QCOMPARE(rows[3].trades, QStringLiteral("8"));
        OpenToCloseStats older = daily_stats();
        older.shorts_inverse.reset();
        QCOMPARE(daily_pnl_rows(older).size(), 3);
        QCOMPARE(daily_pnl_summary(daily_stats()),
                 QStringLiteral("채점 57개 중 거래 35건 (롱 24 · 숏 11) · 거래당 -48.29bp (95% -85.50 – -11.09) · "
                                "합계 -1,690.2bp"));
        QCOMPARE(daily_pnl_summary(OpenToCloseStats{57, {}, {}, {}}),
                 QStringLiteral("채점 57개 · 거래 없음 (모두 보합 예측)"));
        QCOMPARE(daily_pnl_summary(OpenToCloseStats{}), QStringLiteral("채점된 예측 없음"));
    }

    void a_daily_leaderboard_s_rules_say_a_sample_is_a_symbol_s_day_and_when_it_is_answered() {
        const QString rules = ranking_rules({"2026-10-12", "2026-10-08"}, 200, daily_kind());
        const QStringList lines = rules.split('\n');
        QCOMPARE(lines[0], QStringLiteral("2026-10-12 ~ 2026-10-08 (2일)"));
        QCOMPARE(lines[1],
                 QStringLiteral("시가→종가 예측을 시가→종가 정답으로 센다. 정밀도와 커버리지는 상승·하락(으)로 센다."));
        QCOMPARE(lines[2],
                 QStringLiteral("하루 한 번 예측이다. 채점 한 건은 종목 하나의 하루이고, 기간의 날은 예측이 있는 "
                                "최근 거래일이다. 그날 정답은 KRX 일봉이 저장된 뒤(20:35부터)라 오늘은 밤까지 "
                                "비어 있다."));
        QCOMPARE(lines[3],
                 QStringLiteral("상태(수습·벤치)는 순위와 따로, 정답이 있는 날 60·120·180일째에 판정한다(최근 60일)."));
        QCOMPARE(lines[4], QStringLiteral("순위에는 채점 200건 이상, 기준 예측과의 짝도 그만큼 있어야 오른다."));
        QVERIFY(rules.contains(
            QStringLiteral("기준 예측(기준: 시가→종가 늘 보합, 기준: 시가→종가 기후, 기준: 어제 방향 잇기)")));
        // A bar-close kind's rules have no daily lines.
        QVERIFY(!ranking_rules({}, 200, range_kind()).contains(QStringLiteral("하루 한 번")));
    }

    void a_daily_kind_s_special_days_pairs_and_intervals_are_its_days() {
        QCOMPARE(special_tip(), QStringLiteral("VI·서킷브레이커·사이드카가 4분 구간에 낀 예측도 센다(기본은 뺀다)."));
        QCOMPARE(special_tip(range_kind()), special_tip());
        QCOMPARE(special_tip(daily_kind()),
                 QStringLiteral("그날 서킷브레이커·사이드카가 있던 날의 예측도 센다(기본은 뺀다). VI는 특수 상황이 "
                                "아니다(그날 그 종목의 VI 수는 정답에 남는다)."));
        QCOMPARE(lift_header_tip(QStringLiteral("95% 구간"), daily_kind()),
                 QStringLiteral("예측(종목 × 날)마다 따로 센 설명용 구간이다. 같은 날의 종목들은 함께 움직여 실제보다 "
                                "확실해 보일 수 있다. p도 그렇다."));
        QCOMPARE(lift_header_tip(QStringLiteral("짝"), daily_kind()),
                 QStringLiteral("같은 종목·날에 둘 다 채점된 예측의 수다."));
        // The others as the direction's.
        QCOMPARE(lift_header_tip(QStringLiteral("95% 구간"), range_kind()), column_tip(QStringLiteral("95% 구간")));
        QCOMPARE(lift_header_tip(QStringLiteral("거래일 95% 구간"), daily_kind()),
                 column_tip(QStringLiteral("거래일 95% 구간")));
        AlgorithmStats stats;
        stats.kind = "open_to_close";
        stats.algorithm = "cox";
        QVERIFY(lift_tip(stats, daily_kind()).contains(QStringLiteral("lift는 같은 종목·날의 기준 예측과")));
        stats.algorithm = "otc-persist";
        QVERIFY(lift_tip(stats, daily_kind()).startsWith(QStringLiteral("기준 예측 자신이라 lift가 없다.")));
        stats.kind = "range";
        stats.algorithm = "mandelbrot";
        QVERIFY(lift_tip(stats, range_kind()).contains(QStringLiteral("같은 종목·봉 마감의")));
    }
};

QTEST_GUILESS_MAIN(TstKrxStatsView)
#include "tst_krx_stats_view.moc"
