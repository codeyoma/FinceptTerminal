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
    bool applied = false; ///< its promoted models are the ones running
    QString reason;
    QMap<QString, QString> promoted; ///< algorithm -> model version
};

struct EngineStatus {
    QString mode;  ///< "live", or "demo" for the development engine
    QString phase; ///< the market phase, e.g. "continuous" (see phase_label)
    QDateTime now;
    QDateTime last_bar_close; ///< invalid before the first prediction
    int pending_outcomes = 0;
    QList<SpecialWindow> special;
    std::optional<Training> training;
    std::optional<bool> kis_connected; ///< only a live engine reports its KIS connection
};

/// A predicted symbol and its reference price (the mid-point) now.
struct SymbolQuote {
    QString symbol;
    std::optional<double> mid;
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
};

/// A symbol's predictions of its latest bar close: the consensus first, then the
/// algorithms, then the baselines, each group in the engine's order.
struct Latest {
    QString symbol;
    QDateTime bar_close;
    QList<Prediction> predictions;
};

Result<EngineStatus> parse_status(const QJsonDocument& doc);        ///< GET /v1/status
Result<QList<SymbolQuote>> parse_symbols(const QJsonDocument& doc); ///< GET /v1/symbols
Result<Latest> parse_latest(const QJsonDocument& doc);              ///< GET /v1/symbols/{symbol}/latest

enum class Direction { Up, Flat, Down, None };

/// The likeliest class. None without a forecast, or when up and down show as the same
/// whole percent and are likelier than flat: it leans neither way.
Direction direction(const Prediction& p);
/// "상승", "보합" or "하락"; empty for None.
QString direction_label(Direction d);

/// Whether a predictor is one of the consensus predictions.
bool is_consensus(const QString& predictor);
/// A predictor's name on the screen: the consensus and the baselines in Korean, an
/// algorithm by its own name.
QString predictor_label(const QString& predictor);
/// The market phase in Korean; an unknown phase as it is.
QString phase_label(const QString& phase);
/// Why a prediction has no forecast, in Korean; empty for "ok".
QString status_label(const QString& status);
/// A special situation's kind in Korean ("vi", "circuit_breaker", "sidecar").
QString special_label(const QString& kind);

/// Why a request to the engine failed, in Korean, from HttpClient's error: its HTTP status
/// (0 without a response) and message (empty for a transport failure; a JSON parse
/// error's text when the body was not JSON).
QString failure_text(int http_status, const QString& message);

} // namespace fincept::krx_quant
