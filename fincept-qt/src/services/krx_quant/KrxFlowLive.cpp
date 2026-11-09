#include "services/krx_quant/KrxFlowLive.h"

#include "core/config/AppConfig.h"
#include "services/krx_quant/EnginePoller.h"
#include "services/krx_quant/FlowView.h"
#include "services/krx_quant/KrxQuantService.h"

#include <algorithm>

namespace fincept::services {

KrxFlowLive::KrxFlowLive(QObject* parent, std::function<QDateTime()> now) : QObject(parent) {
    poller_ = new EnginePoller(EnginePoller::Symbols::Required, this, std::move(now));
    poller_->set_read([this] { read(); });
    poller_->set_poll([this] { return krx_quant::flow::poll_ms(flow_, poller_->now()); });
    symbol_ = AppConfig::instance().get(kSymbolKey).toString();
    connect(poller_, &EnginePoller::changed, this, &KrxFlowLive::changed);
    auto* service = poller_->service();
    connect(service, &KrxQuantService::flow_ready, this,
            [this](const QString& symbol, const QDate& day, const krx_quant::Flow& flow) {
                reading_ = false;
                if (symbol != symbol_ || day != day_) // asked for another since: read that one now
                    return poller_->read_now();
                flow_ = flow;
                poller_->answered();
            });
    connect(service, &KrxQuantService::flow_failed, this,
            [this](const QString& symbol, const QDate& day, const QString& reason) {
                reading_ = false;
                if (symbol != symbol_ || day != day_)
                    return poller_->read_now();
                poller_->failed(reason);
            });
    connect(poller_, &EnginePoller::symbols_ready, this, [this] {
        const auto& symbols = poller_->symbols();
        const bool known = std::any_of(symbols.begin(), symbols.end(),
                                       [this](const krx_quant::SymbolQuote& s) { return s.symbol == symbol_; });
        if (!known) { // none chosen yet, or one this engine does not predict
            symbol_ = symbols.first().symbol;
            flow_.reset();
        }
    });
    connect(poller_, &EnginePoller::engine_changed, this, [this] {
        flow_.reset();
        reading_ = false;
    });
}

void KrxFlowLive::start() {
    poller_->start();
}

void KrxFlowLive::stop() {
    poller_->stop();
}

bool KrxFlowLive::running() const {
    return poller_->running();
}

void KrxFlowLive::run_while_shown(QObject* widget) {
    poller_->run_while_shown(widget);
}

const QList<krx_quant::SymbolQuote>& KrxFlowLive::symbols() const {
    return poller_->symbols();
}

const QString& KrxFlowLive::failure() const {
    return poller_->failure();
}

const QDateTime& KrxFlowLive::answered_at() const {
    return poller_->answered_at();
}

void KrxFlowLive::set_symbol(const QString& symbol) {
    if (symbol == symbol_)
        return;
    symbol_ = symbol;
    AppConfig::instance().set(kSymbolKey, symbol);
    flow_.reset();
    emit changed();
    if (!reading_) // a read under way is of the old symbol: its answer is dropped, this one read then
        poller_->read_now();
}

void KrxFlowLive::set_day(const QDate& day) {
    if (day == day_)
        return;
    day_ = day;
    flow_.reset();
    emit changed();
    if (!reading_)
        poller_->read_now();
}

void KrxFlowLive::read_now() {
    poller_->read_now();
}

int KrxFlowLive::next_ms() const {
    return poller_->next_ms();
}

void KrxFlowLive::read() {
    reading_ = true;
    emit changed();
    poller_->service()->read_flow(symbol_, day_);
}

} // namespace fincept::services
