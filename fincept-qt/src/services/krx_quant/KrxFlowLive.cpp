#include "services/krx_quant/KrxFlowLive.h"

#include "core/config/AppConfig.h"
#include "services/krx_quant/FlowView.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QTimer>

#include <algorithm>

namespace fincept::services {

KrxFlowLive::KrxFlowLive(QObject* parent, std::function<QDateTime()> now)
    : QObject(parent), now_(now ? std::move(now) : [] { return QDateTime::currentDateTime(); }) {
    service_ = new KrxQuantService(this);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    symbol_ = AppConfig::instance().get(kSymbolKey).toString();
    connect(timer_, &QTimer::timeout, this, &KrxFlowLive::read);
    connect(service_, &KrxQuantService::flow_ready, this,
            [this](const QString& symbol, const QDate& day, const krx_quant::Flow& flow) {
                reading_ = false;
                if (symbol != symbol_ || day != day_) // asked for another since: read that one now
                    return read_wanted();
                flow_ = flow;
                failure_.clear();
                answered_at_ = now_();
                emit changed();
                schedule();
            });
    connect(service_, &KrxQuantService::flow_failed, this,
            [this](const QString& symbol, const QDate& day, const QString& reason) {
                reading_ = false;
                if (symbol != symbol_ || day != day_)
                    return read_wanted();
                failure_ = reason;
                emit changed();
                schedule();
            });
    connect(service_, &KrxQuantService::symbols_ready, this, [this](const QList<krx_quant::SymbolQuote>& symbols) {
        symbols_ = symbols;
        const bool known = std::any_of(symbols_.begin(), symbols_.end(),
                                       [this](const krx_quant::SymbolQuote& s) { return s.symbol == symbol_; });
        if (!known && !symbols_.isEmpty()) { // none chosen yet, or one this engine does not predict
            symbol_ = symbols_.first().symbol;
            flow_.reset();
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
    connect(service_, &KrxQuantService::base_url_changed, this, [this] {
        flow_.reset();
        symbols_.clear();
        answered_at_ = {};
    });
}

void KrxFlowLive::start() {
    if (running_)
        return;
    running_ = true;
    read();
}

void KrxFlowLive::stop() {
    running_ = false;
    timer_->stop();
}

void KrxFlowLive::set_symbol(const QString& symbol) {
    if (symbol == symbol_)
        return;
    symbol_ = symbol;
    AppConfig::instance().set(kSymbolKey, symbol);
    flow_.reset();
    emit changed();
    if (running_ && !reading_) // a read under way is of the old symbol: its answer is dropped, this one read then
        read();
}

void KrxFlowLive::set_day(const QDate& day) {
    if (day == day_)
        return;
    day_ = day;
    flow_.reset();
    emit changed();
    if (running_ && !reading_)
        read();
}

void KrxFlowLive::read_now() {
    if (running_)
        read();
}

void KrxFlowLive::read_wanted() {
    if (running_)
        read();
}

int KrxFlowLive::next_ms() const {
    return running_ ? krx_quant::flow::poll_ms(flow_, now_()) : 0;
}

void KrxFlowLive::read() {
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
    if (symbols_.isEmpty()) { // the symbols first: which one to read, and their names
        service_->read_symbol_list();
        return;
    }
    reading_ = true;
    emit changed();
    service_->read_flow(symbol_, day_);
}

void KrxFlowLive::schedule() {
    if (running_)
        timer_->start(next_ms());
}

} // namespace fincept::services
