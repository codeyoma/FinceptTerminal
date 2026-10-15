// Unit tests for src/services/krx_quant/ChartStudies.h
//
// The KRX chart's indicators are worked out here so that the panel and the page only show them
// (krx-quant #222): each indicator's values on short made-up bars, worked out by hand (the
// supertrend and the parabolic SAR step by step as TradingView's ta.supertrend and ta.sar); the
// front left empty while too few bars come before; the VWAP starting again on a new date; the
// anchored VWAP from its bar; the volume profile's rows; how many bars to read and their cap;
// each symbol's settings as kept, their defaults and broken values; what the page draws and the
// legend.

#include "services/krx_quant/ChartStudies.h"

#include <QTest>

using namespace fincept::krx_quant;

namespace {

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
}

qint64 at(const char* start) {
    return chart_time(kst(start));
}

ChartBar bar(qint64 time, double open, double high, double low, double close, double volume = 100) {
    return ChartBar{time, open, high, low, close, volume, close >= open};
}

/// Bars a minute apart from 09:00 on 12 October 2026, of these highs, lows and closes.
QList<ChartBar> bars(const QList<std::array<double, 3>>& hlc) {
    QList<ChartBar> out;
    qint64 time = at("2026-10-12T09:00:00+09:00");
    for (const auto& [high, low, close] : hlc) {
        out << bar(time, close, high, low, close);
        time += 60;
    }
    return out;
}

using V = std::optional<double>;

/// ``got`` is ``want``, the values within 1e-9.
bool same(const Values& got, const QList<V>& want) {
    if (got.size() != want.size()) {
        qWarning() << "sizes" << got.size() << want.size();
        return false;
    }
    for (qsizetype i = 0; i < got.size(); ++i) {
        if (got[i].has_value() != want[i].has_value() || (got[i] && std::abs(*got[i] - *want[i]) > 1e-9)) {
            qWarning() << "at" << i << (got[i] ? *got[i] : -1e300) << (want[i] ? *want[i] : -1e300);
            return false;
        }
    }
    return true;
}

const V n = std::nullopt;

const LegendRow& row_of(const StudyDrawing& d, Study study) {
    for (const auto& row : d.legend)
        if (row.study == study)
            return row;
    static const LegendRow none;
    return none;
}

} // namespace

class TstKrxChartStudies : public QObject {
    Q_OBJECT

  private slots:
    void the_studies_have_keys_titles_and_defaults() {
        QCOMPARE(kUpperStudies.size(), std::size_t(12));
        for (const Study study : kStudies)
            QCOMPARE(parse_study(study_key(study)), std::optional(study));
        QVERIFY(!parse_study("macd")); // a lower study comes with krx-quant #223
        QCOMPARE(study_title(Study::VolumeProfile), QStringLiteral("매물대분석"));
        QCOMPARE(study_title(Study::AnchoredVwap), QStringLiteral("고정된 VWAP"));
        QVERIFY(!study_spec(Study::Volume).upper);
        QVERIFY(study_spec(Study::Vwap).minute_only);
        // The spec's table (krx-quant #220).
        QCOMPARE(default_settings(Study::MovingAverage).inputs, (QList<double>{5, 20, 60, 120}));
        QCOMPARE(default_settings(Study::MovingAverage).source, std::optional(Source::Close));
        QCOMPARE(default_settings(Study::Ichimoku).inputs, (QList<double>{9, 26, 52, 26}));
        QCOMPARE(default_settings(Study::Bollinger).inputs, (QList<double>{20, 2}));
        QCOMPARE(default_settings(Study::Supertrend).inputs, (QList<double>{10, 3}));
        QCOMPARE(default_settings(Study::VolumeProfile).inputs, (QList<double>{24}));
        QCOMPARE(default_settings(Study::Envelope).inputs, (QList<double>{20, 10}));
        QCOMPARE(default_settings(Study::Fractal).inputs, (QList<double>{2}));
        QCOMPARE(default_settings(Study::ExponentialAverage).inputs, (QList<double>{5, 20, 60, 120}));
        QCOMPARE(default_settings(Study::ParabolicSar).inputs, (QList<double>{0.02, 0.02, 0.2}));
        QCOMPARE(default_settings(Study::PriceChannel).inputs, (QList<double>{20}));
        QCOMPARE(default_settings(Study::Vwap).source, std::optional(Source::HLC3));
        QCOMPARE(default_settings(Study::AnchoredVwap).source, std::optional(Source::HLC3));
        QCOMPARE(settings_text(default_settings(Study::MovingAverage)), QStringLiteral("5 20 60 120"));
        QCOMPARE(settings_text(default_settings(Study::Envelope)), QStringLiteral("20 10%"));
        QCOMPARE(settings_text(default_settings(Study::ParabolicSar)), QStringLiteral("0.02 0.02 0.2"));
        QCOMPARE(settings_text(default_settings(Study::Vwap)), QString());
        StudySettings high = default_settings(Study::MovingAverage);
        high.source = Source::High;
        QCOMPARE(settings_text(high), QStringLiteral("5 20 60 120 고가"));
        QCOMPARE(source_title(Source::HLC3), QStringLiteral("(고가+저가+종가)/3"));
        QCOMPARE(price_of(bar(0, 1, 9, 3, 6), Source::HLC3), 6.0);
        QCOMPARE(price_of(bar(0, 1, 9, 3, 6), Source::HL2), 6.0);
    }

