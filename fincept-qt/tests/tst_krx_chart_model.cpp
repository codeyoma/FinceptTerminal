// Unit tests for src/services/krx_quant/ChartModel.h
//
// The KRX chart's page only draws: what goes on it is worked out here. These tests pin the
// chart's time (KST shown as the chart's UTC), the bars, the consensus icons (their arrow,
// result, faintness, signal and the bar they sit on), each bar's hover box and target, the
// selected consensus's score and the selection itself (krx-quant #159). The expected
// values are worked out by hand.

#include "services/krx_quant/ChartModel.h"

#include <QTest>

#include <limits>

using namespace fincept::krx_quant;

namespace {

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
}

Mark mark(const char* bar_close, Direction predicted, Mark::Result result, bool special = false) {
    Prediction p;
    p.algorithm = "consensus-final";
    p.status = "ok";
    p.bar_close = kst(bar_close);
    p.target_at = p.bar_close.addSecs(240);
    p.p_up = 0.62;
    p.p_flat = 0.25;
    p.p_down = 0.13;
    return Mark{kst(bar_close), predicted, 0.5, result, special, {}, p};
}

/// A forecast of ``algorithm`` at 09:31: up 62%, flat 25%, down 13% unless given.
Prediction forecast(const QString& algorithm, double up = 0.62, double flat = 0.25, double down = 0.13) {
    Prediction p;
    p.algorithm = algorithm;
    p.status = "ok";
    p.bar_close = kst("2026-10-12T09:31:00+09:00");
    p.target_at = p.bar_close.addSecs(240);
    p.p_up = up;
    p.p_flat = flat;
    p.p_down = down;
    return p;
}

/// ``p`` scored up on basis A, the mid-point 79,000 at the bar close and 79,300 four minutes on.
Prediction scored_up(Prediction p) {
    p.outcome = Outcome{Direction::Up, Direction::Down, false, {}, 79000.0, 79100.0, 79300.0};
    return p;
}

/// A final consensus at 09:31 up 62%, three of its five members up, scored up.
Mark final_up() {
    Mark m = mark("2026-10-12T09:31:00+09:00", Direction::Up, Mark::Result::Hit);
    m.prediction.votes = Votes{3, 1, 1};
    return m;
}

const QList<Bar> kBars{{kst("2026-10-12T09:30:00+09:00"), 79000, 79000, 79000, 79000, 10},
                       {kst("2026-10-12T09:31:00+09:00"), 79000, 79000, 79000, 79000, 10},
                       {kst("2026-10-12T09:34:00+09:00"), 79000, 79000, 79000, 79000, 10}};

QList<ChartIcon> plain_icons(const QList<Mark>& marks) {
    return chart_icons(marks, SignalRule{});
}

/// A selected consensus's mark at ``bar_close`` (KST), scored ``happened`` with its target at
/// four minutes on.
Mark selected(const char* bar_close, Direction predicted, Direction happened, bool special = false) {
    Mark m = mark(bar_close, predicted, Mark::Result::Pending, special);
    m.selected = true;
    m.truth.scored = happened != Direction::None;
    m.truth.happened = happened;
    m.truth.special = special;
    m.truth.target_at = m.bar_close.addSecs(240);
    m.result = verdict(predicted, m.truth);
    return m;
}

} // namespace

class TstKrxChartModel : public QObject {
    Q_OBJECT

  private slots:
    void the_chart_shows_kst_as_its_utc() {
        // 2026-10-12 09:30 KST is 00:30 UTC, 1791765000 s; the chart is told 9 hours more.
        QCOMPARE(chart_time(kst("2026-10-12T09:30:00+09:00")), qint64(1791797400));
        QCOMPARE(chart_time(kst("2026-10-12T00:30:00Z")), qint64(1791797400));
    }

    void a_bar_keeps_its_prices_and_says_whether_it_rose() {
        const QList<Bar> bars{{kst("2026-10-12T09:30:00+09:00"), 100, 103, 99, 102, 5000},
                              {kst("2026-10-12T09:31:00+09:00"), 102, 102, 98, 99, 7000},
                              {kst("2026-10-12T09:32:00+09:00"), 99, 99, 99, 99, 0}};
        const auto shown = chart_bars(bars);
        QCOMPARE(shown.size(), 3);
        QCOMPARE(shown[0].time, qint64(1791797400));
        QCOMPARE(shown[1].time, qint64(1791797460));
        QCOMPARE(shown[0].high, 103.0);
        QCOMPARE(shown[1].volume, 7000.0);
        QVERIFY(shown[0].rising);
        QVERIFY(!shown[1].rising);
        QVERIFY(shown[2].rising); // unchanged counts as rising, as Fincept's candles do
    }

    void an_icon_sits_on_the_bar_that_closed_when_it_was_made() {
        // The prediction at the 09:31 bar close was made on the bar 09:30–09:31.
        const auto icons = plain_icons({mark("2026-10-12T09:31:00+09:00", Direction::Up, Mark::Result::Hit)});
        QCOMPARE(icons.size(), 1);
        QCOMPARE(icons[0].time, qint64(1791797400));
        QCOMPARE(icons[0].bar_close, kst("2026-10-12T09:31:00+09:00"));
    }

