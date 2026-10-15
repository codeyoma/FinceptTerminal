// Unit tests for src/services/krx_quant/ChartLook.h
//
// How the KRX chart looks is worked out here so that its toolbar and page only show it
// (krx-quant #221): the styles and their names, each symbol's settings and their defaults, the
// Heikin-Ashi bars, the baseline's base and the bars the day separators fill in every minute
// view. The expected values are worked out by hand.

#include "services/krx_quant/ChartLook.h"

#include <QTest>

using namespace fincept::krx_quant;

namespace {

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
}

Bar bar(const char* start, double open, double high, double low, double close, double volume = 100) {
    return Bar{kst(start), open, high, low, close, volume};
}

qint64 at(const char* start) {
    return chart_time(kst(start));
}

/// Two days of 1-minute bars: 08:00, 08:01 and 19:59 on the 12th, 08:00 and 08:01 on the 13th.
QList<Bar> two_days() {
    return {bar("2026-10-12T08:00:00+09:00", 100, 101, 99, 100), bar("2026-10-12T08:01:00+09:00", 100, 102, 100, 101),
            bar("2026-10-12T19:59:00+09:00", 101, 101, 98, 99), bar("2026-10-13T08:00:00+09:00", 100, 103, 100, 102),
            bar("2026-10-13T08:01:00+09:00", 102, 104, 101, 103)};
}

} // namespace

class TstKrxChartLook : public QObject {
    Q_OBJECT

  private slots:
    void styles_have_keys_and_names() {
        QCOMPARE(kChartStyles.size(), std::size_t(11));
        for (const ChartStyle style : kChartStyles)
            QCOMPARE(parse_style(style_key(style)), std::optional(style));
        QCOMPARE(style_key(ChartStyle::HeikinAshi), QStringLiteral("heikin_ashi"));
        QCOMPARE(style_title(ChartStyle::PointFigure), QStringLiteral("포인트 앤 피겨"));
        QCOMPARE(style_title(ChartStyle::Hollow), QStringLiteral("투명캔들"));
        QVERIFY(!parse_style("candles"));
        QVERIFY(!parse_style(QString()));
        // The bricks come with krx-quant #225: not drawn yet.
        for (const ChartStyle style :
             {ChartStyle::Renko, ChartStyle::Kagi, ChartStyle::PointFigure, ChartStyle::LineBreak}) {
            QVERIFY(timeless(style));
            QVERIFY(!drawable(style));
        }
        QVERIFY(drawable(ChartStyle::Baseline));
        QVERIFY(!timeless(ChartStyle::HeikinAshi));
    }

    void a_symbol_keeps_its_view_and_style() {
        QCOMPARE(prefs_group("005930"), QStringLiteral("krx_quant/chart/005930/"));
        const ChartPrefs fresh = parse_prefs({}, {}); // opened for the first time
        QCOMPARE(fresh.view, 1);
        QCOMPARE(fresh.style, ChartStyle::Candle);
        const ChartPrefs kept = parse_prefs(5, QStringLiteral("line"));
        QCOMPARE(kept.view, 5);
        QCOMPARE(kept.style, ChartStyle::Line);
        QCOMPARE(parse_prefs(kDayView, QStringLiteral("baseline")).view, kDayView);
        QCOMPARE(parse_prefs(QStringLiteral("60"), {}).view, 60); // as an INI file keeps it
        // Broken values are the defaults.
        QCOMPARE(parse_prefs(7, QStringLiteral("hollow")).view, 1);
        QCOMPARE(parse_prefs(QStringLiteral("abc"), {}).view, 1);
        QCOMPARE(parse_prefs(1, QStringLiteral("nonsense")).style, ChartStyle::Candle);
        QCOMPARE(parse_prefs(1, QStringLiteral("renko")).style, ChartStyle::Candle); // not drawn yet
        QVERIFY(valid_view(240) && valid_view(kYearView) && !valid_view(2) && !valid_view(0));
    }

