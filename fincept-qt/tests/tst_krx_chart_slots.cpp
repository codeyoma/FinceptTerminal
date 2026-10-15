// Unit tests for src/services/krx_quant/ChartSlots.h
//
// The minute views' empty bars (krx-quant #236): a minute (an N-minute slot) without trades between
// two bars of a day takes a slot with no candle and no volume, so a circuit breaker's 30 minutes show
// as 30 empty slots instead of the time jumping. Nothing is filled before a day's first bar, after its
// last or between two days; a new bar after a gap fills it at once, and history read before joins
// the same way. What computes on prices (the indicators) sees the real bars alone, what places by
// time (the drawings, the bands) counts the slots. Over them, the special situations' bands and
// hover lines. While today's session is open (krx-quant #238) the slots after the last bar fill up
// to the engine's clock, so a halt in progress grows a slot a minute and its band with it. The
// expected slots and positions are worked out by hand.

#include "services/krx_quant/ChartDrawings.h"
#include "services/krx_quant/ChartSlots.h"
#include "services/krx_quant/ChartStudies.h"

#include <QTest>

#include <cmath>

using namespace fincept::krx_quant;

namespace {

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
}

qint64 at(const char* text) {
    return chart_time(kst(text));
}

/// ``count`` 1-minute bars from ``first`` on, their closes rising by 10 from ``base``.
QList<Bar> run(const char* first, int count, double base = 1000) {
    const QDateTime start = kst(first);
    QList<Bar> out;
    for (int i = 0; i < count; ++i) {
        const double close = base + 10 * (i + 1);
        out << Bar{start.addSecs(60 * i), close - 10, close + 5, close - 15, close, 100.0 + i};
    }
    return out;
}

QList<ChartBar> view_of(const QList<Bar>& bars, int minutes) {
    return chart_bars(minutes == 1 ? bars : group_bars(bars, minutes));
}

/// The times of ``bars`` that are empty, and of those that are not.
QList<qint64> empties(const QList<ChartBar>& bars) {
    QList<qint64> out;
    for (const auto& b : bars)
        if (b.empty)
            out << b.time;
    return out;
}

/// Times every ``step`` minutes from ``first`` on, ``count`` of them.
QList<qint64> every(const char* first, int count, int step = 1) {
    QList<qint64> out;
    for (int i = 0; i < count; ++i)
        out << at(first) + static_cast<qint64>(i) * step * 60;
    return out;
}

/// The 2026-10-15 halt, as the demo engine plays it: trades to 13:09, a circuit breaker's 20 minutes
/// and its 10-minute single-price auction without trades, trades again from 13:40.
QList<Bar> halted_day() {
    return run("2026-10-15T13:00:00+09:00", 10) + run("2026-10-15T13:40:00+09:00", 11, 900);
}

SpecialWindow window(const char* kind, const char* start, const char* end, const QString& detail = {},
                     const QString& symbol = {}) {
    return SpecialWindow{QString::fromLatin1(kind), symbol, kst(start), kst(end), detail};
}

/// The 2026-10-15 session (09:00 to 15:30) at ``now``.
std::optional<SessionClock> session_at(const char* now) {
    return SessionClock{at(now), at("2026-10-15T09:00:00+09:00"), at("2026-10-15T15:30:00+09:00")};
}

bool near(const std::optional<double>& got, double want) {
    return got && std::abs(*got - want) < 1e-3;
}

/// The MA 5's value at ``time`` among ``drawing``'s lines.
std::optional<double> ma5_at(const StudyDrawing& drawing, qint64 time) {
    for (const auto& line : drawing.series)
        if (line.id == QLatin1String("ma.0"))
            for (const auto& [t, value] : line.points)
                if (t == time)
                    return value;
    return std::nullopt;
}

} // namespace

class TstKrxChartSlots : public QObject {
    Q_OBJECT

  private slots:
    void minutes_without_trades_between_two_bars_are_empty_bars() {
        // 09:00, 09:01, then none until 09:05 and 09:06.
        const QList<Bar> bars = run("2026-10-12T09:00:00+09:00", 2) + run("2026-10-12T09:05:00+09:00", 2);
        const auto real = view_of(bars, 1);
        const auto slotted = fill_slots(real, 1);
        QCOMPARE(slotted.size(), 7);
        QCOMPARE(empties(slotted), every("2026-10-12T09:02:00+09:00", 3));
        // An empty bar is its time alone: no prices, no volume.
        const ChartBar& gap = slotted[2];
        QVERIFY(gap.empty);
        QCOMPARE(gap.close, 0.0);
        QCOMPARE(gap.volume, 0.0);
        // The real ones are as they were, in their order: what computes on prices sees them alone.
        QCOMPARE(real_bars(slotted), real);
        QCOMPARE(slotted[1], real[1]);
        QCOMPARE(slotted[5], real[2]);
    }

