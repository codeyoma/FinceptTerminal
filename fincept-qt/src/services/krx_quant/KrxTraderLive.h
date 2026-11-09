#pragma once

#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/EngineData.h"
#include "services/krx_quant/TraderView.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QTimer;

namespace fincept::services {

class EnginePoller;
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
/// The screen's 1-minute chart of the trader's symbol (krx_quant::kTraderSymbol, codeyoma/krx-quant#537) is
/// read here too, without the predictions and the dashboard KrxLive reads for the KRX 4-minute screen: the
/// day's bars (GET …/bars) and the trader's buys and sells (GET …/trader-marks, krx-quant #398), the latest
/// page of them, at once when started and as each minute turns (trader::chart_ms); the bars before as the
/// chart scrolls back, and the marks back to the first bar held, a page at a time. An engine that answers
/// again after a failed read of the state has its chart read at once. Stopped, the chart reads nothing.
///
/// The decisions panel's (codeyoma/krx-quant#601): a book's decisions of the chart's day (that of its newest
/// bar; before any, the engine's today), the day whole, page after page (GET /v1/trader/decisions,
/// krx-quant #584), read with the chart: at once when started, as each minute turns, when the engine
/// answers again after a failed read of the state, on a new address, and when the chart's bars come of
/// another day; and at once when another book is chosen. An engine without them (404) has none, and says
/// so: not a failure. Stopped, it reads nothing.
///
/// Its one write is the screen's pause (pause): it asks first, through the screen's ``confirm``, and
/// sends POST /v1/trader/pause only on a yes. Nothing here resumes the trader: that is the server's
/// alone (krx-quant trade resume). The engine's address is the KRX screen's setting; a new one drops
/// what was read and reads again. Its EnginePoller reads the address and keeps the state's schedule; the
/// chart's timer and the pause keep their own rules here.
class KrxTraderLive : public QObject {
    Q_OBJECT
  public:
    /// The trades asked for: the table's rows.
    static constexpr int kTradesShown = 100;
    /// The timers' object names: the state's schedule, and the chart's (tests fire them).
    static constexpr const char* kStateTimerName = "krxTraderStateTimer";
    static constexpr const char* kChartTimerName = "krxTraderChartTimer";
    /// ``now`` is the clock the schedule reads (tests give their own).
    explicit KrxTraderLive(QObject* parent = nullptr, std::function<QDateTime()> now = {});

    void start(); ///< read at once, then on schedule
    void stop();  ///< read no more until started again
    bool running() const;
    /// Start when ``widget`` is shown, stop when it is hidden.
    void run_while_shown(QObject* widget);
    /// Read the state, the days, the trades, the policy versions and the chart now.
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
    const QDateTime& answered_at() const;
    QDateTime now() const;
    bool reading() const { return reading_state_ || reading_days_ || reading_trades_ || reading_policies_; }
    int next_ms() const; ///< 0 when stopped

    /// The chart's 1-minute bars (codeyoma/krx-quant#537): the day's, after those read before as the chart
    /// scrolled back. Before the open (no bar today yet) the last day's. On another day, or after the chart
    /// was not read for a while (hidden, a failed read) unless the day is the same, those read before are
    /// read again from the new day back.
    QList<krx_quant::Bar> chart_bars() const { return krx_quant::join_bars(older_, day_bars_); }
    /// The trader's buys and sells over them, newest first, as GET …/trader-marks gives them (every policy
    /// version's, practice trades among them); none from an engine without them (404).
    const QList<krx_quant::TraderMark>& marks() const { return marks_; }
    /// The chart's view shows its bars from ``from`` to ``to`` (logical indexes from its left end): with
    /// less than a screen of them before ``from``, the bars before are read (krx_quant::older_read).
    void view_from(double from, double to);
    /// Why the day's bars could not be read last (those shown stay); empty once they can.
    const QString& chart_failure() const { return chart_failure_; }
    /// Why the bars before could not be read last (those shown stay); empty once they can.
    const QString& older_failure() const { return older_failure_; }
    /// Whether the engine has no bars before the first held (a read of those before came back empty).
    bool reached_start() const { return oldest_; }
    /// Whether the day's bars, those before or a page of marks are being read.
    bool chart_reading() const { return reading_bars_ || reading_older_ || marks_paging_; }

    /// The book whose decisions are read (codeyoma/krx-quant#601): krx_quant::trader::kDefaultBook at first,
    /// the practice's. Another drops those read and reads its own at once (when started); an answer for the
    /// one before it is not kept. Kept for another engine; not saved.
    void set_decisions_book(const QString& book);
    const QString& decisions_book() const { return decisions_book_; }
    /// The book's decisions of the day, read whole; none before the first read, after another book was
    /// chosen or another engine set, and from an engine without them.
    const std::optional<krx_quant::TraderDecisions>& decisions() const { return decisions_; }
    /// Whether the engine has the decisions: false once it answered 404 (before krx-quant #584).
    bool decisions_served() const { return decisions_served_; }
    /// Why they could not be read last (those shown stay); empty once they can.
    const QString& decisions_failure() const { return decisions_failure_; }
    /// Whether a page of them is being read.
    bool decisions_reading() const { return reading_decisions_; }

