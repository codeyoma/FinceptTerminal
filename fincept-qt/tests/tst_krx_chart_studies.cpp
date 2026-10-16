// Unit tests for src/services/krx_quant/ChartStudies.h
//
// The KRX chart's indicators are worked out here so that the panel and the page only show them
// (krx-quant #222, #223): each indicator's values on short made-up bars, worked out by hand (the
// supertrend and the parabolic SAR step by step as TradingView's ta.supertrend and ta.sar); the
// front left empty while too few bars come before; the VWAP starting again on a new date; the
// anchored VWAP from its bar; the volume profile's rows; the AD line and the OBV the same however
// many bars come before, and the recursive ones barely moved by more bars before their warm-up;
// how many bars to read and their cap; each symbol's settings and pane heights as kept, their
// defaults and broken values; what the page draws (the lower panes in the order turned on) and
// the legend.

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

/// Likewise, of these highs, lows, closes and volumes.
QList<ChartBar> traded(const QList<std::array<double, 4>>& hlcv) {
    QList<ChartBar> out;
    qint64 time = at("2026-10-12T09:00:00+09:00");
    for (const auto& [high, low, close, volume] : hlcv) {
        out << bar(time, close, high, low, close, volume);
        time += 60;
    }
    return out;
}

/// ``count`` made-up minute bars from 09:00 on 12 October 2026 that wander (a fixed sequence).
QList<ChartBar> wandering(int count) {
    QList<ChartBar> out;
    quint32 seed = 12345;
    const auto next = [&seed]() { // 0–1
        seed = seed * 1103515245u + 12345u;
        return ((seed >> 8) & 0xffff) / 65535.0;
    };
    double close = 70000;
    qint64 time = at("2026-10-12T09:00:00+09:00");
    for (int i = 0; i < count; ++i) {
        const double open = close;
        close = std::round(open + (next() - 0.5) * 200);
        const double high = std::max(open, close) + std::round(next() * 60);
        const double low = std::min(open, close) - std::round(next() * 60);
        out << bar(time, open, high, low, close, std::round(1000 + next() * 9000));
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

/// A made-up day of daily supply data (krx-quant #226): each field ``base`` + its index × 1000 (the
/// ratio ``base`` / 1000 %), the net buys negative on odd fields' days when ``down``.
SupplyDay supply_day(const char* date, double base, bool down = false) {
    SupplyDay day;
    day.date = QDate::fromString(QString::fromLatin1(date), Qt::ISODate);
    for (std::size_t i = 0; i < kSupplyFields; ++i)
        day.values[i] = base + double(i) * 1000;
    day.values[std::size_t(SupplyField::ForeignRatio)] = base / 1000;
    if (down)
        day.values[std::size_t(SupplyField::ForeignNet)] = -base;
    return day;
}

/// One bar at each of these moments (KST, ISO 8601), its close its index.
QList<ChartBar> bars_at(const QList<const char*>& starts) {
    QList<ChartBar> out;
    for (const char* start : starts)
        out << bar(at(start), 1, 1, 1, double(out.size()));
    return out;
}

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
        QCOMPARE(kLowerStudies.size(), std::size_t(25)); // krx-quant #223
        for (const Study study : kStudies)
            QCOMPARE(parse_study(study_key(study)), std::optional(study));
        for (std::size_t i = 0; i < kStudies.size(); ++i) // in the enum's order (StudySet::of)
            QCOMPARE(static_cast<std::size_t>(kStudies[i]), i);
        for (const Study study : kLowerStudies)
            QVERIFY(!study_spec(study).upper);
        QVERIFY(!parse_study("nope"));
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

        // The lower indicators' defaults and guide lines (krx-quant #220's table).
        const auto inputs = [](Study study) { return default_settings(study).inputs; };
        const auto guides = [](Study study) { return study_spec(study).guides; };
        QCOMPARE(study_key(Study::Volume), QStringLiteral("volume"));
        QCOMPARE(inputs(Study::MassIndex), (QList<double>{9, 25}));
        QCOMPARE(guides(Study::MassIndex), (QList<double>{27, 26.5}));
        QCOMPARE(inputs(Study::Momentum), QList<double>{10});
        QCOMPARE(default_settings(Study::Momentum).source, std::optional(Source::Close));
        QCOMPARE(inputs(Study::VolumeOscillator), (QList<double>{5, 10}));
        QCOMPARE(inputs(Study::PercentB), (QList<double>{20, 2}));
        QCOMPARE(guides(Study::PercentB), (QList<double>{1, 0}));
        QCOMPARE(inputs(Study::BandWidth), (QList<double>{20, 2}));
        QCOMPARE(inputs(Study::Stochastic), (QList<double>{14, 3, 3}));
        QCOMPARE(guides(Study::Stochastic), (QList<double>{80, 20}));
        QCOMPARE(inputs(Study::StochasticRsi), (QList<double>{14, 14, 3, 3}));
        QCOMPARE(guides(Study::StochasticRsi), (QList<double>{80, 20}));
        QCOMPARE(inputs(Study::WilliamsR), QList<double>{14});
        QCOMPARE(guides(Study::WilliamsR), (QList<double>{-20, -80}));
        QCOMPARE(inputs(Study::Disparity), QList<double>{20});
        QCOMPARE(guides(Study::Disparity), QList<double>{100});
        QCOMPARE(inputs(Study::IntradayIntensity), QList<double>{21});
        QCOMPARE(guides(Study::IntradayIntensity), QList<double>{0});
        QCOMPARE(inputs(Study::ChaikinOscillator), (QList<double>{3, 10}));
        QCOMPARE(guides(Study::ChaikinOscillator), QList<double>{0});
        QCOMPARE(inputs(Study::Trix), (QList<double>{18, 9}));
        QCOMPARE(guides(Study::Trix), QList<double>{0});
        QCOMPARE(inputs(Study::PriceOscillator), (QList<double>{10, 21, 0}));
        QCOMPARE(guides(Study::PriceOscillator), QList<double>{0});
        QCOMPARE(settings_text(default_settings(Study::PriceOscillator)), QStringLiteral("10 21 단순"));
        QVERIFY(inputs(Study::AdLine).isEmpty() && guides(Study::AdLine).isEmpty());
        QCOMPARE(inputs(Study::Adx), QList<double>{14});
        QCOMPARE(inputs(Study::Atr), QList<double>{14});
        QCOMPARE(inputs(Study::Cci), QList<double>{20});
        QCOMPARE(guides(Study::Cci), (QList<double>{100, -100}));
        QCOMPARE(inputs(Study::Dmi), QList<double>{14});
        QCOMPARE(default_settings(Study::Dmi).lines.size(), 3); // +DI, −DI, ADX
        QCOMPARE(inputs(Study::Macd), (QList<double>{12, 26, 9}));
        QCOMPARE(guides(Study::Macd), QList<double>{0});
        QCOMPARE(settings_text(default_settings(Study::Macd)), QStringLiteral("12 26 9"));
        QCOMPARE(inputs(Study::Mfi), QList<double>{14});
        QCOMPARE(guides(Study::Mfi), (QList<double>{80, 20}));
        QVERIFY(inputs(Study::Obv).isEmpty() && guides(Study::Obv).isEmpty());
        QCOMPARE(inputs(Study::Roc), QList<double>{9});
        QCOMPARE(guides(Study::Roc), QList<double>{0});
        QCOMPARE(inputs(Study::Rsi), QList<double>{14});
        QCOMPARE(guides(Study::Rsi), (QList<double>{70, 30}));
        QCOMPARE(inputs(Study::Rmi), (QList<double>{14, 5}));
        QCOMPARE(guides(Study::Rmi), (QList<double>{70, 30}));
        QCOMPARE(settings_text(default_settings(Study::Stochastic)), QStringLiteral("14 3 3"));
        QVERIFY(study_spec(Study::Obv).volume_scale && study_spec(Study::Volume).volume_scale);
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
        // Nothing before the first day to read: it is whole.
        QVERIFY(same(session_vwap(two_days, Source::HLC3, true), {10, 11.5, 20, 21}));
        QVERIFY(same(session_vwap(two_days, Source::High, true), {12, 12.75, 21, 22}));
        // Otherwise the first day held may be read from its middle: empty, the later days right.
        QVERIFY(same(session_vwap(two_days, Source::HLC3), {n, n, 20, 21}));
        // From its bar on: (12 × 300 + 20 × 50) / 350, then with 22 × 50 too.
        QVERIFY(same(anchored_vwap(two_days, Source::HLC3, 1), {n, 12, 4600.0 / 350, 5700.0 / 400}));
        QVERIFY(same(anchored_vwap(two_days, Source::HLC3, 0), {10, 11.5, 5600.0 / 450, 6700.0 / 500}));
        // No value until the day has volume.
        QList<ChartBar> quiet{bar(at("2026-10-12T09:00:00+09:00"), 10, 10, 10, 10, 0),
                              bar(at("2026-10-12T09:01:00+09:00"), 11, 11, 11, 11, 10)};
        QVERIFY(same(session_vwap(quiet, Source::Close, true), {n, 11}));
        QVERIFY(same(session_vwap(quiet, Source::Close), {n, n}));
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

    void the_lower_indicators_warm_up() {
        const auto w = [](Study study) { return warmup(default_settings(study)); };
        QCOMPARE(w(Study::Volume), 0);
        QCOMPARE(w(Study::MassIndex), 4 * 9 * 2 + 24);
        QCOMPARE(w(Study::Momentum), 10);
        QCOMPARE(w(Study::VolumeOscillator), 4 * 10);
        QCOMPARE(w(Study::PercentB), 19);
        QCOMPARE(w(Study::BandWidth), 19);
        QCOMPARE(w(Study::Stochastic), 13 + 2 + 2);
        QCOMPARE(w(Study::StochasticRsi), 8 * 14 + 13 + 2 + 2);
        QCOMPARE(w(Study::WilliamsR), 13);
        QCOMPARE(w(Study::Disparity), 19);
        QCOMPARE(w(Study::IntradayIntensity), 20);
        QCOMPARE(w(Study::ChaikinOscillator), 4 * 10);
        QCOMPARE(w(Study::Trix), 4 * (3 * 18 + 9));
        QCOMPARE(w(Study::PriceOscillator), 20);
        StudySettings exponential = default_settings(Study::PriceOscillator);
        exponential.inputs[2] = 1;
        QCOMPARE(warmup(exponential), 4 * 21);
        QCOMPARE(w(Study::AdLine), 0);
        QCOMPARE(w(Study::Adx), 8 * 2 * 14);
        QCOMPARE(w(Study::Atr), 8 * 14);
        QCOMPARE(w(Study::Cci), 19);
        QCOMPARE(w(Study::Dmi), 8 * 2 * 14);
        QCOMPARE(w(Study::Macd), 4 * 26 + 4 * 9);
        QCOMPARE(w(Study::Mfi), 14);
        QCOMPARE(w(Study::Obv), 0);
        QCOMPARE(w(Study::Roc), 9);
        QCOMPARE(w(Study::Rsi), 8 * 14);
        QCOMPARE(w(Study::Rmi), 5 + 8 * 14);
        // The longest of those on, the moving averages' 119 included; still capped.
        StudySet set = default_studies();
        set.turn(Study::Rsi, true);
        set.turn(Study::Macd, true);
        QCOMPARE(history_bars(set, 1), kFitBars + 140);
        set.of(Study::Rsi).inputs = {300};
        QCOMPARE(history_bars(set, 1), kMaxHistoryBars);
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
        QCOMPARE(history_bars(set, 1), 0); // whole days instead (first_screen_day)
        set.turn(Study::Vwap, false);

        // The recursive ones read far enough that more bars barely move what is shown.
        QCOMPARE(warmup(default_settings(Study::ExponentialAverage)), 4 * 120);
        QCOMPARE(warmup(default_settings(Study::Supertrend)), 10 * 10);
        QCOMPARE(warmup(default_settings(Study::ParabolicSar)), 100);
        QCOMPARE(warmup(default_settings(Study::Bollinger)), 19); // a fixed look-back
        set.turn(Study::ExponentialAverage, true);
        QCOMPARE(history_bars(set, 1), kFitBars + 480);
        set.of(Study::ExponentialAverage).inputs = {5, 600};
        QCOMPARE(history_bars(set, 1), kMaxHistoryBars); // 2,400 capped
        set.turn(Study::ExponentialAverage, false);
        set.turn(Study::Supertrend, true);
        QCOMPARE(history_bars(set, kDayView), kFitBars + 100);
        set.turn(Study::Supertrend, false);
        set.turn(Study::ParabolicSar, true);
        QCOMPARE(history_bars(set, 5), kFitBars + 100);

        const QDateTime first = kst("2026-10-12T09:00:00+09:00");
        const auto read = history_read(first, 100, 179, 1);
        QVERIFY(read);
        QCOMPARE(read->before, first);
        QCOMPARE(read->limit, kOlderBars);
        QCOMPARE(history_read(first, 50, 179, 15)->limit, 4500); // 300 of 15 minutes (krx-quant #239)
        QVERIFY(!history_read(first, 179, 179, 1));
        QVERIFY(!history_read(QDateTime(), 0, 179, 1));                          // the chart is read first
        QVERIFY(!history_read(first, kMaxHistoryBars, kMaxHistoryBars + 10, 1)); // the cap
        QVERIFY(history_read(first, 500, 179, 1, first.addSecs(-3600)));         // back to the anchor
        QVERIFY(!history_read(first, 500, 179, 1, first));
        QCOMPARE(history_candles(first, 10, 179), std::optional(first));
        QVERIFY(!history_candles(first, 400, 179));
        QVERIFY(!history_candles(first, kMaxHistoryBars, kMaxHistoryBars + 1, first.addDays(-9999)));
    }

    void the_vwap_reads_back_past_the_first_screens_earliest_date() {
        // 40 bars on the 12th from 09:00 and 40 on the 13th from 08:00: the first screen's 60
        // start on the 12th at 09:20, so the bars are read back past 00:00 on the 12th.
        QList<Bar> held;
        for (int i = 0; i < 40; ++i)
            held << Bar{kst("2026-10-12T09:00:00+09:00").addSecs(60 * i), 1, 1, 1, 1, 1};
        for (int i = 0; i < 40; ++i)
            held << Bar{kst("2026-10-13T08:00:00+09:00").addSecs(60 * i), 1, 1, 1, 1, 1};
        const QDateTime target = first_screen_day(held, 1);
        QCOMPARE(target, kst("2026-10-12T00:00:00+09:00"));
        // Worked out from the first screen, not the first bar held: more bars before keep it.
        QList<Bar> more{Bar{kst("2026-10-09T15:00:00+09:00"), 1, 1, 1, 1, 1}};
        QCOMPARE(first_screen_day(more + held, 1), target);
        // A wider view's 60 bars reach further back: the 5-minute view's are all of these days.
        QCOMPARE(first_screen_day(held, 5), kst("2026-10-12T00:00:00+09:00"));
        QCOMPARE(first_screen_day(held.mid(40), 1), kst("2026-10-13T00:00:00+09:00"));
        QVERIFY(!first_screen_day({}, 1).isValid());

        // Read while the first bar held is that day's or later; enough once a date before is held.
        const QDateTime first = held.first().start;
        QVERIFY(history_read(first, 80, 0, 1, target)); // the 12th from 09:00 only: not whole
        QVERIFY(!history_read(more.first().start, 81, 0, 1, target));
        QVERIFY(!history_read(first, kMaxHistoryBars, 0, 1, target)); // the cap still holds

        // Drawn: the first day held is empty (with why in the legend) unless nothing comes before.
        StudySet set = default_studies();
        set.turn(Study::MovingAverage, false);
        set.turn(Study::Vwap, true);
        const auto bars = chart_bars(held);
        StudyDrawing d = draw_studies(bars, set, 1, std::nullopt, false);
        QCOMPARE(d.series[0].points.first().first, bars[40].time); // from the 13th's first bar
        QCOMPARE(row_of(d, Study::Vwap).missing, QStringLiteral("그날 첫 봉을 읽지 않아 비움"));
        d = draw_studies(bars, set, 1, std::nullopt, false, true);
        QCOMPARE(d.series[0].points.first().first, bars[0].time);
        QVERIFY(row_of(d, Study::Vwap).missing.isEmpty());
    }

    void a_hidden_anchored_vwap_waits_for_no_click() {
        StudySet set = default_studies();
        QVERIFY(!anchor_pickable(set)); // off
        set.turn(Study::AnchoredVwap, true);
        QVERIFY(anchor_pickable(set));
        set.of(Study::AnchoredVwap).hidden = true;
        QVERIFY(!anchor_pickable(set)); // its `기준 다시 고르기` is off, and no click is waited for
        const StudyDrawing d = draw_studies(bars({{1, 1, 1}, {2, 1, 1}}), set, 1, std::nullopt, true);
        QVERIFY(!d.picking);
        QVERIFY(row_of(d, Study::AnchoredVwap).note.isEmpty());
        QVERIFY(row_of(d, Study::AnchoredVwap).hidden);
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
            R"({"study":"ma","on":true,"inputs":[5,-1,20.5,60]},{"study":"nope","on":true},)"
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

        // The lower ones (krx-quant #223) round-trip too, a choice among its names' range.
        set.turn(Study::Rsi, true);
        set.turn(Study::PriceOscillator, true);
        set.of(Study::PriceOscillator).inputs = {5, 30, 1};
        set.of(Study::Macd).lines[3].color = QStringLiteral("#ABCDEF");
        QCOMPARE(parse_studies(studies_value(set)), set);
        const StudySet choice = parse_studies(
            QStringLiteral(R"([{"study":"price_oscillator","on":true,"inputs":[10,21,2]},{"study":"rsi","on":true}])"));
        QCOMPARE(choice.of(Study::PriceOscillator).inputs, (QList<double>{10, 21, 0})); // no third choice
        QCOMPARE(choice.studies[0].study, Study::PriceOscillator);
        QCOMPARE(choice.studies[1].study, Study::Rsi);
        QVERIFY(choice.on(Study::Volume)); // not kept: as for a new symbol
    }

    void a_symbol_keeps_its_pane_heights() {
        PaneHeights heights{{Study::Rsi, 140}, {Study::Volume, 80}};
        QCOMPARE(parse_pane_heights(pane_heights_value(heights)), heights);
        QVERIFY(parse_pane_heights({}).isEmpty()); // a symbol opened for the first time
        QVERIFY(parse_pane_heights(QStringLiteral("not json")).isEmpty());
        // Out of range, not whole, not a number, an upper indicator or unknown: dropped.
        const PaneHeights broken = parse_pane_heights(
            QStringLiteral(R"({"rsi":10,"macd":120.5,"obv":"90","ma":100,"nope":100,"cci":1000,"volume":40})"));
        QCOMPARE(broken, (PaneHeights{{Study::Cci, 1000}, {Study::Volume, 40}}));
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
        QCOMPARE(d.panes.size(), 1); // the volume's
        QCOMPARE(d.panes[0].study, Study::Volume);
        QVERIFY(!d.profile && !d.anchor);

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
        QVERIFY(d.panes.isEmpty());
        QVERIFY(std::none_of(d.legend.begin(), d.legend.end(), [](const LegendRow& r) { return r.pane == 1; }));
    }

    void the_lower_indicators_by_hand() {
        // Mass index of EMA 2 and sum 2 over ranges 2, 4, 2, 4, 2: EMA 3, 7/3, 31/9, 67/27; its
        // EMA 8/3, 86/27, 220/81; ratios 7/8, 93/86, 201/220.
        const auto ranges = bars({{2, 0, 1}, {4, 0, 2}, {2, 0, 1}, {4, 0, 2}, {2, 0, 1}});
        QVERIFY(same(mass_index(ranges, 2, 2), {n, n, n, 7.0 / 8 + 93.0 / 86, 93.0 / 86 + 201.0 / 220}));

        QVERIFY(same(momentum({10, 12, 11, 15}, 2), {n, n, 1, 3}));
        QVERIFY(same(rate_of_change({100, 110, 121}, 1), {n, 10, 10}));
        QVERIFY(same(rate_of_change({100, 110, 121}, 2), {n, n, 21}));

        // Volumes 1, 2, 3, 4: EMA 1 is each, EMA 2 1.5, 2.5, 3.5.
        const auto volumes = traded({{1, 1, 1, 1}, {1, 1, 1, 2}, {1, 1, 1, 3}, {1, 1, 1, 4}});
        QVERIFY(same(volume_oscillator(volumes, 1, 2), {n, 50 / 1.5, 50 / 2.5, 50 / 3.5}));

        // Bands of 2 over 1, 3, 1, 3: middle 2, 4 and 0 around it.
        QVERIFY(same(percent_b({1, 3, 1, 3}, 2, 2), {n, 0.75, 0.25, 0.75}));
        QVERIFY(same(band_width({2, 4, 4, 4, 5, 5, 7, 9}, 8, 2), {n, n, n, n, n, n, n, 160})); // 100 × 8 / 5
        QVERIFY(same(percent_b({5, 5, 5}, 2, 2), {n, n, n}));                                  // no width

        // Stochastic of 2: raw 75 (11 in 8–12), 20 (8 in 7–12), 83.3 (12 in 7–13).
        const auto four = bars({{10, 8, 9}, {12, 9, 11}, {11, 7, 8}, {13, 10, 12}});
        const SignalLines fast = stochastic(four, 2, 1, 1);
        QVERIFY(same(fast.line, {n, 75, 20, 500.0 / 6}));
        QVERIFY(same(fast.signal, fast.line));
        const SignalLines slow = stochastic(four, 2, 2, 2);
        QVERIFY(same(slow.line, {n, n, 47.5, (20 + 500.0 / 6) / 2}));
        QVERIFY(same(slow.signal, {n, n, n, (47.5 + (20 + 500.0 / 6) / 2) / 2}));
        QVERIFY(same(stochastic(bars({{5, 5, 5}, {5, 5, 5}}), 2, 1, 1).line, {n, n})); // one price
        QVERIFY(same(williams_r(four, 2), {n, -25, -80, -100.0 / 6}));

        QVERIFY(same(disparity({10, 20, 30}, 2), {n, 400.0 / 3, 120}));

        // (2 × close − high − low) / (high − low) × volume: 100, −300, 0 (high = low).
        const auto three = traded({{10, 8, 10, 100}, {10, 8, 8, 300}, {10, 10, 10, 50}});
        QVERIFY(same(intraday_intensity(three, 2), {n, -50, -30000.0 / 350}));

        // Commodity channel: 1, 2, 3 has mean 2 and mean deviation 2/3: (3 − 2) / 0.01.
        QVERIFY(same(commodity_channel({1, 2, 3}, 3), {n, n, 100}));
        QVERIFY(same(commodity_channel({2, 4, 6, 8}, 2), {n, 200.0 / 3, 200.0 / 3, 200.0 / 3}));
        QVERIFY(same(commodity_channel({5, 5}, 2), {n, n}));
    }

    void the_smoothed_ones_by_hand() {
        // RSI of 2 over 10, 12, 11, 13, 13: rises 2, 0, 2, 0 and falls 0, 1, 0, 0; Wilder's
        // averages from 1 and 0.5: 1.5 and 0.25, then 0.75 and 0.125.
        QVERIFY(same(relative_strength({10, 12, 11, 13, 13}, 2), {n, n, 200.0 / 3, 600.0 / 7, 600.0 / 7}));
        QVERIFY(same(relative_strength({1, 2, 3}, 2), {n, n, 100})); // no falls
        QVERIFY(same(relative_strength({3, 2, 1}, 2), {n, n, 0}));   // no rises
        // RMI of 2 with a momentum of 2 over 10, 12, 11, 13, 13, 12: changes 1, 1, 2, −1.
        QVERIFY(same(relative_momentum({10, 12, 11, 13, 13, 12}, 2, 2), {n, n, n, 100, 100, 60}));
        // Stochastic RSI of 2 and 2: the RSI 66.7, 85.7, 85.7, 40 between its last two.
        const SignalLines k = stochastic_rsi({10, 12, 11, 13, 13, 12}, 2, 2, 1, 1);
        QVERIFY(same(k.line, {n, n, n, 100, n, 0}));
        QVERIFY(same(k.signal, k.line));

        // MACD of 1, 2 and 2 over 1, 3, 2, 6: EMA 2 is 2, 2, 14/3.
        const SignalLines m = macd({1, 3, 2, 6}, 1, 2, 2);
        QVERIFY(same(m.line, {n, 1, 0, 4.0 / 3}));
        QVERIFY(same(m.signal, {n, n, 0.5, 19.0 / 18}));
        QVERIFY(same(m.histogram, {n, n, -0.5, 5.0 / 18}));

        // TRIX of 2 over 1–5: the three EMAs end at 4.5, 4 and 3.5 (2.5 before): +40%.
        const SignalLines t = trix({1, 2, 3, 4, 5}, 2, 1);
        QVERIFY(same(t.line, {n, n, n, n, 40}));
        QVERIFY(same(t.signal, {n, n, n, n, 40}));
        const SignalLines one = trix({100, 110, 99}, 1, 2);
        QVERIFY(same(one.line, {n, 10, -10}));
        QVERIFY(same(one.signal, {n, n, 0}));

        // Price oscillator of 1 and 2 over 10, 20, 10, 40: simple averages 15, 15, 25; EMAs 15,
        // 35/3, 275/9.
        QVERIFY(same(price_oscillator({10, 20, 10, 40}, 1, 2, false), {n, 100.0 / 3, -100.0 / 3, 60}));
        QVERIFY(same(price_oscillator({10, 20, 10, 40}, 1, 2, true),
                     {n, 100.0 / 3, 100 * (10 - 35.0 / 3) / (35.0 / 3), 100 * (40 - 275.0 / 9) / (275.0 / 9)}));

        // DMI of 2: +DM 2, 0, 2, 0 and −DM 0, 2, 0, 1 and true ranges 3, 4, 5, 3 from the second
        // bar; Wilder's from 1, 1 and 3.5; DX 0, 50, 0; ADX from 25.
        const auto five = bars({{10, 8, 9}, {12, 9, 11}, {11, 7, 8}, {13, 10, 12}, {12, 9, 10}});
        const Directional d = directional_movement(five, 2);
        QVERIFY(same(d.plus, {n, n, 100 / 3.5, 150 / 4.25, 75 / 3.625}));
        QVERIFY(same(d.minus, {n, n, 100 / 3.5, 50 / 4.25, 75 / 3.625}));
        QVERIFY(same(d.adx, {n, n, n, 25, 12.5}));

        // MFI of 2: money flows +1100 (11 × 100), −2000 (10 × 200), +1200 (12 × 100).
        const auto flows = traded({{10, 10, 10, 100}, {11, 11, 11, 100}, {10, 10, 10, 200}, {12, 12, 12, 100}});
        QVERIFY(same(money_flow(flows, source_values(flows, Source::Close), 2), {n, n, 110000.0 / 3100, 37.5}));
    }

    void the_cumulative_lines_do_not_depend_on_where_the_bars_start() {
        // Two dates: (2 × close − high − low) / (high − low) × volume is 100, −300 on the 12th,
        // 100, 0, −100 on the 13th; the closes 10, 8, 11, 10, 9.
        QList<ChartBar> two_days{
            bar(at("2026-10-12T15:00:00+09:00"), 10, 10, 8, 10, 100),
            bar(at("2026-10-12T15:01:00+09:00"), 10, 10, 8, 8, 300),
            bar(at("2026-10-13T08:00:00+09:00"), 10, 12, 8, 11, 200),
            bar(at("2026-10-13T08:01:00+09:00"), 10, 10, 10, 10, 50),
            bar(at("2026-10-13T08:02:00+09:00"), 10, 11, 9, 9, 100),
        };
        // 0 from 00:00 of the last bar's date in a minute view, from 1 January in a candle view: a
        // moment, whichever bars are held.
        QCOMPARE(cumulative_zero(two_days, 1), std::optional(at("2026-10-13T00:00:00+09:00")));
        QCOMPARE(cumulative_zero(two_days, kDayView), std::optional(at("2026-01-01T00:00:00+09:00")));
        QCOMPARE(cumulative_zero(two_days.mid(2), 15), cumulative_zero(two_days, 15));
        QVERIFY(!cumulative_zero({}, 1));
        // Its bar: the first from that moment, once a bar before it is held (or none is before).
        QCOMPARE(cumulative_anchor(two_days, 1), std::optional<qsizetype>(2));
        QCOMPARE(cumulative_anchor(two_days, 15), std::optional<qsizetype>(2));
        QVERIFY(!cumulative_anchor(two_days, kDayView)); // 1 January's not held
        QCOMPARE(cumulative_anchor(two_days, kDayView, true), std::optional<qsizetype>(0));
        QVERIFY(!cumulative_anchor(two_days.mid(2), 1)); // the 13th from 08:00 only
        QCOMPARE(cumulative_anchor(two_days.mid(2), 1, true), std::optional<qsizetype>(0));
        QVERIFY(!cumulative_anchor({}, 1, true));
        QVERIFY(same(accumulation_distribution(two_days, 2), {200, -100, 0, 0, -100}));
        QVERIFY(same(on_balance_volume(two_days, 2), {100, -200, 0, -50, -150}));
        // Fewer bars before (as before more were read): the same values where both have them.
        const auto later = two_days.mid(1);
        QCOMPARE(cumulative_anchor(later, 1), std::optional<qsizetype>(1));
        QVERIFY(same(accumulation_distribution(later, 1), accumulation_distribution(two_days, 2).mid(1)));
        QVERIFY(same(on_balance_volume(later, 1), on_balance_volume(two_days, 2).mid(1)));
        // The Chaikin oscillator of 1 and 2 over the AD line 200, −100, 0, 0, −100 (EMA 2 from 50):
        // the same wherever the line is 0, so it needs no 0 of its own.
        QVERIFY(same(chaikin_oscillator(two_days, 1, 2), {n, -150, -50.0 / 3, -50.0 / 9, -950.0 / 27}));
    }

    void the_cumulative_lines_wait_for_their_zeros_bars() {
        // Day candles of the weekdays from ``from`` to 12 October 2026, each its own: the AD
        // line's and the OBV's values, the legend's reason where they are empty.
        const auto days = [](const char* from) {
            QList<ChartBar> out;
            for (QDate d = QDate::fromString(QString::fromLatin1(from), Qt::ISODate); d <= QDate(2026, 10, 12);
                 d = d.addDays(1)) {
                if (d.dayOfWeek() > 5 || (d.month() == 1 && d.day() == 1)) // weekends, New Year's Day
                    continue;
                const int k = static_cast<int>(QDate(2025, 1, 1).daysTo(d));
                const double close = 100 + (k * 37) % 23;
                out << bar(at("1970-01-01T00:00:00+09:00") + QDate(1970, 1, 1).daysTo(d) * 86400, close - 1, close + 2,
                           close - 3, close, 1000 + (k * 53) % 700);
            }
            return out;
        };
        StudySet set = default_studies();
        set.turn(Study::AdLine, true);
        set.turn(Study::Obv, true);
        set.turn(Study::ChaikinOscillator, true);
        const auto series = [](const StudyDrawing& d, const QString& id) {
            for (const auto& s : d.series)
                if (s.id == id)
                    return s.points;
            return QList<std::pair<qint64, std::optional<double>>>{};
        };
        // Read from 2 March, after 1 January: empty (not a value that moves later), with why.
        StudyDrawing d = draw_studies(days("2026-03-02"), set, kDayView, std::nullopt, false);
        QVERIFY(series(d, "obv").isEmpty() && series(d, "ad_line").isEmpty());
        QCOMPARE(row_of(d, Study::Obv).missing, QStringLiteral("그해 첫 봉을 읽지 않아 비움"));
        QCOMPARE(row_of(d, Study::AdLine).missing, QStringLiteral("그해 첫 봉을 읽지 않아 비움"));
        QVERIFY(!series(d, "chaikin_oscillator").isEmpty()); // needs no 0
        // More of that year before: still empty.
        d = draw_studies(days("2026-02-02"), set, kDayView, std::nullopt, false);
        QVERIFY(series(d, "obv").isEmpty());
        // Into the year before: drawn, 0 at the year's first candle (2 January); more candles
        // before change no value.
        const StudyDrawing whole = draw_studies(days("2025-12-29"), set, kDayView, std::nullopt, false);
        QVERIFY(row_of(whole, Study::Obv).missing.isEmpty());
        const auto obv = series(whole, "obv");
        const auto ad = series(whole, "ad_line");
        QCOMPARE(obv.size(), days("2025-12-29").size());
        for (const auto& [time, value] : obv)
            if (time == at("2026-01-02T00:00:00+09:00"))
                QCOMPARE(value, std::optional(0.0));
        const StudyDrawing more = draw_studies(days("2025-03-03"), set, kDayView, std::nullopt, false);
        QCOMPARE(series(more, "obv").mid(series(more, "obv").size() - obv.size()), obv);
        QCOMPARE(series(more, "ad_line").mid(series(more, "ad_line").size() - ad.size()), ad);
        // Nothing before the first held: whole as it is.
        d = draw_studies(days("2026-03-02"), set, kDayView, std::nullopt, false, true);
        QVERIFY(!series(d, "obv").isEmpty() && row_of(d, Study::Obv).missing.isEmpty());

        // A minute view read from the session's middle (an engine restarted at 10:00): empty, still
        // after the session's earlier bars come, drawn once the day before's are held.
        const auto minutes = [](const char* from, int count) {
            QList<ChartBar> out;
            for (int i = 0; i < count; ++i)
                out << bar(at(from) + i * 60, 100, 103 + i % 4, 98 - i % 3, 100 + i % 5, 10 + i % 9);
            return out;
        };
        const auto session = minutes("2026-10-13T10:00:00+09:00", 60);
        d = draw_studies(session, set, 1, std::nullopt, false);
        QVERIFY(series(d, "obv").isEmpty());
        QCOMPARE(row_of(d, Study::Obv).missing, QStringLiteral("그날 첫 봉을 읽지 않아 비움"));
        const auto morning = minutes("2026-10-13T08:00:00+09:00", 120) + session;
        QVERIFY(series(draw_studies(morning, set, 1, std::nullopt, false), "obv").isEmpty());
        const auto day_before = minutes("2026-10-12T19:00:00+09:00", 60) + morning;
        const auto shown = series(draw_studies(day_before, set, 1, std::nullopt, false), "obv");
        QCOMPARE(shown.size(), day_before.size());
        const auto earlier = minutes("2026-10-12T15:00:00+09:00", 240) + day_before;
        const auto again = series(draw_studies(earlier, set, 1, std::nullopt, false), "obv");
        QCOMPARE(again.mid(again.size() - shown.size()), shown);

        // Past the read's cap without reaching the 0's moment: empty, saying so.
        QList<ChartBar> capped;
        for (int i = 0; i < kMaxHistoryBars; ++i)
            capped << bar(at("2026-03-02T00:00:00+09:00") + i * 3600, 100, 101, 99, 100, 10);
        d = draw_studies(capped, set, kDayView, std::nullopt, false);
        QVERIFY(series(d, "obv").isEmpty());
        QCOMPARE(row_of(d, Study::Obv).missing, QStringLiteral("그해 첫 봉이 2,000봉보다 앞이라 비움"));
    }

    void the_cumulative_lines_read_back_past_their_zero() {
        // The moment the history read reaches back to, from the bars as read: the last bar's date
        // (a minute view) or year (a candle view), whichever bars come before.
        QList<Bar> held;
        for (int i = 0; i < 60; ++i)
            held << Bar{kst("2026-10-13T10:00:00+09:00").addSecs(60 * i), 1, 1, 1, 1, 1};
        const QDateTime today = kst("2026-10-13T00:00:00+09:00");
        QCOMPARE(cumulative_start(held, 1), today);
        QCOMPARE(cumulative_start(held, 60), today);
        QList<Bar> before{Bar{kst("2026-10-12T15:00:00+09:00"), 1, 1, 1, 1, 1}};
        QCOMPARE(cumulative_start(before + held, 1), today);
        QCOMPARE(cumulative_start(held, kDayView), kst("2026-01-01T00:00:00+09:00"));
        QVERIFY(!cumulative_start({}, 1).isValid());
        // Read while the first held is from that moment on; enough once one before it is held.
        QVERIFY(history_read(held.first().start, 60, 0, 1, today));
        QVERIFY(!history_read(before.first().start, 61, 0, 1, today));
        QVERIFY(!history_read(held.first().start, kMaxHistoryBars, 0, 1, today)); // the cap
        const QDateTime january = kst("2026-01-01T00:00:00+09:00");
        QVERIFY(history_candles(kst("2026-03-02T00:00:00+09:00"), 160, 0, january));
        QVERIFY(!history_candles(kst("2025-12-29T00:00:00+09:00"), 200, 0, january));
        QVERIFY(!history_candles(kst("2026-03-02T00:00:00+09:00"), kMaxHistoryBars, 0, january));
    }

    void more_bars_barely_move_the_recursive_ones() {
        // Each read with its warm-up before the first screen's 60 bars, then with over 1,000 more
        // bars before those: the 60 values shown move by less than a thousandth of their range
        // (the weight left beyond the warm-up is about e^-8; the RSI's moved most, 0.03%).
        const auto all = wandering(1500);
        for (const Study study :
             {Study::Rsi, Study::Rmi, Study::StochasticRsi, Study::Macd, Study::Trix, Study::Atr, Study::Adx,
              Study::Dmi, Study::MassIndex, Study::VolumeOscillator, Study::ChaikinOscillator}) {
            StudySet set = default_studies();
            set.turn(Study::MovingAverage, false);
            set.turn(Study::Volume, false);
            set.turn(study, true);
            const int read = history_bars(set, 1);
            QCOMPARE(read, kFitBars + warmup(set.of(study)));
            const StudyDrawing full = draw_studies(all, set, 1, std::nullopt, false);
            const StudyDrawing short_read = draw_studies(all.mid(all.size() - read), set, 1, std::nullopt, false);
            QCOMPARE(full.series.size(), short_read.series.size());
            for (qsizetype l = 0; l < full.series.size(); ++l) {
                const auto shown = full.series[l].points.mid(full.series[l].points.size() - kFitBars);
                const auto& other = short_read.series[l].points;
                double low = 1e300;
                double high = -1e300;
                for (const auto& [time, value] : shown)
                    if (value) {
                        low = std::min(low, *value);
                        high = std::max(high, *value);
                    }
                double moved = 0;
                for (const auto& [time, value] : shown) {
                    const auto it = std::find_if(other.begin(), other.end(),
                                                 [time = time](const auto& p) { return p.first == time; });
                    QVERIFY2(it != other.end() && it->second.has_value() == value.has_value(),
                             qPrintable(study_key(study)));
                    if (value)
                        moved = std::max(moved, std::abs(*it->second - *value));
                }
                QVERIFY2(
                    moved <= 1e-3 * std::max(1.0, high - low),
                    qPrintable(QStringLiteral("%1 moved %2 of %3").arg(full.series[l].id).arg(moved).arg(high - low)));
            }
        }
    }

    void each_lower_indicator_has_a_pane_in_the_order_turned_on() {
        QList<ChartBar> minutes;
        for (int i = 0; i < 80; ++i)
            minutes << bar(at("2026-10-12T09:00:00+09:00") + i * 60, 100 + i % 7, 103 + i % 7, 98 + i % 5, 101 + i % 6,
                           10 + i);
        StudySet set = default_studies(); // the moving averages and the volume
        set.turn(Study::Rsi, true);
        set.turn(Study::Macd, true);
        StudyDrawing d = draw_studies(minutes, set, 1, std::nullopt, false, false, {{Study::Rsi, 150}});
        QCOMPARE(d.panes.size(), 3);
        QCOMPARE(d.panes[0].study, Study::Volume);
        QCOMPARE(d.panes[1].study, Study::Rsi);
        QCOMPARE(d.panes[2].study, Study::Macd);
        QCOMPARE(d.panes[0].height, kPaneHeight);
        QCOMPARE(d.panes[1].height, 150);
        QCOMPARE(d.panes[1].guides, (QList<double>{70, 30}));
        QCOMPARE(d.panes[2].guides, QList<double>{0});
        const auto series = [&d](const QString& id) {
            for (const auto& s : d.series)
                if (s.id == id)
                    return s;
            return DrawnSeries{};
        };
        QCOMPARE(series("rsi").pane, 2);
        QCOMPARE(series("rsi").decimals, 2);
        QCOMPARE(series("rsi").points.first().first, minutes[14].time); // from its 15th bar
        QCOMPARE(series("macd.macd").pane, 3);
        QVERIFY(series("macd.histogram").histogram);
        QCOMPARE(series("macd.histogram").color, QStringLiteral("#22C55E"));
        QCOMPARE(series("macd.histogram").down_color, QStringLiteral("#EF4444"));
        QCOMPARE(series("ma.0").pane, 0);
        QCOMPARE(row_of(d, Study::Rsi).pane, 2);
        QCOMPARE(row_of(d, Study::Rsi).values, QStringList{"rsi"});
        QCOMPARE(row_of(d, Study::Macd).values, (QStringList{"macd.macd", "macd.signal", "macd.histogram"}));
        QCOMPARE(row_of(d, Study::Macd).settings, QStringLiteral("12 26 9"));
        QCOMPARE(row_of(d, Study::Volume).pane, 1);

        // Hidden: its pane stays, with its legend row, and draws nothing.
        set.of(Study::Rsi).hidden = true;
        d = draw_studies(minutes, set, 1, std::nullopt, false);
        QCOMPARE(d.panes.size(), 3);
        QVERIFY(d.panes[1].hidden);
        QVERIFY(row_of(d, Study::Rsi).hidden);
        QVERIFY(std::none_of(d.series.begin(), d.series.end(), [](const DrawnSeries& s) { return s.pane == 2; }));

        // Off: the panes below move up; the volume off too, the RSI is on top.
        set.turn(Study::Volume, false);
        set.of(Study::Rsi).hidden = false;
        d = draw_studies(minutes, set, 1, std::nullopt, false);
        QCOMPARE(d.panes.size(), 2);
        QCOMPARE(d.panes[0].study, Study::Rsi);
        QCOMPARE(series("rsi").pane, 1);
        QCOMPARE(series("macd.signal").pane, 2);
        // Turned on again, the volume goes last.
        set.turn(Study::Volume, true);
        d = draw_studies(minutes, set, 1, std::nullopt, false);
        QCOMPARE(d.panes.last().study, Study::Volume);

        // Every lower indicator draws in its pane, on day candles too, its legend after its lines
        // (nothing before these bars to read: the AD line's and the OBV's 0 is among them).
        StudySet every = default_studies();
        for (const Study study : kLowerStudies)
            every.turn(study, true);
        d = draw_studies(minutes, every, kDayView, std::nullopt, false, true);
        QCOMPARE(d.panes.size(), qsizetype(kLowerStudies.size()));
        for (qsizetype i = 0; i < d.panes.size(); ++i) {
            const Study study = d.panes[i].study;
            const LegendRow& row = row_of(d, study);
            QCOMPARE(row.pane, int(i + 1));
            QVERIFY2(!row.values.isEmpty(), qPrintable(study_key(study)));
            for (const QString& id : row.values) {
                if (id == QLatin1String("volume"))
                    continue;
                QCOMPARE(series(id).pane, int(i + 1));
                QVERIFY2(!series(id).points.isEmpty(), qPrintable(id));
            }
        }
    }

    void the_supply_studies_are_the_engine_s_lower_ones() {
        // The daily supply data's eight (krx-quant #226), in their own section, after the 25 below.
        QCOMPARE(kSupplyStudies.size(), std::size_t(8));
        QCOMPARE(kStudies.size(), std::size_t(45));
        QCOMPARE(kStudies.back(), Study::CreditBalance);
        const QList<std::pair<Study, SupplyField>> fields{
            {Study::ShortVolume, SupplyField::ShortVolume},       {Study::IndividualNet, SupplyField::IndividualNet},
            {Study::InstitutionNet, SupplyField::InstitutionNet}, {Study::ForeignNet, SupplyField::ForeignNet},
            {Study::ForeignHolding, SupplyField::ForeignHolding}, {Study::ForeignRatio, SupplyField::ForeignRatio},
            {Study::LendingBalance, SupplyField::LendingBalance}, {Study::CreditBalance, SupplyField::CreditBalance}};
        for (const auto& [study, field] : fields) {
            const StudySpec& spec = study_spec(study);
            QVERIFY(!spec.upper);
            QCOMPARE(spec.supply, std::optional(field));
            QCOMPARE(study_key(study), supply_field_key(field)); // as the engine names it
            QVERIFY(spec.inputs.isEmpty() && !spec.source);      // nothing to set but colours
            QCOMPARE(warmup(default_settings(study)), 0);
            QCOMPARE(settings_text(default_settings(study)), QString());
            QVERIFY(!spec.about.isEmpty() && spec.help.contains(QStringLiteral("계단")));
        }
        for (const Study study : kLowerStudies)
            QVERIFY(!study_spec(study).supply);
        QCOMPARE(study_title(Study::ShortVolume), QStringLiteral("공매도 거래량"));
        QCOMPARE(study_title(Study::ForeignHolding), QStringLiteral("외국인 보유수량"));
        QCOMPARE(study_title(Study::ForeignRatio), QStringLiteral("외국인 지분율"));
        QCOMPARE(study_title(Study::LendingBalance), QStringLiteral("대차잔고"));
        QCOMPARE(study_title(Study::CreditBalance), QStringLiteral("신용잔고"));
        // Flows are bars from 0 (the net buys by their sign, with a 0 line); balances step.
        for (const Study study : {Study::ShortVolume, Study::IndividualNet, Study::InstitutionNet, Study::ForeignNet})
            QVERIFY(study_spec(study).bars);
        for (const Study study :
             {Study::ForeignHolding, Study::ForeignRatio, Study::LendingBalance, Study::CreditBalance})
            QVERIFY(!study_spec(study).bars);
        QCOMPARE(study_spec(Study::ForeignNet).guides, (QList<double>{0}));
        QCOMPARE(default_settings(Study::ForeignNet).lines.size(), 2); // 순매수, 순매도
        QCOMPARE(study_spec(Study::ForeignRatio).decimals, 2);
        QVERIFY(!study_spec(Study::ForeignRatio).volume_scale && study_spec(Study::CreditBalance).volume_scale);
        // Kept and read again like the others; none on at first.
        StudySet set = default_studies();
        QVERIFY(!set.on(Study::ShortVolume));
        set.turn(Study::ForeignNet, true);
        set.of(Study::ForeignNet).lines[1].color = QStringLiteral("#123456");
        QCOMPARE(parse_studies(studies_value(set)), set);
        QCOMPARE(history_bars(set, 1), kFitBars + 119); // the moving averages' alone: no bars for them
    }

    void the_supply_studies_draw_from_an_engine_that_gives_them_on_bars() {
        // Every view on bars; not on bricks (a day's value), nor from an engine that answered 404.
        for (const int view : {1, 5, 240, kDayView, kWeekView, kMonthView, kYearView})
            QVERIFY(study_drawn(Study::ForeignHolding, view));
        QVERIFY(!study_drawn(Study::ForeignHolding, 1, true));
        QVERIFY(!study_drawn(Study::ShortVolume, kDayView, false, false));
        QVERIFY(study_drawn(Study::Rsi, 1, false, false)); // the others do not need the engine
        QCOMPARE(study_unavailable(Study::ShortVolume, 1, true), QStringLiteral("시간축 없는 차트에서는 쓰지 않는다"));
        QCOMPARE(study_unavailable(Study::ShortVolume, 1, false, false),
                 QStringLiteral("이 엔진은 일별 수급을 주지 않는다"));
        QVERIFY(study_unavailable(Study::ShortVolume, kYearView).isEmpty());
    }

    void a_day_s_supply_value_steps_over_its_bars() {
        const QList<SupplyDay> days{supply_day("2026-10-07", 100), supply_day("2026-10-08", 200),
                                    supply_day("2026-10-12", 300)};
        // Minute bars (any minutes): each takes its KST date's value; a date without one, none.
        const QList<ChartBar> minutes =
            bars_at({"2026-10-07T19:59:00+09:00", "2026-10-08T08:00:00+09:00", "2026-10-08T15:30:00+09:00",
                     "2026-10-08T19:00:00+09:00", "2026-10-13T08:00:00+09:00", "2026-10-13T09:01:00+09:00"});
        QVERIFY(same(supply_values(minutes, days, SupplyField::ShortVolume, 1), {100, 200, 200, 200, n, n}));
        QVERIFY(same(supply_values(minutes, days, SupplyField::ForeignRatio, 240), {0.1, 0.2, 0.2, 0.2, n, n}));
        QVERIFY(same(supply_values(minutes, days, SupplyField::CreditBalance, 1), {7100, 7200, 7200, 7200, n, n}));
        // Day candles: each its own day's; today's (not over) none.
        const QList<ChartBar> candles = bars_at({"2026-10-07T00:00:00+09:00", "2026-10-08T00:00:00+09:00",
                                                 "2026-10-12T00:00:00+09:00", "2026-10-13T00:00:00+09:00"});
        QVERIFY(same(supply_values(candles, days, SupplyField::IndividualNet, kDayView), {1100, 1200, 1300, n}));
        // A week, month or year: its last day with a value (the 8th for the week of the 5th, the 12th
        // for the week going on); a period without any, none.
        const QList<ChartBar> weeks =
            bars_at({"2026-09-28T00:00:00+09:00", "2026-10-05T00:00:00+09:00", "2026-10-12T00:00:00+09:00"});
        QVERIFY(same(supply_values(weeks, days, SupplyField::LendingBalance, kWeekView), {n, 6200, 6300}));
        const QList<ChartBar> months = bars_at({"2026-09-01T00:00:00+09:00", "2026-10-01T00:00:00+09:00"});
        QVERIFY(same(supply_values(months, days, SupplyField::ForeignHolding, kMonthView), {n, 4300}));
        const QList<ChartBar> years = bars_at({"2025-01-01T00:00:00+09:00", "2026-01-01T00:00:00+09:00"});
        QVERIFY(same(supply_values(years, days, SupplyField::ForeignHolding, kYearView), {n, 4300}));
        // A field the engine lacks on a day: that day none, in a period the day before it that has one.
        QList<SupplyDay> gap = days;
        gap[2].values[std::size_t(SupplyField::ForeignHolding)] = std::nullopt;
        QVERIFY(same(supply_values(candles, gap, SupplyField::ForeignHolding, kDayView), {4100, 4200, n, n}));
        QVERIFY(same(supply_values(weeks, gap, SupplyField::ForeignHolding, kWeekView), {n, 4200, n}));
        QVERIFY(same(supply_values(minutes, {}, SupplyField::ShortVolume, 1), {n, n, n, n, n, n}));
    }

    void a_flow_adds_up_over_a_period_and_a_level_takes_its_last_day() {
        // The user's rule (2026-10-17): in a week, month or year view the flows (short-sale volume, net
        // buys) are the sum of the period's days with a value, the levels its last day's value.
        QList<SupplyDay> days{supply_day("2026-09-25", 10),       supply_day("2026-09-29", 20),
                              supply_day("2026-09-30", 30, true), supply_day("2026-10-05", 40),
                              supply_day("2026-10-07", 50),       supply_day("2026-10-08", 60),
                              supply_day("2026-10-12", 70)};
        const QList<ChartBar> weeks =
            bars_at({"2026-09-21T00:00:00+09:00", "2026-09-28T00:00:00+09:00", "2026-10-05T00:00:00+09:00",
                     "2026-10-12T00:00:00+09:00", "2026-10-19T00:00:00+09:00"});
        QVERIFY(supply_flow(SupplyField::ShortVolume) && supply_flow(SupplyField::ForeignNet));
        QVERIFY(!supply_flow(SupplyField::ForeignHolding) && !supply_flow(SupplyField::CreditBalance));
        // The first week read starts inside it (no day before it with a value: the read or the engine's
        // history may begin there): empty, not a sum of part of it. The next weeks are whole; the one
        // going on is the sum so far; a week without a value is empty, not 0.
        QVERIFY(same(supply_values(weeks, days, SupplyField::ShortVolume, kWeekView), {n, 50, 150, 70, n}));
        QVERIFY(same(supply_values(weeks, days, SupplyField::IndividualNet, kWeekView), {n, 2050, 3150, 1070, n}));
        QVERIFY(same(supply_values(weeks, days, SupplyField::ForeignNet, kWeekView), {n, 2990, 9150, 3070, n}));
        // A level is the period's last day's, the first period's too (a part of it says that day).
        QVERIFY(same(supply_values(weeks, days, SupplyField::LendingBalance, kWeekView), {6010, 6030, 6060, 6070, n}));
        // A day without the field's value adds nothing; the others still add up.
        days[5].values[std::size_t(SupplyField::ShortVolume)] = std::nullopt; // the 8th
        QVERIFY(same(supply_values(weeks, days, SupplyField::ShortVolume, kWeekView), {n, 50, 90, 70, n}));
        // Months and years likewise: September and 2026 start before the first day read.
        const QList<ChartBar> months = bars_at({"2026-09-01T00:00:00+09:00", "2026-10-01T00:00:00+09:00"});
        QVERIFY(same(supply_values(months, days, SupplyField::ShortVolume, kMonthView), {n, 160}));
        QVERIFY(same(supply_values(months, days, SupplyField::ForeignHolding, kMonthView), {4030, 4070}));
        const QList<ChartBar> years = bars_at({"2026-01-01T00:00:00+09:00", "2027-01-01T00:00:00+09:00"});
        QVERIFY(same(supply_values(years, days, SupplyField::ShortVolume, kYearView), {n, n}));
        days.prepend(supply_day("2025-12-30", 5)); // read from before the year: whole
        QVERIFY(same(supply_values(years, days, SupplyField::ShortVolume, kYearView), {220, n}));
        // The day and minute views are each day's own value, flows too.
        const QList<ChartBar> candles = bars_at({"2026-09-29T00:00:00+09:00", "2026-09-30T00:00:00+09:00"});
        QVERIFY(same(supply_values(candles, days, SupplyField::ForeignNet, kDayView), {3020, -30}));
    }

    void the_page_draws_the_supply_studies_in_their_panes() {
        const QList<SupplyDay> days{supply_day("2026-10-07", 100, true), supply_day("2026-10-08", 200)};
        const QList<ChartBar> minutes = bars_at({"2026-10-07T15:00:00+09:00", "2026-10-07T15:01:00+09:00",
                                                 "2026-10-08T09:00:00+09:00", "2026-10-12T09:00:00+09:00"});
        StudySet set = default_studies();
        for (const Study study : {Study::ForeignNet, Study::ShortVolume, Study::ForeignHolding, Study::ForeignRatio})
            set.turn(study, true);
        StudyDrawing d = draw_studies(minutes, set, 1, std::nullopt, false, false, {}, false, days);
        const auto series = [&d](const QString& id) {
            for (const auto& s : d.series)
                if (s.id == id)
                    return s;
            return DrawnSeries{};
        };
        // The volume's pane first (on by default), then theirs in the order turned on.
        QCOMPARE(d.panes.size(), 5);
        QCOMPARE(d.panes[1].study, Study::ForeignNet);
        QCOMPARE(d.panes[1].guides, (QList<double>{0}));
        // The net buy: bars from 0, the 순매수 colour up and the 순매도 colour down, a day's value on each
        // of its bars; today's bar (no value yet) ends it.
        const DrawnSeries net = series("foreign_net");
        QCOMPARE(net.pane, 2);
        QVERIFY(net.histogram && !net.steps);
        QCOMPARE(net.color, QStringLiteral("#22C55E"));
        QCOMPARE(net.down_color, QStringLiteral("#EF4444"));
        QCOMPARE(net.points.size(), 3);
        QCOMPARE(net.points[0].second, V(-100));
        QCOMPARE(net.points[1].second, V(-100));
        QCOMPARE(net.points[2].second, V(3200));
        QVERIFY(net.volume_scale);
        // The short-sale volume: bars of one colour.
        const DrawnSeries short_volume = series("short_volume");
        QVERIFY(short_volume.histogram);
        QCOMPARE(short_volume.down_color, short_volume.color);
        // The holdings and the ratio: stepping lines; the ratio to two decimals.
        QVERIFY(series("foreign_holding").steps && !series("foreign_holding").histogram);
        QCOMPARE(series("foreign_ratio").decimals, 2);
        QCOMPARE(series("foreign_ratio").points.last().second, V(0.2));
        // The legend: the name, no settings, the value, and why none where a bar has none.
        const LegendRow& row = row_of(d, Study::ForeignNet);
        QCOMPARE(row.title, QStringLiteral("외국인 순매수량"));
        QCOMPARE(row.settings, QString());
        QCOMPARE(row.values, QStringList{QStringLiteral("foreign_net")});
        QCOMPARE(row.missing, QStringLiteral("그날 값 없음"));
        QVERIFY(row.latest);                              // without a crosshair, its latest value
        QVERIFY(!row_of(d, Study::MovingAverage).latest); // the others, the last bar's
        // From an engine without them, on bricks: neither drawn nor in the legend; kept on.
        for (const StudyDrawing& none :
             {draw_studies(minutes, set, 1, std::nullopt, false, false, {}, false, days, false),
              draw_studies(minutes, set, 1, std::nullopt, false, false, {}, true, days)}) {
            QCOMPARE(none.panes.size(), 1);                                        // the volume's alone
            QCOMPARE(row_of(none, Study::ForeignNet).study, Study::MovingAverage); // none: the empty row
        }
        QVERIFY(set.on(Study::ForeignNet));
        // Hidden: its pane stays, empty.
        set.of(Study::ShortVolume).hidden = true;
        d = draw_studies(minutes, set, kDayView, std::nullopt, false, false, {}, false, days);
        QVERIFY(d.panes[2].hidden);
        QCOMPARE(series("short_volume").id, QString());
    }

    void colours_as_css() {
        QCOMPARE(css_color(QStringLiteral("#FF8000"), 0.5), QStringLiteral("rgba(255,128,0,0.5)"));
        QCOMPARE(css_color(QStringLiteral("nope"), 1), QStringLiteral("rgba(128,128,128,1)"));
    }
};

QTEST_MAIN(TstKrxChartStudies)
#include "tst_krx_chart_studies.moc"
