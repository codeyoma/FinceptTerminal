#include "services/krx_quant/KrxClosingLive.h"

#include "core/config/AppConfig.h"
#include "services/krx_quant/ClosingView.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace fincept::services {

KrxClosingLive::KrxClosingLive(QObject* parent, std::function<QDateTime()> now)
    : QObject(parent), now_(now ? std::move(now) : [] { return QDateTime::currentDateTime(); }) {
    service_ = new KrxQuantService(this);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    symbol_ = AppConfig::instance().get(kSymbolKey).toString();
    connect(timer_, &QTimer::timeout, this, &KrxClosingLive::read);
    connect(service_, &KrxQuantService::closing_flow_ready, this,
            [this](const QString& symbol, const krx_quant::ClosingFlow& flow) {
                reading_flow_ = false;
                if (symbol != symbol_) { // asked for another since: read that one now
                    if (running_)
                        read();
                    return;
                }
                flow_ = flow;
                flow_failure_.clear();
                answered_at_ = now_();
                emit changed();
                schedule();
            });
    connect(service_, &KrxQuantService::closing_flow_failed, this,
            [this](const QString& symbol, const QString& reason) {
                reading_flow_ = false;
                if (symbol != symbol_) {
                    if (running_)
                        read();
                    return;
                }
                flow_failure_ = reason;
                emit changed();
                schedule();
            });
    connect(service_, &KrxQuantService::closing_days_ready, this,
            [this](const QString& symbol, const krx_quant::ClosingDays& days) {
                reading_days_ = false;
                if (symbol != symbol_) {
                    if (running_)
                        read();
                    return;
                }
                days_ = days;
                days_read_at_ = now_();
                days_failure_.clear();
                emit changed();
                schedule();
            });
    connect(service_, &KrxQuantService::closing_days_failed, this,
            [this](const QString& symbol, const QString& reason) {
                reading_days_ = false;
                if (symbol != symbol_) {
                    if (running_)
                        read();
                    return;
                }
                days_failure_ = reason;
                emit changed();
                schedule();
            });
    connect(service_, &KrxQuantService::symbols_ready, this, [this](const QList<krx_quant::SymbolQuote>& symbols) {
        symbols_ = symbols;
        if (symbols_.isEmpty()) { // nothing to read: say so and ask again on schedule, not at once
            symbols_failure_ = QStringLiteral("엔진이 예측하는 종목이 없습니다.");
            emit changed();
            schedule();
            return;
        }
        symbols_failure_.clear();
        const bool known = std::any_of(symbols_.begin(), symbols_.end(),
                                       [this](const krx_quant::SymbolQuote& s) { return s.symbol == symbol_; });
        if (!known) { // none chosen yet, or one this engine does not predict
            symbol_ = symbols_.first().symbol;
            flow_.reset();
            days_.reset();
        }
        emit changed();
        if (running_)
            read();
    });
    connect(service_, &KrxQuantService::symbols_failed, this, [this](const QString& reason) {
        symbols_failure_ = reason;
        emit changed();
        schedule();
    });
    connect(service_, &KrxQuantService::base_url_changed, this, [this] {
        flow_.reset();
        days_.reset();
        days_read_at_ = {};
        symbols_.clear();
        flow_failure_.clear();
        days_failure_.clear();
        answered_at_ = {};
    });
}

void KrxClosingLive::start() {
    if (running_)
        return;
    running_ = true;
    read();
}

void KrxClosingLive::stop() {
    running_ = false;
    timer_->stop();
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
    if (running_)
        read();
}

void KrxClosingLive::read_now() {
    days_wanted_ = true;
    if (running_)
        read();
}

QString KrxClosingLive::failure() const {
    QStringList parts;
    for (const QString& part : {symbols_failure_, flow_failure_})
        if (!part.isEmpty() && !parts.contains(part))
            parts << part;
    if (!days_failure_.isEmpty() && !parts.contains(days_failure_))
        parts << QStringLiteral("지난날: %1").arg(days_failure_);
    return parts.join(QStringLiteral(" · "));
}

int KrxClosingLive::next_ms() const {
    if (!running_)
        return 0;
    const bool failed = !symbols_failure_.isEmpty() || !flow_failure_.isEmpty() || !days_failure_.isEmpty();
    return krx_quant::closing::poll_ms(flow_, days_, failed, now_());
}

void KrxClosingLive::read() {
    timer_->stop();
    if (KrxQuantService::base_url() != service_->url()) {
        service_->rebind(); // what was under way is dropped
        reading_flow_ = false;
        reading_days_ = false;
    }
    if (service_->url().isEmpty()) {
        symbols_failure_ = QStringLiteral("엔진 주소가 없습니다. KRX 4분 예측 화면에서 엔진 주소를 넣으세요.");
        emit changed();
        schedule();
        return;
    }
    if (symbols_.isEmpty()) { // the symbols first: which one to read, and their names
        service_->read_symbol_list();
        return;
    }
    // Each read under way is left to answer; an answer for another symbol reads again then.
    if (!reading_flow_) {
        reading_flow_ = true;
        service_->read_closing_flow(symbol_);
    }
    if (!reading_days_ &&
        (days_wanted_ || krx_quant::closing::days_due(days_, symbol_, days_read_at_, flow_, now_()))) {
        days_wanted_ = false;
        reading_days_ = true;
        service_->read_closing_days(symbol_);
    }
    emit changed();
}

void KrxClosingLive::schedule() {
    if (running_)
        timer_->start(next_ms());
}

} // namespace fincept::services