    void moving_averages_leave_the_front_empty() {
        QVERIFY(same(moving_average({1, 2, 3, 4, 5}, 3), {n, n, 2, 3, 4}));
        QVERIFY(same(moving_average({1, 2, 3}, 1), {1, 2, 3}));
        QVERIFY(same(moving_average({1, 2, 3}, 4), {n, n, n}));
        // Seeded by the simple average of the first period, then 2 / (period + 1).
        QVERIFY(same(exponential_average({1, 2, 3, 4, 5}, 3), {n, n, 2, 3, 4}));
        QVERIFY(same(exponential_average({2, 4, 6, 8}, 2), {n, 3, 5, 7}));
        QVERIFY(same(exponential_average({2, 4}, 3), {n, n}));
    }

    void bands_and_channels() {
        // Population standard deviation: 2, 4, 4, 4, 5, 5, 7, 9 has mean 5 and deviation 2.
        const Band wide = bollinger_band({2, 4, 4, 4, 5, 5, 7, 9}, 8, 2);
        QVERIFY(same(wide.middle, {n, n, n, n, n, n, n, 5}));
        QVERIFY(same(wide.upper, {n, n, n, n, n, n, n, 9}));
        QVERIFY(same(wide.lower, {n, n, n, n, n, n, n, 1}));
        const Band two = bollinger_band({1, 3, 1, 3}, 2, 2);
        QVERIFY(same(two.middle, {n, 2, 2, 2}));
        QVERIFY(same(two.upper, {n, 4, 4, 4}));
        QVERIFY(same(two.lower, {n, 0, 0, 0}));

        const Band envelope = envelope_band({10, 20, 30}, 2, 10);
        QVERIFY(same(envelope.middle, {n, 15, 25}));
        QVERIFY(same(envelope.upper, {n, 16.5, 27.5}));
        QVERIFY(same(envelope.lower, {n, 13.5, 22.5}));

        const Band channel = price_channel(bars({{5, 1, 3}, {7, 2, 3}, {6, 0, 3}, {8, 3, 3}}), 2);
        QVERIFY(same(channel.upper, {n, 7, 7, 8}));
        QVERIFY(same(channel.lower, {n, 1, 0, 0}));
        QVERIFY(same(channel.middle, {n, n, n, n}));
    }

    void average_true_range_is_wilders() {
        // True ranges 2, 2 (|11 − 9|), 5 (|15 − 10|): 2, then (2 × 1 + 5) / 2.
        const auto atr = average_true_range(bars({{10, 8, 9}, {11, 9, 10}, {15, 13, 14}}), 2);
        QVERIFY(same(atr, {n, 2, 3.5}));
        QVERIFY(same(average_true_range(bars({{10, 8, 9}}), 2), {n}));
    }

    void ichimoku_draws_its_leading_spans_into_the_future() {
        // Periods 2, 3, 4 and a displacement of 2 on five bars.
        const auto five = bars({{10, 6, 8}, {12, 8, 11}, {14, 10, 12}, {13, 9, 10}, {16, 11, 15}});
        const Ichimoku c = ichimoku_cloud(five, 2, 3, 4, 2);
        QVERIFY(same(c.conversion, {n, 9, 11, 11.5, 12.5}));
        QVERIFY(same(c.base, {n, n, 10, 11, 12.5}));
        QVERIFY(same(c.lagging, {12, 10, 15, n, n})); // each close two bars back
        // Two values past the last bar.
        QVERIFY(same(c.lead_a, {n, n, n, n, 10.5, 11.25, 12.5}));
        QVERIFY(same(c.lead_b, {n, n, n, n, n, 10, 12}));
    }