    void an_icon_shows_the_class_and_its_result() {
        const auto icons = plain_icons({
            mark("2026-10-12T09:31:00+09:00", Direction::Up, Mark::Result::Hit),
            mark("2026-10-12T09:32:00+09:00", Direction::Down, Mark::Result::Miss),
            mark("2026-10-12T09:33:00+09:00", Direction::Flat, Mark::Result::Pending),
            mark("2026-10-12T09:34:00+09:00", Direction::Up, Mark::Result::Unscorable),
        });
        QCOMPARE(icons.size(), 4);
        QCOMPARE(icons[0].arrow, QStringLiteral("▲"));
        QCOMPARE(icons[0].result, QStringLiteral("✓"));
        QCOMPARE(icons[1].arrow, QStringLiteral("▼"));
        QCOMPARE(icons[1].result, QStringLiteral("✗"));
        QCOMPARE(icons[2].arrow, QStringLiteral("–"));
        QCOMPARE(icons[2].result, QStringLiteral("?")); // not scored yet
        QCOMPARE(icons[3].result, QStringLiteral("?")); // could not be scored
        QCOMPARE(icons[1].direction, Direction::Down);
    }

    void a_special_prediction_is_faint() {
        const auto icons = plain_icons({mark("2026-10-12T09:31:00+09:00", Direction::Down, Mark::Result::Hit, true),
                                        mark("2026-10-12T09:32:00+09:00", Direction::Down, Mark::Result::Hit)});
        QVERIFY(icons[0].faint);
        QVERIFY(!icons[1].faint);
    }

    void the_icons_come_in_time_order() {
        const auto icons = plain_icons({mark("2026-10-12T09:33:00+09:00", Direction::Up, Mark::Result::Pending),
                                        mark("2026-10-12T09:31:00+09:00", Direction::Up, Mark::Result::Pending)});
        QCOMPARE(icons[0].time, qint64(1791797400));
        QCOMPARE(icons[1].time, qint64(1791797520));
    }

    void an_icon_is_a_signal_when_sure_enough_and_enough_lean_that_way() {
        QVERIFY(chart_icons({final_up()}, SignalRule{0.6, 2})[0].signal);
        QVERIFY(!chart_icons({final_up()}, SignalRule{0.7, 2})[0].signal); // 62% < 70%
        QVERIFY(!chart_icons({final_up()}, SignalRule{0.6, 4})[0].signal); // 3 < 4 lean up
        Mark blocked = final_up();
        blocked.prediction.blocked = "special";
        QVERIFY(!chart_icons({blocked}, SignalRule{0.6, 2})[0].signal);
        Mark unknown = mark("2026-10-12T09:31:00+09:00", Direction::Up, Mark::Result::Pending);
        QVERIFY(!chart_icons({unknown}, SignalRule{0.6, 2})[0].signal); // no votes: never emphasised
    }

    void a_selected_consensus_on_a_special_bar_is_not_emphasised() {
        Mark m = final_up();
        m.selected = true;
        QVERIFY(chart_icons({m}, SignalRule{0.6, 2})[0].signal);
        m.special = true;
        QVERIFY(!chart_icons({m}, SignalRule{0.6, 2})[0].signal);
        QVERIFY(chart_icons({m}, SignalRule{0.6, 2})[0].faint);
    }

    void a_bar_close_s_truth_is_any_scored_forecast_s() {
        const QDateTime close = kst("2026-10-12T09:31:00+09:00");
        Prediction none;
        none.algorithm = "consensus-final";
        none.status = "no_members";
        none.bar_close = close;
        const Truth pending = truth_of({none}, close);
        QVERIFY(!pending.scored);
        QCOMPARE(pending.target_at, kst("2026-10-12T09:35:00+09:00")); // four minutes on
        QCOMPARE(verdict(Direction::Up, pending), Mark::Result::Pending);

        const Truth truth = truth_of({none, forecast("gauss"), scored_up(forecast("markov"))}, close);
        QVERIFY(truth.scored);
        QCOMPARE(truth.happened, Direction::Up); // basis A, whatever B said
        QCOMPARE(truth.mid_start, std::optional<double>(79000.0));
        QCOMPARE(verdict(Direction::Up, truth), Mark::Result::Hit);
        QCOMPARE(verdict(Direction::Flat, truth), Mark::Result::Miss);
        Truth unscorable = truth;
        unscorable.happened = Direction::None;
        QCOMPARE(verdict(Direction::Up, unscorable), Mark::Result::Unscorable);
    }

    void a_bar_without_predictions_says_so() {
        QCOMPARE(hover_box({}, kst("2026-10-12T09:31:00+09:00"), {"gauss"}, {}, SignalRule{}),
                 (QStringList{"09:31 봉 → 09:35 목표", "예측 없음"}));
    }

    void the_hover_shows_the_selected_consensus_each_selected_model_and_the_final_one() {
        Prediction late = forecast("newton");
        late.status = "timed_out";
        late.p_up = late.p_flat = late.p_down = std::nullopt;
        Prediction none;
        none.algorithm = "consensus-final";
        none.status = "no_members";
        none.bar_close = kst("2026-10-12T09:31:00+09:00");
        const QList<Prediction> at_close{scored_up(forecast("gauss")), late, none};
        const QMap<QString, QString> standings{{"gauss", "probation"}, {"newton", "probation"}};

        const QStringList lines = hover_box(at_close, kst("2026-10-12T09:31:00+09:00"), {"gauss", "markov", "newton"},
                                            standings, SignalRule{0.6, 2});

        QCOMPARE(lines, (QStringList{
                            "09:31 봉 → 09:35 목표 · 실제 상승 +300원",
                            "선택 합의 · 상승 · 상승 62% · 보합 25% · 하락 13% · 상승 1 보합 0 하락 0 · 1/3 모델 · ✓",
                            "gauss · 수습 · 상승 · 상승 62% · 보합 25% · 하락 13% · ✓",
                            "markov · 예측 없음",
                            "newton · 수습 · 시간 초과",
                            "최종 합의 · 합의 대상 없음",
                        }));
    }