    void a_halt_takes_its_thirty_slots() {
        const auto slotted = fill_slots(view_of(halted_day(), 1), 1);
        QCOMPARE(slotted.size(), 10 + 30 + 11);
        QCOMPARE(empties(slotted), every("2026-10-15T13:10:00+09:00", 30));
        // Every minute from 13:00 to 13:50 has its slot, a minute apart.
        for (qsizetype i = 1; i < slotted.size(); ++i)
            QCOMPARE(slotted[i].time - slotted[i - 1].time, 60);
    }

    void nothing_is_filled_before_a_days_first_bar_after_its_last_or_between_days() {
        // Thursday 10:00 and 10:02, then Friday 09:00 and 09:01 (NXT opens 08:00: not filled before 09:00).
        const QList<Bar> bars = run("2026-10-08T10:00:00+09:00", 1) + run("2026-10-08T10:02:00+09:00", 1) +
                                run("2026-10-09T09:00:00+09:00", 2);
        const auto slotted = fill_slots(view_of(bars, 1), 1);
        QCOMPARE(empties(slotted), QList<qint64>{at("2026-10-08T10:01:00+09:00")});
        QCOMPARE(slotted.first().time, at("2026-10-08T10:00:00+09:00"));
        QCOMPARE(slotted.last().time, at("2026-10-09T09:01:00+09:00"));
        // The night and the hours after Thursday's last bar: no slots.
        QCOMPARE(slotted[3].time, at("2026-10-09T09:00:00+09:00"));
    }

    void a_day_with_one_bar_has_nothing_to_fill() {
        const QList<Bar> bars = run("2026-10-08T14:00:00+09:00", 1) + run("2026-10-09T09:00:00+09:00", 1) +
                                run("2026-10-09T09:03:00+09:00", 1);
        const auto slotted = fill_slots(view_of(bars, 1), 1);
        QCOMPARE(empties(slotted), every("2026-10-09T09:01:00+09:00", 2));
        QCOMPARE(fill_slots(view_of(run("2026-10-08T14:00:00+09:00", 1), 1), 1).size(), 1);
        QVERIFY(fill_slots({}, 1).isEmpty());
    }

    void an_n_minute_view_fills_its_own_slots() {
        // 5 minutes: 09:00 (09:00, 09:01), then 09:30 (09:33): 09:05 to 09:25 are empty.
        const QList<Bar> bars = run("2026-10-12T09:00:00+09:00", 2) + run("2026-10-12T09:33:00+09:00", 1);
        const auto five = fill_slots(view_of(bars, 5), 5);
        QCOMPARE(five.size(), 7);
        QCOMPARE(empties(five), every("2026-10-12T09:05:00+09:00", 5, 5));
        // The halt in the 5-minute view: 13:10 to 13:35 are empty, 13:05 and 13:40 hold trades.
        const auto halt = fill_slots(view_of(halted_day(), 5), 5);
        QCOMPARE(empties(halt), every("2026-10-15T13:10:00+09:00", 6, 5));
        QCOMPARE(real_bars(halt), view_of(halted_day(), 5));
        // 60 minutes: 09:00 and 11:00 hold trades, 10:00 is empty.
        const QList<Bar> hours = run("2026-10-12T09:10:00+09:00", 1) + run("2026-10-12T11:20:00+09:00", 1);
        QCOMPARE(empties(fill_slots(view_of(hours, 60), 60)), QList<qint64>{at("2026-10-12T10:00:00+09:00")});
        // 240 minutes: 08:00 and 16:00 hold trades, 12:00 is empty.
        const QList<Bar> long_day = run("2026-10-12T08:10:00+09:00", 1) + run("2026-10-12T16:30:00+09:00", 1);
        QCOMPARE(empties(fill_slots(view_of(long_day, 240), 240)), QList<qint64>{at("2026-10-12T12:00:00+09:00")});
    }

    void a_candle_view_is_not_filled() {
        const QList<ChartBar> days = {ChartBar{at("2026-10-08T00:00:00+09:00"), 1, 2, 1, 2, 10, true},
                                      ChartBar{at("2026-10-12T00:00:00+09:00"), 2, 3, 2, 3, 10, true}};
        QCOMPARE(fill_slots(days, kDayView), days);
        QCOMPARE(fill_slots(days, kWeekView), days);
    }

