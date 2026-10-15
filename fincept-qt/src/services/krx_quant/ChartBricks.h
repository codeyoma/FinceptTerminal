#pragma once

#include "services/krx_quant/ChartLook.h"
#include "services/krx_quant/ChartModel.h"

#include <QList>

#include <optional>

/// The KRX chart's styles without a time axis (krx-quant #220, #225), worked out here so that the
/// chart's page only draws them: renko, kagi, point and figure and line break made of the view's
/// bars, their sizes, the bar they start from, and the bricks as the page and the indicators take
/// them. Qt Core only.
///
/// Each reads the bars' closes, oldest first. A brick holds the bars that made it: those after the
/// brick before, up to the one that ended it. Its time is when it ended, the start of that bar (as
/// the time axis names a bar), and its volume the sum of those bars' volumes.
namespace fincept::krx_quant {

/// The ATR's period for the default box and reversal: ATR(14) of the view's bars.
inline constexpr int kBrickAtr = 14;
/// The boxes a point and figure column needs to turn.
inline constexpr int kFigureReversal = 3;
/// The view's bars the bricks start with at least (three first screens): the bars before them are
/// read first, unless the engine has none (brick_start).
inline constexpr int kBrickBars = 3 * kFitBars;

/// A brick of a style without a time axis: a renko box, a line break's line, a kagi line or a point
/// and figure column. ``open`` is where it starts and ``close`` where it ends (a column's first and
/// last box); ``high`` and ``low`` the higher and the lower.
struct Brick {
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    double volume = 0; ///< the volumes of the bars that made it
    qint64 time = 0;   ///< when it ended: the start of the last bar it holds (chart_time)
    bool rising = true;
    /// A kagi line: thick (yang) where it starts. A line turns thick when it rises past the shoulder
    /// before (the top of the rising line before), thin when it falls past the waist before.
    bool thick = false;
    std::optional<double> turn; ///< a kagi line: the price it turns thick or thin at; none: it does not
    bool operator==(const Brick&) const = default;
};

/// ATR(14) of ``bars`` (the view's, ChartStudies' average_true_range) but the last, its last value
/// to the won (at least 1): the renko and point and figure box and the kagi reversal unless one is
/// set. The last bar may be the one being made: left out, the size stays while it moves, whatever
/// draws the chart again meanwhile, and changes only with the bars before it (a new bar, which ends
/// the one before; the bars before the first read; another view or symbol). None with fewer than 14
/// bars before the last.
std::optional<double> atr_size(const QList<ChartBar>& bars);
/// The size ``style`` draws with: the one set (``sizes``), else ``atr`` (atr_size), the line break's
/// kBreakLines. None for a style with a time axis, or without an ATR.
std::optional<double> brick_size(ChartStyle style, const BrickSizes& sizes, std::optional<double> atr);

/// Renko of the closes, ``box`` won a brick. The first close is where it starts. A close a box or
/// more past the last brick's top adds rising bricks (as many boxes as it went); one a box or more
/// past its bottom adds falling ones: a reversal takes two boxes from the last brick's end. A bar that
/// ends several bricks shares the volume they hold evenly between them. The bars after the last brick
/// make none yet.
QList<Brick> renko_bricks(const QList<ChartBar>& bars, double box);
/// Kagi of the closes, turning at ``reversal`` won. The first line starts at the first close once a
/// close is a reversal away from it. A line goes on while the closes go on its way (its end at the
/// furthest), and a close a reversal back from its end starts the next line there. The last line is
/// the one being drawn, with the bars since it started.
QList<Brick> kagi_lines(const QList<ChartBar>& bars, double reversal);
/// Point and figure of the closes: boxes of ``box`` won at its multiples (X rising, O falling), a
/// column turning on ``reversal`` boxes. The first column starts at the first close's box once a
/// close is a box past it. A column goes on by whole boxes the close reaches, and a close
/// ``reversal`` boxes back from its last starts the next column a box back from it. The last column
/// is the one being drawn.
QList<Brick> figure_columns(const QList<ChartBar>& bars, double box, int reversal = kFigureReversal);
/// Line break of the closes, ``lines`` lines (three-line break). The first line goes from the first
/// close to the first close apart from it. A close past the last line's end adds a line from there
/// on its way; one past the lowest (highest) of the last ``lines`` lines turns, from the last line's
/// start. A bar adds at most one line; the bars after the last make none yet.
QList<Brick> break_lines(const QList<ChartBar>& bars, int lines = kBreakLines);
/// ``style``'s bricks of ``bars`` at ``size`` (brick_size); none for a style with a time axis.
QList<Brick> make_bricks(ChartStyle style, const QList<ChartBar>& bars, double size);

/// Where the bricks start (the first bar they are made of), fixed once for a symbol and view so that
/// reading the bars before does not change them: ``kept`` while ``bars`` (the view's, oldest first)
/// still hold every bar from it. Otherwise their first bar once they are enough to start from: at
/// least kBrickBars, or every bar the engine has (``start_whole``), or kMaxHistoryBars. None while
/// they are being read: no bricks yet.
std::optional<qint64> brick_start(const QList<ChartBar>& bars, std::optional<qint64> kept, bool start_whole);
/// ``bars`` from the one starting at ``start`` on.
QList<ChartBar> bars_from(const QList<ChartBar>& bars, qint64 start);

/// The time the page draws the ``index``th brick at: bricks are drawn in order, a slot each, and
/// several end on one bar. Slots are a minute apart from 00:01 UTC on 1 January 1970, so a line
/// reaching past the last brick (the Ichimoku's leading spans) takes the next as a 1-minute view's.
qint64 brick_slot(qsizetype index);
/// The bricks as bars on their slots (brick_slot), as the page draws them and the indicators count
/// on them (ChartStudies' draw_studies with ``bricks``).
QList<ChartBar> brick_bars(const QList<Brick>& bricks);

} // namespace fincept::krx_quant
