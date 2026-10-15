// Unit tests for src/services/krx_quant/ChartDrawings.h
//
// The KRX chart's drawings (krx-quant #224): the tools and their names, a symbol's drawings as
// kept and read back (broken ones dropped), where a moment lies on a view's bars and back (so a
// drawing stays put across the minute and candle views), and what the magnet picks. The expected
// positions are worked out by hand.

#include "services/krx_quant/ChartDrawings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

Bar bar(const char* start, double open, double high, double low, double close) {
    return Bar{kst(start), open, high, low, close, 100};
}

/// 1-minute bars on Monday 2026-10-12: 09:00, 09:01, 09:02, then none until 09:05 and 09:06.
QList<Bar> minutes() {
    return {bar("2026-10-12T09:00:00+09:00", 100, 104, 99, 102), bar("2026-10-12T09:01:00+09:00", 102, 103, 100, 101),
            bar("2026-10-12T09:02:00+09:00", 101, 105, 101, 104), bar("2026-10-12T09:05:00+09:00", 104, 106, 103, 105),
            bar("2026-10-12T09:06:00+09:00", 105, 105, 102, 103)};
}

QList<ChartBar> view_of(const QList<Bar>& bars, int view) {
    return chart_bars(view == 1 ? bars : group_bars(bars, view));
}

/// Whether ``got`` is ``want`` to within a thousandth of a bar.
bool near(const std::optional<double>& got, double want) {
    return got && std::abs(*got - want) < 1e-3;
}

QString text_of(const QJsonValue& value) {
    return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
}

QJsonObject trend(const QJsonValue& first_time = "2026-10-12T09:00:30+09:00") {
    return QJsonObject{{"tool", "trend_line"},
                       {"points", QJsonArray{QJsonObject{{"time", first_time}, {"price", 101.5}},
                                             QJsonObject{{"time", "2026-10-12T09:06:30+09:00"}, {"price", 104}}}},
                       {"color", "#22C55E"},
                       {"width", 3},
                       {"style", "dashed"}};
}

} // namespace

class TstKrxChartDrawings : public QObject {
    Q_OBJECT

  private slots:
    void tools_have_keys_names_and_points() {
        QCOMPARE(kDrawingTools.size(), std::size_t(12));
        for (const DrawingTool tool : kDrawingTools) {
            QCOMPARE(parse_tool(tool_key(tool)), std::optional(tool));
            QVERIFY(!tool_title(tool).isEmpty());
        }
        QCOMPARE(tool_key(DrawingTool::FibFan), QStringLiteral("fib_fan"));
        QCOMPARE(tool_title(DrawingTool::Ray), QStringLiteral("반직선"));
        QCOMPARE(tool_title(DrawingTool::FibFan), QStringLiteral("피보나치 속도 저항 팬"));
        QVERIFY(!parse_tool("line"));
        QCOMPARE(tool_points(DrawingTool::Horizontal), 1);
        QCOMPARE(tool_points(DrawingTool::Text), 1);
        QCOMPARE(tool_points(DrawingTool::TrendLine), 2);
        QCOMPARE(tool_points(DrawingTool::FibFan), 2);
        QCOMPARE(tool_points(DrawingTool::Pitchfork), 3);
        QCOMPARE(tool_points(DrawingTool::FibChannel), 3);
        for (const LineStyle style : kLineStyles)
            QCOMPARE(parse_line_style(line_style_key(style)), std::optional(style));
        QCOMPARE(line_style_title(LineStyle::Dashed), QStringLiteral("파선"));
        QVERIFY(!parse_line_style("wavy"));
    }

