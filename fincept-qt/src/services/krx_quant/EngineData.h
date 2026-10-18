#pragma once

#include "core/result/Result.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>

#include <array>
#include <optional>

/// What the KRX prediction screen shows, read from the prediction engine's API (v1).
///
/// Qt Core only (no network, no widgets), so it is unit-tested on its own
/// (tests/tst_krx_engine_data.cpp). A value the engine does not know (JSON null) stays
/// empty here; the screen shows it as unknown, never as 0.
namespace fincept::krx_quant {

/// A class of the 4-minute move: what a forecast predicts, or what happened.
enum class Direction { Up, Flat, Down, None };

/// A VI, circuit breaker or sidecar: known today (the status), or a symbol's over days (GET
/// /v1/symbols/{symbol}/special, krx-quant #236).
struct SpecialWindow {
    QString kind;   ///< "vi", "circuit_breaker" or "sidecar"
    QString symbol; ///< empty: the whole market
    QDateTime start;
    QDateTime end;
    QString detail = {}; ///< e.g. "1단계" for a circuit breaker's stage; empty when the engine gives none
    bool operator==(const SpecialWindow&) const = default;
};

/// A field of the daily supply data (GET /v1/symbols/{symbol}/supply, krx-quant #226), in the
/// API's order: the short-sale volume, the net buys of individuals, institutions and foreigners,
/// the foreign holdings and ownership ratio (%), and the stock lending and credit balances.
enum class SupplyField {
    ShortVolume,
    IndividualNet,
    InstitutionNet,
    ForeignNet,
    ForeignHolding,
    ForeignRatio,
    LendingBalance,
    CreditBalance,
};
inline constexpr std::size_t kSupplyFields = 8;
/// Its name in the API: "short_volume", "individual_net", "institution_net", "foreign_net",
/// "foreign_holding", "foreign_ratio", "lending_balance", "credit_balance".
QString supply_field_key(SupplyField field);

/// A day of a symbol's daily supply data (krx-quant #226): each field's value, none where the
/// engine has none that day.
struct SupplyDay {
    QDate date;
    std::array<std::optional<double>, kSupplyFields> values{};
    std::optional<double> value(SupplyField field) const { return values[static_cast<std::size_t>(field)]; }
    bool operator==(const SupplyDay&) const = default;
};

/// The last night's training, as the engine's morning swap saw it.
struct Training {
    QString day;
    bool applied = false;            ///< its promoted models are the ones running
    QString reason;                  ///< why, in the engine's words (see training_reason_label)
    QMap<QString, QString> promoted; ///< algorithm -> model version
};

/// A KIS subscription or poll, and how long it has been silent.
struct Feed {
    QString feed;                        ///< the TR id, e.g. "H0UNCNT0"
    QString key;                         ///< the symbol or code it is for
    std::optional<double> seconds_since; ///< since it last delivered; empty if nothing came yet
    QString gap; ///< an open gap, the engine's judgement of trouble: "stale" or "disconnected: …"
};

/// The USD futures' latest trade today, against the previous close read at 07:30.
struct UsdFutures {
    double price = 0;
    QDateTime at;                     ///< when it traded
    std::optional<double> change_pct; ///< since the previous close; none without it
};

struct EngineStatus {
    QString mode;  ///< "live", or "demo" for the development engine
    QString phase; ///< the market phase, e.g. "continuous" (see phase_label)
    QDateTime now;
    /// Today's session (krx-quant #238): continuous trading's start and the closing auction's end;
    /// invalid on a holiday, or from an engine that does not give it.
    QDateTime session_open = {};
    QDateTime session_close = {};
    QString commit;           ///< the code the engine runs, or "unknown" (krx-quant #85); empty from an older engine
    QDateTime started_at;     ///< when it started; invalid from an older engine
    QDateTime last_bar_close; ///< invalid before the first prediction
    int pending_outcomes = 0;
    QList<SpecialWindow> special;
    std::optional<Training> training;
    std::optional<bool> kis_connected;     ///< only a live engine reports its KIS connection
    QList<Feed> feeds;                     ///< likewise
    std::optional<UsdFutures> usd_futures; ///< none before today's first trade
};

/// A macro indicator as read the morning the regime was decided. Unknown values stay empty.
struct Reading {
    QString indicator; ///< e.g. "usdf"
    QString label;     ///< its name in Korean, from the engine
    QString as_of;     ///< the source's date of the value
    std::optional<double> value;
    std::optional<double> change_pct; ///< since the previous KRX trading day's morning
};

/// GET /v1/macro: the latest day with a decided regime. Empty before any.
struct Macro {
    QString day; ///< "YYYY-MM-DD"
    QDateTime decided_at;
    QString regime; ///< "shock", "risk_off", "risk_on" or "neutral" (see regime_label)
    QStringList reasons;
    QList<Reading> readings;
    QMap<QString, QString> failed; ///< source -> why its last read failed
    QStringList skipped;           ///< sources without an API key
};

/// A predicted symbol and its reference price (the mid-point) now.
struct SymbolQuote {
    QString symbol;
    std::optional<double> mid;
    QString name{};               ///< the company's name (krx-quant #250); empty when the engine does not know it
    QString logo_url{};           ///< its logo as Toss Securities shows it (krx-quant #250); empty from an older engine
    std::optional<double> tick{}; ///< its tick size now (krx-quant #182: the range panel's k); none without a mid
    std::optional<int> flat_ticks{}; ///< k, the flat width in ticks now; likewise
};

/// Where one of a bar close's touch targets was first reached (krx-quant #167).
struct Reach {
    QDateTime at;
    double price = 0; ///< the best bid (upper) or ask (lower) then
};

/// A bar close's touch label (krx-quant #160, #167): did the best bid reach the mid-point plus
/// k ticks within the four minutes (up), else the best ask reach it minus k (down), else flat.
/// Recorded beside the outcome; the chart draws its window, targets and where they were reached.
struct Touch {
    Direction direction = Direction::None; ///< None: it could not be scored
    QString unscorable;                    ///< why not, e.g. "silent quotes"
    std::optional<double> anchor;          ///< the mid-point at the bar close
    std::optional<double> upper;           ///< the targets
    std::optional<double> lower;
    std::optional<Reach> upper_reached;
    std::optional<Reach> lower_reached;
    std::optional<double> tick{}; ///< the tick size at the anchor (krx-quant #255: the bar window's answer)
    std::optional<int> k{};       ///< the flat width in ticks there: the targets are anchor ± k·tick
};

/// A prediction scored 4 minutes after its bar close, on both bases: A from the bar
/// close's mid-point, B from the one when it was displayed.
struct Outcome {
    Direction class_a = Direction::None; ///< None where that basis could not be scored
    Direction class_b = Direction::None;
    bool special = false; ///< a VI, circuit breaker or sidecar touched its 4 minutes
    QString unscorable;   ///< which mid-point was missing, e.g. "missing mid: target"
    // The mid-points it was scored on: at the bar close (basis A), when shown (B) and four
    // minutes on; none where missing.
    std::optional<double> mid_close;
    std::optional<double> mid_display;
    std::optional<double> mid_target;
    std::optional<Touch> touch; ///< the bar close's touch label; none from an engine before it (krx-quant #167)
};

/// How a consensus's members leaned (krx-quant #106): the active algorithms it pooled, by
/// the class each forecast most likely.
struct Votes {
    int up = 0;
    int flat = 0;
    int down = 0;
    int total() const { return up + flat + down; }
    int of(Direction d) const { return d == Direction::Up ? up : d == Direction::Down ? down : flat; }
};

struct Prediction {
    QString algorithm; ///< "consensus-…", an algorithm, or "baseline-…"
    QString model_version;
    QString status; ///< "ok", or why there is no forecast (see status_label)
    // Only when status is "ok".
    std::optional<double> p_up;
    std::optional<double> p_flat;
    std::optional<double> p_down;
    std::optional<double> expected_ticks;
    QString symbol;
    QDateTime bar_close;
    QDateTime target_at;            ///< invalid unless status is "ok": only those are scored
    std::optional<Outcome> outcome; ///< once scored
    /// Why the engine says a final consensus is not to be acted on: "special",
    /// "no_reference" or "expired" (krx-quant #88). Empty when it is not blocked, for every
    /// other prediction, and from an older engine that does not send the field.
    QString blocked;
    /// A consensus's members by direction; none for any other prediction, an empty
    /// consensus, one made before the engine counted them, and from an older engine.
    std::optional<Votes> votes;
    /// A consensus's members, in registration order (krx-quant #107); none as for votes.
    std::optional<QStringList> pooled;
};

/// A symbol's predictions of its latest bar close, in the screen's order (in_screen_order).
struct Latest {
    QString symbol;
    QDateTime bar_close;
    QList<Prediction> predictions;
};

/// A finished 1-minute bar the engine built from the trades, by the minute it started.
struct Bar {
    QDateTime start;
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    double volume = 0;
    /// The trades it was made of (krx-quant #255: the bar window); none for a past day's bar, read
    /// from the backfill's files, which do not know them, and from the stream's bar being made.
    std::optional<int> trades{};
};

/// A measured rate or mean with its 95% interval (no interval for a single value).
struct Interval {
    double value = 0;
    std::optional<double> low;
    std::optional<double> high;
    int n = 0;
};

/// The lift over one baseline: the mean difference of hits (1 or 0) with it, on the symbols
/// and bar closes both predicted, and its one-sided test.
struct Lift {
    QString baseline;             ///< "baseline-flat", "baseline-persist" or "baseline-random"
    std::optional<Interval> gain; ///< its n is the pairs; none without a bar close in common
    std::optional<double> p;
    /// The luck-corrected p, and whether it passes (q ≤ 0.05 and a gain above 0). Empty
    /// where it was not tested with the leaderboard's (a breakdown, an unranked entry).
    std::optional<double> q;
    std::optional<bool> significant;
    /// The same lift taken a trading day at a time, each day's mean one value: what the
    /// engine decides the statuses by. Its n is the days; none from fewer than two days.
    std::optional<Interval> by_day;
};

/// A mean over a range predictor's scored predictions (krx-quant #180): over all of them with
/// its normal 95% interval, and taken a trading day at a time (each day's mean one value; none
/// from fewer than two days).
struct RangeMean {
    std::optional<Interval> mean;
    std::optional<Interval> by_day;
};

/// A range predictor's own statistics (krx-quant #180, an entry's ``extra``): of its scored
/// predictions with an r, each quantile's pinball loss, how often r fell within q10–q90
/// (ideally 80%), the mean absolute error of q50, and the pinball loss less range-climate's on
/// the same bar closes (none for range-climate itself).
struct RangeStats {
    int n = 0;
    QMap<QString, RangeMean> pinball; ///< "q10", "q50", "q90"
    std::optional<Interval> interval_coverage;
    std::optional<Interval> q50_abs_error;
    std::optional<QMap<QString, RangeMean>> vs_climate;
};

/// Barrier trades' P&L in ticks less the round-trip cost (krx-quant #183, research only): how
/// many, their total and the mean a trade with its normal 95% interval (none without a trade).
struct BarrierPnl {
    int trades = 0;
    double total_ticks = 0;
    std::optional<Interval> per_trade;
};

/// A barrier predictor's own statistics (krx-quant #183, an entry's ``extra``): of its scored
/// predictions, the barrier P&L of its calls — a long on "up first", a short (an inverse) on
/// "down first", none on "timeout" — all of them, and each side.
struct BarrierStats {
    int n = 0;
    BarrierPnl pnl;
    BarrierPnl longs;  ///< the "up first" calls
    BarrierPnl shorts; ///< the "down first" calls, as shorts of the stock (the sell tax paid)
    /// The same with the shorts through the symbol's single-stock inverse product (krx-quant
    /// #267): all trades, and the shorts alone. None from an engine before it.
    std::optional<BarrierPnl> pnl_inverse{};
    std::optional<BarrierPnl> shorts_inverse{};
};

/// Open-to-close trades' P&L in basis points of the open less the round-trip cost (krx-quant #264,
/// research only): how many, their total and the mean a trade with its normal 95% interval (none
/// without a trade). The barrier's BarrierPnl in another unit.
struct DailyPnl {
    int trades = 0;
    double total_bp = 0;
    std::optional<Interval> per_trade{};
};

/// An open_to_close predictor's own statistics (krx-quant #264, an entry's ``extra``): of its
/// scored predictions, the P&L of following its calls from the opening auction to the closing
/// auction — a long on up, a short of the stock on down, none on flat — all of them and each side,
/// and the downs through the symbol's single-stock inverse product (krx-quant #267).
struct OpenToCloseStats {
    int n = 0;
    DailyPnl pnl{};
    DailyPnl longs{};  ///< the up calls
    DailyPnl shorts{}; ///< the down calls, as shorts of the stock (the sell tax paid)
    std::optional<DailyPnl> pnl_inverse{};
    std::optional<DailyPnl> shorts_inverse{};
};

/// What a query's stats leave out of a predictor's predictions (krx-quant #83).
struct Excluded {
    int special = 0;             ///< scored, touched by a special situation: left out, or counted in on "include"
    int unscorable = 0;          ///< made, but the query's basis could not score them
    QMap<QString, int> statuses; ///< not made, by status ("vi", "no_book", "timed_out", "error", …)
};

/// One predictor's line of the leaderboard. What was not measured (nothing scored, no
/// direction predicted) stays empty.
struct Entry {
    std::optional<int> rank; ///< none: unranked, or a baseline
    QString algorithm;
    QString status; ///< "probation", "active", … (see standing_label); empty for the consensus and the baselines
    QStringList model_versions;
    int n = 0; ///< scored predictions
    std::optional<Interval> accuracy;
    std::optional<Interval> precision;
    std::optional<Interval> coverage;
    std::optional<Interval> brier;
    int trades = 0;
    double total_ticks = 0;
    std::optional<Interval> per_trade; ///< a down call as a short of the stock (the sell tax paid)
    /// The touch P&L with down calls through the symbol's single-stock inverse product
    /// (krx-quant #267, ``pnl_inverse``); none from an older engine, or of another kind.
    std::optional<BarrierPnl> inverse{};
    std::optional<double> lift_low;   ///< the weakest lift's lower bound; none without a baseline to compare with
    bool significant = false;         ///< beats every baseline after the luck correction (q ≤ 0.05)
    QList<Lift> lifts;                ///< over each baseline, by name; none without a baseline to compare with
    std::optional<Excluded> excluded; ///< on a leaderboard line and a detail's overall; none in a breakdown
    /// A range predictor's quantile stats (its ``extra``, krx-quant #180); none for another kind.
    std::optional<RangeStats> range{};
    /// A barrier predictor's barrier P&L (its ``extra``, krx-quant #183); none for another kind.
    std::optional<BarrierStats> barrier{};
    /// An open_to_close predictor's open-to-close P&L (its ``extra``, krx-quant #264); none for another kind.
    std::optional<OpenToCloseStats> open_to_close{};
};

/// GET /v1/leaderboard: the query it answers and its three lists.
struct Leaderboard {
    QString kind = QStringLiteral("direction"); ///< the kind ranked (krx-quant #179); an older engine's: the direction
    QString symbol;                             ///< a symbol, or "all"
    QString period;                             ///< "today", "5d" or "20d"
    QString basis;   ///< "a" or "b" as asked; an engine since krx-quant #170 scores on the touch target alone
    QString special; ///< "exclude" or "include"
    QStringList days;
    int min_sample = 0;
    QList<Entry> ranked;
    QList<Entry> unranked;
    QList<Entry> baselines;
};

/// A bin of the calibration curve: every class probability forecast in [low, high), and
/// how often that class happened.
struct CalibrationBin {
    double low = 0;
    double high = 0;
    int count = 0;
    std::optional<double> mean_p;     ///< the forecasts' mean; none for an empty bin
    std::optional<Interval> observed; ///< how often it happened (Wilson interval); likewise
};

/// The skill against the class rates (krx-quant #262, a direction detail's ``overall.skill``): from
/// the table of called × happened classes. It only describes: the statuses and ranks go by lift.
struct Skill {
    int n = 0; ///< scored predictions
    double accuracy = 0;
    /// The hits of calls made at the same rates, blind to what happens: Σ P(called c)·P(c happened).
    double expected_accuracy = 0;
    std::optional<double> kappa;   ///< Cohen's; none when the expected accuracy is 1 (one class only)
    std::optional<double> gerrity; ///< Gerrity's (1992), down < flat < up; none when up or down never happened
    double balanced_accuracy = 0;  ///< the mean of each happened class's hit rate
    /// Pearson's χ² of independence, as Pesaran–Timmermann (2009) for independent observations; the
    /// windows overlap, so its p is too small. None with one row or column used, or a used cell
    /// expecting fewer than five (Cochran).
    std::optional<double> chi2;
    int df = 0;
    std::optional<double> p;
    /// Kappa taken a trading day at a time (krx-quant #278): each day's kappa one value, the days'
    /// mean with its Student t 95% interval; its n is the days. None from fewer than two days, or
    /// an engine before it.
    std::optional<Interval> kappa_by_day;
    std::optional<double> kappa_by_day_p; ///< one-sided: no better than the class rates
};

/// GET /v1/algorithms/{name}/stats: one predictor's stats for a leaderboard query.
struct AlgorithmStats {
    QString kind =
        QStringLiteral("direction"); ///< the predictor's kind (krx-quant #179); an older engine's: the direction
    QString algorithm;
    QString symbol; ///< the query's, as in Leaderboard
    QString period;
    QString basis;
    QString special;
    QStringList days;
    int min_sample = 0;
    Entry overall;                     ///< its leaderboard line (rank, luck correction)
    QList<CalibrationBin> calibration; ///< the three classes pooled
    QMap<QString, QList<CalibrationBin>>
        calibration_by_class; ///< by its kind's classes ("up", "flat", "down"): each on its own
    // Breakdowns, by key. Their lifts were not tested together: no q, no verdict.
    QMap<QString, Entry> by_symbol;
    QMap<QString, Entry> by_slot; ///< the predicted bar's 30-minute slot: "09:00" for the closes 09:01–09:30
    QMap<QString, Entry> by_model_version;
    /// The direction's skill (krx-quant #262); none for another kind, an older engine, or nothing scored.
    std::optional<Skill> skill;
    /// By volatility tercile at the bar close (krx-quant #262): "low", "mid", "high", each that has
    /// predictions. The barrier width the barrier kind stored there, in basis points of the mid-point;
    /// a bar close without one is left out.
    QMap<QString, Entry> by_volatility;
    /// Each symbol's two tercile cuts, basis points: low up to the first, mid up to the second.
    QMap<QString, QList<double>> volatility_cuts;
};

Result<EngineStatus> parse_status(const QJsonDocument& doc);        ///< GET /v1/status
Result<QList<SymbolQuote>> parse_symbols(const QJsonDocument& doc); ///< GET /v1/symbols
/// GET /v1/symbols/{symbol}/special?from=&to= (krx-quant #236): the symbol's VIs and the market's
/// circuit breakers and sidecars over those days, in start order.
Result<QList<SpecialWindow>> parse_special(const QJsonDocument& doc);
/// GET /v1/symbols/{symbol}/supply?from=&to= (krx-quant #226): the symbol's daily supply data over
/// those days, oldest first; a day without a date is dropped.
Result<QList<SupplyDay>> parse_supply(const QJsonDocument& doc);
Result<Latest> parse_latest(const QJsonDocument& doc); ///< GET /v1/symbols/{symbol}/latest
/// GET /v1/symbols/{symbol}/predictions: the latest bar closes first.
Result<QList<Prediction>> parse_predictions(const QJsonDocument& doc);
Result<QList<Bar>> parse_bars(const QJsonDocument& doc); ///< GET /v1/symbols/{symbol}/bars, oldest first

/// Day candles the engine stored (krx-quant #111): oldest first, each starting at 00:00 KST of
/// its day, and the market they come from ("UN" integrated, "KRX"; empty with none stored).
struct Candles {
    QString venue;
    QList<Bar> candles;
};
Result<Candles> parse_candles(const QJsonDocument& doc);         ///< GET /v1/symbols/{symbol}/candles?period=day
Result<Leaderboard> parse_leaderboard(const QJsonDocument& doc); ///< GET /v1/leaderboard
Result<AlgorithmStats> parse_stats(const QJsonDocument& doc);    ///< GET /v1/algorithms/{name}/stats
Result<Macro> parse_macro(const QJsonDocument& doc);             ///< GET /v1/macro
/// GET /v1/algorithms: each algorithm's lifecycle status now ("probation", "active", …),
/// by name; one without a status is left out.
Result<QMap<QString, QString>> parse_statuses(const QJsonDocument& doc);

/// What an algorithm is, as the engine describes it (GET /v1/algorithms, its card): the
/// text is the engine's, in Korean, read at run time (the fork holds none of it).
struct AlgorithmCard {
    QString scientist; ///< empty for a baseline
    QString character; ///< its thesis in a few words, e.g. "추세 추종"
    QString logic;     ///< how it forecasts
    QStringList unique_features;
    QString training;    ///< how it learns
    QString forecasting; ///< how it makes its three probabilities (krx-quant #164)
    QString failure;     ///< the market it tends to be wrong in (krx-quant #164)

