#pragma once

#include "services/krx_quant/ChartModel.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <array>
#include <optional>
#include <utility>

/// The KRX chart's indicators (krx-quant #220, #222), worked out here so that the indicators
/// panel and the chart's page only show them: the indicators and their settings, each symbol's
/// as kept, their values over the bars, what the page draws of them and its legend, and how
/// many bars before the first screen to read for them. Qt Core only. Named apart from
/// upstream's ``fincept::ui::indicators`` (src/ui/charts/ChartIndicators), which is not used.
namespace fincept::krx_quant {

/// The chart's indicators, in the order of the panel's lists: those drawn over the prices,
/// then those drawn in a pane below (the volume for now; krx-quant #223 adds the others).
enum class Study {
    MovingAverage,
    Ichimoku,
    Bollinger,
    Supertrend,
    VolumeProfile,
    Envelope,
    Fractal,
    ExponentialAverage,
    ParabolicSar,
    PriceChannel,
    Vwap,
    AnchoredVwap,
    Volume,
};

inline constexpr std::array<Study, 12> kUpperStudies{
    Study::MovingAverage, Study::Ichimoku,     Study::Bollinger, Study::Supertrend,
    Study::VolumeProfile, Study::Envelope,     Study::Fractal,   Study::ExponentialAverage,
    Study::ParabolicSar,  Study::PriceChannel, Study::Vwap,      Study::AnchoredVwap,
};
inline constexpr std::array<Study, 1> kLowerStudies{Study::Volume};
inline constexpr std::array<Study, 13> kStudies{
    Study::MovingAverage, Study::Ichimoku,     Study::Bollinger, Study::Supertrend,
    Study::VolumeProfile, Study::Envelope,     Study::Fractal,   Study::ExponentialAverage,
    Study::ParabolicSar,  Study::PriceChannel, Study::Vwap,      Study::AnchoredVwap,
    Study::Volume,
};

/// The price an indicator reads of each bar (기준값).
enum class Source { Close, Open, High, Low, HL2, HLC3 };
inline constexpr std::array<Source, 6> kSources{Source::Close, Source::Open, Source::High,
                                                Source::Low,   Source::HL2,  Source::HLC3};
/// "close", "open", "high", "low", "hl2", "hlc3": its name in the settings.
QString source_key(Source source);
std::optional<Source> parse_source(const QString& key);
/// 종가, 시가, 고가, 저가, (고가+저가)/2, (고가+저가+종가)/3.
QString source_title(Source source);
double price_of(const ChartBar& bar, Source source);
QList<double> source_values(const QList<ChartBar>& bars, Source source);

/// A number the panel edits: a period, a multiplier, a share.
struct StudyInput {
    QString title;
    double value = 0;
    double min = 1;
    double max = 1;
    double step = 1;
    int decimals = 0; ///< 0: a whole number
};

/// A line's look (or a mark's): its colour ("#rrggbb") and width (1–4; 0: not set, the marks and
/// the volume profile's bars).
struct StudyLine {
    QString title;
    QString color;
    int width = 1;
};

/// What the panel and the page know of an indicator.
struct StudySpec {
    Study study = Study::MovingAverage;
    QString key;   ///< its name in the settings and to the page: "ma", "ichimoku", …
    QString title; ///< its name in the panel and the legend: 이동평균선, …
    QString about; ///< one line under its name in the panel
    QString help;  ///< the panel's `?`: how it is worked out
    bool upper = true;
    /// One line per period, periods added and removed (기간 추가, X): the moving averages.
    bool periods = false;
    bool minute_only = false;     ///< drawn in the minute views only (the VWAP)
    std::optional<Source> source; ///< its default source; none: it reads none
    QList<StudyInput> inputs;     ///< with their defaults; the periods' with ``periods``
    QList<StudyLine> lines;       ///< with their defaults; one per period with ``periods``
};

const StudySpec& study_spec(Study study);
/// "ma", "ichimoku", "bollinger", "supertrend", "volume_profile", "envelope", "fractal", "ema",
/// "psar", "price_channel", "vwap", "anchored_vwap", "volume".
QString study_key(Study study);
std::optional<Study> parse_study(const QString& key);
QString study_title(Study study);

/// The periods (moving averages) an indicator may have at most.
inline constexpr int kMaxPeriods = 8;

/// What an indicator is set to for a symbol.
struct StudySettings {
    Study study = Study::MovingAverage;
    bool on = false;
    bool hidden = false;          ///< on, but not drawn (the legend's eye)
    std::optional<Source> source; ///< none for one that reads no source
    QList<double> inputs;         ///< as the spec's; the periods of the moving averages
    QList<StudyLine> lines;       ///< as the spec's; one per period of the moving averages
    bool operator==(const StudySettings& other) const;
};

/// ``study``'s defaults (off).
StudySettings default_settings(Study study);
/// The settings' text in the legend and the panel: "5 20 60 120", "20 2", "20 10%"; the source
/// after them when it is not the default ("5 20 고가").
QString settings_text(const StudySettings& settings);

/// A moving average's next line (기간 추가): the first of 5, 10, 20, 60, 120, 200, 240, 480 it has
/// not, in the next colour; none past kMaxPeriods.
bool add_period(StudySettings& settings);
/// Drop a moving average's ``index``th line (X); the first stays.
bool remove_period(StudySettings& settings, int index);

/// Every indicator's settings for a symbol: each once, those on first in the order they were
/// turned on (krx-quant #223 stacks the lower panes in that order), then the others.
struct StudySet {
    QList<StudySettings> studies;