    void supertrend_turns_with_the_close() {
        // ATR 1 (each true range) and a multiplier of 1, step by step:
        // 1st: bands 12 / 8, starts down at 12. 2nd: bands 15 / 9, the upper stays 12 (the close
        // was inside), the close 12.5 crosses it: up at 9. 3rd: lower 11, still up. 4th: the lower
        // stays 11, the close 9.5 falls under it: down at the new upper 14.5.
        const auto four = bars({{11, 9, 10}, {13, 11, 12.5}, {14, 12, 13}, {12, 9, 9.5}});
        const Supertrend t = supertrend_lines(four, 1, 1);
        QVERIFY(same(t.down, {12, n, n, 14.5}));
        QVERIFY(same(t.up, {n, 9, 11, n}));
        // No values before the ATR has its period.
        const Supertrend late = supertrend_lines(four, 3, 1);
        QVERIFY(!late.up[0] && !late.down[0] && !late.up[1] && !late.down[1]);
        QVERIFY(late.up[2] || late.down[2]);
    }

    void fractals_need_the_bars_on_both_sides() {
        const auto seven = bars({{1, 0.5, 1}, {2, 1, 1}, {5, 4, 4}, {3, 1, 2}, {2, 0.2, 1}, {4, 3, 3}, {1, 0.9, 1}});
        const auto found = williams_fractals(seven, 2);
        QCOMPARE(found.size(), 2);
        QCOMPARE(found[0].index, 2);
        QVERIFY(found[0].up);
        QCOMPARE(found[1].index, 4);
        QVERIFY(!found[1].up);
        // A peak at the last bars is not one yet; equal highs are not one.
        QVERIFY(williams_fractals(bars({{1, 1, 1}, {2, 1, 1}, {3, 1, 1}}), 2).isEmpty());
        QVERIFY(
            williams_fractals(bars({{1, 0, 1}, {1, 0, 1}, {3, 0, 1}, {3, 0, 1}, {1, 0, 1}, {1, 0, 1}}), 2).isEmpty());
    }

    void parabolic_sar_follows_and_flips() {
        // Up from the second bar (its close is higher), starting at the first low (9); it rises
        // with the new highs (0.02, 0.04, 0.06), held under the two lows before, until the fifth
        // bar's low (8) breaks it: down, from the highest high (13), held over the highs before.
        const auto six =
            bars({{10, 9, 9.5}, {11, 10, 10.5}, {12, 11, 11.5}, {13, 12, 12.5}, {12.5, 8, 8.5}, {9, 7, 7.5}});
        QVERIFY(same(parabolic_sar(six, 0.02, 0.02, 0.2), {n, 9, 9, 9.12, 13, 13}));
    }

    void vwap_starts_again_each_day() {
        QList<ChartBar> two_days{
            bar(at("2026-10-12T09:00:00+09:00"), 10, 12, 8, 10, 100),  // (12 + 8 + 10) / 3 = 10
            bar(at("2026-10-12T09:01:00+09:00"), 12, 13, 11, 12, 300), // 12
            bar(at("2026-10-13T08:00:00+09:00"), 20, 21, 19, 20, 50),  // a new date: 20
            bar(at("2026-10-13T08:01:00+09:00"), 22, 23, 21, 22, 50),  // 22
        };
        QVERIFY(same(session_vwap(two_days, Source::HLC3), {10, 11.5, 20, 21}));
        QVERIFY(same(session_vwap(two_days, Source::High), {12, 12.75, 21, 22}));
        // From its bar on: (12 × 300 + 20 × 50) / 350, then with 22 × 50 too.
        QVERIFY(same(anchored_vwap(two_days, Source::HLC3, 1), {n, 12, 4600.0 / 350, 5700.0 / 400}));
        QVERIFY(same(anchored_vwap(two_days, Source::HLC3, 0), {10, 11.5, 5600.0 / 450, 6700.0 / 500}));
        // No value until the day has volume.
        QList<ChartBar> quiet{bar(at("2026-10-12T09:00:00+09:00"), 10, 10, 10, 10, 0),
                              bar(at("2026-10-12T09:01:00+09:00"), 11, 11, 11, 11, 10)};
        QVERIFY(same(session_vwap(quiet, Source::Close), {n, 11}));
    }

