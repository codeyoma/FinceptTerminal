#pragma once

#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/EngineData.h"

#include <QDate>
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
/// The first kind predicted once a trading day (krx-quant #264): at 08:50, the day's direction from
/// the KRX opening auction to the closing auction.
inline constexpr auto kOpenToCloseKind = "open_to_close";

/// A choice of the kind selector at the top of the screen.
struct KindChoice {
    QString name;  ///< "direction", "barrier", "range"
    QString title; ///< the engine's title when it makes the kind, else the screen's
    bool enabled = false;
    QString tip; ///< what the kind predicts, or why it cannot be chosen
};

/// The selector's choices in the screen's order — 방향, 장벽, 변동폭, 시가→종가 —, each enabled
/// when the engine makes it (``kinds``: GET /v1/kinds; none from an engine before krx-quant #179,
/// which makes the direction alone, the barrier none from one before krx-quant #183, open to close
/// none from one before krx-quant #293) and this screen can show it (all four since krx-quant #264;
/// a kind it does not know, never). The barrier's horizon ladder (krx-quant #263) follows the
/// barrier, in the engine's order, only when the engine makes it.
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

/// A bar close's barriers (krx-quant #183): U above its mid-point, L below.
struct Barriers {
    double upper = 0;
    double lower = 0;
};

/// The barriers U and L of a bar close: its answer's once it is in (none when its path has none:
/// a window the engine did not follow, or one without a mid-point); before, anchor ± w·tick from
/// its predictions (krx-quant #210: the engine gives the bar close's mid-point and tick with each,
/// kept as the price moves on); none from an engine before #210 or without a mid-point there.
std::optional<Barriers> bar_close_barriers(const QList<KindPrediction>& at_close);

/// The barrier panel's title part (krx-quant #185): the bar close's width and barriers,
/// "w 6틱 · U 78,950 · L 77,750", before its answer too (bar_close_barriers, krx-quant #210). From
/// an engine that gives the bar close's mid-point only with the answer (before #210), "w 6틱" alone
/// until then. Empty without a w.
QString barrier_text(const QList<KindPrediction>& at_close);

/// What happened after a kind's bar close: "정답 4틱 · 큼" for the range (r and its class),
/// "정답 위 먼저 (09:32:30 닿음)" for the barrier (the barrier reached first, and when; "정답
/// 시간 만료" when neither was), a daily kind's day as daily_answer_text says it, "정답 <class>"
/// for another kind, "채점 전", or "채점 불가 (why)"; " · 특수 상황" when one touched its four minutes.
QString kind_answer_text(const std::optional<KindAnswer>& answer, const KindInfo& kind);

/// A column of a kind's prediction panel: its header, and what it is (empty where the header says it).
struct PanelColumn {
    QString header;
    QString tip;
};

/// The columns of ``kind``'s prediction panel (krx-quant #182, #185): the predictor, the likeliest
/// class, the kind's three classes' probabilities, the range's q10, q50 and q90, the result while
/// a bar close is ``pinned`` (a daily kind's always, krx-quant #264: its day is answered that
/// night), the model version. Without the kind described, the predictor, the class, the result
/// and the model.
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
/// lines (``upper``, ``lower``) and where each was first received; for the barrier (krx-quant
/// #185), its barriers U and L (bar_close_barriers: before the answer too, from the predictions,
/// krx-quant #210) and, once the answer is in, where each was first reached (U by the best bid,
/// L by the best ask: the quote then), as the direction's touch targets.
QList<BarHover> kind_hovers(const QList<Bar>& bars, const QMap<QDateTime, QList<KindPrediction>>& closes,
                            const KindInfo& kind, const QMap<QString, QString>& standings);

/// Bar closes of a kind's predictions read at a time for the chart's hover boxes (krx-quant
/// #182): the latest ones first, a whole day; then, as the chart scrolls back, as many as it
/// shows, at most kPredictionPage (the engine's most).
inline constexpr int kKindPage = 400;

/// What to read of a kind's predictions for the bars shown (krx-quant #182): ``from`` is the
/// earliest bar close from which on they were read (invalid: none yet), ``oldest_close`` the
/// first bar's close. With no bar shown, nothing; the latest page while none was read (an
/// invalid ``before``); then the bar closes before ``from`` down to ``oldest_close``,
/// kPredictionPage at most; nothing once they reach it.
struct KindRead {
    QDateTime before; ///< invalid: before now
    int limit = kKindPage;
};
std::optional<KindRead> kind_read(const QDateTime& from, const QDateTime& oldest_close);

/// ``from`` once a page of a kind's predictions (``page``) came in: its earliest bar close, if
/// earlier; ``oldest_close`` when the page is empty (the engine has none before: as far back as shown).
QDateTime kind_from_after(const QDateTime& from, const QList<KindPrediction>& page, const QDateTime& oldest_close);

// ---- A kind predicted once a trading day (krx-quant #264): open_to_close ----------------------

/// The day a daily kind's prediction is of: its prediction time's (``bar_close``) date in KST.
QDate daily_day(const QDateTime& bar_close);

/// The prediction time of ``kind`` (its ``daily.predict_at``, "08:50") on the day of ``t`` in KST:
/// a minute bar's close or a day's 08:50 pinned on the chart shows that day's prediction. Invalid
/// for a bar-close kind.
QDateTime daily_close_on(const QDateTime& t, const KindInfo& kind);

/// A day's flat band: its answer's once in, else the one its predictions came with (set from the
/// days before, the same for each); none without days enough to set it.
std::optional<double> daily_band(const QList<KindPrediction>& at_day);

