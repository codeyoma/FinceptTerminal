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

/// What the KRX verdict screen (종합 판단, codeyoma/krx-quant#678) shows, kept for it: every symbol's
/// verdict at its latest bar close (GET /v1/verdict), the engine's symbols with their names and logos,
/// and whether the engine answered. The screen owns it and only draws what it holds.
///
/// While started it reads at once and then again by judgment::poll_ms: every 15 s on a weekday from 08:59
/// to 15:40 KST, else every 5 minutes. The symbol whose members are shown is kept in the settings
/// (`krx_quant/verdict_symbol`); without one, or one without a verdict, the first verdict's. The engine's
/// address is the KRX screen's setting; a new one drops what was read and reads again. Its EnginePoller
/// reads the address and the symbols and keeps the schedule.
class KrxVerdictLive : public QObject {
    Q_OBJECT
  public:
    /// ``now`` is the clock the schedule reads (tests give their own).
    explicit KrxVerdictLive(QObject* parent = nullptr, std::function<QDateTime()> now = {});

    void start(); ///< read at once, then on schedule
    void stop();  ///< read no more until started again
    bool running() const;
    /// Start when ``widget`` is shown, stop when it is hidden.
    void run_while_shown(QObject* widget);
    void read_now();

    /// Show ``symbol``'s members (kept in the settings).
    void set_symbol(const QString& symbol);
    const QString& symbol() const { return symbol_; }
    /// The verdict whose members are shown: the chosen symbol's, else the first; none without any.
    std::optional<krx_quant::SymbolVerdict> chosen() const;

    const std::optional<krx_quant::Verdicts>& verdicts() const { return verdicts_; }
    /// The engine's symbols, in its order, with names and logos; empty before they are read.
    const QList<krx_quant::SymbolQuote>& symbols() const;
    const QString& failure() const;
    const QDateTime& answered_at() const;
    bool reading() const { return reading_; }
    int next_ms() const; ///< 0 when stopped

    static constexpr const char* kSymbolKey = "krx_quant/verdict_symbol";

  signals:
    /// Something to draw again: an answer, a failure, the symbols, a read started, a symbol chosen.
    void changed();

  private:
    void read();

    EnginePoller* poller_ = nullptr;
    QString symbol_;
    std::optional<krx_quant::Verdicts> verdicts_;
    bool reading_ = false;
};

} // namespace fincept::services
