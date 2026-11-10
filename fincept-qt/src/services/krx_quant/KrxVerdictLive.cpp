#include "services/krx_quant/KrxVerdictLive.h"

#include "core/config/AppConfig.h"
#include "services/krx_quant/EnginePoller.h"
#include "services/krx_quant/KrxQuantService.h"
#include "services/krx_quant/VerdictView.h"

namespace fincept::services {

KrxVerdictLive::KrxVerdictLive(QObject* parent, std::function<QDateTime()> now) : QObject(parent) {
    poller_ = new EnginePoller(EnginePoller::Symbols::Names, this, std::move(now));
    poller_->set_read([this] { read(); });
    poller_->set_poll([this] { return krx_quant::judgment::poll_ms(poller_->now()); });
    symbol_ = AppConfig::instance().get(kSymbolKey).toString();
    connect(poller_, &EnginePoller::changed, this, &KrxVerdictLive::changed);
    auto* service = poller_->service();
    connect(service, &KrxQuantService::verdicts_ready, this, [this](const krx_quant::Verdicts& verdicts) {
        reading_ = false;
        verdicts_ = verdicts;
        poller_->answered();
    });
    connect(service, &KrxQuantService::verdicts_failed, this, [this](const QString& reason) {
        reading_ = false;
        poller_->failed(reason); // the last verdicts stay
    });
    connect(poller_, &EnginePoller::engine_changed, this, [this] {
        verdicts_.reset();
        reading_ = false;
    });
}

void KrxVerdictLive::start() {
    poller_->start();
}

void KrxVerdictLive::stop() {
    poller_->stop();
}

bool KrxVerdictLive::running() const {
    return poller_->running();
}

void KrxVerdictLive::run_while_shown(QObject* widget) {
    poller_->run_while_shown(widget);
}

void KrxVerdictLive::read_now() {
    poller_->read_now();
}

void KrxVerdictLive::set_symbol(const QString& symbol) {
    if (symbol == symbol_)
        return;
    symbol_ = symbol;
    AppConfig::instance().set(kSymbolKey, symbol);
    emit changed(); // every symbol's verdict is held: nothing to read
}

std::optional<krx_quant::SymbolVerdict> KrxVerdictLive::chosen() const {
    if (!verdicts_)
        return std::nullopt;
    return krx_quant::judgment::chosen(*verdicts_, symbol_);
}

const QList<krx_quant::SymbolQuote>& KrxVerdictLive::symbols() const {
    return poller_->symbols();
}

const QString& KrxVerdictLive::failure() const {
    return poller_->failure();
}

const QDateTime& KrxVerdictLive::answered_at() const {
    return poller_->answered_at();
}

int KrxVerdictLive::next_ms() const {
    return poller_->next_ms();
}

void KrxVerdictLive::read() {
    reading_ = true;
    emit changed();
    poller_->service()->read_verdicts();
}

} // namespace fincept::services