    void the_anchor_is_the_bar_that_holds_its_moment() {
        const QList<ChartBar> minutes{bar(at("2026-10-12T09:00:00+09:00"), 1, 1, 1, 1),
                                      bar(at("2026-10-12T09:01:00+09:00"), 1, 1, 1, 1),
                                      bar(at("2026-10-12T09:03:00+09:00"), 1, 1, 1, 1)};
        QCOMPARE(anchor_index(minutes, at("2026-10-12T09:01:00+09:00"), 1), std::optional<qsizetype>(1));
        QCOMPARE(anchor_index(minutes, at("2026-10-12T09:01:30+09:00"), 1), std::optional<qsizetype>(1));
        QCOMPARE(anchor_index(minutes, at("2026-10-12T09:02:00+09:00"), 1), std::optional<qsizetype>(2)); // no trade
        QVERIFY(!anchor_index(minutes, at("2026-10-12T08:59:00+09:00"), 1)); // before the bars read
        QVERIFY(!anchor_index(minutes, at("2026-10-12T09:05:00+09:00"), 1));
        // A 5-minute view: the slot on the clock.
        const QList<ChartBar> fives{bar(at("2026-10-12T09:00:00+09:00"), 1, 1, 1, 1),
                                    bar(at("2026-10-12T09:05:00+09:00"), 1, 1, 1, 1)};
        QCOMPARE(anchor_index(fives, at("2026-10-12T09:07:00+09:00"), 5), std::optional<qsizetype>(1));
        // Day candles start at 00:00; a week's on its Monday (12 October 2026).
        const QList<ChartBar> days{bar(at("2026-10-12T00:00:00+09:00"), 1, 1, 1, 1),
                                   bar(at("2026-10-13T00:00:00+09:00"), 1, 1, 1, 1)};
        QCOMPARE(anchor_index(days, at("2026-10-13T09:30:00+09:00"), kDayView), std::optional<qsizetype>(1));
        const QList<ChartBar> weeks{bar(at("2026-10-12T00:00:00+09:00"), 1, 1, 1, 1)};
        QCOMPARE(anchor_index(weeks, at("2026-10-14T10:00:00+09:00"), kWeekView), std::optional<qsizetype>(0));
    }

    void volume_profile_spreads_each_bar_over_its_rows() {
        // Rows of 2 from 0 to 8: the first bar's 40 over 0–4 (20 and 20), the second (a single
        // price, 2) all in the row from 2, the third's 20 in the top row. The last is out of view.
        const QList<ChartBar> three{bar(100, 1, 4, 0, 3, 40), bar(160, 2, 2, 2, 2, 10), bar(220, 7, 8, 6, 8, 20),
                                    bar(280, 50, 60, 40, 50, 1000)};
        const VolumeProfile p = volume_profile(three, 100, 220, 4);
        QCOMPARE(p.low, 0.0);
        QCOMPARE(p.high, 8.0);
        QCOMPARE(p.volumes, (QList<double>{20, 30, 0, 20}));
        QVERIFY(volume_profile(three, 1000, 2000, 4).volumes.isEmpty());
        // Every bar at one price: all in one row.
        QCOMPARE(volume_profile({bar(100, 5, 5, 5, 5, 7)}, 0, 200, 3).volumes, (QList<double>{7, 0, 0}));
    }

