#pragma once

#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/EngineData.h"

#include <QList>
#include <QString>
#include <QStringList>

/// The minute views' time without trades (krx-quant #236), worked out here so that the chart's
/// page only draws it. A minute without trades has no bar, so a halt (a circuit breaker's 30
/// minutes) vanished from the chart and its time jumped. The minute views keep a slot for each
/// such minute (or N-minute slot) between a day's first and last bar: an empty bar, which takes
/// a slot but draws no candle and has no volume. Everything that places by time counts the
/// slots (the drawings, the hover boxes, the four-minute windows, the special situations'
/// bands); nothing that computes on prices does (the indicators, the bricks, the baseline's
/// base, the volume profile, the magnet). Over the slots the special situations the engine
/// knows (VIs of the symbol, circuit breakers and sidecars of the market) are drawn as faint
/// bands with their names. Qt Core only.
namespace fincept::krx_quant {

/// ``bars``, a minute view's bars of ``minutes`` (kMinuteViews) as the chart draws them, oldest
/// first, with each slot of the view between two bars of the same KST day that has no bar
/// filled with an empty bar (ChartBar::empty: its time, no prices, no volume). Nothing is filled
/// before a day's first bar or after its last (before the open, after the close), nor between
/// two days. A candle view's bars (candle_view) come back as they are.
QList<ChartBar> fill_slots(const QList<ChartBar>& bars, int minutes);

/// The real bars among ``slotted``, the empty ones left out: what is computed on prices.
QList<ChartBar> real_bars(const QList<ChartBar>& slotted);

/// What the bar being made did to the slotted bars.
struct LiveSlots {
    bool appended = false; ///< it started a new slot (else it replaced the last bar, or was dropped)
    QList<qint64> filled;  ///< the empty bars put before it, by time, oldest first
};

/// The bar being made (``live``, a real bar of the view ``minutes``) on ``slotted`` (fill_slots'
/// bars, the last always a real one), as ChartDrawings' append_live: one with the last bar's
/// start replaces it; a later one is appended, after an empty bar for each slot between the two
/// when they are of the same day, so a bar after a halt fills the halt at once; an earlier one is
/// left out. A candle view fills nothing.
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
/// left out. None in a candle view or without bars.
QList<SpecialBand> special_bands(const QList<ChartBar>& slotted, const QList<SpecialWindow>& windows, int minutes);

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

} // namespace fincept::krx_quant
