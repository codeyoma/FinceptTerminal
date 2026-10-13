#pragma once

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
        QString basis = "b";      ///< "a" or "b"
        bool special = false;     ///< include the predictions a VI, circuit breaker or sidecar touched

        /// The API's value for ``special``: "include" or "exclude".
        QString special_value() const { return special ? "include" : "exclude"; }
        bool operator==(const LeaderboardQuery&) const = default;
        /// Whether ``answer`` (a Leaderboard, or an AlgorithmStats) answers this query.
        template <typename Answer>
        bool matches(const Answer& answer) const {
            return answer.symbol == symbol && answer.period == period && answer.basis == basis &&
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
    /// The status alone (its feeds' silences change by the second): status_ready or refresh_failed.
    void read_status();
    /// A symbol's 1-minute bars and final consensus over the day: chart_ready or chart_failed.
    void read_chart(const QString& symbol);
    /// The bars that start before the chart's first one (``read.before``) and their final
    /// consensus (krx-quant #108): older_ready or older_failed. Asked again while one is
    /// read, it is not read: KrxLive asks again once that one answers.
    void read_older(const QString& symbol, const krx_quant::OlderRead& read);
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

  signals:
    void snapshot_ready(const fincept::services::KrxQuantService::Snapshot& snapshot);
    void refresh_failed(const QString& reason);
    void status_ready(const fincept::krx_quant::EngineStatus& status);
    void chart_ready(const QString& symbol, const QList<fincept::krx_quant::Bar>& bars,
                     const QList<fincept::krx_quant::Prediction>& finals);
    void chart_failed(const QString& symbol, const QString& reason);
    void older_ready(const QString& symbol, const QList<fincept::krx_quant::Bar>& bars,
                     const QList<fincept::krx_quant::Prediction>& finals);
    void older_failed(const QString& symbol, const QString& reason);
    void candles_ready(const QString& symbol, const QString& period, const QDateTime& before,
                       const fincept::krx_quant::Candles& candles);
    void candles_failed(const QString& symbol, const QString& reason);
    void close_ready(const QString& symbol, const QDateTime& bar_close,
                     const QList<fincept::krx_quant::Prediction>& predictions, const QMap<QString, QString>& statuses);
    void close_failed(const QString& symbol, const QDateTime& bar_close, const QString& reason);
    void leaderboard_ready(const fincept::krx_quant::Leaderboard& leaderboard);
    void leaderboard_failed(const QString& reason);
    void stats_ready(const fincept::krx_quant::AlgorithmStats& stats);
    void stats_failed(const QString& algorithm, const QString& reason);
    void macro_ready(const fincept::krx_quant::Macro& macro);
    void macro_failed(const QString& reason);
    /// The address this service reads changed (also when another panel set it).
    void base_url_changed(const QString& url);

  private:
    /// A step of a read: the answer to one GET, or why it failed.
    using Step = std::function<void(Result<QJsonDocument>)>;
    enum Kind { kRefresh, kStatus, kChart, kLeaderboard, kMacro, kClose, kOlder, kCandles, kKinds };

    /// Start a read of ``kind`` unless one is under way; false if skipped.
    bool begin(Kind kind);
    void end(Kind kind) { busy_[kind] = false; }
    void get(const QString& path, Step step);
    void read_symbols(Snapshot snapshot);
    void read_members(Snapshot snapshot);
    void read_latest(Snapshot snapshot);
    void finish_with(const Snapshot& snapshot);
    /// The refresh failed: why, after ending it.
    void fail_refresh(const QString& reason);

    int generation_ = 0;
    bool busy_[kKinds] = {};
    LeaderboardQuery leaderboard_asked_;                      ///< the leaderboard under way, or the last
    std::optional<LeaderboardQuery> next_leaderboard_;        ///< asked for while another was under way
    std::optional<std::pair<QString, QDateTime>> next_close_; ///< a bar close asked for while another was read
    /// A bar close's read ended: the one asked for meanwhile is read next.
    void close_done();
    /// An algorithm's stats under way: the query asked, and one asked for meanwhile.
    struct StatsRead {
        LeaderboardQuery asked;
        std::optional<LeaderboardQuery> next;
    };
    QHash<QString, StatsRead> stats_reads_; ///< by algorithm
    QString url_;                           ///< the address of the reads under way, or of the last ones
};

/// Why a request failed, in Korean, from HttpClient's error string.
QString krx_failure(const std::string& error);

} // namespace fincept::services

Q_DECLARE_METATYPE(fincept::services::KrxQuantService::Snapshot)
