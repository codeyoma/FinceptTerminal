#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

/// What the KRX prediction screen shows, kept up to date from the engine's answers (REST)
/// and its stream. Qt Core only, unit-tested on its own (tests/tst_krx_board.cpp).
namespace fincept::krx_quant {

/// The final consensus at a bar close, as the chart marks it: what it predicted and, once
/// scored on the chosen basis, whether that was right.
struct Mark {
    enum class Result { Pending, Hit, Miss, Unscorable };
    QDateTime bar_close;
    Direction predicted = Direction::None; ///< the class the engine scores (most_likely)
    double probability = 0;                ///< of that class
    Result result = Result::Pending;
    bool special = false; ///< a VI, circuit breaker or sidecar touched its 4 minutes: left out of the stats
};

/// Each symbol's predictions of its latest bar close and its final consensus over the day.
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
    /// A symbol's final consensus over recent bar closes (GET …/predictions?algorithm=consensus-final).
    void set_history(const QString& symbol, const QList<Prediction>& finals);
    /// What the stream brought. Returns the final consensus predictions in it, for the
    /// signal alerts; nothing for the other events.
    QList<Prediction> apply(const Event& event);

    const QList<SymbolQuote>& symbols() const { return symbols_; }
    const QStringList& members() const { return members_; }
    /// The symbol's predictions of its latest bar close, in the screen's order; empty if none.
    Latest latest(const QString& symbol) const;
    /// The final consensus of each bar close that had a forecast, oldest first, scored on
    /// basis "a" or "b".
    QList<Mark> marks(const QString& symbol, const QString& basis) const;
    /// What a final consensus at that bar close pooled: the active members' forecasts.
    /// Empty unless it is the bar close shown.
    QList<Prediction> pooled(const QString& symbol, const QDateTime& bar_close) const;

  private:
    void put(const Prediction& p);

    QList<SymbolQuote> symbols_;
    QStringList members_;
    QMap<QString, Latest> latest_;
    QMap<QString, QMap<QDateTime, Prediction>> finals_; ///< by symbol, by bar close
};

/// When a bar close's predictions are scored (4 minutes on); invalid if none was made.
QDateTime scoring_at(const Latest& latest);

} // namespace fincept::krx_quant