    void heikin_ashi_bars_follow_each_other() {
        const auto ha = heikin_ashi(chart_bars({bar("2026-10-12T09:00:00+09:00", 100, 110, 95, 105, 10),
                                                bar("2026-10-12T09:01:00+09:00", 105, 108, 100, 101, 20),
                                                bar("2026-10-12T09:02:00+09:00", 101, 102, 90, 92, 30)}));
        QCOMPARE(ha.size(), 3);
        // The first opens halfway between its own open and close.
        QCOMPARE(ha[0].open, 102.5);
        QCOMPARE(ha[0].close, 102.5);
        QCOMPARE(ha[0].high, 110.0);
        QCOMPARE(ha[0].low, 95.0);
        QVERIFY(ha[0].rising);
        // Then halfway between the one before's open and close.
        QCOMPARE(ha[1].open, 102.5);
        QCOMPARE(ha[1].close, 103.5);
        QCOMPARE(ha[1].high, 108.0);
        QCOMPARE(ha[1].low, 100.0);
        QVERIFY(ha[1].rising);
        QCOMPARE(ha[2].open, 103.0);
        QCOMPARE(ha[2].close, 96.25);
        QCOMPARE(ha[2].high, 103.0); // its own open, above the bar's high
        QCOMPARE(ha[2].low, 90.0);
        QVERIFY(!ha[2].rising);
        // The times and volumes stay the bars'.
        QCOMPARE(ha[2].time, at("2026-10-12T09:02:00+09:00"));
        QCOMPARE(ha[1].volume, 20.0);
        QVERIFY(heikin_ashi({}).isEmpty());
    }

    void the_baseline_starts_at_the_day_before_close() {
        const auto minutes = chart_bars(two_days());
        QCOMPARE(baseline_base(minutes, 1, std::nullopt), std::optional(99.0)); // the 12th's last close
        QCOMPARE(baseline_base(chart_bars(group_bars(two_days(), 5)), 5, at("2026-10-13T08:00:00+09:00")),
                 std::optional(99.0)); // nor does the view
        // Only today read: the first bar's open.
        QCOMPARE(baseline_base(minutes.mid(3), 1, std::nullopt), std::optional(100.0));
        QCOMPARE(baseline_base({}, 1, std::nullopt), std::nullopt);
    }

    void a_candle_view_baseline_starts_at_the_first_in_view() {
        const auto days = chart_bars({bar("2026-10-12T00:00:00+09:00", 10, 10, 10, 10),
                                      bar("2026-10-13T00:00:00+09:00", 20, 20, 20, 20),
                                      bar("2026-10-14T00:00:00+09:00", 30, 30, 30, 30)});
        QCOMPARE(baseline_base(days, kDayView, at("2026-10-13T00:00:00+09:00")), std::optional(20.0));
        QCOMPARE(baseline_base(days, kDayView, at("2026-10-12T12:00:00+09:00")), std::optional(20.0));
        QCOMPARE(baseline_base(days, kDayView, std::nullopt), std::optional(10.0));
        QCOMPARE(baseline_base(days, kDayView, at("2026-10-20T00:00:00+09:00")), std::optional(30.0));
    }

    void the_day_separators_fill_each_day_first_bar() {
        const QList<qint64> both{at("2026-10-12T08:00:00+09:00"), at("2026-10-13T08:00:00+09:00")};
        for (const int view : {1, 3, 5, 10, 15, 30, 60, 120, 240})
            QCOMPARE(day_starts(chart_bars(group_bars(two_days(), view)), view), both);
        // A day read from its middle: its first bar read is not its first.
        const auto from_noon = chart_bars(two_days().mid(2));
        QCOMPARE(day_starts(from_noon, 1), QList<qint64>{at("2026-10-13T08:00:00+09:00")});
        // A symbol not on NXT starts at 09:00.
        const auto regular =
            chart_bars({bar("2026-10-12T09:00:00+09:00", 1, 1, 1, 1), bar("2026-10-12T09:01:00+09:00", 1, 1, 1, 1)});
        QCOMPARE(day_starts(regular, 1), QList<qint64>{at("2026-10-12T09:00:00+09:00")});
        // None in a candle view.
        QVERIFY(day_starts(chart_bars(two_days()), kDayView).isEmpty());
        QVERIFY(day_starts({}, 1).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstKrxChartLook)
#include "tst_krx_chart_look.moc"
