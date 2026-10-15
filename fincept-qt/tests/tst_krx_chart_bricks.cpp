// Unit tests for src/services/krx_quant/ChartBricks.h
//
// The KRX chart's styles without a time axis (krx-quant #225): renko, kagi, point and figure and
// line break made of synthetic bars (how many bricks, which way, their prices, when each ended and
// the volumes summed into it), the ATR(14) size and one a user set, the sizes kept for a symbol,
// where the bricks start and that reading the bars before that start changes none of them, and the
// indicators counted on the bricks. The expected values are worked out by hand.

#include "services/krx_quant/ChartBricks.h"
#include "services/krx_quant/ChartStudies.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace fincept::krx_quant;

namespace {

/// A 1-minute bar starting ``minute`` minutes after 09:00 KST on 12 October 2026 (chart_time).
qint64 at(int minute) {
    return chart_time(QDateTime::fromString("2026-10-12T09:00:00+09:00", Qt::ISODate)) + qint64(minute) * 60;
}

/// Bars of these closes and volumes, a minute apart from 09:00, each opening at its close.
QList<ChartBar> closes(const QList<double>& prices, const QList<double>& volumes = {}) {
    QList<ChartBar> out;
    for (qsizetype i = 0; i < prices.size(); ++i) {
        const double c = prices[i];
        out << ChartBar{at(static_cast<int>(i)), c, c, c, c, volumes.value(i, 1), true};
    }
    return out;
}

/// ``count`` bars from ``first`` minutes on, their closes a wave around ``level`` on whole won.
QList<ChartBar> wave(int first, int count, double level) {
    QList<ChartBar> out;
    for (int i = 0; i < count; ++i) {
        const int k = first + i;
        const double c = std::round(level + 40 * std::sin(k / 7.0) + 15 * std::sin(k / 2.3));
        out << ChartBar{at(k), c, c + 6, c - 6, c, 100.0 + (k % 9), true};
    }
    return out;
}

struct Expected {
    double open;
    double close;
    double volume;
    int minute; ///< when it ended: its last bar's
};

void compare(const QList<Brick>& got, const QList<Expected>& want) {
    QCOMPARE(got.size(), want.size());
    for (qsizetype i = 0; i < want.size(); ++i) {
        const QByteArray where = QByteArray("brick ") + QByteArray::number(i);
        QVERIFY2(got[i].open == want[i].open, where.constData());
        QVERIFY2(got[i].close == want[i].close, where.constData());
        QVERIFY2(got[i].high == std::max(want[i].open, want[i].close), where.constData());
        QVERIFY2(got[i].low == std::min(want[i].open, want[i].close), where.constData());
        QVERIFY2(got[i].rising == (want[i].close > want[i].open), where.constData());
        QVERIFY2(qFuzzyCompare(got[i].volume, want[i].volume), (where + " volume").constData());
        QVERIFY2(got[i].time == at(want[i].minute), (where + " time").constData());
    }
}

} // namespace

class TstKrxChartBricks : public QObject {
    Q_OBJECT

