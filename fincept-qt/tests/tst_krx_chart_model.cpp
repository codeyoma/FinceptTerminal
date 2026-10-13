// Unit tests for src/services/krx_quant/ChartModel.h
//
// The KRX chart's page only draws: what goes on it is worked out here. These tests pin the
// chart's time (KST shown as the chart's UTC), the bars, and the final consensus's icons:
// their arrow, result, faintness and the bar they sit on. The expected values are worked
// out by hand.

#include "services/krx_quant/ChartModel.h"

#include <QTest>

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

/// A final consensus at 09:31 up 62%, three of its five members up, scored up on both bases
/// with the mid-point 79,000 at the bar close, 79,100 when shown and 79,300 four minutes on.
Mark scored_up() {
    Mark m = mark("2026-10-12T09:31:00+09:00", Direction::Up, Mark::Result::Hit);
    m.prediction.votes = Votes{3, 1, 1};
    m.prediction.outcome = Outcome{Direction::Up, Direction::Up, false, {}, 79000.0, 79100.0, 79300.0};
    return m;
}

const QList<Bar> kBars{{kst("2026-10-12T09:30:00+09:00"), 79000, 79000, 79000, 79000, 10},
                       {kst("2026-10-12T09:31:00+09:00"), 79000, 79000, 79000, 79000, 10},
                       {kst("2026-10-12T09:34:00+09:00"), 79000, 79000, 79000, 79000, 10}};

