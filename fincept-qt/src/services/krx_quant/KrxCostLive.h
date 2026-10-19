#pragma once

#include "services/krx_quant/CostView.h"
#include "services/krx_quant/EngineData.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QTimer;

namespace fincept::services {

/// What the KRX horizon and cost screen shows (codeyoma/krx-quant#307), kept for it: the cost map
/// for the chosen symbol, period and special situations (GET /v1/cost-map), the engine's symbols, its
/// kinds and algorithms (for a detail window), and whether the engine answered. The screen owns it
/// and only draws what it holds.
///
/// While started it reads at once and then again by cost::poll_ms: every 60 s while a symbol's price
/// is live (continuous trading), else every 10 minutes. A new choice reads at once; an answer to an
/// older one is dropped. The choices and the cost the table is held to are kept in the settings
/// (`krx_quant/cost_symbol`, `cost_period`, `cost_special`, `cost_order`). The engine's address is the
/// KRX screen's setting; a new one drops what was read and reads again.
class KrxCostLive : public QObject {
    Q_OBJECT
  public:
    using Query = KrxQuantService::LeaderboardQuery;

    /// ``now`` is the clock the answers are stamped with (tests give their own).
    explicit KrxCostLive(QObject* parent = nullptr, std::function<QDateTime()> now = {});

    void start(); ///< read at once, then on schedule
    void stop();  ///< read no more until started again
    bool running() const { return running_; }

    /// Show ``query``'s symbol, period and special situations (its kind is not read), kept in the
    /// settings; read at once.
    void set_query(const Query& query);
    const Query& query() const { return query_; }
    /// Hold the table to ``cost``'s round trip (kept in the settings).
    void set_cost(krx_quant::cost::Cost cost);
    krx_quant::cost::Cost cost() const { return cost_; }
    void read_now();

    const std::optional<krx_quant::CostMap>& map() const { return map_; }
    /// The engine's symbols, in its order, with names and logos; empty before they are read.
    const QList<krx_quant::SymbolQuote>& symbols() const { return symbols_; }
    /// The kinds as the engine describes them (a detail window's columns); empty before they are read.
    const QList<krx_quant::KindInfo>& kinds() const { return kinds_; }
    std::optional<krx_quant::KindInfo> kind_info(const QString& kind) const;
    /// What the engine says about each algorithm (a detail window's "학습·예측 방식").
    const QMap<QString, krx_quant::AlgorithmInfo>& algorithms() const { return algorithms_; }
    const QString& failure() const { return failure_; }
    const QDateTime& answered_at() const { return answered_at_; }
    bool reading() const { return reading_; }
    int next_ms() const; ///< 0 when stopped

    /// ``algorithm``'s stats on the query shown (a detail window): stats_ready or stats_failed.
    void read_stats(const QString& algorithm);

    static constexpr const char* kSymbolKey = "krx_quant/cost_symbol";
    static constexpr const char* kPeriodKey = "krx_quant/cost_period";
    static constexpr const char* kSpecialKey = "krx_quant/cost_special";
    static constexpr const char* kCostKey = "krx_quant/cost_order"; ///< "limit" or "market" (default)

  signals:
    /// Something to draw again: an answer, a failure, the symbols or kinds, a read started.
    void changed();
    void stats_ready(const fincept::krx_quant::AlgorithmStats& stats);
    void stats_failed(const QString& algorithm, const QString& reason);

  private:
    void read();
    void schedule();

    std::function<QDateTime()> now_;
    KrxQuantService* service_ = nullptr;
    QTimer* timer_ = nullptr;
    Query query_;
    krx_quant::cost::Cost cost_ = krx_quant::cost::Cost::Market;
    std::optional<krx_quant::CostMap> map_;
    QList<krx_quant::SymbolQuote> symbols_;
    bool symbols_read_ = false;
    QList<krx_quant::KindInfo> kinds_;
    QMap<QString, krx_quant::AlgorithmInfo> algorithms_;
    QString failure_;
    QDateTime answered_at_;
    bool running_ = false;
    bool reading_ = false;
};

} // namespace fincept::services
