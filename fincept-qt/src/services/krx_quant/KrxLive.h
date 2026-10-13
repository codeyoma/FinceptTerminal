#pragma once

#include "services/krx_quant/Board.h"
#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/KrxQuantService.h"
#include "services/krx_quant/KrxStream.h"
#include "services/krx_quant/Signals.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <optional>

namespace fincept::services {

/// What the KRX prediction screen shows, kept current: it follows the engine's stream,
/// reads the engine's API on connecting and on a schedule, merges both into a Board, and
/// judges the signal alerts. The screen owns it (no singleton) and only renders what it
/// says and passes on the user's choices: upstream's screen contract (docs/ARCHITECTURE.md,
/// "screens") keeps state and logic out of screens.
///
/// Reads: with the stream connected, the status every 5 s (the feeds' silences), the
/// symbols, latest predictions and members every 30 s, the chosen symbol's chart and the
/// leaderboard every 60 s (and the chart on a new bar close, the leaderboard 3 s after
/// outcomes), the macro row every 5 min. The stats of each predictor a detail window
/// shows are read with the leaderboard, each on its own. Without the stream, the status, predictions,
/// members and leaderboard every 5 s; the chart and the macro row as with it. While the
/// screen is hidden only what the alerts need is read, and an address applied elsewhere.
class KrxLive : public QObject {
    Q_OBJECT
  public:
    explicit KrxLive(QObject* parent = nullptr);

    /// Follow the engine at the address in the settings.
    void start();
    /// Follow the engine at a new address, from scratch.
    void apply_base_url(const QString& url);
    /// Whether the screen is shown; on showing, everything is read again.
    void set_shown(bool shown);
    /// The symbol whose chart is read.
    void select(const QString& symbol);
    void set_leaderboard_query(const KrxQuantService::LeaderboardQuery& query);
    /// The algorithms whose stats are read with the leaderboard, on its query (a detail
    /// window each); a new one's at once.
    void set_details(const QStringList& algorithms);
    void set_alerts(bool on, const krx_quant::SignalRule& rule);
    /// Show one bar close's judgements of the chosen symbol (a chart icon clicked, krx-quant
    /// #107) instead of the latest's; an invalid time goes back to the latest.
    void pin(const QDateTime& bar_close);
    /// The chart's view of ``minutes`` (krx-quant #109) starts ``from`` of its bars (a logical
    /// index) from its left end: near it, the bars before are read (krx-quant #108).
    void view_from(double from, int minutes = 1);
    /// The chart shows the view of ``minutes``: the day view (krx_quant::kDayView, krx-quant
    /// #111) reads the engine's day candles if it has none yet.
    void set_view(int minutes);
    /// The bar close pinned; invalid while the latest is followed.
    const QDateTime& pinned() const { return pinned_; }
    /// Its judgements as last read, and the algorithms' statuses then.
    const QList<krx_quant::Prediction>& pinned_predictions() const { return pinned_predictions_; }
    const QMap<QString, QString>& statuses() const { return statuses_; }

    const krx_quant::Board& board() const { return board_; }
    /// The engine's status; none while it cannot be read.
    const std::optional<krx_quant::EngineStatus>& status() const { return status_; }
    /// The macro row's last answer; none before one, or since a read failed.
    const std::optional<krx_quant::Macro>& macro() const { return macro_; }
    /// Why the macro row's last read failed; empty once one works.
    const QString& macro_failure() const { return macro_failure_; }
    const QString& selected() const { return selected_; }
    /// The chosen symbol's bars: the day's, after those read before as the chart scrolled
    /// back (krx-quant #108).
    QList<krx_quant::Bar> bars() const { return krx_quant::join_bars(older_, bars_); }
    /// The day's 1-minute bars alone, with the one being made.
    const QList<krx_quant::Bar>& day_bars() const { return bars_; }
    /// The day view's candles (krx-quant #111): those the engine stored, read as the view
    /// scrolled back, then today's made of the day's bars on the engine's market.
    QList<krx_quant::Bar> day_candles() const { return krx_quant::day_candles(candles_, bars_, candle_venue_); }
    /// The market of the engine's day candles: "UN" (integrated) or "KRX"; empty before any.
    const QString& candle_venue() const { return candle_venue_; }
    /// Why the engine could not be read; empty once it can.
    const QString& failure() const { return failure_; }
    /// The stream's state in Korean ("스트림 연결됨", why it is down and when it is tried again).
    const QString& stream_state() const { return stream_state_; }
    /// What the last refresh could not read (some symbols, the members).
    const QStringList& read_errors() const { return read_errors_; }
    /// The engine clock now, from its last status or stream event.
    QDateTime engine_now() const;

