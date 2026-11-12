#include "services/krx_quant/EnginePoller.h"

#include "services/krx_quant/KrxQuantService.h"

#include <QEvent>
#include <QTimer>

#include <utility>

namespace fincept::services {

EnginePoller::EnginePoller(Symbols symbols, QObject* parent, std::function<QDateTime()> now)
    : QObject(parent), want_(symbols), now_(now ? std::move(now) : [] { return QDateTime::currentDateTime(); }) {
    service_ = new KrxQuantService(this);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &EnginePoller::read);
    // Another engine: what was read of the old one goes, first here, then the screen's.
    connect(service_, &KrxQuantService::base_url_changed, this, [this] {
        symbols_.clear();
        answered_at_ = {};
        emit engine_changed();
    });
    if (want_ == Symbols::None)
        return;
    // The names alone: a failure is not the screen's, and they are asked for again with the next read.
    connect(service_, &KrxQuantService::symbols_ready, this, [this](const QList<krx_quant::SymbolQuote>& symbols) {
        symbols_ = symbols;
        emit symbols_ready();
        emit changed();
    });
}

void EnginePoller::set_timer_name(const QString& name) {
    timer_->setObjectName(name);
}

void EnginePoller::start() {
    if (running_)
        return;
    running_ = true;
    emit started();
    read();
}

void EnginePoller::stop() {
    running_ = false;
    timer_->stop();
    emit stopped();
}

void EnginePoller::run_while_shown(QObject* widget) {
    widget->installEventFilter(this);
}

bool EnginePoller::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Show)
        start();
    else if (event->type() == QEvent::Hide)
        stop(); // nothing is asked of the engine while the screen cannot be seen
    return QObject::eventFilter(watched, event);
}

void EnginePoller::read_now() {
    if (running_)
        read();
}

void EnginePoller::schedule() {
    if (running_)
        timer_->start(next_ms());
}

void EnginePoller::answered() {
    failure_.clear();
    answered_at_ = now_();
    emit changed();
    schedule();
}

void EnginePoller::failed(const QString& reason) {
    failure_ = reason;
    emit changed();
    schedule();
}

int EnginePoller::next_ms() const {
    return running_ && poll_ ? poll_() : 0;
}

void EnginePoller::read() {
    timer_->stop();
    if (KrxQuantService::base_url() != service_->url())
        service_->rebind(); // what was under way is dropped; engine_changed follows
    if (service_->url().isEmpty()) {
        failure_ = QString::fromUtf8(kNoAddress);
        emit changed();
        schedule();
        return;
    }
    if (want_ == Symbols::Names && symbols_.isEmpty())
        service_->read_symbol_list();
    if (read_)
        read_();
}

} // namespace fincept::services