    void a_new_bar_after_a_gap_fills_it_at_once() {
        auto slotted = fill_slots(view_of(run("2026-10-15T13:00:00+09:00", 10), 1), 1); // up to 13:09
        // The same minute grown: the last one replaced, nothing filled.
        ChartBar grown = slotted.last();
        grown.close = 1;
        auto made = append_live_slots(slotted, grown, 1);
        QVERIFY(!made.appended);
        QVERIFY(made.filled.isEmpty());
        QCOMPARE(slotted.last().close, 1.0);
        // An earlier one: left out.
        made = append_live_slots(slotted, view_of(run("2026-10-15T13:05:00+09:00", 1), 1).first(), 1);
        QVERIFY(!made.appended);
        QCOMPARE(slotted.size(), 10);
        // The first trade after the halt: its 30 minutes filled before it, at once.
        const ChartBar after = view_of(run("2026-10-15T13:40:00+09:00", 1), 1).first();
        made = append_live_slots(slotted, after, 1);
        QVERIFY(made.appended);
        QCOMPARE(made.filled, every("2026-10-15T13:10:00+09:00", 30));
        QCOMPARE(slotted.size(), 41);
        QCOMPARE(slotted.last(), after);
        // As if read whole: the same slots.
        QCOMPARE(empties(slotted), empties(fill_slots(real_bars(slotted), 1)));
        // The next minute: appended, nothing to fill.
        made = append_live_slots(slotted, view_of(run("2026-10-15T13:41:00+09:00", 1), 1).first(), 1);
        QVERIFY(made.appended);
        QVERIFY(made.filled.isEmpty());
        // The next day's first: no slots over the night.
        made = append_live_slots(slotted, view_of(run("2026-10-16T08:00:00+09:00", 1), 1).first(), 1);
        QVERIFY(made.appended);
        QVERIFY(made.filled.isEmpty());
        // A wider view's: its own slots.
        auto five = fill_slots(view_of(run("2026-10-15T13:00:00+09:00", 10), 5), 5); // 13:00, 13:05
        made = append_live_slots(five, view_of(run("2026-10-15T13:40:00+09:00", 1), 5).first(), 5);
        QCOMPARE(made.filled, every("2026-10-15T13:10:00+09:00", 6, 5));
        // A candle view's: nothing filled.
        QList<ChartBar> days = {ChartBar{at("2026-10-08T00:00:00+09:00"), 1, 2, 1, 2, 10, true}};
        made = append_live_slots(days, ChartBar{at("2026-10-12T00:00:00+09:00"), 2, 3, 2, 3, 10, true}, kDayView);
        QVERIFY(made.appended);
        QVERIFY(made.filled.isEmpty());
        QList<ChartBar> none;
        QVERIFY(append_live_slots(none, after, 1).appended);
    }

    void history_read_before_joins_its_slots() {
        // Today from 09:10, then 09:00 to 09:02 read before as the chart scrolls back: the minutes
        // between them are filled across the join, and today's slots stay as they were after it.
        const QList<Bar> today = run("2026-10-12T09:10:00+09:00", 3) + run("2026-10-12T09:20:00+09:00", 2);
        const QList<Bar> before = run("2026-10-12T09:00:00+09:00", 3);
        const auto shown = fill_slots(view_of(today, 1), 1);
        const auto joined = fill_slots(view_of(join_bars(before, today), 1), 1);
        QCOMPARE(empties(joined).mid(0, 7), every("2026-10-12T09:03:00+09:00", 7));
        QCOMPARE(joined.mid(joined.size() - shown.size()), shown);
        // A day before: nothing over the night.
        const QList<Bar> yesterday = run("2026-10-09T15:00:00+09:00", 2);
        const auto longer = fill_slots(view_of(join_bars(yesterday, join_bars(before, today)), 1), 1);
        QCOMPARE(longer.mid(2), joined);
    }

