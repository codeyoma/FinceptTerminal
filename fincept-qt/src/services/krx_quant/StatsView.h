#pragma once

#include "services/krx_quant/EngineData.h"

#include <QString>

#include <optional>

/// What the KRX screen's leaderboard and an algorithm's detail window show of the engine's
/// stats, as text. Qt Core only, unit-tested on its own (tests/tst_krx_stats_view.cpp).
namespace fincept::krx_quant {

/// A value as a cell shows it, and its 95% interval for the tooltip (empty without one).
struct Shown {
    QString text;
    QString tip;
};

/// A number as the screen writes it (thousands grouped), with ``decimals`` places.
QString number_text(double value, int decimals);
/// A gain with its sign: "+0.010", "-0.030", "0.000".
QString signed_text(double value, int decimals);
/// A rate as a percent; "—" when not measured.
Shown rate_shown(const std::optional<Interval>& rate);
/// A mean (Brier, ticks a trade) with ``decimals`` places; "—" when not measured.
Shown mean_shown(const std::optional<Interval>& mean, int decimals);
/// The weakest lift's lower bound, the leaderboard's order; "—" without a baseline to compare with.
QString weakest_lift_text(const Entry& entry);

/// The lift over one baseline as the detail window lists it.
struct LiftRow {
    QString baseline; ///< its name in Korean
    QString gain;
    QString interval;     ///< 95%
    QString pairs;        ///< the bar closes both predicted
    QString p;            ///< one-sided
    QString q;            ///< luck-corrected; "—" where not tested with the leaderboard's
    QString verdict;      ///< "통과" (q ≤ 0.05 and a gain above 0), "미통과", or "—" likewise
    QString day_interval; ///< 95%, taken by trading day (what the statuses go by); "—" from fewer than two days
    QString days;         ///< the trading days it was taken over; "—" likewise
};

/// One row per baseline, in the entry's order (by name).
QList<LiftRow> lift_rows(const Entry& entry);

/// A 30-minute slot as the bar closes it holds: "09:00" is "09:01–09:30". Anything else as it is.
QString slot_label(const QString& slot);

/// A point of the calibration plot: how often a class happened against how likely it was
/// forecast, over the forecasts in one bin.
struct CalibrationPoint {
    double forecast = 0;       ///< the bin's mean forecast probability
    double observed = 0;       ///< how often that class happened
    std::optional<double> low; ///< ``observed``'s 95% interval
    std::optional<double> high;
    QString tip;
};

/// One point per bin with forecasts in it, in the bins' order.
QList<CalibrationPoint> calibration_points(const QList<CalibrationBin>& bins);

/// A curve of the calibration plot: the three classes pooled, or one class on its own.
struct CalibrationSeries {
    QString key;                    ///< "all", "up", "flat" or "down"
    QString name;                   ///< in Korean
    QList<CalibrationPoint> points; ///< each tip names the curve
};

/// The pooled curve, then up, flat and down: each that has a point. Pooled, the classes
/// can look well calibrated when each class on its own is not. Of another ``kind``'s predictor
/// (krx-quant #182), its classes in order, named by its class titles: the curves' keys are
/// the class names ("small", "medium", "large").
QList<CalibrationSeries> calibration_series(const AlgorithmStats& stats,
                                            const std::optional<KindInfo>& kind = std::nullopt);

/// What the stats left out, in Korean: special situations, the predictions over but without
/// a touch label (krx-quant #170), and those not made by status; "제외 없음" if nothing. Special situations a
/// query includes are counted in, and said apart before it ("특수 상황 포함 2 · …").
QString excluded_text(const Excluded& excluded, const QString& special);

/// A leaderboard period in Korean ("today", "5d", "20d"); an unknown one as it is.
QString period_label(const QString& period);
/// The days a query covered: one, or the first ~ the last (how many).
QString days_text(const QStringList& days);
/// The leaderboard's rules, on its title's tooltip (krx-quant #103): the days it counts,
/// what ranks an entry and in what order, what passing the luck check means, and how a
/// detail opens. Of another ``kind`` (krx-quant #182): what it is scored on and against
/// which baselines.
QString ranking_rules(const QStringList& days, int min_sample, const std::optional<KindInfo>& kind = std::nullopt);
/// What a stats column is, on its header's tooltip, where its name alone could mislead
/// (krx-quant #103, #104); empty for the others.
QString column_tip(const QString& header);
/// The detail window's lift title tooltip: why there is no lift, if so, and what lift is.
/// ``kind`` names another kind's baselines (a baseline itself has no lift).
QString lift_tip(const AlgorithmStats& stats, const std::optional<KindInfo>& kind = std::nullopt);
/// The detail window's calibration title tooltip: what the plot compares, and that nothing
/// was scored, if so; the classes drawn apart are ``kind``'s, the direction's without.
QString calibration_tip(const AlgorithmStats& stats, const std::optional<KindInfo>& kind = std::nullopt);

/// A stats column of a kind besides the direction (krx-quant #182): its header and what it is.
struct StatsColumn {
    QString header;
    QString tip; ///< empty where the name says it
};

/// The stats columns of ``kind``'s leaderboard, detail window and breakdowns, in place of the
/// direction's (krx_tables' kStatsHeaders): the samples, accuracy, precision and coverage of its
/// action classes (named by their titles, "작음·큼 정밀도"), Brier; for the range, the share of
/// r within q10–q90 and q50's error; then the weakest lift's lower bound. No touch P&L: the
/// direction's own.
QList<StatsColumn> kind_stats_columns(const KindInfo& kind);
/// An entry's cells under kind_stats_columns, with their intervals.
QList<Shown> kind_stats_cells(const Entry& entry, const KindInfo& kind);
/// The leaderboard's "운 보정" header tooltip for ``kind``: which baselines it means.
QString luck_tip(const KindInfo& kind);

/// A quantile's row of a range predictor's quantile stats (krx-quant #180, #182), in ticks.
struct QuantileRow {
    QString quantile;     ///< "q10", "q50", "q90"
    QString pinball;      ///< its mean pinball loss
    QString interval;     ///< 95%, normal
    QString day_interval; ///< 95%, a trading day at a time; "—" from fewer than two days
    QString days;
    QString vs_climate; ///< its pinball loss less range-climate's (negative is better); "—" for range-climate
    QString vs_climate_interval; ///< 95% of that, a trading day at a time; "—" likewise
};

/// One row per quantile, q10, q50, q90.
QList<QuantileRow> quantile_rows(const RangeStats& stats);
/// The quantile stats' summary: "구간 적중률(q10~q90) 78.0% (95% 72.3–82.9%, 이상 80%) · q50
/// 오차 3.74틱 (95% 3.15 – 4.32) · 표본 240"; "채점된 예측 없음" without any.
QString range_summary(const RangeStats& stats);
/// The query the stats answer, in Korean: period · symbol · special situations.
QString query_text(const AlgorithmStats& stats);

/// The "학습·예측 방식" section of a predictor's detail window (krx-quant #163): labelled
/// lines of what the engine says about the algorithm (its card, inputs, model, status and
/// history); one line saying the engine gives none for a predictor without (a baseline, a
/// consensus), or, with ``failure``, that the descriptions could not be read and why.
QList<QPair<QString, QString>> about_rows(const std::optional<AlgorithmInfo>& info, const QString& failure = {});

/// The "학습과 검증" section below it (krx-quant #165): what the model was trained on and its
/// training loss, last night's validation (the new and the server's model, promoted or not and
/// why) and, on probation or the bench, how far to the next review. Empty for a predictor
/// that is not an algorithm, or with nothing read.
QList<QPair<QString, QString>> learning_rows(const std::optional<AlgorithmInfo>& info);

} // namespace fincept::krx_quant
