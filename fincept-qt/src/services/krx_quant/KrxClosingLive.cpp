#include "services/krx_quant/KrxClosingLive.h"

#include "core/config/AppConfig.h"
#include "services/krx_quant/ClosingView.h"
#include "services/krx_quant/EnginePoller.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QStringList>

#include <algorithm>
#include <utility>

namespace fincept::services {

KrxClosingLive::KrxClosingLive(QObject* parent, std::function<QDateTime()> now) : QObject(parent) {
    poller_ = new EnginePoller(EnginePoller::Symbols::Required, this, std::move(now));
    poller_->set_read([this] { read(); });
    poller_->set_poll([this] {
        const bool failed = !poller_->failure().isEmpty() || !flow_failure_.isEmpty() || !days_failure_.isEmpty();
        return krx_quant::closing::poll_ms(flow_, days_, failed, poller_->now());
    });
    symbol_ = AppConfig::instance().get(kSymbolKey).toString();
    connect(poller_, &EnginePoller::changed, this, &KrxClosingLive::changed);
    auto* service = poller_->service();
    connect(service, &KrxQuantService::closing_flow_ready, this,
            [this](const QString& symbol, const krx_quant::ClosingFlow& flow) {
                reading_flow_ = false;
                if (dropped(symbol))
                    return;
                flow_ = flow;
                flow_failure_.clear();
                poller_->answered();
            });
    connect(service, &KrxQuantService::closing_flow_failed, this, [this](const QString& symbol, const QString& reason) {
        reading_flow_ = false;
        if (dropped(symbol))
            return;
        flow_failure_ = reason;
        emit changed();
        poller_->schedule();
    });
    connect(service, &KrxQuantService::closing_days_ready, this,
            [this](const QString& symbol, const krx_quant::ClosingDays& days) {
                reading_days_ = false;
                if (dropped(symbol))
                    return;
                days_ = days;
                days_read_at_ = poller_->now();
                days_failure_.clear();
                emit changed();
                poller_->schedule();
            });
    connect(service, &KrxQuantService::closing_days_failed, this, [this](const QString& symbol, const QString& reason) {
        reading_days_ = false;
        if (dropped(symbol))
            return;
        days_failure_ = reason;
        emit changed();
        poller_->schedule();
    });
    connect(poller_, &EnginePoller::symbols_ready, this, [this] {
        poller_->clear_failure(); // the symbols' failure: they came
        const auto& symbols = poller_->symbols();
        const bool known = std::any_of(symbols.begin(), symbols.end(),
                                       [this](const krx_quant::SymbolQuote& s) { return s.symbol == symbol_; });
        if (!known) { // none chosen yet, or one this engine does not predict
            symbol_ = symbols.first().symbol;
            flow_.reset();
            days_.reset();
        }
    });
    connect(poller_, &EnginePoller::engine_changed, this, [this] {
        flow_.reset();
        days_.reset();
        days_read_at_ = {};
        flow_failure_.clear();
        days_failure_.clear();
        reading_flow_ = false;
        reading_days_ = false;
    });
}

void KrxClosingLive::start() {
    poller_->start();
}

void KrxClosingLive::stop() {
    poller_->stop();
}

bool KrxClosingLive::running() const {
    return poller_->running();
}

void KrxClosingLive::run_while_shown(QObject* widget) {
    poller_->run_while_shown(widget);
}

const QList<krx_quant::SymbolQuote>& KrxClosingLive::symbols() const {
    return poller_->symbols();
}

const QDateTime& KrxClosingLive::answered_at() const {
    return poller_->answered_at();
}

bool KrxClosingLive::dropped(const QString& symbol) {
    if (symbol == symbol_)
        return false;
    poller_->read_now(); // asked for another since: read that one now
    return true;
}

void KrxClosingLive::set_symbol(const QString& symbol) {
    if (symbol == symbol_)
        return;
    symbol_ = symbol;
    AppConfig::instance().set(kSymbolKey, symbol);
    flow_.reset();
    days_.reset();
    days_read_at_ = {};
    emit changed();
    // A read under way is of the old symbol: its answer is dropped and this one read then.
    poller_->read_now();
}

void KrxClosingLive::read_now() {
    days_wanted_ = true;
    poller_->read_now();
}

QString KrxClosingLive::failure() const {
    QStringList parts;
    for (const QString& part : {poller_->failure(), flow_failure_})
        if (!part.isEmpty() && !parts.contains(part))
            parts << part;
    if (!days_failure_.isEmpty() && !parts.contains(days_failure_))
        parts << QStringLiteral("지난날: %1").arg(days_failure_);
    return parts.join(QStringLiteral(" · "));
}

int KrxClosingLive::next_ms() const {
    return poller_->next_ms();
}

void KrxClosingLive::read() {
    auto* service = poller_->service();
    // Each read under way is left to answer; an answer for another symbol reads again then.
    if (!reading_flow_) {
        reading_flow_ = true;
        service->read_closing_flow(symbol_);
    }
    // The days are asked when they can have changed, when asked for, and again after they failed.
    const auto now = poller_->now();
    const bool days = days_wanted_ || !days_failure_.isEmpty() ||
                      krx_quant::closing::days_due(days_, symbol_, days_read_at_, flow_, now);
    if (!reading_days_ && days) {
        days_wanted_ = false;
        reading_days_ = true;
        service->read_closing_days(symbol_);
    }
    emit changed();
}

} // namespace fincept::services