    void the_hover_without_a_selection_shows_the_final_consensus_and_its_signal() {
        Prediction final = scored_up(forecast("consensus-final"));
        final.votes = Votes{3, 1, 1};

        const QStringList lines = hover_box({final}, kst("2026-10-12T09:31:00+09:00"), {}, {}, SignalRule{0.6, 2});

        QCOMPARE(lines, (QStringList{"09:31 봉 → 09:35 목표 · 실제 상승 +300원",
                                     "최종 합의 · 상승 · 상승 62% · 보합 25% · 하락 13% · 같은 방향 3/5 · ✓ · 신호"}));
        Prediction pending = forecast("consensus-final");
        QCOMPARE(hover_box({pending}, kst("2026-10-12T09:31:00+09:00"), {}, {}, SignalRule{})[0],
                 QStringLiteral("09:31 봉 → 09:35 목표 · 채점 전"));
    }

    void each_bar_s_hover_has_its_four_minute_window_and_its_touch_targets() {
        // krx-quant #167: the 09:31 bar close's label is in; up at 09:32:30, the bid 79,300.
        Prediction scored = scored_up(forecast("consensus-final"));
        scored.outcome->touch =
            Touch{Direction::Up, {}, 79'000.0, 79'200.0, 78'800.0, Reach{kst("2026-10-12T09:32:30+09:00"), 79'300.0},
                  std::nullopt};
        QMap<QDateTime, QList<Prediction>> closes;
        closes.insert(kst("2026-10-12T09:31:00+09:00"), {scored});

        const auto hovers = bar_hovers(kBars, closes, {}, {}, SignalRule{});

