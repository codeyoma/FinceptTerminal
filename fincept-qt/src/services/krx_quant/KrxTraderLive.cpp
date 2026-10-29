#include "services/krx_quant/KrxTraderLive.h"

#include "services/krx_quant/KrxQuantService.h"

#include <QStringList>
#include <QTimer>

#include <utility>

namespace fincept::services {

namespace view = krx_quant::trader;

KrxTraderLive::KrxTraderLive(QObject* parent, std::function<QDateTime()> now)
    : QObject(parent), now_(now ? std::move(now) : [] { return QDateTime::currentDateTime(); }) {
    service_ = new KrxQuantService(this);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &KrxTraderLive::read);
    connect(service_, &KrxQuantService::trader_state_ready, this, [this](const krx_quant::TraderState& state) {
        reading_state_ = false;
        state_ = state;
        state_failure_.clear();
        answered_at_ = now_();
        if (running_)
            check_history();
        emit changed();
        schedule();
    });
    connect(service_, &KrxQuantService::trader_state_failed, this, [this](const QString& reason) {
        reading_state_ = false;
        state_failure_ = reason;
        emit changed();
        schedule();
    });
    connect(service_, &KrxQuantService::trader_days_ready, this, [this](const QList<krx_quant::TraderDay>& days) {
        reading_days_ = false;
        days_ = days;
        days_failure_.clear();
        history_done();
    });
    connect(service_, &KrxQuantService::trader_days_failed, this, [this](const QString& reason) {
        reading_days_ = false;
        days_failure_ = reason;
        history_done();
    });
    connect(service_, &KrxQuantService::trader_trades_ready, this,
            [this](const QList<krx_quant::TraderTrade>& trades) {
                reading_trades_ = false;
                trades_ = trades;
                trades_failure_.clear();
                history_done();
            });
    connect(service_, &KrxQuantService::trader_trades_failed, this, [this](const QString& reason) {
        reading_trades_ = false;
        trades_failure_ = reason;
        history_done();
    });
    connect(service_, &KrxQuantService::trader_paused, this,
            [this](bool sell_now, const krx_quant::TraderControl& control) {
                sending_ = false;
                pause_failed_ = false;
                pause_note_ = QStringLiteral("%1 요청을 보냈습니다(%2). trader가 1초 안에 읽습니다.")
                                  .arg(sell_now ? QStringLiteral("정지하고 즉시 매도") : QStringLiteral("새 진입 정지"),
                                       krx_quant::kst_text(control.at, "HH:mm:ss"));
                history_wanted_ = true; // the request and what it does, read at once
                emit changed();
                if (running_)
                    read();
            });
    connect(service_, &KrxQuantService::trader_pause_failed, this, [this](bool sell_now, const QString& reason) {
        sending_ = false;
        pause_failed_ = true;
        pause_note_ = QStringLiteral("%1 요청을 보내지 못했습니다: %2")
                          .arg(sell_now ? QStringLiteral("정지하고 즉시 매도") : QStringLiteral("새 진입 정지"), reason);
        emit changed();
    });
    connect(service_, &KrxQuantService::base_url_changed, this, [this] { drop(); });
}

void KrxTraderLive::start() {
    if (running_)
        return;
    running_ = true;
    read();
}

void KrxTraderLive::stop() {
    running_ = false;
    timer_->stop();
}

void KrxTraderLive::read_now() {
    history_wanted_ = true;
    if (running_)
        read();
}

