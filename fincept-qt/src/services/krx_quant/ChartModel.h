#pragma once

#include "services/krx_quant/Board.h"
#include "services/krx_quant/Signals.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>

#include <array>
#include <optional>
#include <utility>

/// What the KRX screen's chart draws, worked out here so that its page only draws
/// (krx-quant #105, #106): the bars, and the final consensus's icons, the bars they sit on,
/// their hover lines, the bars they aim at, and which were signals. Qt Core only.
namespace fincept::krx_quant {

/// Bars kept free to the right of the last one when the chart is fitted: the latest
/// prediction's target bar is four after it, and its slot must show (krx-quant #106).
inline constexpr double kRightOffset = 6;
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

/// A row of a bar close's judgements, the prediction table's while an icon is chosen
/// (krx-quant #107).
struct CloseRow {
    QString algorithm;
    QString name; ///< as the screen names it (predictor_label)
    bool consensus = false;
    bool baseline = false;
    QString status; ///< its lifecycle status in Korean; empty for a consensus or a baseline
    std::optional<double> p_up;
    std::optional<double> p_flat;
    std::optional<double> p_down;
    QString direction;    ///< its likeliest class (a tie goes flat), or why it made no forecast
    QString in_consensus; ///< "포함", "제외" or "알 수 없음" for a model; empty for the others
    QString result;       ///< "✓ 맞음", "✗ 틀림", "채점 전" or "채점 불가" on ``basis``
};

/// A bar close's judgements (``GET …/predictions?bar_close=``) as the table shows them: the
/// consensuses (fast, final), the models by name, then the baselines; each model with its
/// status now (``statuses``, from /v1/algorithms) and whether the final consensus pooled it.
QList<CloseRow> close_rows(const QList<Prediction>& predictions, const QMap<QString, QString>& statuses,
                           const QString& basis);

/// The logical range (bar indexes) a fitted chart of ``bars`` bars shows: the last
/// kFitBars, and kRightOffset free after them.
std::pair<double, double> fit_range(int bars);

/// The views the timeframe buttons pick: bars of that many minutes (krx-quant #109). Each
/// divides a day, so a bar of the view never spans two.
inline constexpr std::array<int, 9> kMinuteViews{1, 3, 5, 10, 15, 30, 60, 120, 240};

/// 1-minute bars as bars of ``minutes`` (one of kMinuteViews), oldest first: each starts on
/// the clock in KST (60: 09:00, 10:00…; 240: 08:00, 12:00, 16:00) and holds the 1-minute
/// bars that start in it: the first's open, the highest high, the lowest low, the last's
/// close, the volumes summed. Minutes without a bar are skipped; a bar of the view without
/// any is not there.
QList<Bar> group_bars(const QList<Bar>& bars, int minutes);

/// Bars of the view to the left end of the view at which the bars before are read (krx-quant #108).
inline constexpr double kOlderMargin = 30;
/// 1-minute bars read before at a time in the 1-minute view: more than half a trading day
/// (08:00–20:00, 720 bars).
inline constexpr int kOlderBars = 400;
/// Bars of a wider view read before at a time, as 1-minute bars (krx-quant #109)…
inline constexpr int kOlderViewBars = 100;
/// …up to the most final consensus predictions the engine gives in one read (a bar's at most one).
inline constexpr int kMaxOlderBars = 1000;

/// The bars to read before the chart's (krx-quant #108), with their final consensus.
struct OlderRead {
    QDateTime before;       ///< bars that start before it; invalid: before now (no bars yet)
    int limit = kOlderBars; ///< 1-minute bars, and final consensus predictions
    /// Final consensus predictions whose bar close is before it: the bars' own, as a bar's
    /// close is its start a minute on. Invalid with ``before``.
    QDateTime finals_before;
};

/// What to read once the view of ``minutes`` starts ``view_from`` of its bars (a logical
/// index, negative past the first bar) from the left end of ``bars`` (1-minute bars): nothing
/// while it is kOlderMargin bars or more away, else the 1-minute bars before the first:
/// kOlderBars, or kOlderViewBars of a wider view, at most kMaxOlderBars. With no bars yet,
/// those before now.
std::optional<OlderRead> older_read(const QList<Bar>& bars, double view_from, int minutes = 1);

/// ``older`` read before ``newer``, as one run of bars oldest first: a bar in both (the
/// same start) is ``newer``'s.
QList<Bar> join_bars(const QList<Bar>& older, const QList<Bar>& newer);

} // namespace fincept::krx_quant
