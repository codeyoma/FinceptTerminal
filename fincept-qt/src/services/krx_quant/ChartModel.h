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
/// (krx-quant #105, #106, #159): the bars; the icons of the final consensus or the selected
/// models' consensus, the bars they sit on and which were signals; each bar's hover box and
/// the bar it aims at; the selected consensus's score over the bars in view. Qt Core only.
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

/// A consensus's icon, on the bar that closed when it was made.
struct ChartIcon {
    qint64 time = 0;     ///< that bar's start, as chart_time
    QDateTime bar_close; ///< the prediction's
    Direction direction = Direction::Flat;
    QString arrow;       ///< "▲" up, "▼" down, "–" flat
    QString result;      ///< "✓" hit, "✗" miss, "?" not scored yet or could not be
    bool faint = false;  ///< a VI, circuit breaker or sidecar touched its 4 minutes: left out of the stats
    bool signal = false; ///< it was a signal under the screen's rule: drawn larger
};

/// The icons of ``marks``, in time order, and their signals under ``rule`` (the screen's
/// threshold and N): a consensus sure enough of a move that enough of its models lean that
/// way, unless the engine blocked its bar close; the selected models' consensus also not on
/// a bar a special situation touched (krx-quant #159).
QList<ChartIcon> chart_icons(const QList<Mark>& marks, const SignalRule& rule);

/// A 1-minute bar's hover box (krx-quant #159): its lines and the bar it aims at.
struct BarHover {
    qint64 time = 0; ///< the bar's start, as chart_time
    QStringList lines;
    qint64 target = 0;         ///< the bar closing at its bar close's target time, as chart_time
    bool target_shown = false; ///< that bar is among the bars drawn
    /// Not drawn yet: how many bars after the last one it will be (a slot on the right).
    int target_after_last = 0;
    std::optional<qint64> hit_time; ///< where the target was reached, as chart_time (krx-quant #160)
    std::optional<double> hit_price;
};

/// The hover box of the bar that closed at ``bar_close``, from its predictions: its bar and
/// target times and what happened; the selected models' consensus and each selected model
/// (``standings``: their statuses now, from the leaderboard); the final consensus last.
/// "예측 없음" alone when it has none.
QStringList hover_box(const QList<Prediction>& at_close, const QDateTime& bar_close, const QStringList& selection,
                      const QMap<QString, QString>& standings, const SignalRule& rule);

/// Each of ``bars``' hover box, from the predictions of their bar closes (``closes``, by
/// bar close: Board::closes).
QList<BarHover> bar_hovers(const QList<Bar>& bars, const QMap<QDateTime, QList<Prediction>>& closes,
                           const QStringList& selection, const QMap<QString, QString>& standings,
                           const SignalRule& rule);

/// How the selected models' consensus did over the bars in view (krx-quant #159): the
/// icons scored there whose 4 minutes are over, special situations left out; how many were
/// right, and how many "늘 보합" (always flat) would have been on the same bars.
struct SelectionScore {
    int n = 0;
    int hits = 0;
    int flat_hits = 0;
};

/// The score of ``marks`` (the selected consensus's) whose bars start from ``from`` to ``to``
/// (as chart_time), at the engine clock ``now``.
SelectionScore score_of(const QList<Mark>& marks, qint64 from, qint64 to, const QDateTime& now);

/// The chart header's score, e.g. "선택 합의 (3) · 맞음 52% · 늘 보합 61% (310개)".
QString score_text(int chosen, const SelectionScore& score);

/// Whether ``predictor`` can be selected for the chart (krx-quant #159): an algorithm or a
/// baseline, not a consensus.
bool selectable(const QString& predictor);

/// ``selection`` with ``predictor`` on or off, in name order, once each.
QStringList toggled(QStringList selection, const QString& predictor, bool on);

/// A selection as kept in the settings ("gauss,markov"), and back: unknown kinds dropped.
QString selection_value(const QStringList& selection);
QStringList parse_selection(const QString& saved);

/// With models selected, the predictions of the bar closes read must be every predictor's:
/// those read with the final consensus alone are read again (krx-quant #159), from the
/// latest back. ``full_from`` is the earliest bar close from which on all were read
/// (invalid: none), ``oldest_close`` the first bar's close. The next page to read: its
/// ``before`` (invalid: before now); none once they reach the first bar or nothing is shown.
std::optional<QDateTime> relabel_before(const QDateTime& full_from, const QDateTime& oldest_close);

