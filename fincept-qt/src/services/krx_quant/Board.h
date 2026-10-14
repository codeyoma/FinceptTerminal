#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

/// What the KRX prediction screen shows, kept up to date from the engine's answers (REST)
/// and its stream. Qt Core only, unit-tested on its own (tests/tst_krx_board.cpp).
namespace fincept::krx_quant {

/// What happened after a bar close, the same for every predictor of it (krx-quant #159),
/// taken from whichever of its forecasts was scored: the chart's icons, its hover and the
/// pinned table all score on it: the touch target (krx-quant #160, #170), whether the best bid
/// reached the bar close's mid-point plus k ticks within four minutes, else the best ask minus
/// k ticks. From an engine before it, the bar close's mid-point to the one four minutes on.
struct Truth {
    bool scored = false;                  ///< an outcome is in
    Direction happened = Direction::None; ///< None: not scored yet, or it could not be
    bool special = false;                 ///< a VI, circuit breaker or sidecar touched its 4 minutes
    QString unscorable;                   ///< why it could not be scored, e.g. "missing mid: target"
    QDateTime target_at;                  ///< when it is scored: a forecast's own, else 4 minutes on
    std::optional<double> mid_start;      ///< the mid-points its move is measured on
    std::optional<double> mid_target;
    std::optional<QDateTime> hit_at; ///< where its target was reached (krx-quant #160); none today
    std::optional<double> hit_price;
    std::optional<Touch> touch; ///< the touch label recorded beside it (krx-quant #167); not scored on yet
};

/// The truth of the bar close ``bar_close`` from its predictions (any predictors).
Truth truth_of(const QList<Prediction>& at_close, const QDateTime& bar_close);

/// An icon at a bar close, as the chart marks it: the final consensus's, or the selected
/// models' consensus (krx-quant #159); what it predicted and, once scored, whether that was
/// right.
struct Mark {
    enum class Result { Pending, Hit, Miss, Unscorable };
    QDateTime bar_close;
    Direction predicted = Direction::None; ///< the class the engine scores (most_likely)
    double probability = 0;                ///< of that class
    Result result = Result::Pending;
    bool special = false;  ///< a VI, circuit breaker or sidecar touched its 4 minutes: left out of the stats
    QString unscorable;    ///< why it could not be scored (Unscorable only), e.g. "missing mid: display"
    Prediction prediction; ///< the consensus itself: its probabilities, votes and outcome
    Truth truth;           ///< what happened after its bar close
    bool selected = false; ///< the selected models' consensus, not the engine's final one
    int made = 0;          ///< the selected models that made a forecast
    int chosen = 0;        ///< the models selected
};

/// Whether a forecast of ``predicted`` was right about ``truth``: Pending before it is
/// scored, Unscorable when it could not be.
Mark::Result verdict(Direction predicted, const Truth& truth);

/// The predictor name of the selected models' consensus (krx-quant #159).
inline constexpr auto kSelected = "selected";

/// The selected models' consensus at a bar close (krx-quant #159).
struct Pooled {
    /// As a consensus: ``kSelected``, the mean of the three probabilities, how the models
    /// leaned (``votes``), the final consensus's ``blocked``.
    Prediction consensus;
    int made = 0;   ///< the selected models with a forecast at that bar close
    int chosen = 0; ///< the models selected (baselines not counted)
};

/// The consensus of the models of ``selection`` (predictor names; its baselines are left
/// out, krx-quant #166) at a bar close, from its predictions: the three probabilities of
/// those with a forecast (status "ok") averaged, each weighing the same, summed in name
/// order; each counted by its likeliest class (a tie goes flat, then up). None when none of
/// them made one, or none is selected.
std::optional<Pooled> select_consensus(const QList<Prediction>& at_close, const QStringList& selection);

/// The baselines among ``selection``, in its order.
QStringList baselines_of(const QStringList& selection);

/// Each symbol's predictions of its latest bar close and its final consensus over the day;
/// for the chosen symbol (focus), every predictor's over the bar closes read (krx-quant #159).
///
/// The answers and the stream come in any order (an answer may have been read before the
/// stream brought something newer), so nothing goes back: a later bar close is never
/// replaced by an earlier one, nor a scored prediction by the same one unscored.
class Board {
  public:
    void set_symbols(const QList<SymbolQuote>& symbols);
    /// The consensus members: the active algorithms (GET /v1/consensus).
    void set_members(const QStringList& members);
    void set_latest(const Latest& latest);
    /// A symbol's predictions over recent bar closes (GET …/predictions): its final consensus,
    /// and, for the chosen symbol, every predictor's read.
    void set_history(const QString& symbol, const QList<Prediction>& predictions);
    /// The symbol the chart shows: every predictor's predictions are kept for it alone, and
    /// dropped when another is chosen.
    void focus(const QString& symbol);
    /// What the stream brought. Returns the final consensus predictions in it, for the
    /// signal alerts; nothing for the other events.
    QList<Prediction> apply(const Event& event);
    /// The engine could not be read: its predictions must not pass for current ones. The
    /// members stay (a signal arriving meanwhile still counts them).
    void forget_predictions();
    /// The engine's clock is at ``now``, before what is shown (a demo engine started again):
    /// what is later than it is dropped, so earlier bar closes show again. Whether anything was.
    bool rewind_to(const QDateTime& now);

    const QList<SymbolQuote>& symbols() const { return symbols_; }
    const QStringList& members() const { return members_; }
    /// The symbol's predictions of its latest bar close, in the screen's order; empty if none.
    Latest latest(const QString& symbol) const;
    /// The icons of the symbol's bar closes, oldest first, scored on their truth: with no
    /// ``selection``, the final consensus of each bar close that had a forecast; else, for the
    /// chosen symbol only, the selected models' consensus (select_consensus) where one of them
    /// made a forecast. Baselines selected are not in it (krx-quant #166): with no model
    /// selected and one baseline, that baseline's own forecasts; with several, none.
    QList<Mark> marks(const QString& symbol, const QStringList& selection = {}) const;
    /// The symbol's predictions of each bar close, oldest first: every predictor's for the
    /// chosen symbol, the final consensus alone for another.
    QMap<QDateTime, QList<Prediction>> closes(const QString& symbol) const;
    /// What a final consensus at that bar close pooled: the active members' forecasts.
    /// Empty unless it is the bar close shown.
    QList<Prediction> pooled(const QString& symbol, const QDateTime& bar_close) const;

  private:
    void put(const Prediction& p);

    QList<SymbolQuote> symbols_;
    QStringList members_;
    QMap<QString, Latest> latest_;
    QMap<QString, QMap<QDateTime, Prediction>> finals_; ///< by symbol, by bar close
    QString focus_;                                     ///< the chosen symbol
    QMap<QDateTime, QMap<QString, Prediction>> closes_; ///< its predictions, by bar close, by predictor
};

/// When a bar close's predictions are scored (4 minutes on); invalid if none was made.
QDateTime scoring_at(const Latest& latest);

} // namespace fincept::krx_quant
