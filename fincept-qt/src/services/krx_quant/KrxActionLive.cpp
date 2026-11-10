#include "services/krx_quant/KrxActionLive.h"

#include "services/krx_quant/ActionView.h"
#include "services/krx_quant/EnginePoller.h"
#include "services/krx_quant/KrxQuantService.h"

namespace fincept::services {

KrxActionLive::KrxActionLive(QObject* parent, std::function<QDateTime()> now) : QObject(parent) {
    poller_ = new EnginePoller(EnginePoller::Symbols::None, this, std::move(now));
    poller_->set_timer_name("krxActionPoll");
    poller_->set_read([this] { poller_->service()->read_action(); });
    poller_->set_poll([this] { return krx_quant::action::poll_ms(board_, poller_->now()); });
    connect(poller_, &EnginePoller::changed, this, &KrxActionLive::changed);
    connect(poller_->service(), &KrxQuantService::action_ready, this,
            [this](const krx_quant::ActionBoard& board, bool served) {
                served_ = served;
                if (served) {
                    board_ = board;
                    emit board_ready();
                } else {
                    board_.reset();
                }
                poller_->answered();
            });
    connect(poller_->service(), &KrxQuantService::action_failed, this,
            [this](const QString& reason) { poller_->failed(reason); });
    // Another engine: what was read of the old one goes.
    connect(poller_, &EnginePoller::engine_changed, this, [this] {
        board_.reset();
        served_ = true;
        emit engine_changed();
    });
}

void KrxActionLive::start() {
    poller_->start();
}

void KrxActionLive::stop() {
    poller_->stop();
}

bool KrxActionLive::running() const {
    return poller_->running();
}

void KrxActionLive::run_while_shown(QObject* widget) {
    poller_->run_while_shown(widget);
}

void KrxActionLive::read_now() {
    poller_->read_now();
}

const QString& KrxActionLive::failure() const {
    return poller_->failure();
}

const QDateTime& KrxActionLive::answered_at() const {
    return poller_->answered_at();
}

QDateTime KrxActionLive::now() const {
    return poller_->now();
}

int KrxActionLive::next_ms() const {
    return poller_->next_ms();
}

} // namespace fincept::services