    void a_new_drawing_takes_its_first_look() {
        const Drawing line = new_drawing(DrawingTool::TrendLine, {{1, 2}, {3, 4}});
        QCOMPARE(line.color, QStringLiteral("#60A5FA"));
        QCOMPARE(line.width, 2);
        QCOMPARE(line.style, LineStyle::Solid);
        QCOMPARE(new_drawing(DrawingTool::FibRetracement, {}).width, 1);
        QCOMPARE(next_drawing_id({}), 1);
        Drawing a = line, b = line;
        a.id = 4;
        b.id = 2;
        QCOMPARE(next_drawing_id({a, b}), 5);
    }

    void drawings_are_kept_and_read_back() {
        Drawing line = new_drawing(DrawingTool::TrendLine,
                                   {{at("2026-10-12T09:00:30+09:00"), 101.5}, {at("2026-10-12T09:06:30+09:00"), 104}});
        line.color = "#22C55E";
        line.width = 3;
        line.style = LineStyle::Dashed;
        Drawing text = new_drawing(DrawingTool::Text, {{at("2026-10-12T09:02:00+09:00"), 105}});
        text.text = QStringLiteral("지지선 확인");
        Drawing fork = new_drawing(DrawingTool::Pitchfork, {{at("2026-10-08T15:00:00+09:00"), 70000},
                                                            {at("2026-10-09T10:00:00+09:00"), 71250.5},
                                                            {at("2026-10-12T09:30:00+09:00"), 69800}});
        fork.style = LineStyle::Dotted;
        const QString kept = drawings_value({line, text, fork});
        // Moments as ISO 8601 in KST, the ids not kept.
        QVERIFY(kept.contains(QStringLiteral("\"time\":\"2026-10-12T09:00:30+09:00\"")));
        QVERIFY(!kept.contains(QStringLiteral("\"id\"")));
        const QList<Drawing> read = parse_drawings(kept);
        QCOMPARE(read.size(), 3);
        line.id = 1;
        text.id = 2;
        fork.id = 3;
        QCOMPARE(read[0], line);
        QCOMPARE(read[1], text);
        QCOMPARE(read[2], fork);
        // None kept, or nothing like a list: none.
        QVERIFY(parse_drawings({}).isEmpty());
        QVERIFY(parse_drawings(QStringLiteral("not json")).isEmpty());
        QVERIFY(parse_drawings(QStringLiteral("{\"tool\":\"ray\"}")).isEmpty());
        QCOMPARE(parse_drawings(QStringLiteral("[]")).size(), 0);
    }

    void broken_drawings_are_dropped() {
        QJsonObject unknown = trend();
        unknown["tool"] = "spiral";
        QJsonObject short_of_points = trend();
        short_of_points["points"] = QJsonArray{trend()["points"].toArray().first()};
        QJsonObject bad_price = trend();
        bad_price["points"] = QJsonArray{QJsonObject{{"time", "2026-10-12T09:00:30+09:00"}, {"price", "101"}},
                                         trend()["points"].toArray().last()};
        QJsonObject empty_text{{"tool", "text"},
                               {"points", QJsonArray{QJsonObject{{"time", "2026-10-12T09:00:00+09:00"}, {"price", 1}}}},
                               {"text", "   "}};
        const QJsonArray all{QJsonValue(7), unknown,    short_of_points, trend("yesterday"),
                             bad_price,     empty_text, trend()};
        const QList<Drawing> read = parse_drawings(text_of(all));
        QCOMPARE(read.size(), 1); // only the last, whole one
        QCOMPARE(read.first().id, 1);
        QCOMPARE(read.first().points.first(), (DrawingPoint{at("2026-10-12T09:00:30+09:00"), 101.5}));

        // A broken look is the tool's first look; a long text is cut.
        QJsonObject look = trend();
        look["color"] = "green";
        look["width"] = 9;
        look["style"] = "wavy";
        QJsonObject half = trend();
        half["tool"] = "fib_fan";
        half["width"] = 2.5;
        QJsonObject longer = empty_text;
        longer["text"] = QString(300, QChar(u'가'));
        const QList<Drawing> fixed = parse_drawings(text_of(QJsonArray{look, half, longer}));
        QCOMPARE(fixed.size(), 3);
        QCOMPARE(fixed[0].color, QStringLiteral("#60A5FA"));
        QCOMPARE(fixed[0].width, 2);
        QCOMPARE(fixed[0].style, LineStyle::Solid);
        QCOMPARE(fixed[1].width, 1); // a Fibonacci tool's
        QCOMPARE(fixed[2].text.size(), kMaxDrawingText);
    }

