#pragma once

#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/EngineData.h"
#include "services/krx_quant/KindView.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

/// What a bar's window shows (krx-quant #255): a 1-minute bar of the chart double-clicked opens a
/// window of its own with the bar, every direction prediction of its bar close (the bar's start
/// and a minute), the range's and the barrier's predictions there, what happened in its four
/// minutes, and a mini chart of those minutes with the levels the chart's four-minute hover draws
/// (krx-quant #167, #182, #185). Worked out here, so the window only renders. Qt Core only,
/// unit-tested on its own (tests/tst_krx_bar_view.cpp).
namespace fincept::krx_quant {

/// The 1-minute bars a bar's window reads before its scoring time: the bar, the four after it, and
/// the one before it with some to spare (minutes without trades have no bar).
inline constexpr int kBarReadBars = 10;

/// Seconds after its scoring time from which on a bar's window reads nothing more, whatever came:
/// the engine scores at the scoring time, and a bar close it could not follow is never scored.
inline constexpr int kBarSettleSeconds = 600;

/// What a bar's window reads of the engine (KrxQuantService::read_bar) for the 1-minute bar of
/// ``symbol`` that closed at ``bar_close``.
struct BarRead {
    QString symbol;
    QDateTime bar_close;
    /// The 1-minute bars that start before its scoring time, the last kBarReadBars of them, oldest
    /// first (GET …/bars?before=&limit=): the bar, the four after it, and those before it.
    QList<Bar> bars;
    /// Every direction prediction of its bar close (GET …/predictions?bar_close=).
    QList<Prediction> predictions;
    /// The algorithms' lifecycle statuses now (GET /v1/algorithms); empty when not read.
    QMap<QString, QString> statuses;
    /// Every prediction of each other kind the engine makes at its bar close (GET
    /// …/kinds/{kind}/predictions?bar_close=), by kind; a kind read is there even with none.
    QMap<QString, QList<KindPrediction>> kinds;
    /// Why a kind's predictions could not be read, by kind.
    QMap<QString, QString> kind_failures;
    /// The symbol's VIs and the market's circuit breakers and sidecars that started on the bar's
    /// day (GET …/special?from=&to=, krx-quant #236); none from an engine without them.
    QList<SpecialWindow> special;
};

/// A bar window's key: its symbol and bar close, "005930@2026-10-16T10:27:00+09:00".
QString bar_key(const QString& symbol, const QDateTime& bar_close);

/// The window's title: the company's name, the symbol, the date and the bar's start, as the chart
/// names its bars: "삼성전자 005930 · 2026-10-16 10:26 봉"; the symbol alone without a name.
QString bar_title(const QString& name, const QString& symbol, const QDateTime& bar_close);

/// When the bar close's four minutes end and it is scored: its predictions' target time, else four
/// minutes on.
QDateTime bar_scoring_at(const BarRead& read);

/// A labelled line of the window's sections.
struct BarLine {
    QString label;
    QString value;
    bool operator==(const BarLine&) const = default;
};

/// "봉 정보" at the engine clock ``now``: the bar's span ("10:26–10:27"), its open, high, low and
/// close, volume and trades ("—" for a past day's bar, whose trades the engine does not know), and
/// its change from the bar before (the last with trades, another day's too) in won and percent. A
/// minute without trades: "체결 없음"; one not over yet: "만드는 중". Then each special situation
/// over the bar and its four minutes, with its span; "없음" without any.
QList<BarLine> bar_info_lines(const BarRead& read, const QDateTime& now);

/// The bar close's reference as its answer measures from (krx-quant #160, #210): the integrated
/// mid-point there, its tick size and k (the flat width in ticks). The direction's touch label's
/// once it is in; before, what the other kinds' predictions came with (krx-quant #200, #210),
/// kept as the price moves on. What neither gives stays empty.
struct BarAnchor {
    std::optional<double> anchor;
    std::optional<double> tick;
    std::optional<int> k;
};
BarAnchor bar_anchor(const BarRead& read);

/// "정답 상세": the bar close's mid-point, tick size and k, the touch targets (anchor ± k ticks)
/// and the touch answer with where each target was first reached and the quote then; the range's
/// r and class with the highest and lowest mid-points and when; the barrier's w, U and L and the
/// barrier reached first and when; and the scoring time. Before an answer is in, "채점 전"; one
/// that could not be scored, "채점 불가 (why)"; " · 특수 상황" when one touched its four minutes.
/// ``kinds``: the engine's (GET /v1/kinds); a kind it does not make has no lines.
QList<BarLine> answer_lines(const BarRead& read, const std::optional<QList<KindInfo>>& kinds);

/// The kinds besides the direction the window shows, in its order: the range, then the barrier.
QStringList bar_kinds();

/// A kind's section of the window: its predictions at the bar close as its panel lists them
/// (krx-quant #182, #185), with the results.
struct KindSection {
    QString kind;  ///< "range", "barrier"
    QString title; ///< "변동폭 예측", "장벽 예측" (the engine's title of the kind)
    /// On its header: its answer and widths ("정답 5틱 · 큼 · k 2틱 · 2k 4틱", "정답 위 먼저 (10:29:28
    /// 닿음) · w 6틱 · U … · L …"), or why it has no rows: the engine does not make the kind, its
    /// predictions could not be read, or there are none at the bar close.
    QString note;
    std::optional<KindInfo> info; ///< none: the engine does not make it
    QList<PanelColumn> columns;   ///< kind_panel_columns, the result's included
    QList<KindRow> rows;          ///< kind_rows
};
QList<KindSection> kind_sections(const BarRead& read, const std::optional<QList<KindInfo>>& kinds);

/// A level across the mini chart's four minutes.
struct MiniLevel {
    QString label; ///< "위 목표", "아래 목표", "U", "L", "최고 중간가", "최저 중간가"
    double price = 0;
    bool upper = true;   ///< in the rising colour (the upper of a pair), else the falling one
    bool dashed = false; ///< its answer is not in yet: as the chart's hover draws a window not over
    bool operator==(const MiniLevel&) const = default;
};

/// Where a level was first reached, to the second.
struct MiniPoint {
    qint64 time = 0; ///< chart_time
    double price = 0;
    bool upper = true;
    QString label; ///< what it is, for its tooltip: "위 목표에 닿음 10:29:28 · 매수 1호가 81,500"
    bool operator==(const MiniPoint&) const = default;
};

/// The window's four-minute mini chart: the bar and the four 1-minute bars from its bar close to its
/// scoring time, the four minutes shaded, and across them the levels the chart's hover draws (the
/// touch targets, krx-quant #167; the range's highest and lowest mid-points, #182; the barrier's U
/// and L, #185), each where it was first reached. The same meanings and colours: upper levels
/// rising, lower ones falling; a level whose answer is not in yet dashed.
struct MiniChart {
    qint64 from = 0;        ///< the bar's start (chart_time): five slots of a minute from it
    qint64 window_from = 0; ///< its bar close
    qint64 window_to = 0;   ///< its scoring time
    /// A slot a minute: the bar's, then the four after it; empty where a minute had no trades or is
    /// not over yet.
    QList<ChartBar> candles;
    QList<MiniLevel> levels;
    QList<MiniPoint> points;
    bool open = false; ///< the four minutes are not over yet: the window's edges dashed
    qint64 now = 0;    ///< the engine clock (chart_time): an empty slot before it had no trades
    double low = 0;    ///< the prices shown: the bars', the levels' and the points' with a margin
    double high = 0;   ///< equal to low when there is nothing to show
};
MiniChart mini_chart(const BarRead& read, const QDateTime& now);

/// Where a moment (chart_time) lies across ``width`` from ``left``: the five minutes of ``mini``
/// evenly, from its bar's start to its scoring time.
double mini_x(const MiniChart& mini, qint64 time, double left, double width);
/// Where a price lies down ``height`` from ``top``: ``mini``'s high at the top, its low at the foot.
double mini_y(const MiniChart& mini, double price, double top, double height);
/// The heights of labels each beside its line (``ys``, the lines' heights, in any order), moved
/// apart to ``gap`` at least and kept within ``top`` to ``bottom``, as near their lines as that
/// lets them; in the order of ``ys``.
QList<double> spread_labels(const QList<double>& ys, double gap, double top, double bottom);

/// Whether nothing more is to come for the window at the engine clock ``now``: a bar of an earlier
/// day once every kind was read; once its four minutes are over, every kind read, its direction
/// scored (or none to score: no forecast made) and each kind answered (or none to answer); from
/// kBarSettleSeconds after its scoring time on, whatever came. Until then it is read again as its
/// predictions, outcomes and answers come, and a kind that could not be read (kind_failures) is
/// read again.
bool bar_settled(const BarRead& read, const QDateTime& now);

/// The window's state on its header: "채점 전 · 갱신 중" while it is read again, then "채점됨",
/// "채점 불가" or "채점되지 않음" (no forecast scored there).
QString bar_state_text(const BarRead& read, const QDateTime& now);

} // namespace fincept::krx_quant