    void the_indicators_see_the_real_bars_alone() {
        // The MA 5 at the first bar after the halt is the mean of the last five trades' closes,
        // 13:06 to 13:09 and 13:40: the empty bars are no values, and the line has no point on them.
        const auto real = view_of(halted_day(), 1);
        const auto slotted = fill_slots(real, 1);
        StudySet set = default_studies();
        const StudyDrawing over_real = draw_studies(real, set, 1, std::nullopt, false);
        const StudyDrawing over_slots = draw_studies(real_bars(slotted), set, 1, std::nullopt, false);
        QCOMPARE(over_slots.series.size(), over_real.series.size());
        for (qsizetype i = 0; i < over_real.series.size(); ++i)
            QCOMPARE(over_slots.series[i].points, over_real.series[i].points);
        // Closes 1070, 1080, 1090, 1100 (13:06–13:09) and 910 (13:40).
        const auto ma = ma5_at(over_slots, at("2026-10-15T13:40:00+09:00"));
        QVERIFY(ma);
        QVERIFY(std::abs(*ma - (1070.0 + 1080 + 1090 + 1100 + 910) / 5) < 1e-9);
        // No point on an empty bar: Lightweight Charts joins the line over the gap.
        for (const auto& line : over_slots.series)
            for (const auto& [time, value] : line.points)
                QVERIFY2(!empties(slotted).contains(time), qPrintable(line.id));
    }

    void drawings_place_by_the_slots() {
        // A moment of the halt lies on its own slot; its time comes back.
        const auto slotted = fill_slots(view_of(halted_day(), 1), 1);
        const qint64 halt = at("2026-10-15T13:25:30+09:00");
        QVERIFY(near(logical_at(slotted, halt, 1), 25)); // 13:25's slot, its middle
        QCOMPARE(time_at(slotted, 25, 1).value_or(0), halt);
        // A moment after the halt keeps its place in the 5-minute view: 13:40's slot (index 8).
        const auto five = fill_slots(view_of(halted_day(), 5), 5);
        QVERIFY(near(logical_at(five, at("2026-10-15T13:42:30+09:00"), 5), 8));
        QVERIFY(near(logical_at(five, halt, 5), 5 - 0.5 + 30.0 / 300)); // 30 s into 13:25's slot
        // The magnet leaves a point over an empty bar where it is: there is no price to snap to.
        QCOMPARE(magnet_spot(slotted, {25, 1234}), (ChartSpot{25, 1234}));
        QCOMPARE(magnet_spot(slotted, {9.2, 1101}), (ChartSpot{9, 1100})); // 13:09, the last before it
    }

    void special_situations_are_named() {
        QCOMPARE(special_title(window("circuit_breaker", "2026-10-15T13:10:00+09:00", "2026-10-15T13:30:00+09:00",
                                      QStringLiteral("1단계"))),
                 QStringLiteral("서킷브레이커 1단계"));
        QCOMPARE(special_title(window("sidecar", "2026-10-15T13:10:00+09:00", "2026-10-15T13:15:00+09:00")),
                 QStringLiteral("사이드카"));
        // A VI by its kind as KIS reports it, in Korean; without one, "VI".
        const auto vi = [](const char* detail) {
            return special_title(window("vi", "2026-10-15T13:45:10+09:00", "2026-10-15T13:47:20+09:00",
                                        QString::fromLatin1(detail), "005930"));
        };
        QCOMPARE(vi("static"), QStringLiteral("정적 VI"));
        QCOMPARE(vi("dynamic"), QStringLiteral("동적 VI"));
        QCOMPARE(vi("both"), QStringLiteral("정적·동적 VI"));
        QCOMPARE(vi(""), QStringLiteral("VI"));
        QCOMPARE(special_title(window("circuit_breaker", "2026-10-15T13:10:00+09:00", "2026-10-15T13:30:00+09:00",
                                      QStringLiteral("3단계"))),
                 QStringLiteral("서킷브레이커 3단계"));
    }

