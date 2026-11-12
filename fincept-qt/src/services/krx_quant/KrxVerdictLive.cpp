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
        waiting_verdicts_ = false;
        verdicts_ = verdicts;
        verdicts_failure_.clear();
        settle();
    });
    connect(service, &KrxQuantService::verdicts_failed, this, [this](const QString& reason) {
        waiting_verdicts_ = false;
        verdicts_failure_ = reason; // the last verdicts stay
        settle();
    });
    connect(service, &KrxQuantService::ranking_ready, this, [this](const krx_quant::Ranking& ranking) {
        waiting_ranking_ = false;
        ranking_ = ranking;
        ranking_failure_.clear();
        settle();
    });
    connect(service, &KrxQuantService::ranking_failed, this, [this](const QString& reason) {
        waiting_ranking_ = false;
        ranking_failure_ = reason; // the last ranking stays
        settle();
    });
    connect(poller_, &EnginePoller::engine_changed, this, [this] {
        // The reads under way were dropped: none of them answers.
        verdicts_.reset();
        ranking_.reset();
        ranking_failure_.clear();
        verdicts_failure_.clear();
        reading_ = waiting_verdicts_ = waiting_ranking_ = false;
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
    // A read still under way is not asked again: its answer settles this one.
    if (poller_->service()->read_verdicts())
        waiting_verdicts_ = true;
    if (poller_->service()->read_ranking())
        waiting_ranking_ = true;
    if (!waiting_verdicts_ && !waiting_ranking_) { // nothing asked (no address in the service): try later
        reading_ = false;
        poller_->schedule();
    }
}

void KrxVerdictLive::settle() {
    if (waiting_verdicts_ || waiting_ranking_)
        return;
    reading_ = false;
    if (verdicts_failure_.isEmpty())
        poller_->answered();
    else
        poller_->failed(verdicts_failure_);
}

} // namespace fincept::services