  private slots:
    void renko_bricks_follow_the_closes_box_by_box() {
        // Box 10 from the first close, 100. A reversal takes two boxes from the last brick's end; a
        // bar that ends several bricks shares the volume they hold evenly.
        const auto bars = closes({100, 105, 112, 131, 115, 109, 125, 131, 95, 97}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
        compare(renko_bricks(bars, 10), {
                                            {100, 110, 6, 2},   // 100 + 105 + 112's volumes
                                            {110, 120, 2, 3},   // 131 ends two: its 4 shared
                                            {120, 130, 2, 3},   //
                                            {120, 110, 11, 5},  // 115 is not two boxes down; 109 is
                                            {120, 130, 15, 7},  // 125 is not two boxes up; 131 is
                                            {120, 110, 4.5, 8}, // 95 ends two
                                            {110, 100, 4.5, 8}, //
                                        });                     // 97 makes none yet
        QVERIFY(renko_bricks(bars, 0).isEmpty());
        QVERIFY(renko_bricks({}, 10).isEmpty());
        QCOMPARE(renko_bricks(closes({100, 104, 96}), 10).size(), 0); // never a box away
    }

    void line_break_turns_past_the_last_three_lines() {
        const auto bars =
            closes({100, 100, 104, 103, 107, 109, 105, 110, 103, 104, 101, 111}, {1, 1, 2, 1, 1, 1, 1, 1, 3, 1, 2, 1});
        compare(break_lines(bars, 3), {
                                          {100, 104, 4, 2},  // the first close apart from 100
                                          {104, 107, 2, 4},  // 103 is neither past 104 nor below 100
                                          {107, 109, 1, 5},  //
                                          {109, 110, 2, 7},  // 105 is not below the three lines' 100
                                          {109, 103, 3, 8},  // below the last three's 104: from 109
                                          {103, 101, 3, 10}, // 104 is not above the last three's 110
                                          {103, 111, 1, 11}, // above 110: from the last line's start
                                      });
        // One line turns sooner: 105 is below the last line's 107, from its start.
        const auto one = break_lines(bars, 1);
        QCOMPARE(one[3].open, 107.0);
        QCOMPARE(one[3].close, 105.0);
        QCOMPARE(one[3].time, at(6));
        QVERIFY(break_lines(closes({100, 100}), 3).isEmpty());
    }

    void kagi_lines_turn_on_the_reversal_and_thicken_past_a_shoulder() {
        const auto bars = closes({100, 105, 112, 118, 110, 107, 103, 112, 114, 125, 112, 99, 108, 130},
                                 {1, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1});
        const auto lines = kagi_lines(bars, 10);
        compare(lines, {
                           {100, 118, 6, 4}, // starts once 112 is 10 from 100; 110 is not 10 back
                           {118, 103, 4, 7}, // 107 is; 112 is not 10 up from 103
                           {103, 125, 2, 9}, // 114 is
                           {125, 99, 3, 12}, // 108 is not 10 up from 99
                           {99, 130, 1, 13}, // the line being drawn
                       });
        // Thick (yang) from the first rising line; thin below the waist before (103), thick again
        // above the shoulder before (125).
        QCOMPARE(lines[0].thick, true);
        QVERIFY(!lines[0].turn && !lines[1].turn && !lines[2].turn);
        QCOMPARE(lines[3].thick, true);
        QCOMPARE(lines[3].turn, std::optional(103.0));
        QCOMPARE(lines[4].thick, false);
        QCOMPARE(lines[4].turn, std::optional(125.0));
        QVERIFY(kagi_lines(closes({100, 105, 95}), 10).isEmpty()); // never a reversal away
        // As the last line goes on, only it changes.
        auto more = bars;
        more << ChartBar{at(14), 140, 140, 140, 140, 5, true};
        const auto grown = kagi_lines(more, 10);
        QCOMPARE(grown.mid(0, 4), lines.mid(0, 4));
        QCOMPARE(grown[4].close, 140.0);
        QCOMPARE(grown[4].volume, 6.0);
        QCOMPARE(grown[4].time, at(14));
    }

    void point_and_figure_columns_reverse_on_three_boxes() {
        const auto bars = closes({100, 108, 112, 135, 109, 98, 91, 88, 115, 121}, {1, 1, 1, 1, 1, 2, 1, 1, 1, 1});
        compare(figure_columns(bars, 10, 3), {
                                                 {100, 130, 5, 4}, // X 100–130; 109 is not three boxes back
                                                 {120, 90, 5, 8},  // O from a box below 130; 115 is not three up
                                                 {100, 120, 1, 9}, // X from a box above 90: being drawn
                                             });
        // Boxes on multiples of the box: 135 reaches 130, 98 reaches 100, 91 does not reach 90.
        const auto columns = figure_columns(bars, 10, 3);
        QVERIFY(columns[0].rising && !columns[1].rising && columns[2].rising);
        // A one-box reversal turns at 109 already: it reaches 110, a box below 120, a box below 130.
        const auto quick = figure_columns(bars, 10, 1);
        QCOMPARE(quick.size(), 3);
        QCOMPARE(quick[0].time, at(3));
        QCOMPARE(quick[1].open, 120.0);
        QCOMPARE(quick[1].close, 90.0); // then on down to 88's 90, till 115 turns it
        QCOMPARE(quick[1].volume, 5.0);
        QCOMPARE(quick[1].time, at(7));
        QVERIFY(figure_columns(closes({100, 105, 95}), 10).isEmpty());
    }

    void the_default_size_is_the_last_atr_14_to_the_won() {
        // True ranges 10, 13, 10, 13…: the first ATR(14) is their mean 11.5, then Wilder's.
        QList<ChartBar> bars;
        for (int i = 0; i < 15; ++i) {
            const double half = i % 2 == 0 ? 5 : 6.5;
            bars << ChartBar{at(i), 100, 100 + half, 100 - half, 100, 1, true};
        }
        QVERIFY(!atr_size(bars.mid(0, 13)));                      // fewer than 14 bars
        QCOMPARE(atr_size(bars.mid(0, 14)), std::optional(12.0)); // 11.5
        QCOMPARE(atr_size(bars), std::optional(11.0));            // (11.5 × 13 + 10) / 14 = 11.39
        // At least a won.
        QList<ChartBar> flat;
        for (int i = 0; i < 14; ++i)
            flat << ChartBar{at(i), 100, 100.2, 100, 100, 1, true};
        QCOMPARE(atr_size(flat), std::optional(1.0));

        // The size drawn: the one set, else the ATR's; the line break's 3 lines.
        const BrickSizes none;
        QCOMPARE(brick_size(ChartStyle::Renko, none, 11.0), std::optional(11.0));
        QCOMPARE(brick_size(ChartStyle::Kagi, none, 11.0), std::optional(11.0));
        QVERIFY(!brick_size(ChartStyle::PointFigure, none, std::nullopt));
        QCOMPARE(brick_size(ChartStyle::LineBreak, none, std::nullopt), std::optional(3.0));
        QVERIFY(!brick_size(ChartStyle::Candle, none, 11.0));
        const BrickSizes set{{ChartStyle::Renko, 150}, {ChartStyle::LineBreak, 4}};
        QCOMPARE(brick_size(ChartStyle::Renko, set, 11.0), std::optional(150.0));
        QCOMPARE(brick_size(ChartStyle::Kagi, set, 11.0), std::optional(11.0)); // set for renko only
        QCOMPARE(brick_size(ChartStyle::LineBreak, set, 11.0), std::optional(4.0));
        // A set size makes the bricks: renko of the same bars at 10 and at 20.
        const auto bars2 = closes({100, 105, 112, 131, 115, 109, 125, 131, 95, 97});
        QCOMPARE(make_bricks(ChartStyle::Renko, bars2, 10).size(), 7);
        const auto wide = make_bricks(ChartStyle::Renko, bars2, 20);
        QCOMPARE(wide.size(), 1); // 100–120 at 131; 95 is not two boxes back from 120
        QCOMPARE(wide[0].close, 120.0);
        QCOMPARE(make_bricks(ChartStyle::LineBreak, bars2, 3), break_lines(bars2, 3));
        QVERIFY(make_bricks(ChartStyle::HeikinAshi, bars2, 10).isEmpty());
    }

    void a_symbol_keeps_the_sizes_it_set() {
        QVERIFY(valid_brick_size(ChartStyle::Renko, 1) && valid_brick_size(ChartStyle::Kagi, 10'000'000));
        QVERIFY(!valid_brick_size(ChartStyle::Renko, 0) && !valid_brick_size(ChartStyle::Renko, 2.5));
        QVERIFY(!valid_brick_size(ChartStyle::PointFigure, 10'000'001) && !valid_brick_size(ChartStyle::Candle, 5));
        QVERIFY(valid_brick_size(ChartStyle::LineBreak, 2) && valid_brick_size(ChartStyle::LineBreak, 10));
        QVERIFY(!valid_brick_size(ChartStyle::LineBreak, 1) && !valid_brick_size(ChartStyle::LineBreak, 11));
        const BrickSizes sizes{{ChartStyle::Renko, 150}, {ChartStyle::LineBreak, 4}};
        const QString kept = brick_sizes_value(sizes);
        QCOMPARE(kept, QStringLiteral(R"({"line_break":4,"renko":150})"));
        QCOMPARE(parse_brick_sizes(kept), sizes);
        QVERIFY(parse_brick_sizes({}).isEmpty()); // a symbol opened for the first time: the defaults
        QVERIFY(parse_brick_sizes(QStringLiteral("[1,2]")).isEmpty());
        // Broken ones are dropped: not a style without a time axis, not a whole won, out of range.
        const auto broken = parse_brick_sizes(QStringLiteral(
            R"({"renko":"150","kagi":2.5,"point_figure":-1,"line_break":12,"candle":5,"x":1,"renko2":3})"));
        QVERIFY(broken.isEmpty());
        QCOMPARE(parse_brick_sizes(QStringLiteral(R"({"kagi":200,"candle":5})")),
                 (BrickSizes{{ChartStyle::Kagi, 200}}));
    }

    void the_start_waits_for_enough_bars_then_stays() {
        const auto few = wave(0, 100, 1000);
        QVERIFY(!brick_start(few, std::nullopt, false));                      // being read
        QCOMPARE(brick_start(few, std::nullopt, true), std::optional(at(0))); // all the engine has
        QVERIFY(!brick_start({}, std::nullopt, true));
        const auto enough = wave(0, kBrickBars, 1000);
        QCOMPARE(brick_start(enough, std::nullopt, false), std::optional(at(0)));
        // Kept while its bar is held, the bars before read or not.
        const auto more = wave(-300, 300 + kBrickBars, 1000);
        QCOMPARE(brick_start(more, at(0), false), std::optional(at(0)));
        QCOMPARE(bars_from(more, at(0)), enough);
        // Its bar gone (the bars read again from later on): started again once there are enough.
        const auto later = wave(50, kBrickBars, 1000);
        QCOMPARE(brick_start(later, at(0), false), std::optional(at(50)));
        QVERIFY(!brick_start(wave(50, 20, 1000), at(0), false));
    }

    void reading_the_bars_before_the_start_changes_no_brick() {
        // The bricks of the bars first held, the start fixed at their first. The bars read before it
        // then (older ones, as for the indicators) start 3 won off the box grid, so counted from the
        // first bar held every renko brick would move by 3 won: the start keeps them.
        const auto held = wave(0, kBrickBars, 1000);
        auto older = wave(-240, 240, 1003);
        older.first().close = 1003;
        const auto start = brick_start(held, std::nullopt, false);
        QVERIFY(start);
        const QList<ChartBar> all = older + held;
        QCOMPARE(brick_start(all, start, false), start);
        for (const ChartStyle style :
             {ChartStyle::Renko, ChartStyle::Kagi, ChartStyle::PointFigure, ChartStyle::LineBreak}) {
            const double size = style == ChartStyle::LineBreak ? 3 : 10;
            const auto before = make_bricks(style, bars_from(held, *start), size);
            QVERIFY2(before.size() > 5, qPrintable(style_key(style)));
            QCOMPARE(make_bricks(style, bars_from(all, *start), size), before);
        }
        // Counted from the first bar held instead, the renko bricks are another grid.
        const auto renko = make_bricks(ChartStyle::Renko, held, 10);
        const auto moved = make_bricks(ChartStyle::Renko, all, 10);
        QVERIFY(std::fmod(renko.last().close - 1000, 10) == 0);
        QVERIFY(std::fmod(std::abs(moved.last().close - 1003), 10) == 0);
        // New bars only add bricks after them, or move the last one being drawn (kagi, point and
        // figure); the ones before stay.
        const auto grown = held + wave(kBrickBars, 30, 1000);
        for (const ChartStyle style :
             {ChartStyle::Renko, ChartStyle::Kagi, ChartStyle::PointFigure, ChartStyle::LineBreak}) {
            const double size = style == ChartStyle::LineBreak ? 3 : 10;
            const auto before = make_bricks(style, held, size);
            const auto after = make_bricks(style, grown, size);
            QVERIFY(after.size() >= before.size());
            QCOMPARE(after.mid(0, before.size() - 1), before.mid(0, before.size() - 1));
        }
    }

    void bricks_are_drawn_a_slot_each_with_their_time_kept() {
        const auto bricks = renko_bricks(closes({100, 105, 112, 131, 115, 109, 125, 131, 95, 97}), 10);
        const auto drawn = brick_bars(bricks);
        QCOMPARE(drawn.size(), bricks.size());
        for (qsizetype i = 0; i < drawn.size(); ++i) {
            QCOMPARE(drawn[i].time, brick_slot(i));
            QCOMPARE(drawn[i].time, 60 * (i + 1));
            QCOMPARE(drawn[i].open, bricks[i].open);
            QCOMPARE(drawn[i].close, bricks[i].close);
            QCOMPARE(drawn[i].volume, bricks[i].volume);
            QCOMPARE(drawn[i].rising, bricks[i].rising);
        }
        QCOMPARE(bricks[1].time, bricks[2].time); // ended by one bar, drawn on two slots
    }

    void indicators_count_on_the_bricks() {
        StudySet set = default_studies();
        for (const Study s : {Study::Vwap, Study::AnchoredVwap, Study::Obv, Study::AdLine})
            set.turn(s, true);
        const auto bricks =
            renko_bricks(closes({100, 105, 112, 131, 115, 109, 125, 131, 95, 97}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}), 10);
        const auto drawn = brick_bars(bricks);
        const StudyDrawing d = draw_studies(drawn, set, 1, at(3), true, false, {}, true);
        // Neither VWAP: they need the bars' real time.
        for (const auto& row : d.legend)
            QVERIFY(row.study != Study::Vwap && row.study != Study::AnchoredVwap);
        QVERIFY(!d.anchor && !d.picking);
        QVERIFY(study_drawn(Study::Vwap, 1) && !study_drawn(Study::Vwap, 1, true));
        QVERIFY(!study_drawn(Study::Vwap, kDayView) && !study_drawn(Study::AnchoredVwap, 1, true));
        QVERIFY(study_drawn(Study::Obv, kDayView, true) && study_drawn(Study::AnchoredVwap, kDayView));
        QCOMPARE(study_unavailable(Study::Vwap, kDayView), QStringLiteral("분봉에서만"));
        QCOMPARE(study_unavailable(Study::AnchoredVwap, 1, true), QStringLiteral("시간축 없는 차트에서는 쓰지 않는다"));
        QVERIFY(study_unavailable(Study::Rsi, 1, true).isEmpty());
        // The OBV is 0 at the first brick and goes by the bricks' closes and volumes: up 2, up 2, down
        // 11, up 15, down 4.5, down 4.5.
        const auto obv =
            std::find_if(d.series.cbegin(), d.series.cend(), [](const DrawnSeries& s) { return s.id == "obv"; });
        QVERIFY(obv != d.series.cend());
        QCOMPARE(obv->points.size(), drawn.size());
        const QList<double> want{0, 2, 4, -7, 8, 3.5, -1};
        for (qsizetype i = 0; i < want.size(); ++i) {
            QCOMPARE(obv->points[i].first, brick_slot(i));
            QCOMPARE(obv->points[i].second, std::optional(want[i]));
        }
        const auto ad =
            std::find_if(d.series.cbegin(), d.series.cend(), [](const DrawnSeries& s) { return s.id == "ad_line"; });
        QVERIFY(ad != d.series.cend() && ad->points.first().second == std::optional(0.0));
        for (const auto& row : d.legend)
            QVERIFY(row.missing.isEmpty());
        // The moving averages over the bricks' closes: MA 5 at the fifth brick (110 120 130 110 130).
        const auto ma =
            std::find_if(d.series.cbegin(), d.series.cend(), [](const DrawnSeries& s) { return s.id == "ma.0"; });
        QVERIFY(ma != d.series.cend());
        QCOMPARE(ma->points.first().first, brick_slot(4));
        QCOMPARE(ma->points.first().second, std::optional(120.0));
    }
};

QTEST_MAIN(TstKrxChartBricks)
#include "tst_krx_chart_bricks.moc"