    void the_magnet_picks_the_nearest_price_of_the_bar_under_it() {
        const auto bars = view_of(minutes(), 1); // the bar 1: open 102, high 103, low 100, close 101
        QCOMPARE(magnet_spot(bars, {1.3, 102.8}), (ChartSpot{1, 103}));  // the high
        QCOMPARE(magnet_spot(bars, {0.6, 100.2}), (ChartSpot{1, 100}));  // the low, the bar's middle
        QCOMPARE(magnet_spot(bars, {1.0, 101.4}), (ChartSpot{1, 101}));  // the close
        QCOMPARE(magnet_spot(bars, {1.49, 101.5}), (ChartSpot{1, 102})); // open and close as near: the open
        QCOMPARE(magnet_spot(bars, {-0.5, 120}), (ChartSpot{0, 104}));   // the first bar's left edge
        QCOMPARE(magnet_spot(bars, {4.4, 90}), (ChartSpot{4, 102}));
        // Past the bars: as it is.
        QCOMPARE(magnet_spot(bars, {4.5, 103.3}), (ChartSpot{4.5, 103.3}));
        QCOMPARE(magnet_spot(bars, {7, 103.3}), (ChartSpot{7, 103.3}));
        QCOMPARE(magnet_spot(bars, {-0.6, 103.3}), (ChartSpot{-0.6, 103.3}));
        QCOMPARE(magnet_spot({}, {1, 2}), (ChartSpot{1, 2}));
    }

    void a_moment_on_the_one_minute_bars() {
        const auto bars = view_of(minutes(), 1);
        QVERIFY(near(logical_at(bars, at("2026-10-12T09:00:00+09:00"), 1), -0.5)); // a bar's start: its left edge
        QVERIFY(near(logical_at(bars, at("2026-10-12T09:00:30+09:00"), 1), 0));    // its middle
        QVERIFY(near(logical_at(bars, at("2026-10-12T09:01:15+09:00"), 1), 0.75));
        // Minutes without trades: the edge between the bars around them.
        QVERIFY(near(logical_at(bars, at("2026-10-12T09:03:00+09:00"), 1), 2.5));
        QVERIFY(near(logical_at(bars, at("2026-10-12T09:04:59+09:00"), 1), 2.5));
        QVERIFY(near(logical_at(bars, at("2026-10-12T09:06:30+09:00"), 1), 4));
        // After the last bar: a slot a minute.
        QVERIFY(near(logical_at(bars, at("2026-10-12T09:07:30+09:00"), 1), 5));
        QVERIFY(near(logical_at(bars, at("2026-10-12T09:10:00+09:00"), 1), 7.5));
        // Before the first: trading minutes back, the weekend and the nights left out. Friday's
        // 19:59 bar is 61 minutes of trading before Monday's 09:00 (one on Friday, 60 on Monday).
        QVERIFY(near(logical_at(bars, at("2026-10-09T19:59:00+09:00"), 1), -61.5));
        QVERIFY(near(logical_at(bars, at("2026-10-10T12:00:00+09:00"), 1), -60.5)); // Saturday: Monday's 08:00
        QVERIFY(near(logical_at(bars, at("2026-10-12T07:00:00+09:00"), 1), -60.5));
        QVERIFY(!logical_at({}, at("2026-10-12T09:00:00+09:00"), 1));

        // And back, to the second.
        const auto time = [&bars](double logical) { return time_at(bars, logical, 1).value_or(0); };
        QCOMPARE(time(0), at("2026-10-12T09:00:30+09:00"));
        QCOMPARE(time(0.75), at("2026-10-12T09:01:15+09:00"));
        QCOMPARE(time(2.5), at("2026-10-12T09:05:00+09:00")); // an edge between bars: the next one's start
        QCOMPARE(time(5), at("2026-10-12T09:07:30+09:00"));
        QCOMPARE(time(7.5), at("2026-10-12T09:10:00+09:00"));
        QCOMPARE(time(-61.5), at("2026-10-09T19:59:00+09:00"));
        QCOMPARE(time(-60.5), at("2026-10-12T08:00:00+09:00")); // the weekend's: the next trading start
        QVERIFY(!time_at({}, 0, 1));
        QVERIFY(!time_at(bars, std::nan(""), 1));
        // A whole trading day back: Friday's 09:00:30.
        QCOMPARE(time(-720), at("2026-10-09T09:00:30+09:00"));
    }

