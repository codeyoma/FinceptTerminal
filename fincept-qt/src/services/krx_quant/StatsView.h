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
/// can look well calibrated when each class on its own is not.
QList<CalibrationSeries> calibration_series(const AlgorithmStats& stats);

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
/// detail opens.
QString ranking_rules(const QStringList& days, int min_sample);
/// What a stats column is, on its header's tooltip, where its name alone could mislead
/// (krx-quant #103, #104); empty for the others.
QString column_tip(const QString& header);
/// The detail window's lift title tooltip: why there is no lift, if so, and what lift is.
QString lift_tip(const AlgorithmStats& stats);
/// The detail window's calibration title tooltip: what the plot compares, and that nothing
/// was scored, if so.
QString calibration_tip(const AlgorithmStats& stats);
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