  signals:
    /// Something to draw again: an answer, a failure, a pause sent or its answer, a read started.
    void changed();
    /// The chart's bars, marks or failures changed (codeyoma/krx-quant#537).
    void chart_changed();
    /// The decisions, whether the engine has them, their failure or their reading changed
    /// (codeyoma/krx-quant#601).
    void decisions_changed();

  private:
    /// The state, and the days and trades when due: the poller's read.
    void read();
    /// On a state's answer: the days and trades asked for when they are due.
    void check_history();
    void read_history();
    /// The trades of the policy chosen, unless they are being read.
    void read_trades();
    /// The days, the trades or the policy versions answered.
    void history_done();
    /// Another engine: what was read of the one before is dropped.
    void drop();
    /// A pause confirmed for an engine other than the one now read: not sent, said so. Always false.
    bool refuse_pause();
    /// The chart's day of bars and the latest page of marks now (or, the address changed, the new
    /// engine's), and again as the next minute turns.
    void read_chart();
    /// The bars before the first held, if the chart's view is near it.
    void read_older();
    /// Back to no bars before the day's: they may not reach the day's (another day, a gap in reading).
    void forget_older();
    /// The trader's marks: the latest page (again with ``latest``), then back to the first bar held, a page
    /// at a time (krx_quant::trader_before).
    void read_marks(bool latest = false);
    /// Back to no chart: another engine.
    void forget_chart();
    /// The decisions of the chosen book and the chart's day, from their latest page back to the day's first
    /// (codeyoma/krx-quant#601); once the read under way ends, if one is.
    void read_decisions();
    /// The chart's day: its newest bar's (KST); invalid without a bar.
    QDate chart_day() const;
    /// On an answer of the chart's bars: their day's decisions, if those read are another day's.
    void check_decisions_day();
    /// Back to no decisions: another engine.
    void forget_decisions();

    EnginePoller* poller_ = nullptr;     ///< its failure: the state's
    KrxQuantService* service_ = nullptr; ///< the poller's
    QTimer* chart_timer_ = nullptr; ///< the chart's next read (codeyoma/krx-quant#537)
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
    QDateTime history_asked_at_;  ///< when they were (now()'s clock)
    bool history_wanted_ = false; ///< read_now, a pause sent: the days and trades too
    QString days_failure_;
    QString trades_failure_;
    QString policies_failure_;
    QString pause_note_;
    bool pause_failed_ = false;
    bool reading_state_ = false;
    bool reading_days_ = false;
    bool reading_trades_ = false;
    bool reading_policies_ = false;
    bool sending_ = false;
    int engine_ = 0; ///< bumped whenever the engine read changes (drop): a pause is bound to one
    // The chart (codeyoma/krx-quant#537).
    QList<krx_quant::Bar> day_bars_;             ///< the day's, as last read
    QList<krx_quant::Bar> older_;                ///< read before them, as the chart scrolled back
    bool oldest_ = false;                        ///< the engine has nothing before older_
    bool reading_bars_ = false;                  ///< the day's are read
    bool reading_older_ = false;                 ///< those before are read
    int older_epoch_ = 0;                        ///< bumped whenever older_ is dropped (forget_older)
    int older_asked_ = 0;                        ///< older_epoch_ when the read under way was asked
    double view_from_ = krx_quant::kOlderMargin; ///< where the chart's view last started
    double view_width_ = 0;                      ///< and how many bars wide it was
    QList<krx_quant::TraderMark> marks_;         ///< the trader's buys and sells
    QDateTime marks_from_;                       ///< the earliest moment from which on they were read; invalid: none
    bool marks_paging_ = false;                  ///< a page of them is read
    bool marks_latest_ = false;                  ///< the latest page is to be read again
    QString chart_failure_;
    QString older_failure_;
    /// The day's bars were not read for a while since those held (hidden, a failed read): days may have
    /// passed, so older_ may not reach the next answer's day.
    bool stale_ = false;
    // The decisions (codeyoma/krx-quant#601).
    QString decisions_book_ = QString::fromLatin1(krx_quant::trader::kDefaultBook);
    std::optional<krx_quant::TraderDecisions> decisions_;   ///< the last read whole
    std::optional<krx_quant::trader::DecisionPages> pages_; ///< the read under way, its pages so far
    QDate decisions_day_;            ///< the day the read under way asked for; invalid: the engine's today
    bool decisions_served_ = true;   ///< false once the engine answered 404
    bool reading_decisions_ = false; ///< a page is being read
    bool decisions_wanted_ = false;  ///< asked for again while a read was under way
    QString decisions_failure_;
};

} // namespace fincept::services