    void a_moment_keeps_its_place_in_the_other_minute_views() {
        const QList<Bar> one = minutes();
        // Placed in the 1-minute view at 09:06's middle, and on the edge into 09:01.
        const qint64 late = *time_at(view_of(one, 1), 4, 1);
        const qint64 early = *time_at(view_of(one, 1), 0.5, 1);
        QCOMPARE(late, at("2026-10-12T09:06:30+09:00"));
        QCOMPARE(early, at("2026-10-12T09:01:00+09:00"));
        // 5 minutes: 09:00 (09:00–09:02) and 09:05 (09:05, 09:06).
        const auto five = view_of(one, 5);
        QCOMPARE(five.size(), 2);
        QVERIFY(near(logical_at(five, late, 5), 1 - 0.5 + 90.0 / 300));
        QVERIFY(near(logical_at(five, early, 5), -0.5 + 60.0 / 300));
        QVERIFY(near(logical_at(five, at("2026-10-12T09:12:30+09:00"), 5), 2)); // the next slot's middle
        // 60 minutes: one bar, 09:00–10:00.
        QVERIFY(near(logical_at(view_of(one, 60), late, 60), -0.5 + 6.5 / 60));
        // 240 minutes: the 08:00 slot holds them; 14:00 is the middle of the 12:00 one after it.
        const auto four = view_of(one, 240);
        QCOMPARE(four.first().time, at("2026-10-12T08:00:00+09:00"));
        QVERIFY(near(logical_at(four, late, 240), -0.5 + (66.5 / 240)));
        QVERIFY(near(logical_at(four, at("2026-10-12T14:00:00+09:00"), 240), 1));
        // After 20:00 the next slot is the next trading day's.
        QVERIFY(near(logical_at(four, at("2026-10-12T21:00:00+09:00"), 240), 2.5));
        QVERIFY(near(logical_at(four, at("2026-10-13T10:00:00+09:00"), 240), 3));
        // And a place in each view comes back to the same moment.
        for (const int view : kMinuteViews) {
            const auto bars = view_of(one, view);
            for (const double logical : {-30.25, -0.5, 0.0, 0.4, 1.25, 2.5, 3.0, 8.75}) {
                const auto time = time_at(bars, logical, view);
                QVERIFY(time);
                QVERIFY2(near(logical_at(bars, *time, view), logical),
                         qPrintable(QStringLiteral("%1 min at %2").arg(view).arg(logical)));
            }
        }
    }

