#pragma once

#include "services/krx_quant/BarView.h"
#include "services/krx_quant/Board.h"
#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/KindView.h"
#include "services/krx_quant/KrxQuantService.h"
#include "services/krx_quant/KrxStream.h"
#include "services/krx_quant/Signals.h"

#include <QDateTime>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <optional>
#include <utility>

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
/// outcomes), the macro row every 5 min. The chosen symbol's special situations over the days of
/// its bars held (krx-quant #236) every 60 s while shown, and as soon as those days change; its daily
/// supply data over the days of the view's bars held (krx-quant #226) every 5 min while shown, and as
/// soon as those days change. The stats of each predictor
/// a detail window shows are read with the leaderboard, each on its own. Without the stream, the status, predictions,
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
    /// The bars whose windows are open (krx-quant #255), each a symbol and a bar close: a new one's
    /// is read at once (KrxQuantService::read_bar), then read again until krx_quant::bar_settled —
    /// as the stream brings its bar close's predictions, outcomes and answers or a later bar close
    /// of its symbol (another of its minutes is over), and without the stream each tick; with it,
    /// every minute too. A window closed is no longer read.
    void set_bars(const QList<QPair<QString, QDateTime>>& bars);
    void set_alerts(bool on, const krx_quant::SignalRule& rule);
    /// The models selected for the chart (krx-quant #159): their consensus is drawn in place
    /// of the final one. While some are selected and the view is of 1-minute bars, the chart
    /// reads every predictor's predictions; on selecting, those of the bar closes shown are
    /// read again from the latest back, and the bars before wait for them.
    void set_selection(const QStringList& selection);
    const QStringList& selection() const { return selection_; }
    /// The predictors' statuses now, from the last leaderboard read: the hover's.
    const QMap<QString, QString>& standings() const { return standings_; }
    /// Show the kind ``saved`` (krx-quant #182: the user's choice, kept in ``krx_quant/kind``)
    /// besides the direction: its latest predictions (the panel), its predictions over the bars
    /// shown (the chart's hover boxes) and its leaderboard are read. Shown only while the engine
    /// makes it and the screen can show it (kind_choices), else the direction: the choice stays.
    void set_kind(const QString& saved);
    /// The kind shown: "direction", or another set_kind chose and the engine makes.
    const QString& kind() const { return kind_; }
    /// What the engine says of its kinds (GET /v1/kinds); none before it answered, or from an
    /// engine before krx-quant #179 (the direction alone).
    const std::optional<QList<krx_quant::KindInfo>>& kinds() const { return kinds_; }
    /// The kind shown as the engine describes it; none for the direction from an older engine.
    std::optional<krx_quant::KindInfo> kind_info() const { return krx_quant::kind_named(kinds_, kind_); }
    /// The predictions of the kind shown (not the direction's): the latest of the chosen symbol,
    /// and its predictions over the bar closes read.
    const krx_quant::KindBoard& kind_board() const { return kind_board_; }
    /// The kind's predictors' statuses now, from its last leaderboard read: its hover's.
    const QMap<QString, QString>& kind_standings() const { return kind_standings_; }
    /// What the engine says about each algorithm (krx-quant #163), read with the detail
    /// windows' stats; empty before the first answer, or for a baseline or consensus.
    const QMap<QString, krx_quant::AlgorithmInfo>& algorithms() const { return algorithms_; }
    /// Why the last read of them failed (they are dropped then, not left stale); empty once one works.
    const QString& algorithms_failure() const { return algorithms_failure_; }
    /// Show one bar close's judgements of the chosen symbol (a chart icon or 1-minute bar clicked,
    /// krx-quant #107, #249) instead of the latest's; an invalid time goes back to the latest.
    void pin(const QDateTime& bar_close);
    /// The chart's view of ``minutes`` (krx-quant #109) shows its bars from ``from`` to ``to``
    /// (logical indexes from its left end): with less than a screen of them before ``from``, the
    /// bars before are read (krx-quant #108, #239).
    void view_from(double from, double to, int minutes = 1);
    /// The chart shows the view of ``minutes``. A candle view (krx_quant::candle_view: day,
    /// week, month, year; krx-quant #111, #112) reads the engine's latest candles of its
    /// period if it has none yet, or none since the engine day changed, and so do its days
    /// for a longer view (its last periods are made of them). The chart's reads call it
    /// again, so a screen left open past midnight gets the day before's stored candles. A
    /// minute view with no bar today (before the open, a holiday) reads the last day's bars,
    /// however the view was reached; the bars before that the indicators want otherwise.
    void set_view(int minutes);
    /// The chart's indicators want ``bars`` bars of the view (krx-quant #222,
    /// krx_quant::history_bars) and the bars back to ``back_to`` (the anchored VWAP's anchor;
    /// invalid: none): the bars (a candle view's candles) before the first are read, a page at a
    /// time, until it has them or kMaxHistoryBars of the view, or the engine has none before.
    /// With ``whole_days`` (the VWAP on in a minute view), also back past the start of the first
    /// screen's earliest date (krx_quant::first_screen_day, worked out once for the symbol and
    /// view), so the days on screen are whole. With ``cumulative`` (the AD line or the OBV on,
    /// krx-quant #223), also back past the moment they are 0 from (krx_quant::cumulative_start:
    /// the last bar's date, or its year in a candle view), likewise worked out once.
    void set_history(int bars, const QDateTime& back_to = {}, bool whole_days = false, bool cumulative = false);
    /// Whether the engine has no 1-minute bars before the first held (an older read came back
    /// empty): the first day held is whole then.
    bool reached_start() const { return oldest_; }
    /// Likewise for the view ``view``: a candle view's candles (krx-quant #223).
    bool reached_start(int view) const;
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
    /// A candle view's candles: the day view's (krx-quant #111) those the engine stored, read
    /// as the view scrolled back, then today's made of the day's bars on the engine's market;
    /// a week, month or year view's (krx-quant #112) the engine's finished ones, then those
    /// made of the day view's candles.
    QList<krx_quant::Bar> view_candles(int view) const;
    /// The chosen symbol's special situations over the days of its 1-minute bars held (krx-quant
    /// #236): its VIs and the market's circuit breakers and sidecars, in start order. None before
    /// they are read, from an engine without them, or while the engine cannot be read.
    const QList<krx_quant::SpecialWindow>& special() const { return special_; }
    /// The chosen symbol's daily supply data over the days of the view's bars held (krx-quant #226):
    /// from the first 1-minute bar (a candle view's first candle) to the engine's day, oldest first.
    /// None before it is read, from an engine without it, or while the engine cannot be read.
    const QList<krx_quant::SupplyDay>& supply() const { return supply_; }
    /// Whether the engine gives the daily supply data: false once it answered 404 (an engine before
    /// krx-quant #226), until another address is applied. True before it answered.
    bool supply_served() const { return supply_served_; }
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
    /// The chosen symbol's predictions, the selection or the statuses changed: the chart's
    /// icons and hover boxes (krx-quant #159).
    void marks_changed();
    /// Another symbol is chosen (krx-quant #221): the chart shows its own view and style. Comes
    /// before its bars are read.
    void selected_changed();
    void chart_changed(); ///< the chosen symbol's bars (or none, while they are read)
    /// Only the chosen symbol's last bar moved: the bar being made (krx-quant #110).
    void live_changed();
    void chart_failed(const QString& reason);
    /// The chosen symbol's special situations changed (krx-quant #236): the chart's bands.
    void special_changed();
    /// The chosen symbol's daily supply data, or whether the engine gives it, changed (krx-quant
    /// #226): the chart's supply indicators and the panel's list.
    void supply_changed();
    void macro_changed(const fincept::krx_quant::Macro& macro);
    void macro_failed(const QString& reason);
    void leaderboard_changed(const fincept::krx_quant::Leaderboard& leaderboard);
    void leaderboard_failed(const QString& reason);
    void stats_changed(const fincept::krx_quant::AlgorithmStats& stats); ///< a detail's, on the leaderboard's query
    void stats_failed(const QString& algorithm, const QString& reason);
    void algorithms_changed(); ///< what the engine says about the algorithms (krx-quant #163)
    void kinds_changed();      ///< what the engine says of its kinds (the selector's choices)
    void kind_changed();       ///< another kind is shown (krx-quant #182)
    /// The kind's predictions of the chosen symbol or their statuses changed: its panel and
    /// hover boxes (krx-quant #182).
    void kind_predictions_changed();
    void pinned_changed(); ///< pinned or back to the latest, or its judgements read
    void pinned_failed(const QString& reason);
    /// The bars before the chart's could not be read; those shown stay (krx-quant #108).
    void older_failed(const QString& reason);
    void signal_raised(const fincept::krx_quant::Signal& signal);
    /// What a bar's window shows was read (krx-quant #255), or could not be.
    void bar_changed(const fincept::krx_quant::BarRead& read);
    void bar_failed(const QString& symbol, const QDateTime& bar_close, const QString& reason);

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
    void on_chart(const QString& symbol, const QList<krx_quant::Bar>& bars,
                  const QList<krx_quant::Prediction>& predictions, bool all);
    /// Read the chosen symbol's chart, every predictor's predictions while all_predictors().
    void read_chart();
    /// Models are selected and the view is of 1-minute bars: the chart needs every
    /// predictor's predictions (krx-quant #159).
    bool all_predictors() const;
    /// The close of the first bar shown (1-minute); invalid without bars.
    QDateTime oldest_close() const;
    /// In the 1-minute view, read the predictions its icons need back to the first bar shown,
    /// a page at a time from the latest back (krx_quant::marks_before): while all_predictors(),
    /// every predictor's (those read with the final consensus alone again, krx-quant #159), else
    /// the final consensus's and "늘 보합"'s (krx-quant #239). With ``ahead`` (bars before the
    /// first are being read), the page before those read even once they reach the first bar.
    void ensure_marks(bool ahead = false);
    /// Where the predictions were read from, once a page of them (``page``) came in: every
    /// predictor's with ``all``, else the final consensus's and "늘 보합"'s.
    void marks_read(const QList<krx_quant::Prediction>& page, bool all);
    /// The bars before the first, if the chart's view (view_from) is near it: a candle view's
    /// candles, a minute view's 1-minute bars.
    void read_older();
    /// Read those 1-minute bars before the first, and their icons' predictions at the same time.
    void read_bars(const krx_quant::OlderRead& read);
    void on_event(const krx_quant::Event& event);
    void consider(const krx_quant::Prediction& final, const QDateTime& now);
    void set_engine_clock(const QDateTime& now);
    /// A new bar close of the chosen symbol has a new bar to read.
    void follow_bar_close(const QDateTime& shown_before);
    /// Show the kind the choice and the engine's kinds allow, if another: read it from scratch.
    void apply_kind();
    /// The kind's latest predictions of the chosen symbol, unless the direction is shown.
    void read_kind_latest();
    /// The kind's predictions of the chosen symbol over the bars shown, unless the direction is
    /// shown: the latest page first (again with ``latest``, on each chart read), then back to
    /// the first bar shown, a page at a time.
    void read_kind_history(bool latest = false);
    /// The kinds besides the direction a bar's window reads (krx-quant #255): those of
    /// krx_quant::bar_kinds the engine makes.
    QStringList window_kinds() const;
    /// Read the bar window ``key``'s bar (bars_watched_).
    void read_bar(const QString& key);
    /// Read each bar window not settled yet.
    void read_bars_due();
    /// The windows whose bar ``event`` concerns, read again a moment later: its bar close's
    /// predictions, outcomes and answers, or a later bar close of its symbol.
    void bars_touched(const krx_quant::Event& event);

    KrxQuantService* service_ = nullptr;
    KrxStream* stream_ = nullptr;
    krx_quant::Board board_;
    krx_quant::Alerts alerts_;
    bool alerts_on_ = true;
    bool shown_ = false;
    QString selected_;
    QList<krx_quant::Bar> bars_;
    bool day_read_ = false;                      ///< bars_ is the engine's answer: empty means no bar today yet
    std::optional<krx_quant::Bar> live_;         ///< the chosen symbol's bar being made, as last sent
    QList<krx_quant::Bar> older_;                ///< read before the day's, as the chart scrolled back
    bool oldest_ = false;                        ///< the engine has nothing before older_
    double view_from_ = krx_quant::kOlderMargin; ///< where the chart's view last started
    double view_width_ = 0;                      ///< and how many bars wide it was
    int view_minutes_ = 1;                       ///< and its minutes
    /// A candle view's candles as read from the engine (krx-quant #111, #112).
    struct Candles {
        QList<krx_quant::Bar> read; ///< so far, oldest first
        bool oldest = false;        ///< the engine has nothing before them
        QDate day;                  ///< the engine day their latest page was read
    };
    QMap<int, Candles> candles_; ///< by view
    QString candle_venue_;       ///< the market of the engine's candles: "UN" or "KRX"
    /// The engine clock's day in KST.
    QDate engine_day() const;
    /// Read ``view``'s latest candles unless read on this engine day (or the engine has none).
    void read_latest(int view);
    /// Back to today's bars only: a symbol or an engine changed, or the engine is lost.
    void forget_older();
    /// Read the next page before the first bar while the indicators want more (set_history).
    void read_history();
    /// The days whose special situations the chart wants (krx-quant #236): from the first 1-minute
    /// bar held to the engine's day, the last 60 at most (what the engine answers at once); none
    /// without a symbol or bars.
    std::optional<std::pair<QDate, QDate>> special_days() const;
    /// Read the chosen symbol's special situations over special_days (``always``), or only when those
    /// days are not the ones read.
    void read_special(bool always = true);
    QList<krx_quant::SpecialWindow> special_;
    std::optional<std::pair<QDate, QDate>> special_read_; ///< the days special_ is of
    /// The days whose daily supply data the chart wants (krx-quant #226): from the first bar of the
    /// view held (a minute view's first 1-minute bar, a candle view's first candle) to the engine's
    /// day, the last kSupplyDays at most; none without a symbol or bars.
    std::optional<std::pair<QDate, QDate>> supply_days() const;
    /// Read the chosen symbol's daily supply data over supply_days (``always``), or only when those
    /// days are not the ones read.
    void read_supply(bool always = true);
    QList<krx_quant::SupplyDay> supply_;
    std::optional<std::pair<QDate, QDate>> supply_read_;  ///< the days supply_ is of
    bool supply_served_ = true;                           ///< the engine answered it (not 404)
    int history_ = 0;                                     ///< bars of the view the indicators want
    QDateTime history_back_to_;                           ///< and the moment they reach back to
    bool whole_days_ = false;                             ///< and whole days on the first screen (the VWAP)
    QDateTime day_target_;    ///< where those start, from the first screen; invalid: not worked out yet
    bool cumulative_ = false; ///< and back past the AD line's and the OBV's 0 (krx-quant #223)
    QDateTime zero_target_;   ///< that moment, from the bars first held; invalid: not worked out yet
    KrxQuantService::LeaderboardQuery query_;
    QStringList details_; ///< the algorithms whose stats are read
    QDateTime pinned_;
    QList<krx_quant::Prediction> pinned_predictions_;
    QMap<QString, QString> statuses_;
    std::optional<krx_quant::EngineStatus> status_;
    std::optional<krx_quant::Macro> macro_;
    QString macro_failure_;
    QStringList selection_;                              ///< the models selected for the chart
    QMap<QString, QString> standings_;                   ///< the predictors' statuses, from the leaderboard
    QMap<QString, krx_quant::AlgorithmInfo> algorithms_; ///< the engine's cards, models and statuses
    QString algorithms_failure_;                         ///< why their last read failed
    /// The earliest bar close from which on every predictor's predictions were read; invalid:
    /// none (krx-quant #159); krx_quant::from_the_start: all.
    QDateTime full_from_;
    /// Likewise the final consensus's and "늘 보합"'s (krx-quant #239).
    QDateTime finals_from_;
    bool marks_paging_ = false; ///< a page of either is read
    // Another kind than the direction (krx-quant #182).
    std::optional<QList<krx_quant::KindInfo>> kinds_; ///< GET /v1/kinds; none before, or from an older engine
    QString saved_kind_ = krx_quant::kDirectionKind;  ///< the user's choice
    QString kind_ = krx_quant::kDirectionKind;        ///< the kind shown
    krx_quant::KindBoard kind_board_;
    QMap<QString, QString> kind_standings_; ///< the kind's predictors' statuses, from its leaderboard
    QDateTime kind_from_;                   ///< the earliest bar close from which on its predictions were read
    bool kind_paging_ = false;              ///< a page of them is read
    bool kind_latest_page_ = false;         ///< the latest page is to be read again
    QString failure_;
    QString stream_state_;
    QStringList read_errors_;
    qint64 clock_offset_ms_ = 0; ///< the engine clock minus this one
    int ticks_ = 0;
    QTimer timer_;
    QTimer leaderboard_soon_; ///< a moment after outcomes come in
    /// A bar window (krx-quant #255): its bar, and whether nothing more is to come for it.
    struct BarWatch {
        QString symbol;
        QDateTime bar_close;
        bool settled = false;
    };
    QMap<QString, BarWatch> bars_watched_; ///< by krx_quant::bar_key
    QSet<QString> bars_due_;               ///< those the stream touched, read when bars_soon_ fires
    QTimer bars_soon_;                     ///< a moment after the stream touched one: a bar close brings many
};

} // namespace fincept::services

Q_DECLARE_METATYPE(fincept::krx_quant::Signal)