    void the_times_after_the_last_bar() {
        const QList<ChartBar> five{bar(at("2026-10-12T10:00:00+09:00"), 1, 1, 1, 1)};
        QCOMPARE(future_times(five, 5, 2),
                 (QList<qint64>{at("2026-10-12T10:05:00+09:00"), at("2026-10-12T10:10:00+09:00")}));
        const QList<ChartBar> friday{bar(at("2026-10-16T00:00:00+09:00"), 1, 1, 1, 1)};
        QCOMPARE(future_times(friday, kDayView, 2),
                 (QList<qint64>{at("2026-10-19T00:00:00+09:00"), at("2026-10-20T00:00:00+09:00")}));
        const QList<ChartBar> december{bar(at("2026-12-01T00:00:00+09:00"), 1, 1, 1, 1)};
        QCOMPARE(future_times(december, kMonthView, 2),
                 (QList<qint64>{at("2027-01-01T00:00:00+09:00"), at("2027-02-01T00:00:00+09:00")}));
        QCOMPARE(future_times(december, kYearView, 1), (QList<qint64>{at("2027-01-01T00:00:00+09:00")}));
        QCOMPARE(future_times(december, kWeekView, 1), (QList<qint64>{at("2026-12-08T00:00:00+09:00")}));
        QVERIFY(future_times({}, 1, 3).isEmpty());
    }

    void bars_to_read_for_the_indicators() {
        StudySet set = default_studies(); // the moving averages up to 120
        QCOMPARE(warmup(set.of(Study::MovingAverage)), 119);
        QCOMPARE(history_bars(set, 1), kFitBars + 119);
        QCOMPARE(history_bars(set, kDayView), kFitBars + 119);
        StudySettings ichimoku = default_settings(Study::Ichimoku);
        QCOMPARE(warmup(ichimoku), 51 + 26); // the leading span B at the left end
        set.turn(Study::MovingAverage, false);
        QCOMPARE(history_bars(set, 1), 0); // the volume needs none
        set.turn(Study::Ichimoku, true);
        QCOMPARE(history_bars(set, 1), kFitBars + 77);
        set.of(Study::Ichimoku).hidden = true; // hidden is still on
        QCOMPARE(history_bars(set, 1), kFitBars + 77);
        set.turn(Study::Ichimoku, false);
        set.turn(Study::MovingAverage, true);
        set.of(Study::MovingAverage).inputs = {5, 3000};
        QCOMPARE(history_bars(set, 1), kMaxHistoryBars); // capped
        set.turn(Study::MovingAverage, false);
        set.turn(Study::Vwap, true);
        QCOMPARE(history_bars(set, 1), 0);

        const QDateTime first = kst("2026-10-12T09:00:00+09:00");
        const auto read = history_read(first, 100, 179, 1);
        QVERIFY(read);
        QCOMPARE(read->before, first);
        QCOMPARE(read->limit, kOlderBars);
        QCOMPARE(read->finals_before, first.addSecs(60));
        QCOMPARE(history_read(first, 50, 179, 15)->limit, kMaxOlderBars); // 100 of 15 minutes, at most 1000
        QVERIFY(!history_read(first, 179, 179, 1));
        QVERIFY(!history_read(QDateTime(), 0, 179, 1));                          // the chart is read first
        QVERIFY(!history_read(first, kMaxHistoryBars, kMaxHistoryBars + 10, 1)); // the cap
        QVERIFY(history_read(first, 500, 179, 1, first.addSecs(-3600)));         // back to the anchor
        QVERIFY(!history_read(first, 500, 179, 1, first));
        QCOMPARE(history_candles(first, 10, 179), std::optional(first));
        QVERIFY(!history_candles(first, 400, 179));
        QVERIFY(!history_candles(first, kMaxHistoryBars, kMaxHistoryBars + 1, first.addDays(-9999)));
    }

