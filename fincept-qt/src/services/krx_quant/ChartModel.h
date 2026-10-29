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

/// The bar close of the 1-minute bar that starts at ``time`` (chart_time): a minute on, in KST, as
/// the engine names it (krx-quant #249).
QDateTime bar_close_of(qint64 time);

/// A 1-minute bar as the chart draws it.
struct ChartBar {
    qint64 time = 0; ///< its start, as chart_time
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    double volume = 0;
    bool rising = true; ///< closed at or above its open: drawn in the rising color
    /// An empty bar (krx-quant #236, ChartSlots' fill_slots): a minute view's slot without trades
    /// between two bars of a day. Its time alone counts; it has no prices and no volume.
    bool empty = false;
    bool operator==(const ChartBar&) const = default;
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

/// Where a touch target was first reached, as the chart draws it: its time (chart_time, to the
/// second) and the quote then.
struct ChartPoint {
    qint64 time = 0;
    double price = 0;
};

/// A 1-minute bar's hover box (krx-quant #159) and its four-minute window (krx-quant #167): the
/// span from its bar close to four minutes on, the touch targets over it and where they were
/// first reached; the targets only once the touch label is in. While the range is shown
/// (krx-quant #182, KindView's kind_hovers), the two lines are its highest and lowest
/// mid-points instead, and where each was first received; while the barrier is (krx-quant
/// #185), its barriers U and L and where each was first reached.
struct BarHover {
    qint64 time = 0; ///< the bar's start, as chart_time
    QStringList lines;
    qint64 window_from = 0; ///< its bar close, as chart_time
    qint64 window_to = 0;   ///< four minutes on (its target time)
    std::optional<double> upper;
    std::optional<double> lower;
    std::optional<ChartPoint> upper_reached;
    std::optional<ChartPoint> lower_reached;
};

/// What happened after a bar close: "정답 상승 (09:32:30 닿음)", the touch target and when it
/// was reached (krx-quant #170); "실제 상승 +500원" from an engine before it; or why it is not
/// known yet ("채점 전", "채점 불가 (why)"); " · 특수 상황" when one touched its four minutes.
/// The hover box's first line and the bar window's (krx-quant #255).
QString truth_text(const Truth& truth);

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

/// How the selected models' consensus did over the bars in view (krx-quant #159, #166): the
/// icons scored there whose 4 minutes are over, special situations left out; how many were
/// right, how many "늘 보합" (always flat) would have been on the same bars, how the up and
/// down calls did, on how many trading days, and how many special bars were left out.
struct SelectionScore {
    int n = 0;
    int hits = 0;
    int flat_hits = 0;
    int up_n = 0;      ///< icons that predicted up
    int up_hits = 0;   ///< of those, the ones right
    int down_n = 0;    ///< icons that predicted down
    int down_hits = 0; ///< of those, the ones right
    int days = 0;      ///< trading days (KST) of the bars counted: the overlapping windows are not independent
    int special = 0;   ///< scored bars in view a special situation touched, left out
};

/// The score of ``marks`` (the selected consensus's) whose bars start from ``from`` to ``to``
/// (as chart_time), at the engine clock ``now``.
SelectionScore score_of(const QList<Mark>& marks, qint64 from, qint64 to, const QDateTime& now);

/// What the chart draws for ``selection`` (krx-quant #166): "선택 합의 (3)" for its models'
/// consensus, a baseline's name when it is the only one selected; empty when nothing is drawn
/// (several baselines alone).
QString selection_head(const QStringList& selection);

/// The chart header's score, e.g. "선택 합의 (3) · 맞음 52% · 늘 보합 61% · ▲ 맞음 40% (52) ·
/// ▼ 맞음 45% (60) · 310개 4거래일 · 특수 12개 제외".
QString score_text(const QString& head, const SelectionScore& score);

/// Whether ``predictor`` can be selected for the chart (krx-quant #159): an algorithm or a
/// baseline, not a consensus.
bool selectable(const QString& predictor);

/// ``selection`` with ``predictor`` on or off, in name order, once each.
QStringList toggled(QStringList selection, const QString& predictor, bool on);

/// A selection as kept in the settings ("gauss,markov"), and back: unknown kinds dropped.
QString selection_value(const QStringList& selection);
QStringList parse_selection(const QString& saved);

/// The most bar closes the engine gives the predictions of in one read (its ``limit``).
inline constexpr int kPredictionPage = 1000;

/// Where predictions were read from once a page before came back empty: the engine has none
/// before, so every one is read (krx-quant #239). Before any bar close.
QDateTime from_the_start();

/// The 1-minute view's icons need the predictions of the bar closes held: every predictor's
/// with models selected (those read with the final consensus alone are read again, krx-quant
/// #159), else the final consensus's and "늘 보합"'s. They are read apart from the bars, a page
/// at a time from the latest back (krx-quant #239). ``from`` is the earliest bar close from
/// which on they were read (invalid: none; from_the_start: all), ``oldest_close`` the first
/// bar's close. The next page to read: its ``before`` (invalid: before now); none once they
/// reach the first bar or nothing is shown. ``ahead`` (bars before the first are being read):
/// the page before ``from`` even once they reach the first bar, read along with those bars.
std::optional<QDateTime> marks_before(const QDateTime& from, const QDateTime& oldest_close, bool ahead = false);

/// ``full_from`` once a page of every predictor's predictions (``page``) read before it came
/// in: its earliest bar close, if earlier; from_the_start when the page is empty (none before it).
QDateTime full_from_after(const QDateTime& full_from, const QList<Prediction>& page);

/// Likewise ``finals_from`` once a page of the final consensus's and "늘 보합"'s, read at once
/// before it, came in (krx-quant #239): the later of their earliest bar closes, as far back
/// as both reach; one with none in the page has none before, so the other's.
QDateTime finals_from_after(const QDateTime& finals_from, const QList<Prediction>& page);

// ---- The trader's buys and sells (krx-quant #398) -------------------------------------------

/// The symbol the trader trades (krx-quant #393): only its chart reads and marks them.
inline constexpr const char* kTraderSymbol = "005930";
/// The most marks the engine gives in one read (its ``limit``).
inline constexpr int kTraderPage = 1000;

/// ``kept`` (the marks read so far) with ``page`` read over them, newest first: a mark in both (the
/// same trade and side) is the page's.
QList<TraderMark> join_trader_marks(const QList<TraderMark>& kept, const QList<TraderMark>& page);

/// The marks are read apart from the bars, a page at a time from the latest back to the 1-minute bar
/// starting at ``first`` (the first held), as the icons' predictions are (marks_before). ``from`` is
/// the earliest moment from which on they were read (invalid: none; from_the_start: all). The next
/// page to read: its ``before`` (invalid: before now); none without a bar or once they reach the
/// first bar.
std::optional<QDateTime> trader_before(const QDateTime& from, const QDateTime& first);

/// ``from`` once a page read with ``limit`` (``page``) came in: its earliest mark's time, if earlier;
/// from_the_start once the page is short of ``limit`` (the engine has none before it).
QDateTime trader_from_after(const QDateTime& from, const QList<TraderMark>& page, int limit = kTraderPage);

/// A buy or sell as the chart draws it, on the 1-minute bar its fill came in: a buy ▲ under the bar,
/// a sell ▼ over it (over the consensus's icon too), coloured by whether it was the live account's.
struct TraderIcon {
    qint64 time = 0; ///< the bar's start, as chart_time
    bool buy = true;
    bool live = false; ///< the live account's; else paper or KIS's mock trading
    QString id;        ///< the marker's: its trade and side, "t12b", "t12s"
    bool operator==(const TraderIcon&) const = default;
};

/// The icons of ``marks``, in time order.
QList<TraderIcon> trader_icons(const QList<TraderMark>& marks);

/// A mark's line in its bar's hover box: "자동 매매 매수 70,100원 × 14주 · 모의", "자동 매매 매도
/// 70,250원 × 14주 · 익절 · 순손익 -162원 · 실전" (모의 for paper, KIS 모의 for KIS's mock trading).
QString trader_line(const TraderMark& mark);

/// ``hovers`` (the 1-minute bars' boxes) with each of ``marks``' lines last in its bar's box, in time
/// order; a bar without a box gets one of its lines alone.
QList<BarHover> with_trader_lines(QList<BarHover> hovers, const QList<TraderMark>& marks);

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
    /// Its expected move in ticks and its model version (the bar window's, krx-quant #255); none and
    /// empty for the selected models' consensus, which has neither.
    std::optional<double> expected_ticks{};
    QString model_version{};
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

/// Bars of the view before its left end, at least, under which the bars before are read
/// (krx-quant #108): fewer than the view's width of them (a screen, krx-quant #239).
inline constexpr double kOlderMargin = 30;
/// The bars before the view's left end under which the bars before are read, for a view
/// ``width`` bars wide: a screen's, at least kOlderMargin.
double older_margin(double width);
/// 1-minute bars read before at a time (krx-quant #109, #239): kOlderViewBars of the view…
inline constexpr int kOlderViewBars = 300;
/// …at least kOlderBars (the 1-minute view's: more than a trading day of 08:00–20:00, 720 bars)…
inline constexpr int kOlderBars = 1000;
/// …and at most the engine's most in one read (an hour view's 300 are 18,000).
inline constexpr int kMaxOlderBars = 20000;

/// The 1-minute bars to read before the chart's (krx-quant #108). Their predictions are read
/// apart (krx-quant #239, marks_before).
struct OlderRead {
    QDateTime before;       ///< bars that start before it; invalid: before now (no bars yet)
    int limit = kOlderBars; ///< 1-minute bars
};

/// What to read once the view of ``minutes`` starts ``view_from`` of its bars (a logical
/// index, negative past the first bar) from its left end and is ``width`` bars wide, where the
/// 1-minute bar starting at ``first`` is: nothing while older_margin(width) bars or more are
/// before its left end, else the 1-minute bars before it: kOlderViewBars of the view, at least
/// kOlderBars and at most kMaxOlderBars. With no bars yet (``first`` invalid), those before now.
std::optional<OlderRead> older_read(const QDateTime& first, double view_from, int minutes = 1, double width = 0);

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
/// negative past the first) from its left end and is ``width`` candles wide, where the first
/// candle starts at ``first``: those before it, once fewer than older_margin(width) are before
/// its left end (krx-quant #239); none while more. With no candles yet (``first`` invalid), the
/// latest: an invalid time, before now.
std::optional<QDateTime> older_candles(const QDateTime& first, double view_from, double width = 0);

/// The last bar of the ``minutes`` view of ``bars`` (1-minute bars) as the chart draws it, to
/// move it as it is made; none without bars. A candle view's last candle is its own list's.
std::optional<ChartBar> last_chart_bar(const QList<Bar>& bars, int minutes);

/// ``older`` read before ``newer``, as one run of bars oldest first: a bar in both (the
/// same start) is ``newer``'s.
QList<Bar> join_bars(const QList<Bar>& older, const QList<Bar>& newer);

} // namespace fincept::krx_quant