/// ``full_from`` once a page of every predictor's predictions (``page``) read before it came
/// in: its earliest bar close; ``oldest_close`` when the page is empty (none before it).
QDateTime full_from_after(const QDateTime& full_from, const QList<Prediction>& page, const QDateTime& oldest_close);

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
    QString result;       ///< "✓ 맞음", "✗ 틀림", "채점 전" or "채점 불가" on the bar close's truth
};

/// A bar close's judgements (``GET …/predictions?bar_close=``) as the table shows them: the
/// selected models' consensus when models are selected (krx-quant #159), the consensuses
/// (fast, final), the models by name, then the baselines; each model with its status now
/// (``statuses``, from /v1/algorithms) and whether the final consensus pooled it. Each is
/// scored on the bar close's truth (truth_of).
QList<CloseRow> close_rows(const QList<Prediction>& predictions, const QMap<QString, QString>& statuses,
                           const QStringList& selection = {});

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
/// index, negative past the first bar) from its left end, where the 1-minute bar starting at
/// ``first`` is: nothing while it is kOlderMargin bars or more away, else the 1-minute bars
/// before it: kOlderBars, or kOlderViewBars of a wider view, at most kMaxOlderBars. With no
/// bars yet (``first`` invalid), those before now.
std::optional<OlderRead> older_read(const QDateTime& first, double view_from, int minutes = 1);

/// The bar being made (a stream "bar", krx-quant #110) on ``bars``, the day's 1-minute bars
/// oldest first: the same minute as the last bar, grown, replaces it; a later minute follows
/// it (the last one is complete then). An earlier minute, or the same minute smaller than
/// shown (sent before what was read), is dropped. Whether ``bars`` changed.
bool merge_live(QList<Bar>& bars, const Bar& live);

/// The day view's value among the views (krx-quant #111): a day's minutes, so a view's
/// value grows with what one of its bars spans. Its bars are the day candles the engine
/// stored (``GET …/candles?period=day``) and today's, made of the day's 1-minute bars.
inline constexpr int kDayView = 24 * 60;
/// The week, month and year views (krx-quant #112), likewise about their minutes. Their
/// bars are the finished candles the engine stored, then this period's (and any the engine
/// has not stored yet) made of the day view's candles.
inline constexpr int kWeekView = 7 * kDayView;
inline constexpr int kMonthView = 31 * kDayView;
inline constexpr int kYearView = 366 * kDayView;
/// Candles read at a time, first and as a candle view scrolls back.
inline constexpr int kCandleRead = 400;

/// Whether ``view`` draws candles (the day view or a longer one), not 1-minute bars.
inline constexpr bool candle_view(int view) {
    return view >= kDayView;
}

/// The engine's period of a candle view: "day", "week", "month" or "year".
QString candle_period(int view);

/// The day the candle of ``view`` holding ``day`` starts: the day, its week's Monday, its
/// month's 1st or its year's 1 January.
QDate period_start(const QDate& day, int view);

/// The week, month or year ``view``'s candles: ``stored`` (the engine's, of finished
/// periods, oldest first), then those made of ``days`` (the day view's candles, today's
/// included) for the periods after the last stored one: this period's, and one the engine
/// has not stored yet. Each starts at 00:00 KST of its period's first day.
QList<Bar> period_candles(const QList<Bar>& stored, const QList<Bar>& days, int view);

/// The candle of the last day of ``bars`` (1-minute bars, oldest first), starting at 00:00
/// KST that day: of all that day's bars, or, with ``venue`` "KRX" (the engine's candles are
/// the KRX regular session's), of those from 09:00 to 15:30 only. None without any.
std::optional<Bar> day_candle(const QList<Bar>& bars, const QString& venue);

/// The day view's candles: ``stored`` (the engine's, oldest first), then the candle of
/// ``day_bars``' day (day_candle), in place of a stored one of that day.
QList<Bar> day_candles(const QList<Bar>& stored, const QList<Bar>& day_bars, const QString& venue);

/// The candles to read once a candle view starts ``view_from`` of them (a logical index,
/// negative past the first) from its left end, where the first candle starts at ``first``:
/// those before it, once less than kOlderMargin away; none while further. With no candles
/// yet (``first`` invalid), the latest: an invalid time, before now.
std::optional<QDateTime> older_candles(const QDateTime& first, double view_from);

/// The last bar of the ``minutes`` view of ``bars`` (1-minute bars) as the chart draws it, to
/// move it as it is made; none without bars. A candle view's last candle is its own list's.
std::optional<ChartBar> last_chart_bar(const QList<Bar>& bars, int minutes);

/// ``older`` read before ``newer``, as one run of bars oldest first: a bar in both (the
/// same start) is ``newer``'s.
QList<Bar> join_bars(const QList<Bar>& older, const QList<Bar>& newer);

} // namespace fincept::krx_quant
