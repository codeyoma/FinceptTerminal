#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace fincept::services {

class EnginePoller;

/// What the KRX closing flow screen shows (codeyoma/krx-quant#305), kept for it: the chosen symbol's
/// closing demand today (GET /v1/symbols/{symbol}/closing-flow) and its past days with their summary
/// (…/closing-flow/days), the engine's symbols with their names and logos, and whether the engine
/// answered. The screen owns it and only draws what it holds.
///
/// While started it reads at once and then again by closing::poll_ms: every 60 s while the demand is
/// the minute's (continuous trading), else at the open, in the evening until the day's row is
/// computed, and when the day changes. The days are read with the first read, for a new symbol, on a
/// new day and in that evening (closing::days_due), and again after they failed; not every minute. The symbol chosen is
/// kept in the settings (`krx_quant/closing_symbol`); without one, the engine's first. The engine's address is the KRX
/// screen's setting; a new one drops what was read and reads again. Its EnginePoller reads the address and
/// the symbols and keeps the schedule.
class KrxClosingLive : public QObject {
    Q_OBJECT
  public:
    /// ``now`` is the clock the schedule reads (tests give their own).
    explicit KrxClosingLive(QObject* parent = nullptr, std::function<QDateTime()> now = {});

    void start(); ///< read at once, then on schedule
    void stop();  ///< read no more until started again
    bool running() const;
    /// Start when ``widget`` is shown, stop when it is hidden.
    void run_while_shown(QObject* widget);

    /// Show ``symbol`` (kept in the settings), read at once.
    void set_symbol(const QString& symbol);
    const QString& symbol() const { return symbol_; }
    /// Read both now, the days too.
    void read_now();

    const std::optional<krx_quant::ClosingFlow>& flow() const { return flow_; }
    const std::optional<krx_quant::ClosingDays>& days() const { return days_; }
    /// The engine's symbols, in its order, with names and logos; empty before they are read.
    const QList<krx_quant::SymbolQuote>& symbols() const;
    /// Why the last read failed (today's, the days', or the symbols'); empty when it did not.
    QString failure() const;
    const QDateTime& answered_at() const;
    bool reading() const { return reading_flow_ || reading_days_; }
    int next_ms() const; ///< 0 when stopped

    static constexpr const char* kSymbolKey = "krx_quant/closing_symbol";

  signals:
    /// Something to draw again: an answer, a failure, the symbols, a read started.
    void changed();

  private:
    void read();
    /// An answer for a symbol no longer shown is dropped: the one shown is read now. Whether it was.
    bool dropped(const QString& symbol);

    EnginePoller* poller_ = nullptr; ///< its failure: the address's or the symbols'
    QString symbol_;
    std::optional<krx_quant::ClosingFlow> flow_;
    std::optional<krx_quant::ClosingDays> days_;
    QDateTime days_read_at_;   ///< when the days shown were answered (now_'s clock)
    bool days_wanted_ = false; ///< read_now: the days too
    QString flow_failure_;
    QString days_failure_;
    bool reading_flow_ = false;
    bool reading_days_ = false;
};

} // namespace fincept::services