bool KrxTraderLive::pause(bool sell_now, const Confirm& confirm) {
    if (sending_ || !view::pause_action(sell_now, state_).enabled)
        return false;
    if (!confirm || !confirm(view::pause_question(sell_now, state_)))
        return false; // asked, and not confirmed: nothing is sent
    if (KrxQuantService::base_url() != service_->url()) {
        // Another KRX screen set another engine since this state was read: what was confirmed is not it.
        if (running_)
            read(); // the new engine's, what was read of this one dropped
        pause_failed_ = true;
        pause_note_ = QStringLiteral("엔진 주소가 바뀌었습니다. 새 엔진의 상태를 읽은 뒤에 다시 누르세요.");
        emit changed();
        return false;
    }
    if (!service_->pause_trader(sell_now)) {
        pause_failed_ = true;
        pause_note_ = QStringLiteral("정지 요청을 보내지 못했습니다: 엔진 주소가 없습니다.");
        emit changed();
        return false;
    }
    sending_ = true;
    pause_failed_ = false;
    pause_note_ = QStringLiteral("정지 요청을 보내는 중…");
    emit changed();
    return true;
}

QString KrxTraderLive::failure() const {
    QStringList parts;
    if (!state_failure_.isEmpty())
        parts << state_failure_;
    if (!days_failure_.isEmpty() && !parts.contains(days_failure_))
        parts << QStringLiteral("날마다: %1").arg(days_failure_);
    if (!trades_failure_.isEmpty() && !parts.contains(trades_failure_))
        parts << QStringLiteral("매매: %1").arg(trades_failure_);
    return parts.join(QStringLiteral(" · "));
}

int KrxTraderLive::next_ms() const {
    return running_ ? view::poll_ms(state_, now_()) : 0;
}

void KrxTraderLive::read() {
    timer_->stop();
    if (KrxQuantService::base_url() != service_->url()) {
        service_->rebind(); // what was under way is dropped
        reading_state_ = false;
        reading_days_ = false;
        reading_trades_ = false;
        sending_ = false;
    }
    if (service_->url().isEmpty()) {
        state_failure_ = QStringLiteral("엔진 주소가 없습니다. KRX 4분 예측 화면에서 엔진 주소를 넣으세요.");
        emit changed();
        schedule();
        return;
    }
    if (!reading_state_) {
        reading_state_ = true;
        service_->read_trader();
    }
    // The first read asks for all three at once; later ones, the days and trades as the state says.
    if ((!state_ || history_wanted_) && !reading_days_ && !reading_trades_)
        read_history();
    emit changed();
}

void KrxTraderLive::check_history() {
    if (!state_)
        return;
    const QString key = view::history_key(*state_);
    if (!history_key_)
        history_key_ = key; // asked with the first state, before it answered: of the same moment
    if (reading_days_ || reading_trades_) {
        if (*history_key_ != key) // changed since they were asked: their answer is stale
            history_wanted_ = true;
        return;
    }
    const bool stale = !history_asked_at_.isValid() || history_asked_at_.msecsTo(now_()) >= view::kIdleMs;
    // A failure is asked again with the next state's answer, not at once: no loop on an engine that fails.
    if (history_wanted_ || !days_ || !trades_ || !days_failure_.isEmpty() || !trades_failure_.isEmpty() ||
        *history_key_ != key || stale)
        read_history();
}

void KrxTraderLive::read_history() {
    history_wanted_ = false;
    history_asked_at_ = now_();
    history_key_ = state_ ? std::optional(view::history_key(*state_)) : std::nullopt;
    reading_days_ = true;
    service_->read_trader_days();
    reading_trades_ = true;
    service_->read_trader_trades(kTradesShown);
}

void KrxTraderLive::history_done() {
    emit changed();
    // Asked for again while they were read (read_now, a pause sent, a state changed meanwhile): now.
    if (running_ && history_wanted_ && !reading_days_ && !reading_trades_)
        read_history();
}

void KrxTraderLive::schedule() {
    if (running_)
        timer_->start(next_ms());
}

void KrxTraderLive::drop() {
    state_.reset();
    days_.reset();
    trades_.reset();
    history_key_.reset();
    history_asked_at_ = {};
    state_failure_.clear();
    days_failure_.clear();
    trades_failure_.clear();
    pause_note_.clear();
    pause_failed_ = false;
    answered_at_ = {};
}

} // namespace fincept::services
