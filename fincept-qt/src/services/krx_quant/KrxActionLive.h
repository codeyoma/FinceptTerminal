#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace fincept::services {

class EnginePoller;

/// What the KRX "할 일" screen shows (codeyoma/krx-quant#674, spec #671), kept for it: the cards
/// (GET /v1/action), whether the engine serves them, and whether it answered. The screen owns it and only
/// draws what it holds.
///
/// While started it reads at once and then again by action::poll_ms: every second while a card buys,
/// sells or holds, every 2 s in the session, else every minute. The engine's address is the KRX screen's
/// setting (`krx_quant/base_url`); a new one drops what was read and reads again. Its EnginePoller reads
/// the address and keeps the schedule.
class KrxActionLive : public QObject {
    Q_OBJECT
  public:
    /// ``now`` is the clock the schedule reads (tests give their own).
    explicit KrxActionLive(QObject* parent = nullptr, std::function<QDateTime()> now = {});

    void start(); ///< read at once, then on schedule
    void stop();  ///< read no more until started again
    bool running() const;
    /// Start when ``widget`` is shown, stop when it is hidden.
    void run_while_shown(QObject* widget);
    /// Read again now (the refresh button).
    void read_now();

    const std::optional<krx_quant::ActionBoard>& board() const { return board_; }
    /// False when the engine answered 404: one before krx-quant #672, without the cards.
    bool served() const { return served_; }
    /// Why the last read failed; empty after an answer.
    const QString& failure() const;
    /// When the engine last answered (this computer's clock); invalid before.
    const QDateTime& answered_at() const;
    QDateTime now() const;
    /// The interval to the next read now, ms (0 when stopped).
    int next_ms() const;

  signals:
    /// Something to draw again: an answer, a failure, a read started.
    void changed();
    /// A new answer with cards (before changed): the screen's alerts read it.
    void board_ready();
    /// Another engine: what was read of the old one went.
    void engine_changed();

  private:
    EnginePoller* poller_ = nullptr;
    std::optional<krx_quant::ActionBoard> board_;
    bool served_ = true;
};

} // namespace fincept::services