    bool operator==(const AlgorithmCard&) const = default;
};

/// What a feature or a parameter is (krx-quant #164), as the engine says: its meaning, its
/// unit and when it is known (at the bar close, the day before, …).
struct Meaning {
    QString meaning;
    QString unit;
    QString known;

    bool operator==(const Meaning&) const = default;
};

/// A change of an algorithm's lifecycle status, as the engine recorded it.
struct StatusRecord {
    QDateTime at;
    QString from; ///< empty when it was first registered
    QString to;
    QString reason;
};

/// What a model was trained on (krx-quant #165): its days, samples and loss on its own
/// training data, and the days by where their outcomes came from.
struct TrainingInfo {
    QString first_day; ///< "YYYY-MM-DD"
    QString last_day;
    int samples = 0;
    double loss = 0;                  ///< mean log loss on its training samples
    std::optional<int> recorded_days; ///< outcomes from recorded mid-points; none when not said
    std::optional<int> bar_days;      ///< outcomes from one-minute closes
};

/// A model in last night's validation: its version and validation loss.
struct ValidatedModel {
    QString version;
    std::optional<double> loss;
    std::optional<bool> trained_on_validation; ///< the server's model only
};

/// The algorithm's report of the latest night's training (krx-quant #165), as stored.
struct NightValidation {
    QString day; ///< the night, "YYYY-MM-DD"
    QStringList validation_days;
    std::optional<int> samples;
    std::optional<ValidatedModel> challenger; ///< the new model; none when not trained
    std::optional<ValidatedModel> champion;   ///< the server's; none without validation data
    bool promoted = false;
    QString reason;
};

/// The weakest lift by trading day toward a review (krx-quant #165).
struct WeakestLift {
    QString baseline;
    int n = 0; ///< trading days
    std::optional<double> value;
    std::optional<double> low;
    std::optional<double> high;
};

/// Where a probation or bench algorithm stands toward its next review (krx-quant #165), as of
/// the latest review.
struct Stage {
    QString status; ///< "probation" or "bench"
    QString began;  ///< "YYYY-MM-DD"
    QString counted_from;
    bool waiting = false; ///< counted_from null: no model trained on the touch target runs yet (krx-quant #170)
    int days = 0;         ///< trading days counted
    QList<int> review_days;
    std::optional<int> next_review; ///< the day counted; none past the last
    QString next_review_on;         ///< "YYYY-MM-DD", empty when none
    int lift_days = 0;
    std::optional<double> level;
    std::optional<WeakestLift> weakest;
    bool enough = false;
};

/// One algorithm of GET /v1/algorithms (krx-quant #163): its card, inputs, the model it
/// runs now and its status with the history. Empty where the engine leaves a value out.
struct AlgorithmInfo {
    QString name;
    QString kind = QStringLiteral("algorithm"); ///< "algorithm", "baseline" or "consensus" (krx-quant #164)
    /// The kind of prediction it makes (krx-quant #179, the engine's ``kind``): "direction",
    /// "range", …; the direction from an older engine, and for the baselines and consensus listed here.
    QString prediction_kind = QStringLiteral("direction");
    std::optional<AlgorithmCard> card;
    QStringList features;
    /// By name; none from an older engine (krx-quant #164), which gives no such field.
    std::optional<QMap<QString, Meaning>> feature_meanings;
    std::optional<QMap<QString, Meaning>> param_meanings;
    std::optional<int> training_days;
    QString model_version;
    QList<QPair<QString, QString>> params;     ///< name, value as text, by name
    QString trained;                           ///< e.g. "2026-09-08..2026-10-08, 22560 samples, log loss 0.61"
    std::optional<TrainingInfo> training;      ///< the same as values (krx-quant #165)
    std::optional<NightValidation> validation; ///< last night's report (krx-quant #165)
    std::optional<Stage> stage;                ///< on probation or the bench (krx-quant #165)
    QString trained_until;                     ///< "YYYY-MM-DD"
    QString status;
    QDateTime since;
    QList<StatusRecord> history; ///< oldest first
};

/// Every algorithm of GET /v1/algorithms, by name, and the baselines' and the consensus'
/// cards beside them (krx-quant #164: only a card, no model or status).
Result<QMap<QString, AlgorithmInfo>> parse_algorithms(const QJsonDocument& doc);
/// GET /v1/consensus: its members, the active algorithms.
Result<QStringList> parse_members(const QJsonDocument& doc);

/// A baseline of a kind and its card, as GET /v1/kinds lists it.
struct KindBaseline {
    QString name;
    std::optional<AlgorithmCard> card;

