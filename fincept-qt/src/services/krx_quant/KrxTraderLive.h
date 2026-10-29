#pragma once

#include "services/krx_quant/EngineData.h"
#include "services/krx_quant/TraderView.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QTimer;

namespace fincept::services {

class KrxQuantService;

/// What the KRX auto trader screen shows (codeyoma/krx-quant#397, #401), kept for it: the trader's state
/// (GET /v1/trader), its days (GET /v1/trader/days), the last trades of the champion or of the policy
/// version chosen (GET /v1/trader/trades, ``policy=``), the policy versions (GET /v1/trader/policies), and
/// whether the engine answered. The screen owns it and only draws what it holds.
///
/// While started it reads the state at once and then again by trader::poll_ms: every 2 s while the
/// trader runs with a position, every 5 s in the continuous session, else every 5 minutes. The days,
/// the trades and the policy versions are read with the first read, when the state says they may have
/// changed (trader::history_key), after they failed, after a pause was sent, and at least every 5
/// minutes; the trades also as soon as another policy is chosen. Stopped (the screen hidden), it reads
/// nothing.
///
/// Its one write is the screen's pause (pause): it asks first, through the screen's ``confirm``, and
/// sends POST /v1/trader/pause only on a yes. Nothing here resumes the trader: that is the server's
/// alone (krx-quant trade resume). The engine's address is the KRX screen's setting; a new one drops
/// what was read and reads again.
class KrxTraderLive : public QObject {
    Q_OBJECT
  public:
    /// The trades asked for: the table's rows.
    static constexpr int kTradesShown = 100;
    /// ``now`` is the clock the schedule reads (tests give their own).
    explicit KrxTraderLive(QObject* parent = nullptr, std::function<QDateTime()> now = {});

    void start(); ///< read at once, then on schedule
    void stop();  ///< read no more until started again
    bool running() const { return running_; }
    /// Read the state, the days, the trades and the policy versions now.
    void read_now();
    /// Whose trades to read (codeyoma/krx-quant#401): none, the champion's (the default); else that
    /// policy version's. Another choice drops the trades read and reads them at once (when started); an
    /// answer for the choice before it is not kept. A new engine goes back to the champion's.
    void set_trades_policy(std::optional<qint64> policy);
    const std::optional<qint64>& trades_policy() const { return trades_policy_; }

    /// What a pause asks before it is sent: the question, and whether to send it (the confirm window's
    /// answer). Called at once, before anything is sent.
    using Confirm = std::function<bool(const krx_quant::trader::Question&)>;
    /// The screen's pause: ``sell_now`` false pauses new entries, true also sells the open position
    /// at once. Sent only when pause_action allows it and ``confirm`` says yes; whether it was sent.
    bool pause(bool sell_now, const Confirm& confirm);
    /// Whether a pause is being sent.
    bool sending() const { return sending_; }
    /// What came of the last pause sent: "… 요청을 보냈습니다 …", or why it could not be; empty before.
    const QString& pause_note() const { return pause_note_; }
    bool pause_failed() const { return pause_failed_; }

    const std::optional<krx_quant::TraderState>& state() const { return state_; }
    const std::optional<QList<krx_quant::TraderDay>>& days() const { return days_; }
    const std::optional<QList<krx_quant::TraderTrade>>& trades() const { return trades_; }
    const std::optional<krx_quant::TraderPolicies>& policies() const { return policies_; }
    /// Why the last read failed (the state's, the days', the trades' or the policies'); empty when it did not.
    QString failure() const;
    /// When the state shown was answered, on ``now``'s clock.
    const QDateTime& answered_at() const { return answered_at_; }
    QDateTime now() const { return now_(); }
    bool reading() const { return reading_state_ || reading_days_ || reading_trades_ || reading_policies_; }
    int next_ms() const; ///< 0 when stopped

  signals:
    /// Something to draw again: an answer, a failure, a pause sent or its answer, a read started.
    void changed();

  private:
    void read();
    /// On a state's answer: the days and trades asked for when they are due.
    void check_history();
    void read_history();
    /// The trades of the policy chosen, unless they are being read.
    void read_trades();
    /// The days, the trades or the policy versions answered.
    void history_done();
    void schedule();
    /// Another engine: what was read of the one before is dropped.
    void drop();
    /// A pause confirmed for an engine other than the one now read: not sent, said so. Always false.
    bool refuse_pause();

    std::function<QDateTime()> now_;
    KrxQuantService* service_ = nullptr;
    QTimer* timer_ = nullptr;
    std::optional<krx_quant::TraderState> state_;
    std::optional<QList<krx_quant::TraderDay>> days_;
    std::optional<QList<krx_quant::TraderTrade>> trades_;
    std::optional<krx_quant::TraderPolicies> policies_;
    std::optional<qint64> trades_policy_;       ///< whose trades are shown: none, the champion's
    std::optional<qint64> trades_asked_policy_; ///< whose trades the read under way asked for
    bool trades_wanted_ = false;                ///< another policy chosen while its trades were read
    /// The state's key when the days and trades were last asked for; none when they were asked before
    /// any state answered (the first read).
    std::optional<QString> history_key_;
    QDateTime history_asked_at_; ///< when they were (now_'s clock)
    bool history_wanted_ = false; ///< read_now, a pause sent: the days and trades too
    QString state_failure_;
    QString days_failure_;
    QString trades_failure_;
    QString policies_failure_;
    QString pause_note_;
    bool pause_failed_ = false;
    QDateTime answered_at_;
    bool running_ = false;
    bool reading_state_ = false;
    bool reading_days_ = false;
    bool reading_trades_ = false;
    bool reading_policies_ = false;
    bool sending_ = false;
    int engine_ = 0; ///< bumped whenever the engine read changes (drop): a pause is bound to one
};

} // namespace fincept::services
