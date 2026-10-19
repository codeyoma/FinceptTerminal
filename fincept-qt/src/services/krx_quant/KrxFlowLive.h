#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QTimer;

namespace fincept::services {

class KrxQuantService;

/// What the KRX flow screen shows (codeyoma/krx-quant#306), kept for it: the chosen symbol's flows of
/// the chosen day (GET /v1/symbols/{symbol}/flow), the engine's symbols with their names and logos,
/// and whether the engine answered. The screen owns it and only draws what it holds.
///
/// While started it reads at once and then again by flow::poll_ms: every 30 s on today while KRX
/// trades, else every 5 minutes. The symbol chosen is kept in the settings (`krx_quant/flow_symbol`);
/// without one, the engine's first. The engine's address is the KRX screen's setting; a new one drops
/// what was read and reads again.
class KrxFlowLive : public QObject {
    Q_OBJECT
  public:
    /// ``now`` is the clock the schedule reads (tests give their own).
    explicit KrxFlowLive(QObject* parent = nullptr, std::function<QDateTime()> now = {});

    void start(); ///< read at once, then on schedule
    void stop();  ///< read no more until started again
    bool running() const { return running_; }

    /// Show ``symbol`` (kept in the settings), read at once.
    void set_symbol(const QString& symbol);
    const QString& symbol() const { return symbol_; }
    /// Show ``day`` (invalid: the engine's default, today or the last trading day), read at once.
    void set_day(const QDate& day);
    const QDate& day() const { return day_; }
    void read_now();

    const std::optional<krx_quant::Flow>& flow() const { return flow_; }
    /// The engine's symbols, in its order, with names and logos; empty before they are read.
    const QList<krx_quant::SymbolQuote>& symbols() const { return symbols_; }
    const QString& failure() const { return failure_; }
    const QDateTime& answered_at() const { return answered_at_; }
    bool reading() const { return reading_; }
    int next_ms() const; ///< 0 when stopped

    static constexpr const char* kSymbolKey = "krx_quant/flow_symbol";

  signals:
    /// Something to draw again: an answer, a failure, the symbols, a read started.
    void changed();

  private:
    void read();
    void read_wanted();
    void schedule();

    std::function<QDateTime()> now_;
    KrxQuantService* service_ = nullptr;
    QTimer* timer_ = nullptr;
    QString symbol_;
    QDate day_;
    std::optional<krx_quant::Flow> flow_;
    QList<krx_quant::SymbolQuote> symbols_;
    QString failure_;
    QDateTime answered_at_;
    bool running_ = false;
    bool reading_ = false;
};

} // namespace fincept::services
