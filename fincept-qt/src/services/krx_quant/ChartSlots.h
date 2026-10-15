#pragma once

#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/// The minute views' time without trades (krx-quant #236), worked out here so that the chart's
/// page only draws it. A minute without trades has no bar, so a halt (a circuit breaker's 30
/// minutes) vanished from the chart and its time jumped. The minute views keep a slot for each
/// such minute (or N-minute slot) between a day's first and last bar: an empty bar, which takes
/// a slot but draws no candle and has no volume. Everything that places by time counts the
/// slots (the drawings, the hover boxes, the four-minute windows, the special situations'
/// bands); nothing that computes on prices does (the indicators, the bricks, the baseline's
/// base, the volume profile, the magnet). Over the slots the special situations the engine
/// knows (VIs of the symbol, circuit breakers and sidecars of the market) are drawn as faint
/// bands with their names. While the session is open (krx-quant #238), today's slots after the last
/// bar are filled up to the engine's clock too, so a halt in progress shows at its length as it
/// goes on, and its band grows with the clock. Qt Core only.
namespace fincept::krx_quant {

/// The engine's session today and its clock (GET /v1/status: ``session``, ``now``), as chart_time.
struct SessionClock {
    qint64 now = 0;
    qint64 open = 0;  ///< continuous trading's start
    qint64 close = 0; ///< the closing auction's end
    bool operator==(const SessionClock&) const = default;
};

/// The clock of ``status``'s session at ``now`` (the engine's clock); none without a status or a
/// session (a holiday, an engine before it gave one).
std::optional<SessionClock> session_clock(const std::optional<EngineStatus>& status, const QDateTime& now);

/// While ``clock``'s session is open (from its open to its close), the start of the slot of the
/// minute view ``minutes`` that holds its ``now``: the slots before it are over. None before the
/// open, from the close on, without a clock, or in a candle view. The chart is drawn again when it
/// moves (a slot after the last bar is over).
std::optional<qint64> clock_slot(const std::optional<SessionClock>& clock, int minutes);

/// ``bars``, a minute view's bars of ``minutes`` (kMinuteViews) as the chart draws them, oldest
/// first, with each slot of the view between two bars of the same KST day that has no bar
/// filled with an empty bar (ChartBar::empty: its time, no prices, no volume). Nothing is filled
/// before a day's first bar or between two days. After a day's last bar nothing is filled either
/// (after the close), but for today while ``clock``'s session is open (krx-quant #238): the slots
/// after the last bar that are over by its ``now`` (clock_slot) are empty bars too, so a halt or a
/// quiet stock shows as the time it has lasted, a slot more each minute. A day without a bar yet
/// has none. A candle view's bars (candle_view) come back as they are.
QList<ChartBar> fill_slots(const QList<ChartBar>& bars, int minutes,
                           const std::optional<SessionClock>& clock = std::nullopt);

/// The real bars among ``slotted``, the empty ones left out: what is computed on prices.
QList<ChartBar> real_bars(const QList<ChartBar>& slotted);

/// What the bar being made did to the slotted bars.
struct LiveSlots {
    bool appended = false; ///< it started a new slot (else it took a slot there, or was dropped)
    QList<qint64> filled;  ///< the empty bars put before it, by time, oldest first
    /// The empty bar whose slot it took (krx-quant #238: one filled up to the clock), by time.
    std::optional<qint64> replaced;
    /// It took a slot before the last: the page cannot move it alone and draws the bars again.
    bool redraw = false;
};

/// The bar being made (``live``, a real bar of the view ``minutes``) on ``slotted`` (fill_slots'
/// bars), as ChartDrawings' append_live: one with the start of a bar there replaces it, an empty
/// one's too (filled up to the clock, krx-quant #238); a later one than the last is appended, after
/// an empty bar for each slot between the two when they are of the same day, so a bar after a halt
/// fills the halt at once; an earlier one without a slot is left out. A candle view fills nothing.
LiveSlots append_live_slots(QList<ChartBar>& slotted, const ChartBar& live, int minutes);

/// A special situation as the chart names it: a circuit breaker with its stage ("서킷브레이커 1단계"),
/// "사이드카", a VI by its kind as KIS reports it (static "정적 VI", dynamic "동적 VI", both "정적·동적
/// VI"; "VI" without one). Another detail follows the name as it is.
QString special_title(const SpecialWindow& window);

/// A special situation's band over a minute view.
struct SpecialBand {
    QString kind;    ///< "circuit_breaker", "sidecar" or "vi"
    double from = 0; ///< where its start lies, a bar position on the bars drawn (ChartDrawings' logical_at)
    double to = 0;   ///< where its end lies
    QString label;   ///< special_title
    bool operator==(const SpecialBand&) const = default;
};

/// The bands of ``windows`` over ``slotted`` (fill_slots' bars of the minute view ``minutes``),
/// in the windows' order: each that ends after the first bar starts and spans some time on the
/// bars, from where its start lies to where its end lies. On empty bars a halt's band covers its
/// slots; past the last bar, an ongoing one runs into the free space on the right (at a slot per
/// minute of trading time, ChartDrawings' logical_at). A window without a valid start and end is
/// left out. With ``clock`` (krx-quant #238), one still going on at its ``now`` ends there: the band
/// grows with the clock instead of running to the end the engine expects. None in a candle view or
/// without bars.
QList<SpecialBand> special_bands(const QList<ChartBar>& slotted, const QList<SpecialWindow>& windows, int minutes,
                                 const std::optional<SessionClock>& clock = std::nullopt);

/// A hover box's line for each of ``windows`` that overlaps the slot from ``from`` to ``to``
/// (chart_time, ``to`` not in it), in their order: "서킷브레이커 1단계 13:10–13:30" (with the
/// seconds when either has some, "VI 10:31:05–10:33:12").
QStringList special_lines(const QList<SpecialWindow>& windows, qint64 from, qint64 to);

/// An empty bar's hover line: the span of its slot and "체결 없음" ("10:31–10:32 · 체결 없음", a
/// wider view's "10:30–10:35 · 체결 없음"): a real bar's box names it by its close ("10:33 봉"), so
/// the start alone would read as the minute before.
QString empty_line(qint64 time, int minutes);

/// The hover boxes of a minute view: ``hovers`` (the 1-minute view's boxes of its real bars,
/// krx-quant #159, #182, #185; none in a wider view) with a line for each of ``windows`` over the
/// bar's slot (special_lines), and a box of its own, without a four-minute window, for each empty
/// bar (empty_line, then the windows over it) and for each other bar the windows are over that has
/// none (their lines alone: a wider view's). In time order. None in a candle view.
QList<BarHover> slot_hovers(const QList<ChartBar>& slotted, const QList<BarHover>& hovers,
                            const QList<SpecialWindow>& windows, int minutes);

/// The hover boxes the bar being made (starting at ``live``) changed on ``slotted`` (after
/// append_live_slots, ``made``), as slot_hovers gives them: each empty bar it filled before it, and
/// the bar itself when it started a new slot or took an empty one's (its windows' lines alone; none
/// without). Its own box of the 1-minute view comes with the next full drawing.
QList<BarHover> live_hovers(const QList<ChartBar>& slotted, qint64 live, const LiveSlots& made,
                            const QList<SpecialWindow>& windows, int minutes);

} // namespace fincept::krx_quant
