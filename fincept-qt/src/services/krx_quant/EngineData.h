#pragma once

#include "core/result/Result.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

/// What the KRX prediction screen shows, read from the prediction engine's API (v1).
///
/// Qt Core only (no network, no widgets), so it is unit-tested on its own
/// (tests/tst_krx_engine_data.cpp). A value the engine does not know (JSON null) stays
/// empty here; the screen shows it as unknown, never as 0.
namespace fincept::krx_quant {

/// A class of the 4-minute move: what a forecast predicts, or what happened.
enum class Direction { Up, Flat, Down, None };

/// A VI, circuit breaker or sidecar known today.
struct SpecialWindow {
    QString kind;   ///< "vi", "circuit_breaker" or "sidecar"
    QString symbol; ///< empty: the whole market
    QDateTime start;
    QDateTime end;
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
};

/// A prediction scored 4 minutes after its bar close, on both bases: A from the bar
/// close's mid-point, B from the one when it was displayed.
struct Outcome {
    Direction class_a = Direction::None; ///< None where that basis could not be scored
    Direction class_b = Direction::None;
    bool special = false; ///< a VI, circuit breaker or sidecar touched its 4 minutes
    QString unscorable;   ///< which mid-point was missing, e.g. "missing mid: target"
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
    std::optional<Interval> per_trade;
    std::optional<double> lift_low;   ///< the weakest lift's lower bound; none without a baseline to compare with
    bool significant = false;         ///< beats every baseline after the luck correction (q ≤ 0.05)
    QList<Lift> lifts;                ///< over each baseline, by name; none without a baseline to compare with
    std::optional<Excluded> excluded; ///< on a leaderboard line and a detail's overall; none in a breakdown
};

/// GET /v1/leaderboard: the query it answers and its three lists.
struct Leaderboard {
    QString symbol;  ///< a symbol, or "all"
    QString period;  ///< "today", "5d" or "20d"
    QString basis;   ///< "a" or "b"
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

/// GET /v1/algorithms/{name}/stats: one predictor's stats for a leaderboard query.
struct AlgorithmStats {
    QString algorithm;
    QString symbol; ///< the query's, as in Leaderboard
    QString period;
    QString basis;
    QString special;
    QStringList days;
    int min_sample = 0;
    Entry overall;                                             ///< its leaderboard line (rank, luck correction)
    QList<CalibrationBin> calibration;                         ///< the three classes pooled
    QMap<QString, QList<CalibrationBin>> calibration_by_class; ///< "up", "flat", "down": each class on its own
    // Breakdowns, by key. Their lifts were not tested together: no q, no verdict.
    QMap<QString, Entry> by_symbol;
    QMap<QString, Entry> by_slot; ///< the predicted bar's 30-minute slot: "09:00" for the closes 09:01–09:30
    QMap<QString, Entry> by_model_version;
};

Result<EngineStatus> parse_status(const QJsonDocument& doc);        ///< GET /v1/status
Result<QList<SymbolQuote>> parse_symbols(const QJsonDocument& doc); ///< GET /v1/symbols
Result<Latest> parse_latest(const QJsonDocument& doc);              ///< GET /v1/symbols/{symbol}/latest
/// GET /v1/symbols/{symbol}/predictions: the latest bar closes first.
Result<QList<Prediction>> parse_predictions(const QJsonDocument& doc);
Result<QList<Bar>> parse_bars(const QJsonDocument& doc);         ///< GET /v1/symbols/{symbol}/bars, oldest first
Result<Leaderboard> parse_leaderboard(const QJsonDocument& doc); ///< GET /v1/leaderboard
Result<AlgorithmStats> parse_stats(const QJsonDocument& doc);    ///< GET /v1/algorithms/{name}/stats
Result<Macro> parse_macro(const QJsonDocument& doc);             ///< GET /v1/macro
/// GET /v1/consensus: its members, the active algorithms.
Result<QStringList> parse_members(const QJsonDocument& doc);

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
    enum class Type { Prediction, Consensus, Outcome, Health, Lifecycle, Other };
    Type type = Type::Other;       ///< Other: a type this screen does not know (a newer engine's)
    QDateTime at;                  ///< the engine clock when it was sent
    QString kind;                  ///< a consensus: "fast" or "final"
    QDateTime bar_close;           ///< predictions and a consensus
    QList<Prediction> predictions; ///< the predictions, the consensus, or the outcomes (scored predictions)
    bool kis_connected = false;    ///< health
    QList<Feed> feeds;             ///< health
    QString algorithm;             ///< lifecycle
    StatusChange change;           ///< lifecycle
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
