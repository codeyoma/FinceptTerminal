#include "services/krx_quant/KrxCostLive.h"

#include "core/config/AppConfig.h"
#include "services/krx_quant/EnginePoller.h"

#include <algorithm>
#include <utility>

namespace fincept::services {

namespace {

using krx_quant::cost::Cost;

bool known(const QList<krx_quant::SymbolQuote>& symbols, const QString& symbol) {
    return std::any_of(symbols.begin(), symbols.end(),
                       [&symbol](const krx_quant::SymbolQuote& s) { return s.symbol == symbol; });
}

} // namespace

KrxCostLive::KrxCostLive(QObject* parent, std::function<QDateTime()> now) : QObject(parent) {
    poller_ = new EnginePoller(EnginePoller::Symbols::First, this, std::move(now));
    poller_->set_read([this] { read(); });
    poller_->set_poll([this] { return krx_quant::cost::poll_ms(map_); });
    auto& config = AppConfig::instance();
    query_.symbol = config.get(kSymbolKey, QStringLiteral("all")).toString();
    query_.period = config.get(kPeriodKey, QStringLiteral("today")).toString();
    if (!QStringList{"today", "5d", "20d"}.contains(query_.period))
        query_.period = QStringLiteral("today");
    query_.special = config.get(kSpecialKey, false).toBool();
    cost_ = config.get(kCostKey).toString() == QLatin1String("limit") ? Cost::Limit : Cost::Market;
    connect(poller_, &EnginePoller::changed, this, &KrxCostLive::changed);
    auto* service = poller_->service();
    connect(service, &KrxQuantService::cost_map_ready, this, [this](const Query& query, const krx_quant::CostMap& map) {
        reading_ = false;
        if (!(query == query_)) // asked for another since: read that one now
            return read_now();
        map_ = map;
        poller_->answered();
        read_details(details_); // the windows' stats, as fresh as the map
    });
    connect(service, &KrxQuantService::cost_map_failed, this, [this](const Query& query, const QString& reason) {
        reading_ = false;
        if (!(query == query_))
            return read_now();
        poller_->failed(reason);
    });
    connect(poller_, &EnginePoller::symbols_ready, this, [this] {
        if (query_.symbol != QLatin1String("all") && !known(poller_->symbols(), query_.symbol)) {
            query_.symbol = QStringLiteral("all"); // one this engine does not predict: every symbol
            map_.reset();
        }
    });
    connect(service, &KrxQuantService::kinds_ready, this, [this](const QList<krx_quant::KindInfo>& kinds) {
        kinds_ = kinds;
        emit changed();
    });
    connect(service, &KrxQuantService::algorithms_ready, this,
            [this](const QMap<QString, krx_quant::AlgorithmInfo>& algorithms) {
                algorithms_ = algorithms;
                emit changed();
            });
    connect(service, &KrxQuantService::stats_ready, this, [this](const krx_quant::AlgorithmStats& stats) {
        if (query_.matches(stats)) // an older query's: the one asked since answers instead
            emit stats_ready(stats);
    });
    connect(service, &KrxQuantService::stats_failed, this, &KrxCostLive::stats_failed);
    connect(poller_, &EnginePoller::engine_changed, this, [this] {
        map_.reset();
        kinds_.clear();
        algorithms_.clear();
        reading_ = false;
    });
}

void KrxCostLive::start() {
    poller_->start();
}

void KrxCostLive::stop() {
    poller_->stop();
}

bool KrxCostLive::running() const {
    return poller_->running();
}

void KrxCostLive::run_while_shown(QObject* widget) {
    poller_->run_while_shown(widget);
}

const QList<krx_quant::SymbolQuote>& KrxCostLive::symbols() const {
    return poller_->symbols();
}

const QString& KrxCostLive::failure() const {
    return poller_->failure();
}

const QDateTime& KrxCostLive::answered_at() const {
    return poller_->answered_at();
}

void KrxCostLive::set_query(const Query& query) {
    Query wanted = query;
    wanted.kind = query_.kind; // not asked: the map has every kind
    if (wanted == query_)
        return;
    query_ = wanted;
    auto& config = AppConfig::instance();
    config.set(kSymbolKey, query_.symbol);
    config.set(kPeriodKey, query_.period);
    config.set(kSpecialKey, query_.special);
    map_.reset();
    emit changed();
    read_now(); // a read under way is of the old query: its answer is dropped, this one read then
    read_details(details_);
}

void KrxCostLive::set_cost(Cost cost) {
    if (cost == cost_)
        return;
    cost_ = cost;
    AppConfig::instance().set(kCostKey, cost == Cost::Limit ? QStringLiteral("limit") : QStringLiteral("market"));
    emit changed();
}

void KrxCostLive::read_now() {
    poller_->read_now();
}

std::optional<krx_quant::KindInfo> KrxCostLive::kind_info(const QString& kind) const {
    for (const auto& k : kinds_)
        if (k.name == kind)
            return k;
    return std::nullopt;
}

int KrxCostLive::next_ms() const {
    return poller_->next_ms();
}

void KrxCostLive::set_details(const QStringList& algorithms) {
    const QStringList before = std::exchange(details_, algorithms);
    QStringList opened;
    for (const auto& algorithm : details_)
        if (!before.contains(algorithm))
            opened << algorithm;
    read_details(opened);
}

void KrxCostLive::read_details(const QStringList& algorithms) {
    if (poller_->service()->url().isEmpty())
        return;
    for (const auto& algorithm : algorithms)
        poller_->service()->read_stats(algorithm, query_);
}

void KrxCostLive::read() {
    auto* service = poller_->service();
    // A detail window's columns and descriptions: asked again with each read until they are in (a read
    // under way is not asked twice).
    if (kinds_.isEmpty())
        service->read_kinds();
    if (algorithms_.isEmpty())
        service->read_algorithms();
    reading_ = true;
    emit changed();
    service->read_cost_map(query_);
}

} // namespace fincept::services
