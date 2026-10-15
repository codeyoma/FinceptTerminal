#pragma once

#include "services/krx_quant/ChartModel.h"

#include <QList>
#include <QString>
#include <QVariant>

#include <array>
#include <optional>

/// How the KRX screen's chart looks, worked out here so that its toolbar and page only show it
/// (krx-quant #220, #221): the chart styles and the bars each draws, the baseline's base price,
/// the day separators' bars, and the chart settings each symbol keeps. Qt Core only.
namespace fincept::krx_quant {

/// The chart's styles, in the order of the toolbar's menu. Renko, kagi, point and figure and
/// line break have no time axis: they draw bricks in order (krx-quant #225).
enum class ChartStyle {
    Bar,
    Candle,
    Line,
    Area,
    Renko,
    Kagi,
    PointFigure,
    LineBreak,
    HeikinAshi,
    Hollow,
    Baseline,
};

inline constexpr std::array<ChartStyle, 11> kChartStyles{
    ChartStyle::Bar,        ChartStyle::Candle, ChartStyle::Line,        ChartStyle::Area,
    ChartStyle::Renko,      ChartStyle::Kagi,   ChartStyle::PointFigure, ChartStyle::LineBreak,
    ChartStyle::HeikinAshi, ChartStyle::Hollow, ChartStyle::Baseline,
};

/// The style's name in the settings and to the chart's page: "bar", "candle", "line", "area",
/// "renko", "kagi", "point_figure", "line_break", "heikin_ashi", "hollow", "baseline".
QString style_key(ChartStyle style);
/// The style named ``key`` (style_key); none for any other text.
std::optional<ChartStyle> parse_style(const QString& key);
/// The style's name on the menu: 바, 캔들, 라인, 영역, 렌코, 카기, 포인트 앤 피겨, 라인 브레이크,
/// 하이킨아시, 투명캔들, 베이스라인.
QString style_title(ChartStyle style);
/// Whether ``style`` draws bricks in order rather than bars in time.
bool timeless(ChartStyle style);
/// Whether this build draws ``style``: every style with a time axis (the others come with
/// krx-quant #225).
bool drawable(ChartStyle style);

/// The chart's views (krx-quant #109, #111, #112): kMinuteViews, then the day, week, month and
/// year views.
bool valid_view(int view);

/// What a symbol's chart keeps (krx-quant #221): its view and style. A symbol opened for the
/// first time shows 1-minute candles.
struct ChartPrefs {
    int view = 1;
    ChartStyle style = ChartStyle::Candle;
};

/// Where a symbol's chart settings are kept in the app settings: "krx_quant/chart/005930/"
/// (krx-quant #221), followed by "view" or "style". The settings later issues keep for a symbol
/// (indicators, pane heights, drawings, the anchored VWAP) go under the same group.
QString prefs_group(const QString& symbol);
inline constexpr auto kPrefsView = "view";
inline constexpr auto kPrefsStyle = "style";
/// Whether the minute views' day separators are drawn, for every symbol (krx-quant #221).
inline constexpr auto kDaySeparatorKey = "krx_quant/chart_day_separator";

/// A symbol's settings from what was kept (``view``, ``style``: invalid when none): a view not
/// among the chart's, or a style unknown or not drawn by this build, is its default.
ChartPrefs parse_prefs(const QVariant& view, const QVariant& style);

/// ``bars`` as Heikin-Ashi bars, oldest first: each closes at the mean of its bar's open, high,
/// low and close, opens halfway between the Heikin-Ashi bar before's open and close (the first
/// halfway between its own open and close), and spans its bar's high and low and its own open
/// and close. The time and volume stay the bar's; ``rising`` is the Heikin-Ashi bar's.
QList<ChartBar> heikin_ashi(const QList<ChartBar>& bars);

/// The baseline style's base price (krx-quant #221). In a minute view, the day before's close:
/// the close of the last bar before the latest day's first one (none read: the first bar's
/// open). In a candle view, the close of the first bar in view, the first starting at or after
/// ``first_in_view`` (chart_time; none: the first bar's; past the last bar: the last's). None
/// without bars.
std::optional<double> baseline_base(const QList<ChartBar>& bars, int view, std::optional<qint64> first_in_view);

/// What the chart is drawn as besides its data (krx-quant #221): the style, and whether a minute
/// view fills its days' first bars.
struct ChartLook {
    ChartStyle style = ChartStyle::Candle;
    bool day_separator = true;
};

/// The bars a minute view's day separators fill (krx-quant #221), by time (chart_time): each
/// bar that starts a new KST date after the bar before it. The first bar read is one when it
/// starts by 09:00, as a day's first bar does (08:00 with NXT, 09:00 without); a later one, a day
/// read from its middle, is not. None in a candle view.
QList<qint64> day_starts(const QList<ChartBar>& bars, int view);

} // namespace fincept::krx_quant