    void the_bands_cover_their_slots() {
        const auto slotted = fill_slots(view_of(halted_day(), 1), 1); // 13:00 at 0 … 13:50 at 50
        const QList<SpecialWindow> windows = {
            window("circuit_breaker", "2026-10-15T13:10:00+09:00", "2026-10-15T13:30:00+09:00",
                   QStringLiteral("1단계")),
            window("vi", "2026-10-15T13:45:10+09:00", "2026-10-15T13:47:20+09:00", {}, "005930"),
            window("vi", "2026-10-15T10:00:00+09:00", "2026-10-15T10:02:00+09:00"),      // before the bars: none
            window("sidecar", "2026-10-15T13:52:00+09:00", "2026-10-15T13:57:00+09:00"), // past the last bar
            window("vi", "2026-10-15T13:20:00+09:00", "2026-10-15T13:20:00+09:00"),      // no time: none
            SpecialWindow{"vi", {}, {}, kst("2026-10-15T13:20:00+09:00"), {}},           // no start: none
        };
        const auto bands = special_bands(slotted, windows, 1);
        QCOMPARE(bands.size(), 3);
        QCOMPARE(bands[0], (SpecialBand{"circuit_breaker", 9.5, 29.5, QStringLiteral("서킷브레이커 1단계")}));
        QCOMPARE(bands[1].label, QStringLiteral("VI"));
        QVERIFY(std::abs(bands[1].from - (45 - 0.5 + 10.0 / 60)) < 1e-6);
        QVERIFY(std::abs(bands[1].to - (47 - 0.5 + 20.0 / 60)) < 1e-6);
        // Past the last bar (13:50 at 50): in the free space on the right, a slot a minute.
        QCOMPARE(bands[2].kind, QStringLiteral("sidecar"));
        QVERIFY(std::abs(bands[2].from - 51.5) < 1e-6);
        QVERIFY(std::abs(bands[2].to - 56.5) < 1e-6);
        // The 5-minute view: 13:10 to 13:30 over the slots 2 to 5 (13:10–13:25).
        const auto five = special_bands(fill_slots(view_of(halted_day(), 5), 5), windows.mid(0, 1), 5);
        QCOMPARE(five.size(), 1);
        QVERIFY(std::abs(five[0].from - 1.5) < 1e-6);
        QVERIFY(std::abs(five[0].to - 5.5) < 1e-6);
        // None in a candle view, or without bars.
        QVERIFY(special_bands(slotted, windows, kDayView).isEmpty());
        QVERIFY(special_bands({}, windows, 1).isEmpty());
    }

    void hover_lines_name_the_empty_bars_and_the_bands() {
        const QList<SpecialWindow> windows = {
            window("circuit_breaker", "2026-10-15T13:10:00+09:00", "2026-10-15T13:30:00+09:00",
                   QStringLiteral("1단계")),
            window("vi", "2026-10-15T13:45:10+09:00", "2026-10-15T13:47:20+09:00", QStringLiteral("dynamic"), "005930"),
        };
        // The window's lines: whole minutes without seconds; the end is not in it.
        QCOMPARE(special_lines(windows, at("2026-10-15T13:29:00+09:00"), at("2026-10-15T13:30:00+09:00")),
                 QStringList{QStringLiteral("서킷브레이커 1단계 13:10–13:30")});
        QVERIFY(special_lines(windows, at("2026-10-15T13:30:00+09:00"), at("2026-10-15T13:31:00+09:00")).isEmpty());
        QCOMPARE(special_lines(windows, at("2026-10-15T13:47:00+09:00"), at("2026-10-15T13:48:00+09:00")),
                 QStringList{QStringLiteral("동적 VI 13:45:10–13:47:20")});
        QCOMPARE(special_lines(windows, at("2026-10-15T13:00:00+09:00"), at("2026-10-15T14:00:00+09:00")).size(), 2);
        QCOMPARE(empty_line(at("2026-10-15T13:10:00+09:00"), 1), QStringLiteral("13:10–13:11 · 체결 없음"));
        QCOMPARE(empty_line(at("2026-10-15T13:10:00+09:00"), 5), QStringLiteral("13:10–13:15 · 체결 없음"));

        // The 1-minute view: the real bars' boxes keep their lines and four-minute windows, a band's
        // line after them; each empty bar has a box of its own.
        const auto slotted = fill_slots(view_of(halted_day(), 1), 1);
        BarHover before;
        before.time = at("2026-10-15T13:09:00+09:00");
        before.lines = {QStringLiteral("13:09 봉 → 13:13 목표 · 채점 전")};
        before.window_from = at("2026-10-15T13:10:00+09:00");
        before.window_to = at("2026-10-15T13:14:00+09:00");
        BarHover vi_bar;
        vi_bar.time = at("2026-10-15T13:46:00+09:00");
        vi_bar.lines = {QStringLiteral("13:46 봉")};
        const auto boxes = slot_hovers(slotted, {before, vi_bar}, windows, 1);
        QCOMPARE(boxes.size(), 30 + 2 + 2); // the empty bars, the two given, 13:45 and 13:47 under the VI
        QCOMPARE(boxes.first().time, before.time);
        QCOMPARE(boxes.first().lines, before.lines); // 13:09 is before the circuit breaker
        QCOMPARE(boxes.first().window_to, before.window_to);
        QCOMPARE(boxes[1].time, at("2026-10-15T13:10:00+09:00"));
        QCOMPARE(boxes[1].lines, (QStringList{QStringLiteral("13:10–13:11 · 체결 없음"),
                                              QStringLiteral("서킷브레이커 1단계 13:10–13:30")}));
        QCOMPARE(boxes[1].window_to, 0); // no four-minute window on an empty bar
        QCOMPARE(boxes[30].lines, QStringList{QStringLiteral("13:39–13:40 · 체결 없음")}); // after the circuit breaker
        QCOMPARE(boxes[31].time, at("2026-10-15T13:45:00+09:00")); // a bar without a box: the VI's line alone
        QCOMPARE(boxes[31].lines, QStringList{QStringLiteral("동적 VI 13:45:10–13:47:20")});
        QCOMPARE(boxes[32].lines,
                 (QStringList{QStringLiteral("13:46 봉"), QStringLiteral("동적 VI 13:45:10–13:47:20")}));
        QCOMPARE(boxes[33].time, at("2026-10-15T13:47:00+09:00")); // the VI ends 20 s into it
        // Without windows (shown off): the empty bars' boxes alone, and the real ones' as they came.
        QCOMPARE(slot_hovers(slotted, {before}, {}, 1).size(), 31);

        // The 5-minute view: no boxes of its own but the empty slots' and those under a band.
        const auto five = slot_hovers(fill_slots(view_of(halted_day(), 5), 5), {}, windows, 5);
        QCOMPARE(five.size(), 6 + 1); // 13:10–13:35 empty; 13:45 under the VI
        QCOMPARE(five[0].lines, (QStringList{QStringLiteral("13:10–13:15 · 체결 없음"),
                                             QStringLiteral("서킷브레이커 1단계 13:10–13:30")}));
        QCOMPARE(five[6].time, at("2026-10-15T13:45:00+09:00"));
        // None in a candle view.
        QVERIFY(slot_hovers(slotted, {before}, windows, kDayView).isEmpty());
    }

