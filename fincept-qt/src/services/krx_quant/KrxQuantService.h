#pragma once

#include "services/krx_quant/BarView.h"
#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace fincept::services {

/// Reads the KRX prediction engine (a separate service) over its HTTP API v1 for the KRX
/// screen's live state (KrxLive), which owns it. Screens never call HttpClient directly.
///
/// The engine's address is a setting (`krx_quant/base_url`), empty until set. Nothing is
/// cached: each read asks the engine again. Each kind of read runs one at a time (a slow
/// engine is not asked again and again); a restart (a new address) drops every answer
/// still under way.
class KrxQuantService : public QObject {
    Q_OBJECT

  public:
    /// The status, the symbols, each symbol's latest predictions and the consensus members,
    /// read in one refresh.
    struct Snapshot {
        krx_quant::EngineStatus status;
        QList<krx_quant::SymbolQuote> symbols;
        QMap<QString, krx_quant::Latest> latest; ///< by symbol; missing where its answer failed
        std::optional<QStringList> members;      ///< none where its answer failed
        QStringList errors;                      ///< why those answers failed
    };

    /// What a leaderboard is asked for (GET /v1/leaderboard).
    struct LeaderboardQuery {
        QString symbol = "all";
        QString period = "today"; ///< "today", "5d" or "20d"
        bool special = false;     ///< include the predictions a VI, circuit breaker or sidecar touched
        /// The kind ranked (krx-quant #182): "direction" (asked without ``kind``, as before) or another.
        /// An algorithm's stats are of its own kind: they are asked without it.
        QString kind = "direction";

        /// The API's value for ``special``: "include" or "exclude".
        QString special_value() const { return special ? "include" : "exclude"; }
        bool operator==(const LeaderboardQuery&) const = default;
        /// Whether ``answer`` (an AlgorithmStats) answers this query: its symbol, period and special
        /// situations; a stats answer is of its predictor's kind.
        template <typename Answer>
        bool matches(const Answer& answer) const {
            return answer.symbol == symbol && answer.period == period && answer.special == special_value();
        }
        /// Whether ``answer`` answers this query, its kind too.
        bool matches(const krx_quant::Leaderboard& answer) const {
            return answer.kind == kind && answer.symbol == symbol && answer.period == period &&
                   answer.special == special_value();
        }
    };

    explicit KrxQuantService(QObject* parent = nullptr);

    static QString base_url();
    /// Save the engine's address (a trailing slash is dropped).
    static void set_base_url(const QString& url);
    /// The address this service reads: the setting's when it last (re)started.
    const QString& url() const { return url_; }