  signals:
    void base_url_changed(const QString& url);
    void status_changed();      ///< the status, the failure, the stream's state or the engine clock
    void predictions_changed(); ///< the board: symbols, latest predictions, final consensus, members
    void chart_changed();       ///< the chosen symbol's bars (or none, while they are read)
    /// Only the chosen symbol's last bar moved: the bar being made (krx-quant #110).
    void live_changed();
    void chart_failed(const QString& reason);
    void macro_changed(const fincept::krx_quant::Macro& macro);
    void macro_failed(const QString& reason);
    void leaderboard_changed(const fincept::krx_quant::Leaderboard& leaderboard);
    void leaderboard_failed(const QString& reason);
    void stats_changed(const fincept::krx_quant::AlgorithmStats& stats); ///< a detail's, on the leaderboard's query
    void stats_failed(const QString& algorithm, const QString& reason);
    void pinned_changed(); ///< pinned or back to the latest, or its judgements read
    void pinned_failed(const QString& reason);
    /// The bars before the chart's could not be read; those shown stay (krx-quant #108).
    void older_failed(const QString& reason);
    void signal_raised(const fincept::krx_quant::Signal& signal);

  private:
    void tick();
    void read_everything();
    void read_leaderboard();
    /// The stats of every detail window.
    void read_details();
    void on_snapshot(const KrxQuantService::Snapshot& snapshot);
    /// A status read; ``with_snapshot`` when it came with the predictions.
    void on_status(const krx_quant::EngineStatus& status, bool with_snapshot);
    void on_failure(const QString& reason);
    void on_chart(const QString& symbol, const QList<krx_quant::Bar>& bars, const QList<krx_quant::Prediction>& finals);
    void on_event(const krx_quant::Event& event);
    void consider(const krx_quant::Prediction& final, const QDateTime& now);
    void set_engine_clock(const QDateTime& now);
    /// A new bar close of the chosen symbol has a new bar to read.
    void follow_bar_close(const QDateTime& shown_before);

    KrxQuantService* service_ = nullptr;
    KrxStream* stream_ = nullptr;
    krx_quant::Board board_;
    krx_quant::Alerts alerts_;
    bool alerts_on_ = true;
    bool shown_ = false;
    QString selected_;
    QList<krx_quant::Bar> bars_;
    std::optional<krx_quant::Bar> live_;         ///< the chosen symbol's bar being made, as last sent
    QList<krx_quant::Bar> older_;                ///< read before the day's, as the chart scrolled back
    bool oldest_ = false;                        ///< the engine has nothing before older_
    double view_from_ = krx_quant::kOlderMargin; ///< where the chart's view last started
    int view_minutes_ = 1;                       ///< and its minutes
    QList<krx_quant::Bar> candles_;              ///< the engine's day candles read so far, oldest first
    QString candle_venue_;                       ///< their market
    bool candles_oldest_ = false;                ///< the engine has nothing before candles_
    /// Back to today's bars only: a symbol or an engine changed, or the engine is lost.
    void forget_older();
    KrxQuantService::LeaderboardQuery query_;
    QStringList details_; ///< the algorithms whose stats are read
    QDateTime pinned_;
    QList<krx_quant::Prediction> pinned_predictions_;
    QMap<QString, QString> statuses_;
    std::optional<krx_quant::EngineStatus> status_;
    std::optional<krx_quant::Macro> macro_;
    QString macro_failure_;
    QString failure_;
    QString stream_state_;
    QStringList read_errors_;
    qint64 clock_offset_ms_ = 0; ///< the engine clock minus this one
    int ticks_ = 0;
    QTimer timer_;
    QTimer leaderboard_soon_; ///< a moment after outcomes come in
};

} // namespace fincept::services

Q_DECLARE_METATYPE(fincept::krx_quant::Signal)