    bool operator==(const KindBaseline&) const = default;
};

/// The day of a kind predicted once a trading day (krx-quant #264, GET /v1/kinds' ``daily``): when
/// it is predicted (KST, "08:50"), the auctions its window runs between ("open", "close") and their
/// titles ("시가 단일가", "종가 단일가"), when its answers are first looked for ("20:35", once the
/// day's KRX day candle is stored), and the days with an answer its statuses are judged on.
struct DailySchedule {
    QString predict_at{};
    QString from{};
    QString to{};
    QString from_title{};
    QString to_title{};
    QString answer_from{};
    QList<int> probation_days{};
    QList<int> bench_days{};
    int recent_days = 0;

    bool operator==(const DailySchedule&) const = default;
};

/// A kind of prediction the engine makes (GET /v1/kinds, krx-quant #179): the direction, the
/// range (krx-quant #180), …: its three classes in order (a forecast's class is the likeliest,
/// a tie the middle one, then the first), the classes precision and coverage count, its answer
/// version, the fields its output has besides the probabilities, and its baselines. The titles
/// and meanings are the engine's Korean.
struct KindInfo {
    QString name;  ///< "direction", "range", …
    QString title; ///< e.g. "변동폭"
    QStringList classes;
    QStringList class_titles;
    QStringList action;
    int answer_version = 0;
    /// How long its windows run from the bar close, minutes (krx-quant #263): 4, the barrier
    /// ladder's 15, 30 and 60; 4 from an engine before #263, whose kinds all ran four. 0 for a
    /// kind predicted once a day (krx-quant #264: the engine's null), which has no bar close.
    int horizon_minutes = 4;
    QMap<QString, Meaning> fields;
    QList<KindBaseline> baselines; ///< in the engine's order
    /// When a kind predicted once a trading day is (krx-quant #264); none for a bar-close kind.
    std::optional<DailySchedule> daily{};