    /// Read a snapshot from one address, the setting's when the refresh starts. Either
    /// snapshot_ready or refresh_failed follows. Skipped while one is under way, unless the
    /// setting changed since (another KRX panel applied an address): then it restarts.
    void refresh();
    /// Drop every read under way and refresh from the setting's address.
    void restart();
    /// Drop every read under way and take the setting's address, reading nothing (restart() without
    /// its refresh): for a reader of one answer alone, e.g. a dashboard tile of the status
    /// (codeyoma/krx-quant#302). base_url_changed follows when the address is a new one.
    void rebind();
    /// The status alone (its feeds' silences change by the second): status_ready or refresh_failed.
    void read_status();
    /// A symbol's 1-minute bars and predictions over the day, read at once (krx-quant #239):
    /// chart_ready or chart_failed once both answered. The predictions are every predictor's
    /// with ``all`` (models selected for the chart, krx-quant #159), else the final consensus's
    /// and "늘 보합"'s (always made where there is a reference price, so the bar close's truth
    /// shows while the final consensus has no members).
    void read_chart(const QString& symbol, bool all = false);
    /// The 1-minute bars that start before the chart's first one (``read.before``, krx-quant
    /// #108), without their predictions (read_predictions, krx-quant #239): older_ready or
    /// older_failed. Asked again while one is read, it is not read (false): KrxLive asks again
    /// once that one answers.
    bool read_older(const QString& symbol, const krx_quant::OlderRead& read);
    /// The predictions of the ``limit`` bar closes before ``before`` (invalid: before now) for
    /// the chart's icons, as read_chart's: every predictor's with ``all`` (krx-quant #159), else
    /// the final consensus's and "늘 보합"'s at once (krx-quant #239): predictions_ready or
    /// predictions_failed. Asked again while one is read, it is not read (false).
    bool read_predictions(const QString& symbol, const QDateTime& before, int limit, bool all);
    /// kCandleRead candles of ``period`` ("day", "week", "month", "year") the engine stored
    /// before ``before`` (invalid: before now), for the candle views (krx-quant #111, #112):
    /// candles_ready or candles_failed. Asked again while one is read, it is not read:
    /// KrxLive asks again once that one answers.
    void read_candles(const QString& symbol, const QString& period, const QDateTime& before);
    /// Every prediction of one bar close (krx-quant #107) and the algorithms' statuses now:
    /// close_ready or close_failed.
    void read_close(const QString& symbol, const QDateTime& bar_close);
    /// leaderboard_ready or leaderboard_failed. Asked again while one is under way, the
    /// latest query is read once it ends, and the stale answer is dropped; asked for the one
    /// under way, that one answers (a slow engine polled every 5 s still answers).
    void read_leaderboard(const LeaderboardQuery& query);
    /// An algorithm's stats for a leaderboard query: stats_ready or stats_failed. Each
    /// algorithm's are read on their own, several at once (a detail window each); asked
    /// again while that algorithm's are under way, likewise.
    void read_stats(const QString& algorithm, const LeaderboardQuery& query);
    void read_macro(); ///< macro_ready or macro_failed
    /// What the engine says about each algorithm (GET /v1/algorithms: card, inputs, model,
    /// status history; krx-quant #163): algorithms_ready or algorithms_failed. Asked again
    /// while one is read, it is not read.
    void read_algorithms();
    /// The kinds of prediction the engine makes (GET /v1/kinds, krx-quant #179, #182):
    /// kinds_ready or kinds_failed (an engine before #179 has no such answer). Asked again while
    /// one is read, it is not read.
    void read_kinds();
    /// A symbol's predictions of ``kind`` (besides the direction) at its latest bar close
    /// (krx-quant #182): kind_latest_ready or kind_latest_failed. Asked again while one is read,
    /// it is not read.
    void read_kind_latest(const QString& symbol, const QString& kind);
    /// A symbol's predictions of ``kind`` of the ``limit`` bar closes before ``before`` (invalid:
    /// before now), for the chart's hover boxes (krx-quant #182): kind_history_ready or
    /// kind_history_failed. Asked again while one is read, it is not read: KrxLive asks again.
    void read_kind_history(const QString& symbol, const QString& kind, const QDateTime& before, int limit);
    /// A symbol's special situations from ``from`` to ``to`` (KST dates, both in; GET
    /// /v1/symbols/{symbol}/special, krx-quant #236): its VIs and the market's circuit breakers and
    /// sidecars, for the chart's bands. special_ready, also with none from an engine without the
    /// answer (404, before krx-quant #236), or special_failed. Asked again while one is read, it is
    /// not read: KrxLive asks again once that one answers.
    void read_special(const QString& symbol, const QDate& from, const QDate& to);
    /// A symbol's daily supply data from ``from`` to ``to`` (KST dates, both in; GET
    /// /v1/symbols/{symbol}/supply, krx-quant #226), for the chart's supply indicators. supply_ready,
    /// with ``served`` false and none from an engine without the answer (404, before krx-quant #226),
    /// or supply_failed. Asked again while one is read, it is not read: KrxLive asks again once that
    /// one answers.
    void read_supply(const QString& symbol, const QDate& from, const QDate& to);
    /// What a bar's window shows (krx-quant #255, krx_quant::BarRead), read at once: every prediction
    /// of ``bar_close`` (GET …/predictions?bar_close=), the algorithms' statuses now (GET
    /// /v1/algorithms), the last kBarReadBars 1-minute bars before its scoring time (GET
    /// …/bars?before=&limit=), each of ``kinds``' predictions there (GET
    /// …/kinds/{kind}/predictions?bar_close=) and the symbol's special situations of that day (GET
    /// …/special?from=&to=). bar_ready once all answered, or bar_failed when its predictions or bars
    /// could not be read; without the statuses, a kind's predictions or the special situations it
    /// still shows (a kind's failure in BarRead::kind_failures). Each bar's on its own, several at
    /// once; asked again while that bar's is read, it is read again once that one answers.
    void read_bar(const QString& symbol, const QDateTime& bar_close, const QStringList& kinds);
    /// The pre-open briefing of ``day`` (invalid: the engine's default, today or the next trading
    /// day; GET /v1/briefing, codeyoma/krx-quant#304): briefing_ready, or briefing_failed (also from an
    /// engine before #304, which has no such answer). Asked again while one is read, it is not read.
    void read_briefing(const QDate& day);
    /// The symbols the engine predicts, with their names and logos (GET /v1/symbols, krx-quant #250),
    /// read alone: symbols_ready or symbols_failed. Asked again while one is read, it is not read.
    void read_symbol_list();
    /// A symbol's intraday flows of ``day`` (invalid: the engine's default, today or the last trading
    /// day; GET /v1/symbols/{symbol}/flow, codeyoma/krx-quant#306): flow_ready or flow_failed (also from an
    /// engine before #306). Asked again while one is read, it is not read.
    void read_flow(const QString& symbol, const QDate& day);
    /// The cost map for ``query``'s symbol, period and special situations (its kind is not asked;
    /// GET /v1/cost-map, codeyoma/krx-quant#307): cost_map_ready or cost_map_failed (also from an
    /// engine before #307). Asked again while one is read, the latest query is read once it ends and
    /// the answer under way is dropped, as read_leaderboard's.
    void read_cost_map(const LeaderboardQuery& query);