    void a_symbol_keeps_its_indicators() {
        // Opened for the first time: the moving averages and the volume.
        const StudySet fresh = parse_studies({});
        QCOMPARE(fresh, default_studies());
        QVERIFY(fresh.on(Study::MovingAverage) && fresh.on(Study::Volume));
        QCOMPARE(fresh.studies.size(), qsizetype(kStudies.size()));
        for (const Study study : kStudies)
            QCOMPARE(fresh.on(study), study == Study::MovingAverage || study == Study::Volume);
        QCOMPARE(fresh.of(Study::MovingAverage).inputs, (QList<double>{5, 20, 60, 120}));
        QCOMPARE(parse_studies(QStringLiteral("not json")), default_studies());
        QCOMPARE(parse_studies(QStringLiteral("{\"study\":\"ma\"}")), default_studies());

        // Kept and read back the same, in the order turned on.
        StudySet set = default_studies();
        set.turn(Study::Bollinger, true);
        set.turn(Study::Ichimoku, true);
        set.turn(Study::MovingAverage, false);
        QCOMPARE(set.studies[0].study, Study::Volume);
        QCOMPARE(set.studies[1].study, Study::Bollinger);
        QCOMPARE(set.studies[2].study, Study::Ichimoku);
        QCOMPARE(set.studies[3].study, Study::MovingAverage); // first among those off
        set.of(Study::Bollinger).inputs = {30, 2.5};
        set.of(Study::Bollinger).source = Source::HLC3;
        set.of(Study::Bollinger).lines[1] = StudyLine{QStringLiteral("상단"), QStringLiteral("#123456"), 3};
        set.of(Study::Ichimoku).hidden = true;
        QVERIFY(add_period(set.of(Study::ExponentialAverage)));
        const StudySet back = parse_studies(studies_value(set));
        QCOMPARE(back, set);
        QCOMPARE(back.of(Study::Bollinger).lines[1].color, QStringLiteral("#123456"));

        // Broken values are their defaults; an unknown study is dropped, a missing one fresh.
        const StudySet broken = parse_studies(QStringLiteral(
            R"([{"study":"bollinger","on":true,"source":"vwap","inputs":[0,50],"lines":[{"color":"red","width":9}]},)"
            R"({"study":"ma","on":true,"inputs":[5,-1,20.5,60]},{"study":"macd","on":true},)"
            R"({"study":"bollinger","on":false}])"));
        const StudySettings& b = broken.of(Study::Bollinger);
        QVERIFY(b.on);
        QCOMPARE(b.inputs, (QList<double>{20, 2}));
        QCOMPARE(b.source, std::optional(Source::Close));
        QCOMPARE(b.lines[0].color, default_settings(Study::Bollinger).lines[0].color);
        QCOMPARE(b.lines[0].width, 1);
        QCOMPARE(broken.of(Study::MovingAverage).inputs, (QList<double>{5, 60}));
        QCOMPARE(broken.of(Study::MovingAverage).lines.size(), 2);
        QVERIFY(broken.on(Study::Volume)); // not kept: as for a new symbol
        QVERIFY(!broken.on(Study::Ichimoku));
        QCOMPARE(broken.studies[0].study, Study::Bollinger);
        QCOMPARE(broken.studies[1].study, Study::MovingAverage);
        QCOMPARE(broken.studies.size(), qsizetype(kStudies.size()));
        QCOMPARE(parse_studies(QStringLiteral(R"([{"study":"ema","on":true,"inputs":[]}])"))
                     .of(Study::ExponentialAverage)
                     .inputs,
                 (QList<double>{5, 20, 60, 120}));
    }

    void moving_averages_add_and_drop_periods() {
        StudySettings ma = default_settings(Study::MovingAverage);
        QVERIFY(add_period(ma));
        QCOMPARE(ma.inputs.last(), 10.0); // the first of 5, 10, 20… it has not
        QCOMPARE(ma.lines.size(), 5);
        QCOMPARE(ma.lines.last().color, QStringLiteral("#A3E635")); // a colour not used yet
        while (add_period(ma)) {
        }
        QCOMPARE(ma.inputs.size(), kMaxPeriods);
        QVERIFY(!remove_period(ma, 0)); // the first stays
        QVERIFY(remove_period(ma, 1));
        QCOMPARE(ma.inputs.mid(0, 3), (QList<double>{5, 60, 120}));
        QCOMPARE(ma.lines.size(), ma.inputs.size());
        QVERIFY(!add_period(ma = default_settings(Study::Bollinger)));
    }

