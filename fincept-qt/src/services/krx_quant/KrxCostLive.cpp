#include "services/krx_quant/KrxCostLive.h"

#include "core/config/AppConfig.h"

#include <QTimer>

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

KrxCostLive::KrxCostLive(QObject* parent, std::function<QDateTime()> now)
    : QObject(parent), now_(now ? std::move(now) : [] { return QDateTime::currentDateTime(); }) {
    service_ = new KrxQuantService(this);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    auto& config = AppConfig::instance();
    query_.symbol = config.get(kSymbolKey, QStringLiteral("all")).toString();
    query_.period = config.get(kPeriodKey, QStringLiteral("today")).toString();
    if (!QStringList{"today", "5d", "20d"}.contains(query_.period))
        query_.period = QStringLiteral("today");
    query_.special = config.get(kSpecialKey, false).toBool();
    cost_ = config.get(kCostKey).toString() == QLatin1String("limit") ? Cost::Limit : Cost::Market;
    connect(timer_, &QTimer::timeout, this, &KrxCostLive::read);
    connect(service_, &KrxQuantService::cost_map_ready, this,
            [this](const Query& query, const krx_quant::CostMap& map) {
                reading_ = false;
                if (!(query == query_)) // asked for another since: read that one now
                    return read_now();
                map_ = map;
                failure_.clear();
                answered_at_ = now_();
                emit changed();
                schedule();
                read_details(details_); // the windows' stats, as fresh as the map
            });
    connect(service_, &KrxQuantService::cost_map_failed, this, [this](const Query& query, const QString& reason) {
        reading_ = false;
        if (!(query == query_))
            return read_now();
        failure_ = reason;
        emit changed();
        schedule();
    });
    connect(service_, &KrxQuantService::symbols_ready, this, [this](const QList<krx_quant::SymbolQuote>& symbols) {
        symbols_ = symbols;
        symbols_read_ = true;
        if (query_.symbol != QLatin1String("all") && !known(symbols_, query_.symbol)) {
            query_.symbol = QStringLiteral("all"); // one this engine does not predict: every symbol
            map_.reset();
        }
        emit changed();
        if (running_ && !reading_)
            read();
    });
    connect(service_, &KrxQuantService::symbols_failed, this, [this](const QString& reason) {
        failure_ = reason;
        emit changed();
        schedule();
    });
    connect(service_, &KrxQuantService::kinds_ready, this, [this](const QList<krx_quant::KindInfo>& kinds) {
        kinds_ = kinds;
        emit changed();
    });
    connect(service_, &KrxQuantService::algorithms_ready, this,
            [this](const QMap<QString, krx_quant::AlgorithmInfo>& algorithms) {
                algorithms_ = algorithms;
                emit changed();
            });
    connect(service_, &KrxQuantService::stats_ready, this, [this](const krx_quant::AlgorithmStats& stats) {
        if (query_.matches(stats)) // an older query's: the one asked since answers instead
            emit stats_ready(stats);
    });
    connect(service_, &KrxQuantService::stats_failed, this, &KrxCostLive::stats_failed);
    connect(service_, &KrxQuantService::base_url_changed, this, [this] {
        map_.reset();
        symbols_.clear();
        symbols_read_ = false;
        kinds_.clear();
        algorithms_.clear();
        answered_at_ = {};
    });
}

void KrxCostLive::start() {
    if (running_)
        return;
    running_ = true;
    read();
}

void KrxCostLive::stop() {
    running_ = false;
    timer_->stop();
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
    if (running_)
        read();
}

std::optional<krx_quant::KindInfo> KrxCostLive::kind_info(const QString& kind) const {
    for (const auto& k : kinds_)
        if (k.name == kind)
            return k;
    return std::nullopt;
}

int KrxCostLive::next_ms() const {
    return running_ ? krx_quant::cost::poll_ms(map_) : 0;
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
    if (service_->url().isEmpty())
        return;
    for (const auto& algorithm : algorithms)
        service_->read_stats(algorithm, query_);
}

void KrxCostLive::read() {
    timer_->stop();
    if (KrxQuantService::base_url() != service_->url()) {
        service_->rebind(); // what was under way is dropped
        reading_ = false;
    }
    if (service_->url().isEmpty()) {
        failure_ = QStringLiteral("엔진 주소가 없습니다. KRX 4분 예측 화면에서 엔진 주소를 넣으세요.");
        emit changed();
        schedule();
        return;
    }
    if (!symbols_read_) { // the symbols first: whether the one chosen is still the engine's, and their names
        service_->read_symbol_list();
        return;
    }
    // A detail window's columns and descriptions: asked again with each read until they are in (a read
    // under way is not asked twice).
    if (kinds_.isEmpty())
        service_->read_kinds();
    if (algorithms_.isEmpty())
        service_->read_algorithms();
    reading_ = true;
    emit changed();
    service_->read_cost_map(query_);
}

void KrxCostLive::schedule() {
    if (running_)
        timer_->start(next_ms());
}

} // namespace fincept::services