    void the_session_and_its_clock() {
        EngineStatus status;
        QVERIFY(!session_clock(status, kst("2026-10-15T10:00:00+09:00"))); // no session (a holiday)
        QVERIFY(!session_clock(std::nullopt, kst("2026-10-15T10:00:00+09:00")));
        status.session_open = kst("2026-10-15T09:00:00+09:00");
        status.session_close = kst("2026-10-15T15:30:00+09:00");
        QCOMPARE(session_clock(status, kst("2026-10-15T13:20:30+09:00")), session_at("2026-10-15T13:20:30+09:00"));
        // The slot of the clock while the session is open; none before its open or from its close on.
        QCOMPARE(clock_slot(session_at("2026-10-15T13:20:30+09:00"), 1), at("2026-10-15T13:20:00+09:00"));
        QCOMPARE(clock_slot(session_at("2026-10-15T13:27:00+09:00"), 5), at("2026-10-15T13:25:00+09:00"));
        QCOMPARE(clock_slot(session_at("2026-10-15T13:27:00+09:00"), 60), at("2026-10-15T13:00:00+09:00"));
        QVERIFY(!clock_slot(session_at("2026-10-15T08:30:00+09:00"), 1));
        QVERIFY(!clock_slot(session_at("2026-10-15T15:30:00+09:00"), 1));
        QVERIFY(!clock_slot(session_at("2026-10-15T13:20:30+09:00"), kDayView));
        QVERIFY(!clock_slot(std::nullopt, 1));
    }

    void a_halt_in_progress_grows_a_slot_a_minute() {
        // Trades to 13:09, then the circuit breaker: no bar after it yet.
        const auto real = view_of(run("2026-10-15T13:00:00+09:00", 10), 1);
        // Without the clock (a completed day), nothing after the last bar.
        QCOMPARE(fill_slots(real, 1).size(), 10);
        // 13:10:30: the 13:10 slot is not over yet.
        QCOMPARE(fill_slots(real, 1, session_at("2026-10-15T13:10:30+09:00")).size(), 10);
        // 13:11: 13:10 is over, an empty bar; then a slot more each minute.
        QCOMPARE(empties(fill_slots(real, 1, session_at("2026-10-15T13:11:00+09:00"))),
                 QList<qint64>{at("2026-10-15T13:10:00+09:00")});
        QCOMPARE(empties(fill_slots(real, 1, session_at("2026-10-15T13:12:10+09:00"))),
                 every("2026-10-15T13:10:00+09:00", 2));
        const auto at_25 = fill_slots(real, 1, session_at("2026-10-15T13:25:30+09:00"));
        QCOMPARE(empties(at_25), every("2026-10-15T13:10:00+09:00", 15));
        // Its band grows with the clock: from 13:10 to 13:25:30, not to the 13:30 the engine expects.
        const QList<SpecialWindow> halt = {window("circuit_breaker", "2026-10-15T13:10:00+09:00",
                                                  "2026-10-15T13:30:00+09:00", QStringLiteral("1단계"))};
        const auto bands = special_bands(at_25, halt, 1, session_at("2026-10-15T13:25:30+09:00"));
        QCOMPARE(bands.size(), 1);
        QVERIFY(std::abs(bands[0].from - 9.5) < 1e-6);
        QVERIFY(std::abs(bands[0].to - 25.0) < 1e-6); // 30 s into the 13:25 slot, after the last empty bar
        // Over (13:40, the clock past its end): its whole length.
        const auto over = special_bands(fill_slots(real, 1, session_at("2026-10-15T13:40:30+09:00")), halt, 1,
                                        session_at("2026-10-15T13:40:30+09:00"));
        QVERIFY(std::abs(over[0].to - 29.5) < 1e-6);
        // The first trade after it: the bars as read, the slots the same up to it.
        const auto resumed = fill_slots(view_of(halted_day(), 1), 1, session_at("2026-10-15T13:50:30+09:00"));
        QCOMPARE(empties(resumed), every("2026-10-15T13:10:00+09:00", 30));
    }

