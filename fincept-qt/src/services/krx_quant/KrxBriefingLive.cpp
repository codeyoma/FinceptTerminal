#include "services/krx_quant/KrxBriefingLive.h"

#include "services/krx_quant/BriefingView.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QTimer>

namespace fincept::services {

KrxBriefingLive::KrxBriefingLive(QObject* parent, std::function<QDateTime()> now)
    : QObject(parent), now_(now ? std::move(now) : [] { return QDateTime::currentDateTime(); }) {
    service_ = new KrxQuantService(this);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &KrxBriefingLive::read);
    connect(service_, &KrxQuantService::briefing_ready, this,
            [this](const QDate& day, const krx_quant::Briefing& briefing) {
                reading_ = false;
                if (day != day_) // asked for another day since: read that one now
                    return read_wanted();
                briefing_ = briefing;
                failure_.clear();
                answered_at_ = now_();
                emit changed();
                schedule();
            });
    connect(service_, &KrxQuantService::briefing_failed, this, [this](const QDate& day, const QString& reason) {
        reading_ = false;
        if (day != day_)
            return read_wanted();
        failure_ = reason;
        emit changed();
        schedule();
    });
    connect(service_, &KrxQuantService::symbols_ready, this, [this](const QList<krx_quant::SymbolQuote>& symbols) {
        symbols_.clear();
        for (const auto& s : symbols)
            symbols_.insert(s.symbol, s);
        emit changed();
    });
    // Another engine: what was read of the old one goes.
    connect(service_, &KrxQuantService::base_url_changed, this, [this] {
        briefing_.reset();
        symbols_.clear();
        answered_at_ = {};
    });
}

void KrxBriefingLive::start() {
    if (running_)
        return;
    running_ = true;
    read();
}

void KrxBriefingLive::stop() {
    running_ = false;
    timer_->stop();
}

void KrxBriefingLive::set_day(const QDate& day) {
    if (day == day_)
        return;
    day_ = day;
    briefing_.reset();
    emit changed();
    // A read under way is of the old day: its answer is dropped and this day read then.
    if (running_ && !reading_)
        read();
}

void KrxBriefingLive::read_wanted() {
    if (running_)
        read();
}

void KrxBriefingLive::read_now() {
    if (running_)
        read();
}

int KrxBriefingLive::next_ms() const {
    return running_ ? krx_quant::briefing::poll_ms(briefing_, now_()) : 0;
}

void KrxBriefingLive::read() {
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
    if (symbols_.isEmpty())
        service_->read_symbol_list();
    reading_ = true;
    emit changed();
    service_->read_briefing(day_);
}

void KrxBriefingLive::schedule() {
    if (running_)
        timer_->start(next_ms());
}

} // namespace fincept::services