    const StudySettings& of(Study study) const;
    StudySettings& of(Study study);
    bool on(Study study) const { return of(study).on; }
    /// Turn ``study`` on (after those on) or off; its settings stay.
    void turn(Study study, bool on);
    bool operator==(const StudySet& other) const { return studies == other.studies; }
};

/// A symbol opened for the first time (krx-quant #220): the moving averages 5, 20, 60 and 120 of
/// the close and the volume on, every other indicator off at its defaults.
StudySet default_studies();
/// Where a symbol keeps its indicators: krx_quant/chart/<symbol>/ + kPrefsStudies (JSON, studies_value).
inline constexpr auto kPrefsStudies = "indicators";
/// Where a symbol keeps the anchored VWAP's anchor: krx_quant/chart/<symbol>/ + kPrefsAnchor,
/// the moment as ISO 8601 (KST).
inline constexpr auto kPrefsAnchor = "vwap_anchor";
/// The set as kept: a JSON array of {"study", "on", "hidden", "source", "inputs", "lines":
/// [{"color", "width"}]}, in the set's order.
QString studies_value(const StudySet& set);
/// The set kept (invalid: none kept, a symbol opened for the first time): default_studies() for
/// none or a value that is not such an array; an indicator not in it, or unknown, at its fresh
/// default; a broken setting (unknown source, a number out of its range, a colour that is not
/// #rrggbb, a width out of 1–4) at its default.
StudySet parse_studies(const QVariant& kept);

// The values, one per bar (oldest first), none where too few bars come before.
using Values = QList<std::optional<double>>;

/// The simple moving average of ``period`` values.
Values moving_average(const QList<double>& values, int period);
/// The exponential moving average (2 / (period + 1)), starting at the simple average of the
/// first ``period`` values.
Values exponential_average(const QList<double>& values, int period);
/// The average true range: Wilder's average of the true ranges (the first bar's is its high
/// minus its low), starting at the simple average of the first ``period``.
Values average_true_range(const QList<ChartBar>& bars, int period);

/// A line with two around it.
struct Band {
    Values middle;
    Values upper;
    Values lower;
};
/// The Bollinger bands: the simple average of ``period`` values, and ``deviations`` population
/// standard deviations of those values above and below.
Band bollinger_band(const QList<double>& values, int period, double deviations);
/// The envelope: the simple average of ``period`` values, and ``percent`` % above and below it.
Band envelope_band(const QList<double>& values, int period, double percent);
/// The price channel: the highest high and the lowest low of the last ``period`` bars (this
/// one's included); its middle is none.
Band price_channel(const QList<ChartBar>& bars, int period);

/// The Ichimoku cloud. The conversion and base lines are the midpoints of the highest high and
/// the lowest low over ``conversion`` and ``base`` bars. The lagging span is each bar's close
/// ``displacement`` bars back (none for the last ``displacement``). The leading spans, the
/// midpoint of those two lines and the midpoint over ``lead`` bars, are drawn ``displacement``
/// bars on: they hold one value per bar and then ``displacement`` more, past the last bar.
struct Ichimoku {
    Values conversion;
    Values base;
    Values lagging;
    Values lead_a;
    Values lead_b;
};
Ichimoku ichimoku_cloud(const QList<ChartBar>& bars, int conversion, int base, int lead, int displacement);

/// The supertrend (as TradingView's ta.supertrend): bands ``multiplier`` average true ranges
/// around (high + low) / 2 that only tighten while the close stays inside; the lower one under
/// an uptrend (``up``), the upper one over a downtrend (``down``). The trend turns when the
/// close crosses the line; it starts down.
struct Supertrend {
    Values up;
    Values down;
};
Supertrend supertrend_lines(const QList<ChartBar>& bars, int atr_period, double multiplier);

/// A Williams fractal: a bar whose high (``up``) or low is above (below) those of the
/// ``period`` bars on each side.
struct Fractal {
    qsizetype index = 0;
    bool up = true;
};
QList<Fractal> williams_fractals(const QList<ChartBar>& bars, int period);

/// The parabolic SAR (as TradingView's ta.sar), from the second bar on: the trend starts up
/// when the second close is above the first.
Values parabolic_sar(const QList<ChartBar>& bars, double start, double step, double maximum);

/// The VWAP of each day: the sum of ``source`` × volume over the sum of volumes, from the first
/// bar of each KST date (the day separators' bar) on; none until a day has volume. Only a whole
/// day has one: every date after the first held (a date before it is held), and the first only
/// when ``first_day_whole`` (nothing before it to read). A day read from its middle would give a
/// wrong VWAP that moves as the bars before are read, so it stays empty (krx-quant #222).
Values session_vwap(const QList<ChartBar>& bars, Source source, bool first_day_whole = false);
/// The VWAP from the bar ``anchor`` on; none before it.
Values anchored_vwap(const QList<ChartBar>& bars, Source source, qsizetype anchor);
/// The bar of the view ``view`` that holds the moment ``anchor`` (chart_time): the first bar
/// starting at or after the start of the view's bar that would hold it (a minute view's slot
/// on the clock, a day, its week, month or year). None when that is before the first bar
/// (not read) or after the last.
std::optional<qsizetype> anchor_index(const QList<ChartBar>& bars, qint64 anchor, int view);

/// A volume profile: the bars' volumes over ``rows`` equal price rows from the lowest low to
/// the highest high, each bar's spread over the rows its low to high crosses by how much of it
/// each holds (all in one row when its high is its low). Buying and selling are not apart.
struct VolumeProfile {
    double low = 0;
    double high = 0;
    QList<double> volumes; ///< from the lowest row up
};
/// The profile of the bars starting from ``from`` to ``to`` (chart_time; the bars in view);
/// no rows without one.
VolumeProfile volume_profile(const QList<ChartBar>& bars, qint64 from, qint64 to, int rows);

/// The times of the ``count`` bars after the last one, as the view ``view`` would start them:
/// a minute view's on the clock, the next weekdays, weeks, months' 1st or years' 1 January.
QList<qint64> future_times(const QList<ChartBar>& bars, int view, int count);

/// The bars before its first value an indicator needs. A fixed look-back's is its longest period
/// less one. A recursive one depends on every bar before, so its warm-up is long enough that more
/// bars barely move the values shown (as on TradingView, they may still move a little as more
/// load): the EMA 4 × its longest period (the weight left beyond is about e^-8), the supertrend
/// 10 × its ATR period, the parabolic SAR 100 bars. The VWAP needs whole days instead
/// (first_screen_day), the anchored VWAP its anchor's bar.
int warmup(const StudySettings& settings);
/// The most bars of a view read for the indicators (the engine's largest ``limit``).
inline constexpr int kMaxHistoryBars = 2000;
/// The bars of the view ``view`` to read for the indicators on and shown there: the first
/// screen's (kFitBars) and the longest warm-up before them, at most kMaxHistoryBars. Zero when
/// none needs any.
int history_bars(const StudySet& set, int view);
/// The 1-minute bars to read before ``first`` (the first read; invalid: none yet) so that the
/// minute view of ``minutes`` holds ``wanted`` bars (it holds ``have``) and reaches back to
/// ``back_to`` (invalid: anywhere): the same read as older_read's at the left end; none once it
/// does, or once it holds kMaxHistoryBars.
std::optional<OlderRead> history_read(const QDateTime& first, int have, int wanted, int minutes,
                                      const QDateTime& back_to = {});
/// Where a minute view's bars must reach back to for the VWAP's days on its first screen to be
/// whole (krx-quant #222): 00:00 KST of the date of the first screen's first bar (the last kFitBars
/// bars of the view of ``minutes`` among ``bars``, 1-minute bars oldest first). Worked out once
/// from the first screen, not from the first bar held, so the reads do not keep moving back.
/// Invalid without bars.
QDateTime first_screen_day(const QList<Bar>& bars, int minutes);
/// Whether the anchored VWAP's bar can be chosen (again) by a click: it is on and shown. While it
/// is hidden nothing of it is drawn, so neither is the wait for a click (its panel button is off).
bool anchor_pickable(const StudySet& set);
/// Likewise for a candle view: the candles before ``first`` to read (its ``before``).
std::optional<QDateTime> history_candles(const QDateTime& first, int have, int wanted, const QDateTime& back_to = {});

/// A line the page draws: its series.
struct DrawnSeries {
    QString id; ///< "ma.0", "ichimoku.lead_a", "volume" (the volume pane's own)
    Study study = Study::MovingAverage;
    QString color;
    int width = 1;
    bool dots = false; ///< points without a line (the parabolic SAR)
    /// Its points by time up to its last value: a bar's time, or one of the times past the last
    /// bar; a missing value breaks the line.
    QList<std::pair<qint64, std::optional<double>>> points;
    /// Where the last bar's value lands on it, in bars from the last bar: the leading spans'
    /// ``displacement`` on, the lagging span's ``displacement`` back. The bar being made moves it.
    int live = 0;
};

/// Two lines with the space between them filled: ``up_color`` where the first is above.
struct DrawnFill {
    QString first;
    QString second;
    QString up_color;   ///< as CSS: rgba(…)
    QString down_color; ///< as CSS
};

/// A mark over (``above``) or under a bar: the fractals.
struct DrawnMark {
    qint64 time = 0;
    bool above = true;
    QString color;
};

/// A row of a pane's legend (krx-quant #220): the indicator's name, its settings and the values
/// of its lines at the crosshair (or the last bar); hovered, its buttons hide, set and drop it.
struct LegendRow {
    Study study = Study::MovingAverage;
    int pane = 0; ///< 0: the prices', 1: the volume's
    QString title;
    QString settings;
    bool hidden = false; ///< on but not drawn: dimmed, without values
    QStringList values;  ///< the series whose values follow, in order
    QStringList colors;  ///< theirs
    QString note;        ///< what it waits for (the anchored VWAP's anchor)
    /// Shown in place of the values where none of its lines has one: why (the VWAP of a day not
    /// read whole). Empty: nothing shown.
    QString missing;
};

/// What the page draws of the indicators over ``bars`` (as drawn, oldest first) in the view
/// ``view``: the lines, the fills between them, the fractals' marks, whether the volume profile
/// (worked out over the bars in view, volume_profile) and the volume pane are drawn, the
/// anchored VWAP's bar, and the legend. An indicator off, or hidden, draws nothing; a hidden one
/// keeps its legend row. The VWAP in a candle view is neither drawn nor in the legend; its
/// settings stay.
struct StudyDrawing {
    QList<DrawnSeries> series;
    QList<DrawnFill> fills;
    QList<DrawnMark> marks;
    bool profile = false;
    QString profile_color; ///< as CSS
    int profile_rows = 24;
    std::optional<qint64> anchor; ///< the anchored VWAP's bar (chart_time), its marker
    QString anchor_color;
    bool picking = false; ///< the next click on the chart anchors it
    bool volume = true;   ///< the volume pane is there
    bool volume_hidden = false;
    QList<LegendRow> legend;
};
/// ``anchor`` is the anchored VWAP's moment (chart_time; none: not chosen); ``picking`` whether a
/// click on the chart chooses it (it is turned on without one, or chosen again).
/// ``first_day_whole``: the first day held is whole, nothing before it to read (session_vwap).
StudyDrawing draw_studies(const QList<ChartBar>& bars, const StudySet& set, int view, std::optional<qint64> anchor,
                          bool picking, bool first_day_whole = false);

/// ``color`` (#rrggbb) at ``alpha`` (0–1) as CSS: rgba(r,g,b,a).
QString css_color(const QString& color, double alpha);

} // namespace fincept::krx_quant