    void a_quiet_stock_shows_its_quiet_minutes_mid_session() {
        // Trades at 10:00 and 10:20, none since; at 10:45:10 the minutes up to 10:44 are empty bars.
        const auto real = view_of(run("2026-10-15T10:00:00+09:00", 1) + run("2026-10-15T10:20:00+09:00", 1), 1);
        const auto slotted = fill_slots(real, 1, session_at("2026-10-15T10:45:10+09:00"));
        QCOMPARE(empties(slotted), every("2026-10-15T10:01:00+09:00", 19) + every("2026-10-15T10:21:00+09:00", 24));
        QCOMPARE(slotted.last().time, at("2026-10-15T10:44:00+09:00"));
        // A day with a single bar so far grows too.
        QCOMPARE(fill_slots(view_of(run("2026-10-15T10:00:00+09:00", 1), 1), 1, session_at("2026-10-15T10:03:00+09:00"))
                     .size(),
                 3);
    }

    void nothing_grows_after_the_close_or_before_the_first_bar() {
        const auto real = view_of(run("2026-10-15T15:00:00+09:00", 1), 1);
        // After the close (15:30): a completed day, nothing after its last bar.
        QCOMPARE(fill_slots(real, 1, session_at("2026-10-15T15:45:00+09:00")).size(), 1);
        // Before today's first bar: yesterday's bars do not grow into today.
        const auto yesterday = view_of(run("2026-10-14T15:00:00+09:00", 2), 1);
        QCOMPARE(fill_slots(yesterday, 1, session_at("2026-10-15T10:00:00+09:00")), yesterday);
        // Before the open: an NXT bar at 08:10 does not grow before 09:00.
        const auto early = view_of(run("2026-10-15T08:10:00+09:00", 1), 1);
        QCOMPARE(fill_slots(early, 1, session_at("2026-10-15T08:40:00+09:00")).size(), 1);
        // A candle view: as it is.
        const QList<ChartBar> days = {ChartBar{at("2026-10-15T00:00:00+09:00"), 1, 2, 1, 2, 10, true}};
        QCOMPARE(fill_slots(days, kDayView, session_at("2026-10-15T13:00:00+09:00")), days);
    }

    void a_wider_view_grows_by_its_own_slots() {
        // 5 minutes: 13:00 and 13:05 hold trades; at 13:27 the 13:10, 13:15 and 13:20 slots are over.
        const auto five = view_of(run("2026-10-15T13:00:00+09:00", 10), 5);
        QCOMPARE(empties(fill_slots(five, 5, session_at("2026-10-15T13:27:00+09:00"))),
                 every("2026-10-15T13:10:00+09:00", 3, 5));
        QCOMPARE(empties(fill_slots(five, 5, session_at("2026-10-15T13:24:59+09:00"))),
                 every("2026-10-15T13:10:00+09:00", 2, 5));
    }