    bool operator==(const KindInfo&) const = default;
};

/// Whether ``kind`` is predicted once a trading day (krx-quant #264, open_to_close), not at each bar close.
inline bool is_daily(const KindInfo& kind) {
    return kind.daily.has_value();
}

/// Whether the kind named ``name`` is a barrier (krx-quant #183): the four-minute "barrier" or
/// one of its horizon ladder, "barrier_15m", "barrier_30m", "barrier_60m" (krx-quant #263).
/// Each shows as the barrier does: its panel, its hover, its barrier P&L.
bool is_barrier_kind(const QString& name);

/// GET /v1/kinds: every kind the engine makes, the direction first. An engine before
/// krx-quant #179 has no such answer: the screen then knows the direction only.
Result<QList<KindInfo>> parse_kinds(const QJsonDocument& doc);

/// A range answer's window (krx-quant #180): from the bar close's integrated mid-point
/// (``anchor``), how high and low the mid-point went in the four minutes and when it first did.
/// What the engine could not tell (no mid-point at the bar close) stays empty.
struct RangePath {
    std::optional<double> r; ///< the range, ticks (half-tick steps)
    std::optional<int> k;    ///< the flat width at the anchor: small below it, large from twice it
    std::optional<double> tick;
    std::optional<double> anchor;
    std::optional<double> max_mid;
    QDateTime max_mid_at; ///< the bar close for the anchor
    std::optional<double> min_mid;
    QDateTime min_mid_at;
    std::optional<double> up;   ///< max_mid less the anchor, ticks
    std::optional<double> down; ///< the anchor less min_mid, ticks
    int quotes = 0;
    bool silent = false; ///< the quotes stopped for over 60 seconds
};

/// A barrier answer's window (krx-quant #183): the width w the bar close fixed (ticks), the
/// barriers U and L it put around the bar close's integrated mid-point (``anchor``), and when
/// each was first reached in the four minutes — U by the best bid, L by the best ask — with
/// the best quote then. What the engine could not tell (no mid-point at the bar close, a
/// barrier not reached) stays empty.
struct BarrierPath {
    std::optional<int> w;        ///< the width, ticks
    std::optional<double> sigma; ///< the four-minute volatility it came from, ticks; none when w = k for want of bars
    std::optional<int> k;
    std::optional<double> tick;
    std::optional<double> anchor;
    std::optional<double> upper; ///< U = anchor + w ticks
    std::optional<double> lower; ///< L = anchor − w ticks
    std::optional<double> cost;  ///< the round trip's cost, ticks
    std::optional<double> bid_start;
    std::optional<double> ask_start;
    QDateTime up_at; ///< when the best bid first reached U; invalid if it did not
    std::optional<double> up_bid;
    std::optional<double> up_ask;
    QDateTime down_at; ///< when the best ask first reached L; invalid if it did not
    std::optional<double> down_bid;
    std::optional<double> down_ask;
    std::optional<double> bid_end;
    std::optional<double> ask_end;
    int quotes = 0;
    bool silent = false; ///< the quotes stopped for over 60 seconds
};

/// An open_to_close answer's day (krx-quant #264): the day's KRX opening and closing auction
/// prices (won, adjusted as read) from its KRX day candle, the change close / open − 1, the flat
/// band it was classed by (none without days enough to set it: "no band"), and the symbol's VIs
/// that day (counted, not special).
struct OpenToClosePath {
    double open = 0;
    double close = 0;
    double change = 0;
    std::optional<double> band{};
    std::optional<double> sigma{};
    std::optional<double> scale{};
    int vi = 0;
};

/// A bar close's answer of a kind (krx-quant #179), every predictor's of that kind there.
struct KindAnswer {
    int answer_version = 0;
    QString klass;      ///< its class; empty when its window could not be scored
    QString unscorable; ///< why not, e.g. "silent quotes"
    bool special = false;
    QDateTime scored_at;
    std::optional<RangePath> range;       ///< a range answer's path; none for another kind or a window not followed
    std::optional<BarrierPath> barrier{}; ///< a barrier answer's path (krx-quant #183); likewise
    /// An open_to_close answer's day (krx-quant #264); none for another kind, or a day whose KRX
    /// day candle never came ("no candle": an empty path).
    std::optional<OpenToClosePath> open_to_close{};
};

/// A prediction of a kind besides the direction (krx-quant #179): its output by key — the
/// kind's p_<class> and its fields (the range's q10, q50, q90) — only when the status is "ok".
struct KindPrediction {
    QString algorithm;
    QString model_version;
    QString status; ///< "ok", or why there is no forecast
    QString symbol;
    QDateTime bar_close;
    QDateTime target_at; ///< invalid unless the status is "ok"
    /// k at its bar close (krx-quant #200): the flat band it was made with, kept as the price
    /// moves on; none from an engine before #200, for one stored before, or without a mid-point.
    std::optional<int> flat_ticks;
    /// The barrier width w at its bar close (krx-quant #183), ticks: fixed there by the engine, the
    /// same for every predictor; none for a kind without barriers, from an older engine, or
    /// without a mid-point.
    std::optional<int> width_ticks{};
    /// The integrated mid-point at its bar close (krx-quant #210), the reference its answer is
    /// measured from, kept as the price moves on, and the tick size there: a barrier's U and L
    /// are anchor ± w·tick before its answer is in. None from an engine before #210, for one
    /// stored before, or without a mid-point.
    std::optional<double> anchor{};
    std::optional<double> tick{};
    /// A kind predicted once a day (krx-quant #264): the day's flat band, |close ÷ open − 1|, set
    /// from the days before and known at its prediction time, the same for every predictor of the
    /// day; none for a bar-close kind, or without days enough to set it.
    std::optional<double> band{};
    QMap<QString, double> output;
    std::optional<KindAnswer> answer; ///< its bar close's, once its four minutes are over