  signals:
    void snapshot_ready(const fincept::services::KrxQuantService::Snapshot& snapshot);
    void refresh_failed(const QString& reason);
    void status_ready(const fincept::krx_quant::EngineStatus& status);
    void chart_ready(const QString& symbol, const QList<fincept::krx_quant::Bar>& bars,
                     const QList<fincept::krx_quant::Prediction>& predictions, bool all);
    void chart_failed(const QString& symbol, const QString& reason);
    void older_ready(const QString& symbol, const QList<fincept::krx_quant::Bar>& bars);
    void older_failed(const QString& symbol, const QString& reason);
    void predictions_ready(const QString& symbol, const QDateTime& before,
                           const QList<fincept::krx_quant::Prediction>& predictions, bool all);
    void predictions_failed(const QString& symbol, const QString& reason);
    void candles_ready(const QString& symbol, const QString& period, const QDateTime& before,
                       const fincept::krx_quant::Candles& candles);
    void candles_failed(const QString& symbol, const QString& period, const QString& reason);
    void close_ready(const QString& symbol, const QDateTime& bar_close,
                     const QList<fincept::krx_quant::Prediction>& predictions, const QMap<QString, QString>& statuses);
    void close_failed(const QString& symbol, const QDateTime& bar_close, const QString& reason);
    void leaderboard_ready(const fincept::krx_quant::Leaderboard& leaderboard);
    void leaderboard_failed(const QString& reason);
    void stats_ready(const fincept::krx_quant::AlgorithmStats& stats);
    void stats_failed(const QString& algorithm, const QString& reason);
    void macro_ready(const fincept::krx_quant::Macro& macro);
    void macro_failed(const QString& reason);
    void algorithms_ready(const QMap<QString, fincept::krx_quant::AlgorithmInfo>& algorithms);
    void algorithms_failed(const QString& reason);
    void kinds_ready(const QList<fincept::krx_quant::KindInfo>& kinds);
    void kinds_failed(const QString& reason);
    void kind_latest_ready(const fincept::krx_quant::KindLatest& latest);
    void kind_latest_failed(const QString& symbol, const QString& kind, const QString& reason);
    void kind_history_ready(const QString& symbol, const QString& kind, const QDateTime& before,
                            const QList<fincept::krx_quant::KindPrediction>& predictions);
    void kind_history_failed(const QString& symbol, const QString& kind, const QString& reason);
    void special_ready(const QString& symbol, const QDate& from, const QDate& to,
                       const QList<fincept::krx_quant::SpecialWindow>& windows);
    void special_failed(const QString& symbol, const QDate& from, const QDate& to, const QString& reason);
    void supply_ready(const QString& symbol, const QDate& from, const QDate& to,
                      const QList<fincept::krx_quant::SupplyDay>& days, bool served);
    void supply_failed(const QString& symbol, const QDate& from, const QDate& to, const QString& reason);
    void bar_ready(const fincept::krx_quant::BarRead& read);
    void bar_failed(const QString& symbol, const QDateTime& bar_close, const QString& reason);
    void briefing_ready(const QDate& day, const fincept::krx_quant::Briefing& briefing);
    void briefing_failed(const QDate& day, const QString& reason);
    void symbols_ready(const QList<fincept::krx_quant::SymbolQuote>& symbols);
    void symbols_failed(const QString& reason);
    void flow_ready(const QString& symbol, const QDate& day, const fincept::krx_quant::Flow& flow);
    void flow_failed(const QString& symbol, const QDate& day, const QString& reason);
    void cost_map_ready(const fincept::services::KrxQuantService::LeaderboardQuery& query,
                        const fincept::krx_quant::CostMap& map);
    void cost_map_failed(const fincept::services::KrxQuantService::LeaderboardQuery& query, const QString& reason);
    /// The address this service reads changed (also when another panel set it).
    void base_url_changed(const QString& url);