    void a_real_bar_takes_an_empty_bars_slot() {
        // Up to the clock at 13:25:30: 13:10 to 13:24 are empty bars.
        const auto clock = session_at("2026-10-15T13:25:30+09:00");
        const QList<Bar> traded = run("2026-10-15T13:00:00+09:00", 10);
        // Read again with a trade at 13:15 (a late one): its slot is a real bar now.
        const auto read = fill_slots(view_of(traded + run("2026-10-15T13:15:00+09:00", 1), 1), 1, clock);
        QCOMPARE(read.size(), 25);
        QVERIFY(!read[15].empty);
        QCOMPARE(empties(read).size(), 14);
        // The bar being made on the last empty bar's slot: taken where it is, the page moves it alone.
        auto slotted = fill_slots(view_of(traded, 1), 1, clock);
        const ChartBar on_last = view_of(run("2026-10-15T13:24:00+09:00", 1), 1).first();
        auto made = append_live_slots(slotted, on_last, 1);
        QVERIFY(!made.appended);
        QCOMPARE(made.replaced, at("2026-10-15T13:24:00+09:00"));
        QVERIFY(!made.redraw);
        QCOMPARE(slotted.last(), on_last);
        // On an earlier empty bar's: taken, and the page draws again.
        const ChartBar earlier = view_of(run("2026-10-15T13:15:00+09:00", 1), 1).first();
        made = append_live_slots(slotted, earlier, 1);
        QCOMPARE(made.replaced, at("2026-10-15T13:15:00+09:00"));
        QVERIFY(made.redraw);
        QCOMPARE(slotted[15], earlier);
        QCOMPARE(slotted.size(), 25);
        // A real bar's slot before the last: replaced, drawn again, no empty bar taken.
        made = append_live_slots(slotted, view_of(run("2026-10-15T13:05:00+09:00", 1, 5), 1).first(), 1);
        QVERIFY(!made.replaced);
        QVERIFY(made.redraw);
        // Before the first: no slot, left out.
        made = append_live_slots(slotted, view_of(run("2026-10-15T12:00:00+09:00", 1), 1).first(), 1);
        QVERIFY(!made.replaced && !made.redraw && !made.appended);
        QCOMPARE(slotted.size(), 25);
    }

    void a_live_bar_under_a_band_names_it() {
        // The circuit breaker from 13:10, the clock at 13:12:30: 13:10 and 13:11 are empty bars.
        const QList<SpecialWindow> halt = {window("circuit_breaker", "2026-10-15T13:10:00+09:00",
                                                  "2026-10-15T13:30:00+09:00", QStringLiteral("1단계"))};
        auto slotted =
            fill_slots(view_of(run("2026-10-15T13:00:00+09:00", 10), 1), 1, session_at("2026-10-15T13:12:30+09:00"));
        // A trade at 13:14 (the bar being made): 13:12 and 13:13 are filled before it, and the bar itself,
        // under the band, has the band's line until its own box comes with the next drawing.
        const qint64 live = at("2026-10-15T13:14:00+09:00");
        auto made = append_live_slots(slotted, view_of(run("2026-10-15T13:14:00+09:00", 1), 1).first(), 1);
        QVERIFY(made.appended);
        auto boxes = live_hovers(slotted, live, made, halt, 1);
        QCOMPARE(boxes.size(), 3);
        QCOMPARE(boxes[0].lines, (QStringList{QStringLiteral("13:12–13:13 · 체결 없음"),
                                              QStringLiteral("서킷브레이커 1단계 13:10–13:30")}));
        QCOMPARE(boxes[2].time, live);
        QCOMPARE(boxes[2].lines, QStringList{QStringLiteral("서킷브레이커 1단계 13:10–13:30")});
        // The bar growing within its minute changes no box.
        made = append_live_slots(slotted, view_of(run("2026-10-15T13:14:00+09:00", 1, 50), 1).first(), 1);
        QVERIFY(live_hovers(slotted, live, made, halt, 1).isEmpty());
        // Taking an empty bar's slot (13:11): its box is the band's line now, not "체결 없음".
        made = append_live_slots(slotted, view_of(run("2026-10-15T13:11:00+09:00", 1), 1).first(), 1);
        boxes = live_hovers(slotted, at("2026-10-15T13:11:00+09:00"), made, halt, 1);
        QCOMPARE(boxes.size(), 1);
        QCOMPARE(boxes[0].lines, QStringList{QStringLiteral("서킷브레이커 1단계 13:10–13:30")});
        // Outside any window: an appended bar has no box, the empty bars filled before it have theirs.
        auto quiet = fill_slots(view_of(run("2026-10-15T10:00:00+09:00", 1), 1), 1);
        made = append_live_slots(quiet, view_of(run("2026-10-15T10:03:00+09:00", 1), 1).first(), 1);
        boxes = live_hovers(quiet, at("2026-10-15T10:03:00+09:00"), made, halt, 1);
        QCOMPARE(boxes.size(), 2);
        QCOMPARE(boxes[1].lines, QStringList{QStringLiteral("10:02–10:03 · 체결 없음")});
    }
};

QTEST_GUILESS_MAIN(TstKrxChartSlots)
#include "tst_krx_chart_slots.moc"
