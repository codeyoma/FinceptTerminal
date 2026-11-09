#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace fincept::services {

class EnginePoller;

/// What the KRX pre-open briefing screen shows (codeyoma/krx-quant#304), kept for it: the day's
/// briefing (GET /v1/briefing), the symbols' names and logos, and whether the engine answered.
/// The screen owns it and only draws what it holds (the KRX screen's KrxLive likewise).
///
/// While started it reads at once and then again by briefing::poll_ms: every 30 s from 07:00 to
/// 09:10 on the day shown, else every 5 minutes. The engine's address is the KRX screen's setting
/// (`krx_quant/base_url`); a new one drops what was read and reads again. Its EnginePoller reads the
/// address and the names and keeps the schedule.
class KrxBriefingLive : public QObject {
    Q_OBJECT
  public:
    /// ``now`` is the clock the schedule reads (tests give their own).
    explicit KrxBriefingLive(QObject* parent = nullptr, std::function<QDateTime()> now = {});

    void start(); ///< read at once, then on schedule
    void stop();  ///< read no more until started again
    bool running() const;
    /// Start when ``widget`` is shown, stop when it is hidden.
    void run_while_shown(QObject* widget);

    /// Show ``day`` (invalid: the engine's default, today or the next trading day), read at once.
    void set_day(const QDate& day);
    const QDate& day() const { return day_; }
    /// Read again now (the refresh button).
    void read_now();

    const std::optional<krx_quant::Briefing>& briefing() const { return briefing_; }
    /// The symbols' names and logos (GET /v1/symbols), by code; empty before they are read.
    const QMap<QString, krx_quant::SymbolQuote>& symbols() const { return symbols_; }
    /// Why the last read failed; empty after an answer.
    const QString& failure() const;
    /// When the engine last answered (this computer's clock); invalid before.
    const QDateTime& answered_at() const;
    bool reading() const { return reading_; }
    /// The interval to the next read now, ms (0 when stopped).
    int next_ms() const;

  signals:
    /// Something to draw again: an answer, a failure, the names, a read started.
    void changed();

  private:
    void read();

    EnginePoller* poller_ = nullptr;
    QDate day_;
    std::optional<krx_quant::Briefing> briefing_;
    QMap<QString, krx_quant::SymbolQuote> symbols_;
    bool reading_ = false;
};

} // namespace fincept::services