QList<ChartIcon> plain_icons(const QList<Mark>& marks) {
    return chart_icons(marks, {}, "b", SignalRule{});
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

    void the_hover_says_the_times_the_classes_the_members_the_signal_and_the_result() {
        QCOMPARE(hover_lines(scored_up(), "b", SignalRule{0.6, 2}),
                 (QStringList{"09:31 봉 → 09:35 목표", "합의 상승 · 상승 62% · 보합 25% · 하락 13%", "같은 방향 3/5",
                              "상승 신호 (62% ≥ 60%, 같은 방향 3 ≥ 2)", "실제 +200원 · ✓ 맞음"}));
        // Basis A: from the bar close's mid-point.
        QCOMPARE(hover_lines(scored_up(), "a", SignalRule{0.6, 2}).last(), QStringLiteral("실제 +300원 · ✓ 맞음"));
        QCOMPARE(hover_lines(scored_up(), "b", SignalRule{0.7, 2})[3], QStringLiteral("신호 아님"));
    }

    void the_hover_before_scoring_or_without_votes_says_so() {
        Mark m = mark("2026-10-12T09:31:00+09:00", Direction::Up, Mark::Result::Pending);
        const auto lines = hover_lines(m, "b", SignalRule{0.6, 2});
        QCOMPARE(lines[2], QStringLiteral("같은 방향 모델 수 알 수 없음"));
        QCOMPARE(lines[3], QStringLiteral("신호 알 수 없음"));
        QCOMPARE(lines[4], QStringLiteral("채점 전"));
        m.prediction.blocked = "special";
        m.prediction.votes = Votes{3, 1, 1};
        QCOMPARE(hover_lines(m, "b", SignalRule{0.6, 2})[3], QStringLiteral("신호 아님 (엔진이 막음: 특수 상황)"));
        Mark unscorable = mark("2026-10-12T09:31:00+09:00", Direction::Up, Mark::Result::Unscorable);
        unscorable.unscorable = "missing mid: target";
        QCOMPARE(hover_lines(unscorable, "b", SignalRule{}).last(), QStringLiteral("채점 불가 (missing mid: target)"));
    }

    void an_icon_knows_its_target_bar_and_whether_it_signalled() {
        // 09:31's target is 09:35: the bar 09:34–09:35 is there.
        const auto icons = chart_icons({scored_up()}, kBars, "b", SignalRule{0.6, 2});
        QCOMPARE(icons[0].target, qint64(1791797400 + 4 * 60));
        QVERIFY(icons[0].target_shown);
        QVERIFY(icons[0].signal);
        QCOMPARE(icons[0].hover.size(), 5);
        // Not yet: the 09:36 prediction's 09:40 bar is in the future, 5 bars after the last.
        Mark later = mark("2026-10-12T09:36:00+09:00", Direction::Down, Mark::Result::Pending);
        const auto pending = chart_icons({later}, kBars, "b", SignalRule{0.6, 2});
        QVERIFY(!pending[0].target_shown);
        QCOMPARE(pending[0].target_after_last, 5);
        QVERIFY(!pending[0].signal); // no votes: never emphasised
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

        const auto rows = close_rows(made, statuses, "b");

        QStringList order;
        for (const auto& r : rows)
            order << r.algorithm;
        QCOMPARE(order, (QStringList{"consensus-fast", "consensus-final", "gauss", "markov", "newton", "turing",
                                     "baseline-flat"}));
        QVERIFY(rows[6].baseline && !rows[2].baseline);
        // gauss: active, pooled, leaned down, scored down on basis B: right.
        QCOMPARE(rows[2].status, QStringLiteral("정식"));
        QCOMPARE(rows[2].in_consensus, QStringLiteral("포함"));
        QCOMPARE(rows[2].direction, QStringLiteral("하락"));
        QCOMPARE(rows[2].result, QStringLiteral("✓ 맞음"));
        QCOMPARE(close_rows(made, statuses, "a")[2].result, QStringLiteral("✗ 틀림")); // up on basis A
        // markov: up and flat tie, so flat (as scored); newton on probation, not pooled.
        QCOMPARE(rows[3].direction, QStringLiteral("보합"));
        QCOMPARE(rows[4].in_consensus, QStringLiteral("제외"));
        QCOMPARE(rows[4].result, QStringLiteral("채점 전"));
        QCOMPARE(rows[5].direction, QStringLiteral("시간 초과")); // no forecast: why
        QCOMPARE(rows[1].in_consensus, QString());                // a consensus itself
        QCOMPARE(rows[1].result, QStringLiteral("✗ 틀림"));       // up, scored down on B
        // Without the final consensus's members (an older engine): unknown.
        QList<Prediction> older = made;
        older[2].pooled.reset();
        QCOMPARE(close_rows(older, statuses, "b")[2].in_consensus, QStringLiteral("알 수 없음"));
    }

    void the_bars_before_are_read_once_the_view_nears_the_left_end() {
        const QList<Bar> bars{{kst("2026-10-12T09:00:00+09:00"), 1, 1, 1, 1, 1},
                              {kst("2026-10-12T09:01:00+09:00"), 1, 1, 1, 1, 1}};

        QVERIFY(!older_read(bars, 30).has_value()); // 30 bars from the end: not yet
        const auto near = older_read(bars, 29.5);
        QVERIFY(near.has_value());
        QCOMPARE(near->before, kst("2026-10-12T09:00:00+09:00")); // before the first bar
        QCOMPARE(near->limit, 400);
        // Its own final consensus: the 08:59 bar closes at 09:00, so bar closes up to 09:00.
        QCOMPARE(near->finals_before, kst("2026-10-12T09:01:00+09:00"));
        QVERIFY(older_read(bars, -12).has_value()); // scrolled past the first bar
        // No bars yet (before the open): those before now.
        const auto empty = older_read({}, 0);
        QVERIFY(empty.has_value());
        QVERIFY(!empty->before.isValid() && !empty->finals_before.isValid());
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

    void the_view_to_fit_is_the_last_sixty_bars() {
        QCOMPARE(fit_range(200), (std::pair<double, double>{140.0, 199.0 + kRightOffset}));
        QCOMPARE(fit_range(20), (std::pair<double, double>{0.0, 19.0 + kRightOffset}));
    }
};

QTEST_GUILESS_MAIN(TstKrxChartModel)
#include "tst_krx_chart_model.moc"