  private:
    /// A step of a read: the answer to one GET, or why it failed.
    using Step = std::function<void(Result<QJsonDocument>)>;
    enum Kind {
        kRefresh,
        kStatus,
        kChart,
        kLeaderboard,
        kMacro,
        kClose,
        kOlder,
        kCandles,
        kPredictions,
        kAlgorithms,
        kKindList,    ///< GET /v1/kinds
        kKindLatest,  ///< a kind's latest predictions
        kKindHistory, ///< a kind's predictions over bar closes
        kSpecial,     ///< a symbol's special situations over days (krx-quant #236)
        kSupply,      ///< a symbol's daily supply data over days (krx-quant #226)
        kBriefing,    ///< a day's pre-open briefing (codeyoma/krx-quant#304)
        kSymbolList,  ///< the symbols alone
        kFlow,        ///< a symbol's intraday flows (codeyoma/krx-quant#306)
        kCostMap,     ///< the cost map (codeyoma/krx-quant#307)
        kKinds        ///< how many kinds of read there are
    };

    /// Start a read of ``kind`` unless one is under way; false if skipped.
    bool begin(Kind kind);
    void end(Kind kind) { busy_[kind] = false; }
    void get(const QString& path, Step step);
    void read_symbols(Snapshot snapshot);
    void read_members(Snapshot snapshot);
    /// A chart's predictions at ``path`` (a query string without the algorithm): every
    /// predictor's with ``all``, else the final consensus's and "늘 보합"'s, read at once
    /// (krx-quant #239). ``done`` gets them once all answered, or why they could not be read.
    void read_chart_predictions(const QString& symbol, const QString& query, bool all,
                                std::function<void(Result<QList<krx_quant::Prediction>>)> done);
    void read_latest(Snapshot snapshot);
    void finish_with(const Snapshot& snapshot);
    /// The refresh failed: why, after ending it.
    void fail_refresh(const QString& reason);

    int generation_ = 0;
    bool busy_[kKinds] = {};
    LeaderboardQuery leaderboard_asked_;                      ///< the leaderboard under way, or the last
    std::optional<LeaderboardQuery> next_leaderboard_;        ///< asked for while another was under way
    LeaderboardQuery cost_map_asked_;                         ///< the cost map under way, or the last
    std::optional<LeaderboardQuery> next_cost_map_;           ///< asked for while another was under way
    std::optional<std::pair<QString, QDateTime>> next_close_; ///< a bar close asked for while another was read
    /// A bar close's read ended: the one asked for meanwhile is read next.
    void close_done();
    /// An algorithm's stats under way: the query asked, and one asked for meanwhile.
    struct StatsRead {
        LeaderboardQuery asked;
        std::optional<LeaderboardQuery> next;
    };
    QHash<QString, StatsRead> stats_reads_; ///< by algorithm
    /// A bar's read under way (krx-quant #255): whether it was asked for again meanwhile, with what kinds.
    struct BarReadAsked {
        bool again = false;
        QStringList kinds;
    };
    QHash<QString, BarReadAsked> bar_reads_; ///< by krx_quant::bar_key
    QString url_;                            ///< the address of the reads under way, or of the last ones
};

/// Why a request failed, in Korean, from HttpClient's error string.
QString krx_failure(const std::string& error);

} // namespace fincept::services

Q_DECLARE_METATYPE(fincept::services::KrxQuantService::Snapshot)
