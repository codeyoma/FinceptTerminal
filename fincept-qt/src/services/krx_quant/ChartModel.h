#pragma once

#include "services/krx_quant/Board.h"
#include "services/krx_quant/Signals.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <utility>

/// What the KRX screen's chart draws, worked out here so that its page only draws
/// (krx-quant #105, #106): the bars, and the final consensus's icons, the bars they sit on,
/// their hover lines, the bars they aim at, and which were signals. Qt Core only.
namespace fincept::krx_quant {

/// Bars kept free to the right of the last one when the chart is fitted.
inline constexpr double kRightOffset = 3;
/// Bars a fitted chart shows.
inline constexpr int kFitBars = 60;

/// The chart's time for a moment: seconds since the epoch, 9 hours on, as the chart shows
/// its times in UTC and they should read in KST.
qint64 chart_time(const QDateTime& t);

/// A 1-minute bar as the chart draws it.
struct ChartBar {
    qint64 time = 0; ///< its start, as chart_time
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    double volume = 0;
    bool rising = true; ///< closed at or above its open: drawn in the rising color
};

QList<ChartBar> chart_bars(const QList<Bar>& bars);

/// A final consensus's icon, on the bar that closed when it was made.
struct ChartIcon {
    qint64 time = 0;     ///< that bar's start, as chart_time
    QDateTime bar_close; ///< the prediction's
    Direction direction = Direction::Flat;
    QString arrow;             ///< "▲" up, "▼" down, "–" flat
    QString result;            ///< "✓" hit, "✗" miss, "?" not scored yet or could not be
    bool faint = false;        ///< a VI, circuit breaker or sidecar touched its 4 minutes: left out of the stats
    QStringList hover;         ///< hover_lines
    qint64 target = 0;         ///< the bar it aims at (the one closing at its target time), as chart_time
    bool target_shown = false; ///< that bar is among the bars drawn
    /// Not drawn yet: how many bars after the last one it will be (a slot on the right).
    int target_after_last = 0;
    bool signal = false; ///< it was a signal under the screen's rule: drawn larger
};

/// The icons of ``marks`` over ``bars``, in time order: their results on ``basis`` ("a" or
/// "b", the leaderboard's) and their signals under ``rule`` (the screen's threshold and N).
QList<ChartIcon> chart_icons(const QList<Mark>& marks, const QList<Bar>& bars, const QString& basis,
                             const SignalRule& rule);

/// A final consensus icon's hover, five lines: its bar and target times; its class and three
/// probabilities; its members leaning that way of all (krx-quant #106); whether it was a
/// signal under ``rule``; and, once scored on ``basis``, the mid-point's move and the result.
QStringList hover_lines(const Mark& mark, const QString& basis, const SignalRule& rule);

/// The logical range (bar indexes) a fitted chart of ``bars`` bars shows: the last
/// kFitBars, and kRightOffset free after them.
std::pair<double, double> fit_range(int bars);

} // namespace fincept::krx_quant