    void the_page_draws_what_is_on() {
        QList<ChartBar> minutes;
        for (int i = 0; i < 130; ++i)
            minutes << bar(at("2026-10-12T09:00:00+09:00") + i * 60, 100 + i, 101 + i, 99 + i, 100 + i, 10);
        StudySet set = default_studies();
        StudyDrawing d = draw_studies(minutes, set, 1, std::nullopt, false);
        QCOMPARE(d.series.size(), 4);
        QCOMPARE(d.series[0].id, QStringLiteral("ma.0"));
        QCOMPARE(d.series[3].points.first().first, minutes[119].time); // MA 120 from its 120th bar
        QCOMPARE(d.series[3].points.size(), 11);
        QCOMPARE(d.legend.size(), 2);
        QCOMPARE(d.legend[0].title, QStringLiteral("이동평균선"));
        QCOMPARE(d.legend[0].settings, QStringLiteral("5 20 60 120"));
        QCOMPARE(d.legend[0].values, (QStringList{"ma.0", "ma.1", "ma.2", "ma.3"}));
        QCOMPARE(d.legend[1].pane, 1);
        QCOMPARE(d.legend[1].values, QStringList{"volume"});
        QVERIFY(d.volume && !d.profile && !d.anchor);

        // Hidden: its legend row stays, dimmed, without lines.
        set.of(Study::MovingAverage).hidden = true;
        d = draw_studies(minutes, set, 1, std::nullopt, false);
        QVERIFY(d.series.isEmpty());
        QVERIFY(d.legend[0].hidden);

        // The VWAP is the minute views': in a day view it is neither drawn nor in the legend.
        set = default_studies();
        set.turn(Study::MovingAverage, false);
        set.turn(Study::Vwap, true);
        QCOMPARE(draw_studies(minutes, set, 1, std::nullopt, false).series.size(), 1);
        d = draw_studies(minutes, set, kDayView, std::nullopt, false);
        QVERIFY(d.series.isEmpty());
        QCOMPARE(d.legend.size(), 1); // the volume's
        QVERIFY(set.on(Study::Vwap)); // still on

        // Ichimoku's leading spans run 26 bars past the last; the lagging span ends 26 back.
        set = default_studies();
        set.turn(Study::MovingAverage, false);
        set.turn(Study::Ichimoku, true);
        d = draw_studies(minutes, set, 1, std::nullopt, false);
        QCOMPARE(d.series.size(), 5);
        const auto& lead = d.series[3];
        QCOMPARE(lead.id, QStringLiteral("ichimoku.lead_a"));
        QCOMPARE(lead.points.last().first, minutes.last().time + 26 * 60);
        QCOMPARE(lead.live, 26);
        QCOMPARE(d.series[2].points.last().first, minutes[129 - 26].time);
        QCOMPARE(d.series[2].live, -26);
        QCOMPARE(d.fills.size(), 1);

        // The anchored VWAP: waiting for a click, then from its bar.
        set = default_studies();
        set.turn(Study::AnchoredVwap, true);
        d = draw_studies(minutes, set, 1, std::nullopt, true);
        QVERIFY(d.picking);
        QVERIFY(!d.anchor);
        QCOMPARE(row_of(d, Study::AnchoredVwap).note, QStringLiteral("차트를 눌러 기준 봉을 고르세요"));
        d = draw_studies(minutes, set, 1, minutes[10].time + 30, false);
        QCOMPARE(d.anchor, std::optional(minutes[10].time));
        QCOMPARE(d.series.last().id, QStringLiteral("anchored_vwap"));
        QCOMPARE(d.series.last().points.first().first, minutes[10].time);
        QCOMPARE(row_of(d, Study::AnchoredVwap).settings, QStringLiteral("10-12 09:10"));
        d = draw_studies(minutes, set, 1, minutes[0].time - 3600, false);
        QCOMPARE(row_of(d, Study::AnchoredVwap).note, QStringLiteral("기준 봉을 읽지 않았다"));

        // The fractals are marks, the volume profile a flag; the volume can be turned off.
        set = default_studies();
        set.turn(Study::Fractal, true);
        set.turn(Study::VolumeProfile, true);
        set.turn(Study::Volume, false);
        d = draw_studies(bars({{1, 0.5, 1}, {2, 1, 1}, {5, 4, 4}, {3, 1, 2}, {2, 0.2, 1}}), set, 1, std::nullopt,
                         false);
        QCOMPARE(d.marks.size(), 1);
        QVERIFY(d.marks[0].above);
        QVERIFY(d.profile);
        QCOMPARE(d.profile_rows, 24);
        QVERIFY(!d.volume);
        QVERIFY(std::none_of(d.legend.begin(), d.legend.end(), [](const LegendRow& r) { return r.pane == 1; }));
    }

    void colours_as_css() {
        QCOMPARE(css_color(QStringLiteral("#FF8000"), 0.5), QStringLiteral("rgba(255,128,0,0.5)"));
        QCOMPARE(css_color(QStringLiteral("nope"), 1), QStringLiteral("rgba(128,128,128,1)"));
    }
};

QTEST_MAIN(TstKrxChartStudies)
#include "tst_krx_chart_studies.moc"
