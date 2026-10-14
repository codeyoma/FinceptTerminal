#pragma once

#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

/// What the KRX screen shows of a prediction kind besides the direction (krx-quant #182, the
/// range; #185, the barrier): the kind selector's choices, the kind's prediction panel, its hover
/// boxes and lines on the chart, and what the detail window says of a kind's baseline. Worked out
/// here so the widgets only render.
/// Qt Core only, unit-tested on its own (tests/tst_krx_kind_view.cpp).
namespace fincept::krx_quant {

inline constexpr auto kDirectionKind = "direction";
inline constexpr auto kRangeKind = "range";
inline constexpr auto kBarrierKind = "barrier";

/// A choice of the kind selector at the top of the screen.
struct KindChoice {
    QString name;  ///< "direction", "barrier", "range"
    QString title; ///< the engine's title when it makes the kind, else the screen's
    bool enabled = false;
    QString tip; ///< what the kind predicts, or why it cannot be chosen
};

/// The selector's choices in the screen's order — 방향, 장벽, 변동폭 —, each enabled when the
/// engine makes it (``kinds``: GET /v1/kinds; none from an engine before krx-quant #179, which
/// makes the direction alone, and the barrier none from one before krx-quant #183) and this
/// screen can show it (all three since krx-quant #185; a kind it does not know, never).
QList<KindChoice> kind_choices(const std::optional<QList<KindInfo>>& kinds);

/// The kind the screen shows: ``saved`` (the setting ``krx_quant/kind``) when it can be chosen,
/// else the direction. The setting itself stays: back on an engine that makes it, it shows again.
QString shown_kind(const QString& saved, const QList<KindChoice>& choices);

/// The kind named ``name`` among ``kinds``; none when the engine did not list it.
std::optional<KindInfo> kind_named(const std::optional<QList<KindInfo>>& kinds, const QString& name);

/// Whether ``predictor`` is one of ``kind``'s baselines.
bool is_kind_baseline(const KindInfo& kind, const QString& predictor);

/// What the detail window says of a kind's baseline (krx-quant #182): its card, as GET
/// /v1/kinds gives it (GET /v1/algorithms lists the direction's alone); none for another predictor.
std::optional<AlgorithmInfo> kind_baseline_info(const std::optional<QList<KindInfo>>& kinds, const QString& predictor);

/// The class a kind's forecast predicts as the engine scores it: the likeliest of the kind's
/// classes, a tie going to the middle one, then the first (krx-quant #179); -1 without a forecast.
int likeliest_class(const KindPrediction& p, const KindInfo& kind);

/// A tick count as the range writes it: whole ticks bare, else one decimal ("2", "4.5", "2.4").
QString ticks_text(double ticks);

/// The flat width k of a kind's bar close (krx-quant #180): its answer's (the engine's k at the
/// bar close) once it is in; before, the one its predictions came with (krx-quant #200: k at the
/// bar close, what their classes were predicted against, though the price has since moved into
/// another band); from an engine that gives neither, ``now`` (GET /v1/symbols' flat_ticks, k at
/// the mid-point now); none without any.
std::optional<int> bar_close_k(const QList<KindPrediction>& at_close, std::optional<int> now);

/// The range panel's widths, on its title: "k 2틱 · 2k 4틱"; empty without a k.
QString widths_text(std::optional<int> k);

/// The barrier width w of a bar close (krx-quant #183): its answer's once it is in, else the one
/// its predictions came with (the engine fixes it at the bar close, the same for each); none
/// without either (another kind, an engine before #183, no mid-point at the bar close).
std::optional<int> bar_close_width(const QList<KindPrediction>& at_close);

/// The barrier panel's title part (krx-quant #185): the bar close's width and barriers,
/// "w 6틱 · U 78,950 · L 77,750". U and L come with the answer (the engine gives the bar close's
/// mid-point then): before it, "w 6틱" alone. Empty without a w.
QString barrier_text(const QList<KindPrediction>& at_close);

/// What happened after a kind's bar close: "정답 4틱 · 큼" for the range (r and its class),
/// "정답 위 먼저 (09:32:30 닿음)" for the barrier (the barrier reached first, and when; "정답
/// 시간 만료" when neither was), "정답 <class>" for another kind, "채점 전", or "채점 불가
/// (why)"; " · 특수 상황" when one touched its four minutes.
QString kind_answer_text(const std::optional<KindAnswer>& answer, const KindInfo& kind);

/// A column of a kind's prediction panel: its header, and what it is (empty where the header says it).
struct PanelColumn {
    QString header;
    QString tip;
};

/// The columns of ``kind``'s prediction panel (krx-quant #182, #185): the predictor, the likeliest
/// class, the kind's three classes' probabilities, the range's q10, q50 and q90, the result while
/// a bar close is ``pinned``, the model version. Without the kind described, the predictor, the
/// class, the result and the model.
QList<PanelColumn> kind_panel_columns(const std::optional<KindInfo>& kind, bool pinned);

/// A row of a kind's prediction panel (krx-quant #182).
struct KindRow {
    QString algorithm;
    QString name; ///< as the screen names it (predictor_label)
    bool baseline = false;
    QList<std::optional<double>> probabilities; ///< in the kind's class order; none without a forecast
    QString top;                                ///< the likeliest class's title, or why it made no forecast
    std::optional<double> q10;                  ///< the range's quantiles, ticks
    std::optional<double> q50;
    std::optional<double> q90;
    QString model_version;
    QString result; ///< on its bar close's answer: "✓ 맞음", "✗ 틀림", "채점 전" or "채점 불가"
};

/// A bar close's predictions of ``kind`` as its panel lists them: the algorithms by name, then
/// the baselines by name.
QList<KindRow> kind_rows(const QList<KindPrediction>& at_close, const KindInfo& kind);

/// The hover box of the bar that closed at ``bar_close`` while ``kind`` is chosen (krx-quant
/// #182): its bar and target times with the answer ("09:31 봉 → 09:35 목표 · 정답 4틱 · 큼"; the
/// barrier's, krx-quant #185, with its width: "… · 정답 위 먼저 (09:32:30 닿음) · w 6틱"), then a
/// line per predictor, the algorithms by name then the baselines: its status now (``standings``,
/// the kind's leaderboard), likeliest class and its probability and the range's q10~q90 — or,
/// for the barrier, its likeliest class and the three probabilities, as the direction's —, and
/// ✓/✗/? on the answer. "예측 없음" alone without predictions.
QStringList kind_hover_box(const QList<KindPrediction>& at_close, const QDateTime& bar_close, const KindInfo& kind,
                           const QMap<QString, QString>& standings);

/// Each of ``bars``' hover box while ``kind`` is chosen, from its predictions by bar close
/// (``closes``: KindBoard::closes), with the bar close's four-minute window (as krx-quant #167
/// draws it): once a range answer is in, its highest and lowest mid-points as the window's two
/// lines (``upper``, ``lower``) and where each was first received; once a barrier answer is in
/// (krx-quant #185), its barriers U and L and where each was first reached (U by the best bid,
/// L by the best ask: the quote then), as the direction's touch targets.
QList<BarHover> kind_hovers(const QList<Bar>& bars, const QMap<QDateTime, QList<KindPrediction>>& closes,
                            const KindInfo& kind, const QMap<QString, QString>& standings);

/// Bar closes of a kind's predictions read at a time for the chart's hover boxes (krx-quant
/// #182): the latest ones first, a whole day; then, as the chart scrolls back, as many as it
/// shows, at most kMaxOlderBars (the engine's most).
inline constexpr int kKindPage = 400;

/// What to read of a kind's predictions for the bars shown (krx-quant #182): ``from`` is the
/// earliest bar close from which on they were read (invalid: none yet), ``oldest_close`` the
/// first bar's close. With no bar shown, nothing; the latest page while none was read (an
/// invalid ``before``); then the bar closes before ``from`` down to ``oldest_close``,
/// kMaxOlderBars at most; nothing once they reach it.
struct KindRead {
    QDateTime before; ///< invalid: before now
    int limit = kKindPage;
};
std::optional<KindRead> kind_read(const QDateTime& from, const QDateTime& oldest_close);

/// ``from`` once a page of a kind's predictions (``page``) came in: its earliest bar close, if
/// earlier; ``oldest_close`` when the page is empty (the engine has none before: as far back as shown).
QDateTime kind_from_after(const QDateTime& from, const QList<KindPrediction>& page, const QDateTime& oldest_close);

} // namespace fincept::krx_quant