    void a_moment_on_the_candle_views() {
        // Day candles on Thursday 10-08, Friday 10-09 and Monday 10-12, each from 00:00 KST.
        const QList<ChartBar> days =
            chart_bars({bar("2026-10-08T00:00:00+09:00", 1, 2, 1, 2), bar("2026-10-09T00:00:00+09:00", 2, 3, 2, 3),
                        bar("2026-10-12T00:00:00+09:00", 3, 4, 3, 4)});
        // A day candle spans 08:00–20:00: 14:00 is its middle, the night before its start.
        QVERIFY(near(logical_at(days, at("2026-10-12T14:00:00+09:00"), kDayView), 2));
        QVERIFY(near(logical_at(days, at("2026-10-09T08:00:00+09:00"), kDayView), 0.5));
        QVERIFY(near(logical_at(days, at("2026-10-09T03:00:00+09:00"), kDayView), 0.5));
        QVERIFY(near(logical_at(days, at("2026-10-09T17:00:00+09:00"), kDayView), 1.25));
        QVERIFY(near(logical_at(days, at("2026-10-10T12:00:00+09:00"), kDayView), 1.5)); // Saturday: an edge
        QVERIFY(near(logical_at(days, at("2026-10-13T14:00:00+09:00"), kDayView), 3));   // the next weekday
        QVERIFY(near(logical_at(days, at("2026-10-07T14:00:00+09:00"), kDayView), -1));  // the weekday before
        QCOMPARE(time_at(days, 2, kDayView), std::optional(at("2026-10-12T14:00:00+09:00")));
        QCOMPARE(time_at(days, 0.5, kDayView), std::optional(at("2026-10-09T08:00:00+09:00")));
        QCOMPARE(time_at(days, 3.25, kDayView), std::optional(at("2026-10-13T17:00:00+09:00")));
        // The 1-minute view's 09:06:30 on Monday's candle: 1h6m30s of its 12 hours in.
        QVERIFY(near(logical_at(days, at("2026-10-12T09:06:30+09:00"), kDayView), 1.5 + 3990.0 / 43200));

        // Weeks from Monday: Wednesday 14:00 is the middle of its week (2.5 of 5 trading days).
        const QList<ChartBar> weeks =
            chart_bars({bar("2026-10-05T00:00:00+09:00", 1, 2, 1, 2), bar("2026-10-12T00:00:00+09:00", 2, 3, 2, 3)});
        QVERIFY(near(logical_at(weeks, at("2026-10-14T14:00:00+09:00"), kWeekView), 1));
        QVERIFY(near(logical_at(weeks, at("2026-10-19T08:00:00+09:00"), kWeekView), 1.5));
        // Months: October 2026 has 22 weekdays, ten of them before the 15th.
        const QList<ChartBar> months =
            chart_bars({bar("2026-09-01T00:00:00+09:00", 1, 2, 1, 2), bar("2026-10-01T00:00:00+09:00", 2, 3, 2, 3)});
        QVERIFY(near(logical_at(months, at("2026-10-15T08:00:00+09:00"), kMonthView), 0.5 + 10.0 / 22));
        QVERIFY(near(logical_at(months, at("2026-12-01T08:00:00+09:00"), kMonthView), 2.5)); // a month on
        QVERIFY(near(logical_at(months, at("2026-08-01T00:00:00+09:00"), kMonthView), -1.5));
        // Years: 2026 has 261 weekdays; July 1st follows 129 of them.
        const QList<ChartBar> years =
            chart_bars({bar("2025-01-01T00:00:00+09:00", 1, 2, 1, 2), bar("2026-01-01T00:00:00+09:00", 2, 3, 2, 3)});
        QVERIFY(near(logical_at(years, at("2026-07-01T08:00:00+09:00"), kYearView), 0.5 + 129.0 / 261));
        // And back.
        for (const auto& [bars, view] : {std::pair{days, kDayView}, std::pair{weeks, kWeekView},
                                         std::pair{months, kMonthView}, std::pair{years, kYearView}}) {
            for (const double logical : {-3.6, -0.2, 0.3, 1.0, 1.4, 2.75, 6.1}) {
                const auto time = time_at(bars, logical, view);
                QVERIFY(time);
                QVERIFY2(near(logical_at(bars, *time, view), logical),
                         qPrintable(QStringLiteral("view %1 at %2").arg(view).arg(logical)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TstKrxChartDrawings)
#include "tst_krx_chart_drawings.moc"