        QCOMPARE(hovers.size(), 3); // every bar, with predictions or not
        // The bar 09:30–09:31 closed at 09:31: its window runs 09:31–09:35.
        QCOMPARE(hovers[0].time, qint64(1791797400));
        QCOMPARE(hovers[0].window_from, qint64(1791797400 + 60));
        QCOMPARE(hovers[0].window_to, qint64(1791797400 + 5 * 60));
        QCOMPARE(hovers[0].upper, std::optional<double>(79'200.0));
        QCOMPARE(hovers[0].lower, std::optional<double>(78'800.0));
        QCOMPARE(hovers[0].upper_reached->time, chart_time(kst("2026-10-12T09:32:30+09:00")));
        QCOMPARE(hovers[0].upper_reached->price, 79'300.0);
        QVERIFY(!hovers[0].lower_reached.has_value());
        QVERIFY(hovers[0].lines.first().endsWith(QStringLiteral(" · 정답 상승 (09:32:30 닿음)")));
        // No label yet: the window alone, four minutes from its bar close.
        QVERIFY(!hovers[1].upper.has_value());
        QCOMPARE(hovers[1].window_to - hovers[1].window_from, qint64(240));
        QCOMPARE(hovers[1].lines.last(), QStringLiteral("예측 없음"));
    }

    void the_truth_is_the_touch_label_when_the_engine_gives_one() {
        // krx-quant #170: the touch target, not the four-minute move (here up +300 won).
        Prediction p = scored_up(forecast("consensus-final"));
        p.outcome->touch = Touch{
            Direction::None, QStringLiteral("silent quotes"), 79'000.0, 79'200.0, 78'800.0, std::nullopt, std::nullopt};
        QCOMPARE(hover_box({p}, kst("2026-10-12T09:31:00+09:00"), {}, {}, SignalRule{})[0],
                 QStringLiteral("09:31 봉 → 09:35 목표 · 채점 불가 (silent quotes)"));
        p.outcome->touch->direction = Direction::Flat;
        QCOMPARE(hover_box({p}, kst("2026-10-12T09:31:00+09:00"), {}, {}, SignalRule{})[0],
                 QStringLiteral("09:31 봉 → 09:35 목표 · 정답 보합"));
        p.outcome->touch->direction = Direction::Down;
        p.outcome->touch->lower_reached = Reach{kst("2026-10-12T09:33:05+09:00"), 78'800.0};
        QCOMPARE(hover_box({p}, kst("2026-10-12T09:31:00+09:00"), {}, {}, SignalRule{})[0],
                 QStringLiteral("09:31 봉 → 09:35 목표 · 정답 하락 (09:33:05 닿음)"));
    }

    void the_score_counts_the_scored_bars_in_view_whose_four_minutes_are_over() {
        const QList<Mark> marks{
            selected("2026-10-12T09:31:00+09:00", Direction::Up, Direction::Up),       // hit
            selected("2026-10-12T09:32:00+09:00", Direction::Up, Direction::Flat),     // miss, flat right
            selected("2026-10-12T09:33:00+09:00", Direction::Flat, Direction::Flat),   // hit, flat right
            selected("2026-10-12T09:34:00+09:00", Direction::Down, Direction::Up),     // miss
            selected("2026-10-12T09:35:00+09:00", Direction::Up, Direction::Up, true), // special: out, counted aside
            selected("2026-10-12T09:36:00+09:00", Direction::Up, Direction::None),     // not scored: out
            selected("2026-10-12T09:37:00+09:00", Direction::Up, Direction::Up),       // its 09:41 not yet
        };
        const qint64 from = chart_time(kst("2026-10-12T09:30:00+09:00"));
        const qint64 to = chart_time(kst("2026-10-12T09:40:00+09:00"));

        const SelectionScore score = score_of(marks, from, to, kst("2026-10-12T09:40:30+09:00"));

        QCOMPARE(score.n, 4);
        QCOMPARE(score.hits, 2);
        QCOMPARE(score.flat_hits, 2);
        QCOMPARE(score.up_n, 2); // 09:31 right, 09:32 wrong
        QCOMPARE(score.up_hits, 1);
        QCOMPARE(score.down_n, 1);
        QCOMPARE(score.down_hits, 0);
        QCOMPARE(score.days, 1);
        QCOMPARE(score.special, 1);
        QCOMPARE(score_text("선택 합의 (3)", score),
                 QStringLiteral("선택 합의 (3) · 맞음 50% · 늘 보합 50% · ▲ 맞음 50% (2) · ▼ 맞음 0% (1) · 4개 1거래일 "
                                "· 특수 1개 제외"));
        // Out of view: the 09:31 bar (09:30–09:31) is left out from 09:31 on.
        QCOMPARE(score_of(marks, from + 60, to, kst("2026-10-12T09:40:30+09:00")).n, 3);
        QCOMPARE(score_text("선택 합의 (2)", SelectionScore{}), QStringLiteral("선택 합의 (2) · 채점된 봉 없음"));
    }

    void the_score_counts_trading_days_and_says_none_for_a_direction_never_called() {
        QList<Mark> marks{selected("2026-10-12T09:31:00+09:00", Direction::Flat, Direction::Flat),
                          selected("2026-10-13T09:31:00+09:00", Direction::Flat, Direction::Up)};
        const SelectionScore score =
            score_of(marks, 0, std::numeric_limits<qint64>::max(), kst("2026-10-14T00:00:00+09:00"));
        QCOMPARE(score.days, 2);
        QVERIFY(score_text("기준: 늘 보합", score).contains(QStringLiteral("▲ 맞음 — (0)")));
    }

    void the_head_names_the_models_consensus_or_a_lone_baseline() {
        QCOMPARE(selection_head({"baseline-flat", "gauss", "markov"}), QStringLiteral("선택 합의 (2)"));
        QCOMPARE(selection_head({"baseline-flat"}), QStringLiteral("기준: 늘 보합"));
        QVERIFY(selection_head({"baseline-flat", "baseline-persist"}).isEmpty()); // nothing drawn
    }

    void the_hover_compares_baselines_without_averaging_them() {
        const QList<Prediction> at_close{forecast("gauss"), forecast("baseline-flat", 0, 1, 0)};
        const QStringList with =
            hover_box(at_close, kst("2026-10-12T09:31:00+09:00"), {"baseline-flat", "gauss"}, {}, SignalRule{});
        QVERIFY(with[1].startsWith(QStringLiteral("선택 합의 · 상승 · 상승 62%"))); // gauss alone
        QVERIFY(with[1].contains(QStringLiteral("1/1 모델")));
        QVERIFY(with.contains(QStringLiteral("기준: 늘 보합 · 보합 · 상승 0% · 보합 100% · 하락 0% · ?")));
        const QStringList alone =
            hover_box(at_close, kst("2026-10-12T09:31:00+09:00"), {"baseline-flat"}, {}, SignalRule{});
        QVERIFY(!alone[1].startsWith(QStringLiteral("선택 합의"))); // no consensus line
    }

    void a_selection_holds_models_and_baselines_once_each_in_name_order() {
        QVERIFY(selectable("gauss"));
        QVERIFY(selectable("baseline-flat"));
        QVERIFY(!selectable("consensus-final"));
        QVERIFY(!selectable("consensus-fast"));
        QVERIFY(!selectable(QString()));

        QStringList chosen = toggled({}, "markov", true);
        chosen = toggled(chosen, "gauss", true);
        chosen = toggled(chosen, "gauss", true);
        chosen = toggled(chosen, "consensus-final", true);
        QCOMPARE(chosen, (QStringList{"gauss", "markov"}));
        QCOMPARE(toggled(chosen, "gauss", false), (QStringList{"markov"}));

        QCOMPARE(selection_value(chosen), QStringLiteral("gauss,markov"));
        QCOMPARE(parse_selection("markov, gauss,,consensus-final,gauss"), (QStringList{"gauss", "markov"}));
        QVERIFY(parse_selection(QString()).isEmpty());
    }

    void once_models_are_selected_the_bar_closes_shown_are_read_again_from_the_latest_back() {
        const QDateTime oldest = kst("2026-10-08T09:01:00+09:00");                   // the first bar's close
        QVERIFY(!relabel_before({}, {}).has_value());                                // nothing shown
        QCOMPARE(relabel_before({}, oldest), std::optional<QDateTime>(QDateTime())); // from the latest

        // A page of every predictor's back to 10-12 09:01: read on before it.
        const QDateTime from = full_from_after({}, {forecast("gauss")}, oldest);
        QCOMPARE(from, kst("2026-10-12T09:31:00+09:00"));
        QCOMPARE(relabel_before(from, oldest), std::optional<QDateTime>(from));
        // Nothing before: as far back as shown, and done.
        QCOMPARE(full_from_after(from, {}, oldest), oldest);
        QVERIFY(!relabel_before(oldest, oldest).has_value());
    }

    void a_bar_close_s_judgements_come_consensus_first_then_models_then_baselines() {
        auto at = [](const char* algorithm, double up, double flat, double down) {
            Prediction p;
            p.algorithm = algorithm;
            p.status = "ok";
            p.bar_close = kst("2026-10-12T09:31:00+09:00");
            p.p_up = up;
            p.p_flat = flat;
            p.p_down = down;
            return p;
        };
        auto final = at("consensus-final", 0.5, 0.3, 0.2);
        final.pooled = QStringList{"gauss", "markov"};
        final.outcome = Outcome{Direction::Up, Direction::Down, false, {}, 79000.0, 79100.0, 79300.0};
        auto gauss = at("gauss", 0.2, 0.2, 0.6);
        gauss.outcome = Outcome{Direction::Up, Direction::Down, false, {}, std::nullopt, std::nullopt, std::nullopt};
        auto newton = at("newton", 0.6, 0.3, 0.1);
        Prediction late = at("turing", 0, 0, 0);
        late.status = "timed_out";
        late.p_up = late.p_flat = late.p_down = std::nullopt;
        const QList<Prediction> made{
            at("baseline-flat", 0, 1, 0), newton, final, gauss, at("consensus-fast", 0.6, 0.3, 0.1), late,
            at("markov", 0.4, 0.4, 0.2)};
        const QMap<QString, QString> statuses{{"gauss", "active"}, {"markov", "active"}, {"newton", "probation"}};

        const auto rows = close_rows(made, statuses);

        QStringList order;
        for (const auto& r : rows)
            order << r.algorithm;
        QCOMPARE(order, (QStringList{"consensus-fast", "consensus-final", "gauss", "markov", "newton", "turing",
                                     "baseline-flat"}));
        QVERIFY(rows[6].baseline && !rows[2].baseline);
        // gauss: active, pooled, leaned down; the bar close went up on basis A (krx-quant #159).
        QCOMPARE(rows[2].status, QStringLiteral("정식"));
        QCOMPARE(rows[2].in_consensus, QStringLiteral("포함"));
        QCOMPARE(rows[2].direction, QStringLiteral("하락"));
        QCOMPARE(rows[2].result, QStringLiteral("✗ 틀림"));
        // markov: up and flat tie, so flat (as scored); newton on probation, not pooled, its
        // own outcome not in but the bar close's truth is.
        QCOMPARE(rows[3].direction, QStringLiteral("보합"));
        QCOMPARE(rows[4].in_consensus, QStringLiteral("제외"));
        QCOMPARE(rows[4].result, QStringLiteral("✓ 맞음"));
        QCOMPARE(rows[5].direction, QStringLiteral("시간 초과")); // no forecast: why
        QCOMPARE(rows[5].result, QString());
        QCOMPARE(rows[1].in_consensus, QString());          // a consensus itself
        QCOMPARE(rows[1].result, QStringLiteral("✓ 맞음")); // up, and up it went
        // Without the final consensus's members (an older engine): unknown.
        QList<Prediction> older = made;
        older[2].pooled.reset();
        QCOMPARE(close_rows(older, statuses)[2].in_consensus, QStringLiteral("알 수 없음"));

        // Models selected: their consensus first. gauss and newton: up 0.4, flat 0.25, down 0.35.
        const auto chosen = close_rows(made, statuses, {"gauss", "newton"});
        QCOMPARE(chosen.size(), rows.size() + 1);
        QCOMPARE(chosen[0].algorithm, QStringLiteral("selected"));
        QCOMPARE(chosen[0].name, QStringLiteral("선택 합의"));
        QVERIFY(chosen[0].consensus);
        QCOMPARE(chosen[0].direction, QStringLiteral("상승"));
        QCOMPARE(*chosen[0].p_up, 0.4);
        QCOMPARE(chosen[0].result, QStringLiteral("✓ 맞음"));
        QCOMPARE(close_rows(made, statuses, {"turing"})[0].direction, QStringLiteral("예측 없음"));
        QCOMPARE(close_rows(made, statuses, {"baseline-flat"})[0].algorithm,
                 QStringLiteral("consensus-fast")); // no row
    }

    void the_bars_before_are_read_once_the_view_nears_the_left_end() {
        const QDateTime first = kst("2026-10-12T09:00:00+09:00"); // the first bar shown

        QVERIFY(!older_read(first, 30).has_value()); // 30 bars from the end: not yet
        const auto near = older_read(first, 29.5);
        QVERIFY(near.has_value());
        QCOMPARE(near->before, kst("2026-10-12T09:00:00+09:00")); // before the first bar
        QCOMPARE(near->limit, 400);
        // Its own final consensus: the 08:59 bar closes at 09:00, so bar closes up to 09:00.
        QCOMPARE(near->finals_before, kst("2026-10-12T09:01:00+09:00"));
        QVERIFY(older_read(first, -12).has_value()); // scrolled past the first bar
        // No bars yet (before the open): those before now.
        const auto empty = older_read(QDateTime(), 0);
        QVERIFY(empty.has_value());
        QVERIFY(!empty->before.isValid() && !empty->finals_before.isValid());
    }

    void a_wider_view_reads_as_many_minutes_as_its_bars_need() {
        const QDateTime first = kst("2026-10-12T09:00:00+09:00");

        QCOMPARE(older_read(first, 29, 1)->limit, 400);
        QCOMPARE(older_read(first, 29, 3)->limit, 400);   // 100 bars of 3 minutes: fewer than the 1-minute view's
        QCOMPARE(older_read(first, 29, 5)->limit, 500);   // 100 bars of 5 minutes
        QCOMPARE(older_read(first, 29, 60)->limit, 1000); // at most the engine's most predictions in a read
        QVERIFY(!older_read(first, 30, 60).has_value());  // 30 bars of the view from the end: not yet
        QCOMPARE(older_read(QDateTime(), 0, 15)->limit, 1000);
    }

    void minute_bars_group_on_the_clock_within_a_day() {
        auto bar = [](const char* start, double open, double high, double low, double close, double volume) {
            return Bar{kst(start), open, high, low, close, volume};
        };
        auto starts = [](const QList<Bar>& bars) {
            QStringList out;
            for (const auto& b : bars)
                out << b.start.toOffsetFromUtc(9 * 3600).toString(Qt::ISODate);
            return out;
        };
        // 5 minutes: 09:00–09:04 (09:02 traded nothing) and 09:05–09:09.
        const QList<Bar> morning{bar("2026-10-12T09:00:00+09:00", 100, 105, 99, 101, 10),
                                 bar("2026-10-12T09:01:00+09:00", 101, 103, 100, 102, 5),
                                 bar("2026-10-12T09:03:00+09:00", 102, 108, 101, 107, 7),
                                 bar("2026-10-12T09:05:00+09:00", 107, 107, 104, 105, 3),
                                 bar("2026-10-12T09:09:00+09:00", 105, 106, 103, 104, 2)};

        const auto five = group_bars(morning, 5);

        QCOMPARE(starts(five), (QStringList{"2026-10-12T09:00:00+09:00", "2026-10-12T09:05:00+09:00"}));
        QCOMPARE(five[0].open, 100.0);
        QCOMPARE(five[0].high, 108.0);
        QCOMPARE(five[0].low, 99.0);
        QCOMPARE(five[0].close, 107.0);
        QCOMPARE(five[0].volume, 22.0);
        QCOMPARE(five[1].low, 103.0);
        QCOMPARE(five[1].close, 104.0);
        QCOMPARE(five[1].volume, 5.0);
        QCOMPARE(starts(group_bars(morning, 60)), QStringList{"2026-10-12T09:00:00+09:00"});
        QCOMPARE(group_bars(morning, 1).size(), 5); // as they are

        // 240 minutes start at 08:00, 12:00 and 16:00, and a day's last never takes the next's first.
        const QList<Bar> days{
            bar("2026-10-08T19:59:00+09:00", 1, 1, 1, 1, 1), bar("2026-10-09T08:00:00+09:00", 1, 1, 1, 1, 1),
            bar("2026-10-09T11:59:00+09:00", 1, 1, 1, 1, 1), bar("2026-10-09T12:00:00+09:00", 1, 1, 1, 1, 1),
            bar("2026-10-09T16:30:00+09:00", 1, 1, 1, 1, 1)};
        QCOMPARE(starts(group_bars(days, 240)),
                 (QStringList{"2026-10-08T16:00:00+09:00", "2026-10-09T08:00:00+09:00", "2026-10-09T12:00:00+09:00",
                              "2026-10-09T16:00:00+09:00"}));
        QCOMPARE(group_bars(days, 240)[1].volume, 2.0); // 08:00 and 11:59
        // Bars read in another offset still group on the KST clock.
        QCOMPARE(starts(group_bars({bar("2026-10-12T00:30:00Z", 1, 1, 1, 1, 1)}, 60)),
                 QStringList{"2026-10-12T09:00:00+09:00"});
    }

    void the_bar_being_made_moves_the_last_bar_and_a_new_minute_follows_it() {
        auto bar = [](const char* start, double close, double volume) {
            return Bar{kst(start), 100, std::max(100.0, close), std::min(100.0, close), close, volume};
        };
        QList<Bar> bars{bar("2026-10-12T09:30:00+09:00", 101, 10), bar("2026-10-12T09:31:00+09:00", 102, 4)};

        QVERIFY(merge_live(bars, bar("2026-10-12T09:31:00+09:00", 104, 6))); // the same minute, grown
        QCOMPARE(bars.size(), 2);
        QCOMPARE(bars.last().close, 104.0);
        QVERIFY(!merge_live(bars, bar("2026-10-12T09:31:00+09:00", 104, 6))); // unchanged
        QVERIFY(!merge_live(bars, bar("2026-10-12T09:31:00+09:00", 103, 5))); // sent before what is shown
        QVERIFY(!merge_live(bars, bar("2026-10-12T09:30:00+09:00", 99, 20))); // an earlier minute
        QCOMPARE(bars.last().close, 104.0);
        QVERIFY(merge_live(bars, bar("2026-10-12T09:32:00+09:00", 105, 1))); // a new minute: 09:31 is done
        QCOMPARE(bars.size(), 3);
        QCOMPARE(bars[1].close, 104.0);
        QVERIFY(merge_live(bars = {}, bar("2026-10-12T09:00:00+09:00", 100, 1))); // the day's first

        // In the 5-minute view the bar being made moves its group's bar: 09:30–09:34.
        QList<Bar> day{bar("2026-10-12T09:29:00+09:00", 90, 7), bar("2026-10-12T09:30:00+09:00", 101, 10),
                       bar("2026-10-12T09:31:00+09:00", 102, 4)};
        QVERIFY(merge_live(day, bar("2026-10-12T09:32:00+09:00", 107, 3)));
        const auto five = last_chart_bar(day, 5);
        QVERIFY(five.has_value());
        QCOMPARE(five->time, chart_time(kst("2026-10-12T09:30:00+09:00")));
        QCOMPARE(five->close, 107.0);
        QCOMPARE(five->high, 107.0);
        QCOMPARE(five->volume, 17.0); // 10 + 4 + 3, not 09:29's
        QCOMPARE(last_chart_bar(day, 1)->close, 107.0);
        QVERIFY(!last_chart_bar({}, 5).has_value());
    }

    void bars_read_before_join_those_shown_once_each_oldest_first() {
        auto bar = [](const char* start, double close) { return Bar{kst(start), close, close, close, close, 1}; };
        // Read before: Thursday's last two bars and, overlapping, today's first (as stored).
        const QList<Bar> older{bar("2026-10-08T15:19:00+09:00", 2), bar("2026-10-12T09:00:00+09:00", 9),
                               bar("2026-10-08T15:18:00+09:00", 1)};
        const QList<Bar> shown{bar("2026-10-12T09:00:00+09:00", 3), bar("2026-10-12T09:01:00+09:00", 4)};

        const auto joined = join_bars(older, shown);

        QStringList starts;
        QList<double> closes;
        for (const auto& b : joined) {
            starts << b.start.toString(Qt::ISODate);
            closes << b.close;
        }
        QCOMPARE(starts, (QStringList{"2026-10-08T15:18:00+09:00", "2026-10-08T15:19:00+09:00",
                                      "2026-10-12T09:00:00+09:00", "2026-10-12T09:01:00+09:00"}));
        QCOMPARE(closes, (QList<double>{1, 2, 3, 4})); // today's 09:00 as shown, not as read before
        QCOMPARE(join_bars({}, shown).size(), 2);
    }

    void today_s_day_candle_is_made_of_the_day_s_bars_on_the_engine_s_market() {
        auto bar = [](const char* start, double open, double high, double low, double close, double volume) {
            return Bar{kst(start), open, high, low, close, volume};
        };
        // Integrated bars from NXT's pre-market to after the KRX close; the first is the
        // day before's, read as the chart scrolled back.
        const QList<Bar> bars{bar("2026-10-08T19:59:00+09:00", 70, 70, 70, 70, 9),
                              bar("2026-10-12T08:00:00+09:00", 100, 104, 99, 101, 5),
                              bar("2026-10-12T09:00:00+09:00", 102, 108, 101, 107, 20),
                              bar("2026-10-12T15:30:00+09:00", 106, 106, 103, 105, 30),
                              bar("2026-10-12T17:10:00+09:00", 105, 105, 96, 97, 2)};

        const auto integrated = day_candle(bars, "UN");
        QVERIFY(integrated.has_value());
        QCOMPARE(integrated->start, kst("2026-10-12T00:00:00+09:00"));
        QCOMPARE((QList<double>{integrated->open, integrated->high, integrated->low, integrated->close}),
                 (QList<double>{100, 108, 96, 97}));
        QCOMPARE(integrated->volume, 57.0); // the day's, not the 8th's

        // The engine's candles are KRX's: 09:00 (the opening auction) to 15:30 (the closing one).
        const auto krx = day_candle(bars, "KRX");
        QCOMPARE((QList<double>{krx->open, krx->high, krx->low, krx->close, krx->volume}),
                 (QList<double>{102, 108, 101, 105, 50}));
        QVERIFY(!day_candle({}, "UN").has_value());
        QVERIFY(!day_candle({bars[1]}, "KRX").has_value()); // before KRX opens: none yet
    }

    void the_day_view_is_the_stored_candles_then_today_s() {
        auto candle = [](const char* day, double close) { return Bar{kst(day), close, close, close, close, 1}; };
        const QList<Bar> stored{candle("2026-10-07T00:00:00+09:00", 1), candle("2026-10-08T00:00:00+09:00", 2)};
        const QList<Bar> today{{kst("2026-10-12T09:00:00+09:00"), 3, 4, 3, 4, 10},
                               {kst("2026-10-12T09:01:00+09:00"), 4, 5, 4, 5, 10}};

        auto starts = [](const QList<Bar>& bars) {
            QStringList out;
            for (const auto& b : bars)
                out << b.start.toString(Qt::ISODate);
            return out;
        };
        const auto shown = day_candles(stored, today, "UN");
        QCOMPARE(starts(shown),
                 (QStringList{"2026-10-07T00:00:00+09:00", "2026-10-08T00:00:00+09:00", "2026-10-12T00:00:00+09:00"}));
        QCOMPARE(shown.last().close, 5.0);
        QCOMPARE(shown.last().volume, 20.0);
        QCOMPARE(starts(day_candles(stored, {}, "UN")), starts(stored)); // before the open: none of today

        // A candle the engine stored for today too (after 20:30) gives way to the day's bars.
        const auto again = day_candles({stored[1], candle("2026-10-12T00:00:00+09:00", 9)}, today, "UN");
        QCOMPARE(again.size(), 2);
        QCOMPARE(again.last().close, 5.0);

        // The bar being made moves today's candle.
        QList<Bar> moving = today;
        QVERIFY(merge_live(moving, {kst("2026-10-12T09:02:00+09:00"), 5, 9, 5, 8, 1}));
        const Bar last = day_candles(stored, moving, "UN").last();
        QCOMPARE(last.start, kst("2026-10-12T00:00:00+09:00"));
        QCOMPARE((QList<double>{last.high, last.close, last.volume}), (QList<double>{9, 8, 21}));
    }

    void a_week_starts_on_monday_a_month_on_the_1st_a_year_on_1_january() {
        QCOMPARE(period_start(QDate(2026, 10, 14), kWeekView), QDate(2026, 10, 12)); // a Wednesday
        QCOMPARE(period_start(QDate(2026, 10, 12), kWeekView), QDate(2026, 10, 12)); // a Monday
        QCOMPARE(period_start(QDate(2026, 10, 18), kWeekView), QDate(2026, 10, 12)); // a Sunday
        QCOMPARE(period_start(QDate(2026, 1, 2), kWeekView), QDate(2025, 12, 29));   // across the year
        QCOMPARE(period_start(QDate(2026, 10, 14), kMonthView), QDate(2026, 10, 1));
        QCOMPARE(period_start(QDate(2026, 10, 14), kYearView), QDate(2026, 1, 1));
        QCOMPARE(period_start(QDate(2026, 10, 14), kDayView), QDate(2026, 10, 14));
        QCOMPARE(candle_period(kWeekView), QStringLiteral("week"));
        QCOMPARE(candle_period(kYearView), QStringLiteral("year"));
        QVERIFY(candle_view(kDayView) && candle_view(kYearView) && !candle_view(240));
    }

    void this_week_month_and_year_are_made_of_the_days_after_the_engine_s_last() {
        auto candle = [](const char* start, double open, double high, double low, double close, double volume) {
            return Bar{kst(start), open, high, low, close, volume};
        };
        // The engine's weeks end with 21 September's; then the days of the week of the 29th
        // (30 September to 2 October: across the month) and of this week, today's last.
        const QList<Bar> weeks{candle("2026-09-14T00:00:00+09:00", 1, 1, 1, 1, 1),
                               candle("2026-09-21T00:00:00+09:00", 2, 2, 2, 2, 1)};
        const QList<Bar> days{candle("2026-09-21T00:00:00+09:00", 9, 9, 9, 9, 9), // stored already: left out
                              candle("2026-09-30T00:00:00+09:00", 10, 12, 9, 11, 5),
                              candle("2026-10-02T00:00:00+09:00", 11, 15, 10, 14, 7),
                              candle("2026-10-05T00:00:00+09:00", 14, 16, 13, 15, 3),
                              candle("2026-10-06T00:00:00+09:00", 15, 15, 8, 9, 4)};

        auto starts = [](const QList<Bar>& bars) {
            QStringList out;
            for (const auto& b : bars)
                out << b.start.toString(Qt::ISODate);
            return out;
        };
        const auto shown = period_candles(weeks, days, kWeekView);
        QCOMPARE(starts(shown), (QStringList{"2026-09-14T00:00:00+09:00", "2026-09-21T00:00:00+09:00",
                                             "2026-09-28T00:00:00+09:00", "2026-10-05T00:00:00+09:00"}));
        const Bar& across = shown[2]; // not stored by the engine yet: made of its days
        QCOMPARE((QList<double>{across.open, across.high, across.low, across.close, across.volume}),
                 (QList<double>{10, 15, 9, 14, 12}));
        const Bar& now = shown.last(); // this week, today's candle included
        QCOMPARE((QList<double>{now.open, now.high, now.low, now.close, now.volume}), (QList<double>{14, 16, 8, 9, 7}));

        // Months: September's from the days (the engine stored up to August), then October's.
        const auto months = period_candles({candle("2026-08-01T00:00:00+09:00", 1, 1, 1, 1, 1)}, days, kMonthView);
        QCOMPARE(starts(months),
                 (QStringList{"2026-08-01T00:00:00+09:00", "2026-09-01T00:00:00+09:00", "2026-10-01T00:00:00+09:00"}));
        QCOMPARE(months[1].close, 11.0);
        QCOMPARE(months[2].open, 11.0);
        // A year with nothing stored: all of it from the days read.
        const auto years = period_candles({}, days, kYearView);
        QCOMPARE(starts(years), QStringList{"2026-01-01T00:00:00+09:00"});
        QCOMPARE(years[0].volume, 28.0);
    }

    void the_candles_before_are_read_once_the_day_view_nears_the_left_end() {
        const QDateTime first = kst("2026-03-02T00:00:00+09:00"); // the first candle shown
        QVERIFY(!older_candles(first, kOlderMargin).has_value());
        QCOMPARE(older_candles(first, kOlderMargin - 1), first);
        QCOMPARE(older_candles(first, -5), first);
        const auto latest = older_candles({}, 100); // none shown yet: the latest
        QVERIFY(latest.has_value() && !latest->isValid());
    }

    void the_view_to_fit_is_the_last_sixty_bars() {
        QCOMPARE(fit_range(200), (std::pair<double, double>{140.0, 199.0 + kRightOffset}));
        QCOMPARE(fit_range(20), (std::pair<double, double>{0.0, 19.0 + kRightOffset}));
    }
};

QTEST_GUILESS_MAIN(TstKrxChartModel)
#include "tst_krx_chart_model.moc"