/// The band as the panel writes it: "보합 ±0.47%" (|close ÷ open − 1| up to it is flat).
QString band_text(double band);

/// What happened on a daily kind's day: "정답 하락 -0.85% (81,950 → 81,250)" (its class, the change
/// from the opening to the closing auction and their prices, from the day's KRX day candle); "채점
/// 전 (20:35부터)" before its answer (``kind``'s answer_from); "채점 불가 (보합 폭 없음)" or "(일봉
/// 없음)" when it could not be scored; " · 특수 상황" on a day of a circuit breaker or sidecar, " · VI
/// 2회" with the symbol's VIs that day (counted in, not special).
QString daily_answer_text(const std::optional<KindAnswer>& answer, const KindInfo& kind);

/// The daily kind's panel title: "005930 · 10-12 시가→종가 (08:50 예측) · 보합 ±0.47% · 채점 전
/// (20:35부터)", the answer once in (daily_answer_text). ``last_day``: the latest is the last
/// trading day's, before today's prediction time or on a day without one: "(08:50 예측, 지난
/// 거래일)". Without a prediction (``bar_close`` invalid): "005930 · 시가→종가 · 예측 없음"; a day
/// pinned without one (``at_day`` empty): "005930 · 10-09 시가→종가 · 예측 없음".
QString daily_title(const QString& symbol, const KindInfo& kind, const QDateTime& bar_close,
                    const QList<KindPrediction>& at_day, bool last_day);

/// The daily panel title's tooltip: the window, when it is predicted and answered, what flat means,
/// and how its statuses are judged.
QString daily_tip(const KindInfo& kind);

/// A day's call as the chart marks it (krx-quant #264): the three probabilities of the kind's
/// algorithms that made a forecast that day (status "ok"; the baselines left out), each weighing
/// the same, summed in name order, and the likeliest class (a tie goes to the middle, then the
/// first). The engine makes no consensus of a daily kind. None when no algorithm made one.
struct DailyCall {
    int klass = -1;
    QList<double> probabilities{}; ///< in the kind's class order
    QStringList algorithms{};      ///< those averaged, by name
};
std::optional<DailyCall> daily_call(const QList<KindPrediction>& at_day, const KindInfo& kind);

/// The day view's icons of a daily kind (krx-quant #264): on each of ``days``' candles (the day
/// view's, starting at 00:00 KST) with a call (daily_call) among ``closes`` (KindBoard::closes, by
/// prediction time), its class (▲ up, ● flat, ▼ down, as the direction's), its result on the day's
/// answer (✓, ✗, ? before it or unscorable) and faint on a special day; clicked, its prediction time.
QList<ChartIcon> daily_icons(const QList<Bar>& days, const QMap<QDateTime, QList<KindPrediction>>& closes,
                             const KindInfo& kind);

/// The day view's hover boxes of a daily kind: on each of ``days``' candles with predictions, the day
/// and its answer ("10-08 시가→종가 (08:50 예측) · 정답 하락 -0.85% (81,950 → 81,250) · 보합 ±0.47%"),
/// the algorithms' mean when more than one made a forecast, then a line per predictor, the
/// algorithms by name then the baselines: its status now (``standings``, the kind's leaderboard),
/// likeliest class, three probabilities and ✓/✗/?. No window: a day is no bar close.
QList<BarHover> daily_hovers(const QList<Bar>& days, const QMap<QDateTime, QList<KindPrediction>>& closes,
                             const KindInfo& kind, const QMap<QString, QString>& standings);

/// How the day view's calls did over the days in view (krx-quant #264), the candles from ``from``
/// to ``to`` (chart_time): the days answered with a class, special days left out and counted
/// apart; how many calls were right, how many a constant middle class ("늘 보합") would have been,
/// how the up and down calls did. ``algorithms``: the algorithms of those calls, by name.
struct DailyScore {
    SelectionScore score{};
    QStringList algorithms{};
};
DailyScore daily_score(const QMap<QDateTime, QList<KindPrediction>>& closes, const KindInfo& kind, qint64 from,
                       qint64 to);

/// The chart header's score of a daily kind: "cox · 맞음 45% · 늘 보합 33% · ▲ 맞음 40% (10) · ▼ 맞음
/// 50% (6) · 20일 · 특수 1일 제외"; several algorithms are "알고리즘 평균 (2)"; "채점된 날 없음" without
/// any. Empty without a call in view.
QString daily_score_text(const DailyScore& score);

/// Days of a daily kind's predictions read at a time (krx-quant #264): a year's at first; on each
/// chart read the latest few again, for answers the stream may have missed.
inline constexpr int kDailyPage = 250;
inline constexpr int kDailyRefresh = 5;

/// What to read of a daily kind's predictions (krx-quant #264): ``from`` is the earliest prediction
/// time from which on they were read (invalid: none yet; from_the_start: all), ``first_day`` the
/// earliest day the chart holds (a candle's or a minute bar's), ``latest`` the latest days again.
/// The latest kDailyPage days while none was read; with ``latest``, the latest kDailyRefresh; then
/// the days before ``from`` back to ``first_day``, kPredictionPage at most; nothing once they
/// reach it or without a day held.
std::optional<KindRead> daily_read(const QDateTime& from, const QDate& first_day, bool latest);

/// ``from`` once a page of a daily kind's predictions (``page``) came in: its earliest prediction
/// time, if earlier; from_the_start when the page is empty (the engine has none before).
QDateTime daily_from_after(const QDateTime& from, const QList<KindPrediction>& page);

} // namespace fincept::krx_quant
