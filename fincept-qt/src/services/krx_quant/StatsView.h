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
    QString interval; ///< 95%
    QString pairs;    ///< the bar closes both predicted
    QString p;        ///< one-sided
    QString q;        ///< luck-corrected; "—" where not tested with the leaderboard's
    QString verdict;  ///< "통과" (q ≤ 0.05 and a gain above 0), "미통과", or "—" likewise
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

/// A leaderboard period in Korean ("today", "5d", "20d"); an unknown one as it is.
QString period_label(const QString& period);
/// A scoring basis in Korean ("b", "a"); an unknown one as it is.
QString basis_label(const QString& basis);
/// The days a query covered: one, or the first ~ the last (how many).
QString days_text(const QStringList& days);
/// The query the stats answer, in Korean: period · scoring basis · symbol · special situations.
QString query_text(const AlgorithmStats& stats);

} // namespace fincept::krx_quant