    /// An output value; none when not there.
    std::optional<double> value(const QString& key) const {
        const auto it = output.constFind(key);
        return it == output.cend() ? std::nullopt : std::optional<double>(*it);
    }
};

/// A symbol's predictions of a kind at its latest bar close (GET …/kinds/{kind}/latest).
struct KindLatest {
    QString symbol;
    QString kind;
    QDateTime bar_close; ///< invalid before any
    QList<KindPrediction> predictions;
};

Result<KindLatest> parse_kind_latest(const QJsonDocument& doc);
/// GET /v1/symbols/{symbol}/kinds/{kind}/predictions: the latest bar closes first.
Result<QList<KindPrediction>> parse_kind_predictions(const QJsonDocument& doc);

/// A kind's answer to a symbol's bar close, as the stream brings it (krx-quant #179).
struct KindAnswerAt {
    QString symbol;
    QDateTime bar_close;
    KindAnswer answer;
};

/// The screen's order: the final consensus, the fast one, the algorithms, then the
/// baselines; the algorithms and the baselines by name, so the rows stay put whatever order
/// the stream brings them in.
QList<Prediction> in_screen_order(QList<Prediction> predictions);

/// An algorithm's status change.
struct StatusChange {
    QString from; ///< empty when it was first registered
    QString to;
};

/// A message of the engine's stream (GET /v1/stream, WebSocket): what the engine did, as
/// it did it.
struct Event {
    /// KindPrediction and KindAnswer: another kind's (krx-quant #179), named by prediction_kind.
    enum class Type { Prediction, Consensus, Outcome, Health, Lifecycle, Bar, KindPrediction, KindAnswer, Other };
    Type type = Type::Other;                  ///< Other: a type this screen does not know (a newer engine's)
    QDateTime at;                             ///< the engine clock when it was sent
    QString kind;                             ///< a consensus: "fast" or "final"
    QDateTime bar_close;                      ///< predictions and a consensus
    QList<Prediction> predictions;            ///< the predictions, the consensus, or the outcomes (scored predictions)
    bool kis_connected = false;               ///< health
    QList<Feed> feeds;                        ///< health
    QString algorithm;                        ///< lifecycle
    StatusChange change;                      ///< lifecycle
    QString symbol;                           ///< bar
    Bar bar;                                  ///< bar: the one-minute bar being made, so far (krx-quant #110)
    QString prediction_kind{};                ///< a kind event's kind, e.g. "range"
    QList<KindPrediction> kind_predictions{}; ///< a kind_prediction event's
    QList<KindAnswerAt> kind_answers{};       ///< a kind_answer event's
};

/// One stream message (JSON text).
Result<Event> parse_event(const QString& message);
/// The stream's address at the engine's (http → ws, https → wss); empty for an address
/// that is not http(s).
QString stream_url(const QString& base_url);

/// The direction the screen shows: the likeliest class. None without a forecast, or when
/// up and down show as the same whole percent and are likelier than flat: it leans
/// neither way.
Direction shown_direction(const Prediction& p);
/// The class a forecast predicts as the engine scores and ranks it (and as the signal rule
/// counts it): the likeliest, a tie going to flat, then up. None without a forecast.
Direction most_likely(const Prediction& p);
/// "상승", "보합" or "하락"; empty for None.
QString direction_label(Direction d);

/// Whether a predictor is one of the consensus predictions.
bool is_consensus(const QString& predictor);
/// Whether a predictor is one of the baselines.
bool is_baseline(const QString& predictor);
/// A predictor's name on the screen: the consensus and the baselines in Korean, an
/// algorithm by its own name.
QString predictor_label(const QString& predictor);
/// The market phase in Korean; an unknown phase as it is.
QString phase_label(const QString& phase);
/// Why a prediction has no forecast, in Korean; empty for "ok".
QString status_label(const QString& status);
/// A special situation's kind in Korean ("vi", "circuit_breaker", "sidecar").
QString special_label(const QString& kind);
/// The day's macro regime in Korean; an unknown one as it is.
QString regime_label(const QString& regime);
/// An algorithm's status in Korean ("probation", "active", …); empty for none.
QString standing_label(const QString& status);
/// Why last night's training runs or not, in Korean where the engine's reason is known; an
/// unknown one as it is.
QString training_reason_label(const QString& reason);

/// A macro reading as the screen writes it: its name, value and change.
QString reading_text(const Reading& reading);
/// The USD futures on the macro row: the latest trade today with its change and time, or
/// before any, the morning's reading (``usdf``), named as that reading; empty without either.
QString usd_futures_text(const std::optional<UsdFutures>& live, const QList<Reading>& readings);

/// The code the engine runs and when it started, as parts of the status row:
/// {"엔진 0123abc", "10-13 06:02 시작"}. An engine built without its commit gives
/// "엔진 커밋 모름"; an older engine that says neither, no part.
QStringList engine_build_parts(const EngineStatus& status);

/// The header's engine dot (krx-quant #103), in place of the status line at the bottom:
/// reading until the engine first answers, ok, or failed when the engine or anything a
/// refresh reads could not be read. Its tooltip says why, then the refresh's and the
/// engine's clocks.
struct EngineDot {
    enum class State { Reading, Ok, Failed };
    State state = State::Reading;
    QString tip;
};
EngineDot engine_dot(bool answered, const QString& failure, const QStringList& read_errors, const QDateTime& refreshed,
                     const QDateTime& engine_now);

/// A time in KST, the exchange's and the engine's, in ``format`` (QDateTime's); "—" if invalid.
QString kst_text(const QDateTime& t, const QString& format);
/// A price as the exchange writes it: thousands grouped, a decimal only for a mid-point
/// between ticks.
QString price_text(double price);

/// Why a request to the engine failed, in Korean, from HttpClient's error: its HTTP status
/// (0 without a response) and message (empty for a transport failure; a JSON parse
/// error's text when the body was not JSON).
QString failure_text(int http_status, const QString& message);

} // namespace fincept::krx_quant
