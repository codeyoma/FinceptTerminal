#include "services/krx_quant/KrxBriefingLive.h"

#include "services/krx_quant/BriefingView.h"
#include "services/krx_quant/EnginePoller.h"
#include "services/krx_quant/KrxQuantService.h"

namespace fincept::services {

KrxBriefingLive::KrxBriefingLive(QObject* parent, std::function<QDateTime()> now) : QObject(parent) {
    poller_ = new EnginePoller(EnginePoller::Symbols::Names, this, std::move(now));
    poller_->set_read([this] { read(); });
    poller_->set_poll([this] { return krx_quant::briefing::poll_ms(briefing_, poller_->now()); });
    connect(poller_, &EnginePoller::changed, this, &KrxBriefingLive::changed);
    connect(poller_->service(), &KrxQuantService::briefing_ready, this,
            [this](const QDate& day, const krx_quant::Briefing& briefing) {
                reading_ = false;
                if (day != day_) // asked for another day since: read that one now
                    return poller_->read_now();
                briefing_ = briefing;
                poller_->answered();
            });
    connect(poller_->service(), &KrxQuantService::briefing_failed, this,
            [this](const QDate& day, const QString& reason) {
                reading_ = false;
                if (day != day_)
                    return poller_->read_now();
                poller_->failed(reason);
            });
    connect(poller_, &EnginePoller::symbols_ready, this, [this] {
        symbols_.clear();
        for (const auto& s : poller_->symbols())
            symbols_.insert(s.symbol, s);
    });
    // Another engine: what was read of the old one goes.
    connect(poller_, &EnginePoller::engine_changed, this, [this] {
        briefing_.reset();
        symbols_.clear();
        reading_ = false;
    });
}

void KrxBriefingLive::start() {
    poller_->start();
}

void KrxBriefingLive::stop() {
    poller_->stop();
}

bool KrxBriefingLive::running() const {
    return poller_->running();
}

void KrxBriefingLive::run_while_shown(QObject* widget) {
    poller_->run_while_shown(widget);
}

const QString& KrxBriefingLive::failure() const {
    return poller_->failure();
}

const QDateTime& KrxBriefingLive::answered_at() const {
    return poller_->answered_at();
}

void KrxBriefingLive::set_day(const QDate& day) {
    if (day == day_)
        return;
    day_ = day;
    briefing_.reset();
    emit changed();
    // A read under way is of the old day: its answer is dropped and this day read then.
    if (!reading_)
        poller_->read_now();
}

void KrxBriefingLive::read_now() {
    poller_->read_now();
}

int KrxBriefingLive::next_ms() const {
    return poller_->next_ms();
}

void KrxBriefingLive::read() {
    reading_ = true;
    emit changed();
    poller_->service()->read_briefing(day_);
}

} // namespace fincept::services
