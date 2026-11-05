#include "services/krx_quant/KrxTraderLive.h"

#include "services/krx_quant/KrxQuantService.h"

#include <QStringList>
#include <QTimeZone>
#include <QTimer>

#include <utility>

namespace fincept::services {

namespace view = krx_quant::trader;

namespace {

/// A bar's day in KST, where the engine's day is.
QDate kst_day(const krx_quant::Bar& bar) {
    return bar.start.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(9 * 3600)).date();
}

} // namespace

KrxTraderLive::KrxTraderLive(QObject* parent, std::function<QDateTime()> now)
    : QObject(parent), now_(now ? std::move(now) : [] { return QDateTime::currentDateTime(); }) {
    service_ = new KrxQuantService(this);
    timer_ = new QTimer(this);
    timer_->setObjectName(kStateTimerName);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &KrxTraderLive::read);
    chart_timer_ = new QTimer(this);
    chart_timer_->setObjectName(kChartTimerName);
    chart_timer_->setSingleShot(true);
    connect(chart_timer_, &QTimer::timeout, this, &KrxTraderLive::read_chart);
    connect(service_, &KrxQuantService::trader_state_ready, this, [this](const krx_quant::TraderState& state) {
        reading_state_ = false;
        // The engine answers again after a failure (it restarted, or was out of reach): its chart at once.
        const bool back = !state_failure_.isEmpty();
        state_ = state;
        state_failure_.clear();
        answered_at_ = now_();
        if (running_)
            check_history();
        emit changed();
        schedule();
        if (back)
            read_chart();
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
                if (trades_asked_policy_ == trades_policy_) { // else asked before another choice: not kept
                    trades_ = trades;
                    trades_failure_.clear();
                }
                history_done();
            });
    connect(service_, &KrxQuantService::trader_trades_failed, this, [this](const QString& reason) {
        reading_trades_ = false;
        if (trades_asked_policy_ == trades_policy_)
            trades_failure_ = reason;
        history_done();
    });
    connect(service_, &KrxQuantService::trader_policies_ready, this,
            [this](const krx_quant::TraderPolicies& policies) {
                reading_policies_ = false;
                policies_ = policies;
                policies_failure_.clear();
                history_done();
            });
    connect(service_, &KrxQuantService::trader_policies_failed, this, [this](const QString& reason) {
        reading_policies_ = false;
        policies_failure_ = reason;
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
    // The chart (codeyoma/krx-quant#537): the day's bars, those before, and the trader's buys and sells.
    connect(service_, &KrxQuantService::bars_ready, this, [this](const QString&, const QList<krx_quant::Bar>& bars) {
        reading_bars_ = false;
        // The bars read before the day held reach this answer's day when it is the same day, or when the
        // day held had no bar yet and the chart was read all along since (before the open: they are the
        // last trading day's). Else (past midnight, shown again later, the engine back after a failure)
        // they may leave days out: they are read again from this day back, as the view needs.
        const bool same_day =
            !day_bars_.isEmpty() && !bars.isEmpty() && kst_day(bars.first()) == kst_day(day_bars_.first());
        if (!same_day && (stale_ || !day_bars_.isEmpty()))
            forget_older();
        stale_ = false;
        day_bars_ = bars;
        chart_failure_.clear();
        emit chart_changed();
        if (!running_)
            return;
        read_marks(); // over them
        if (day_bars_.isEmpty() && older_.isEmpty()) {
            view_from_ = 0; // nothing today yet (before the open, a holiday): the last day's
            read_older();
        }
        check_decisions_day();
    });
    connect(service_, &KrxQuantService::bars_failed, this, [this](const QString&, const QString& reason) {
        reading_bars_ = false;
        chart_failure_ = reason; // the bars shown stay; asked again as the next minute turns
        stale_ = true;           // days may pass before the next answer
        emit chart_changed();
    });
    connect(service_, &KrxQuantService::older_ready, this, [this](const QString&, const QList<krx_quant::Bar>& bars) {
        reading_older_ = false;
        if (older_asked_ != older_epoch_)
            return read_older(); // asked before those held were dropped: not before the first held now
        oldest_ = bars.isEmpty();
        older_ = krx_quant::join_bars(bars, older_);
        older_failure_.clear();
        emit chart_changed();
        if (running_) {
            read_marks(); // the buys and sells over them
            check_decisions_day();
        }
    });
    connect(service_, &KrxQuantService::older_failed, this, [this](const QString&, const QString& reason) {
        reading_older_ = false;
        if (older_asked_ != older_epoch_)
            return read_older(); // the bars it was asked for were dropped meanwhile
        older_failure_ = reason; // the bars shown stay; scrolling to the left end again asks again
        emit chart_changed();
    });
    connect(service_, &KrxQuantService::trader_ready, this,
            [this](const QString&, const QDateTime& before, const QList<krx_quant::TraderMark>& marks) {
                marks_paging_ = false;
                const bool back = before.isValid();
                // More since the last read than a page holds: those held and the latest page do not meet,
                // so all are read again from this page back to the first bar.
                if (!back && krx_quant::trader_gap(marks_, marks)) {
                    marks_.clear();
                    marks_from_ = {};
                }
                // A page back was asked a millisecond past where they were read from (trader_before):
                // unless reading again from scratch moved that since.
                if (!back || (marks_from_.isValid() && before == marks_from_.addMSecs(1)))
                    marks_from_ = krx_quant::trader_from_after(marks_from_, marks, back);
                if (const auto joined = krx_quant::join_trader_marks(marks_, marks); joined != marks_) {
                    marks_ = joined;
                    emit chart_changed();
                }
                read_marks(); // back to the first bar held
            });
    connect(service_, &KrxQuantService::trader_failed, this, [this](const QString&, const QString&) {
        marks_paging_ = false; // asked again with the next chart read; the marks shown stay
    });
    // The decisions (codeyoma/krx-quant#601): the chosen book's day, page after page back to its first.
    connect(
        service_, &KrxQuantService::trader_decisions_ready, this,
        [this](const QString& book, const QDateTime& before, const krx_quant::TraderDecisions& answer, bool served) {
            reading_decisions_ = false;
            // Asked for another book, or stopped since: not kept (a book chosen since is read now).
            if (book != decisions_book_ || !running_ || (before.isValid() && !pages_)) {
                pages_.reset();
                emit decisions_changed();
                if (running_)
                    read_decisions();
                return;
            }
            if (!served) { // an engine before krx-quant #584: none, and not a failure
                pages_.reset();
                decisions_.reset();
                decisions_served_ = false;
                decisions_failure_.clear();
            } else {
                decisions_served_ = true;
                if (!before.isValid())
                    pages_ = view::decision_pages(answer);
                else
                    view::join_decisions(*pages_, answer, before);
                if (const auto next = view::decisions_before(*pages_)) { // the page before, of the same day
                    reading_decisions_ =
                        service_->read_trader_decisions(decisions_book_, pages_->read.day, *next, view::kDecisionPage);
                    if (reading_decisions_)
                        return;
                }
                decisions_ = pages_->read;
                pages_.reset();
                decisions_failure_.clear();
            }
            emit decisions_changed();
            if (decisions_wanted_) // asked again meanwhile (a minute turned, the chart's day changed)
                read_decisions();
        });
    connect(service_, &KrxQuantService::trader_decisions_failed, this,
            [this](const QString& book, const QDateTime&, const QString& reason) {
                reading_decisions_ = false;
                pages_.reset();
                if (book == decisions_book_)
                    decisions_failure_ = reason; // those shown stay; asked again as the next minute turns
                emit decisions_changed();
                if (running_ && (decisions_wanted_ || book != decisions_book_))
                    read_decisions();
            });
    connect(service_, &KrxQuantService::base_url_changed, this, [this] {
        drop();
        read_chart(); // the new engine's, at once
    });
}

void KrxTraderLive::start() {
    if (running_)
        return;
    running_ = true;
    stale_ = true; // not read while hidden: days may have passed
    read_chart();  // first: a new address is taken there, and the chart read from it
    read();
}

void KrxTraderLive::stop() {
    running_ = false;
    timer_->stop();
    chart_timer_->stop();
}

void KrxTraderLive::read_now() {
    history_wanted_ = true;
    if (!running_)
        return;
    read_chart();
    read();
}

void KrxTraderLive::view_from(double from, double to) {
    view_from_ = from;
    view_width_ = to - from;
    read_older();
}

void KrxTraderLive::read_chart() {
    chart_timer_->stop();
    if (!running_)
        return;
    if (KrxQuantService::base_url() != service_->url())
        return read(); // another engine: read() takes it, and its base_url_changed reads its chart
    if (!service_->url().isEmpty()) {
        if (!reading_bars_) { // else the read under way answers
            reading_bars_ = true;
            service_->read_bars(QString::fromLatin1(krx_quant::kTraderSymbol));
        }
        read_marks(true); // the buys and sells since, and the sells of trades open then
        read_decisions(); // codeyoma/krx-quant#601
    }
    chart_timer_->start(view::chart_ms(now_()));
}

void KrxTraderLive::read_older() {
    if (!running_ || oldest_ || reading_older_ || service_->url().isEmpty())
        return; // one at a time: the view moves on, and asks again once it is drawn
    // The first bar shown: the earliest read before, else the day's first; none: those before now.
    const QDateTime first = !older_.isEmpty()      ? older_.first().start
                            : !day_bars_.isEmpty() ? day_bars_.first().start
                                                   : QDateTime();
    if (const auto read = krx_quant::older_read(first, view_from_, 1, view_width_)) {
        reading_older_ = service_->read_older(QString::fromLatin1(krx_quant::kTraderSymbol), *read);
        older_asked_ = older_epoch_;
    }
}

void KrxTraderLive::forget_older() {
    older_.clear();
    oldest_ = false;
    ++older_epoch_; // a page before them under way is not theirs
}

void KrxTraderLive::read_marks(bool latest) {
    marks_latest_ |= latest;
    if (!running_ || marks_paging_)
        return; // asked again once started, or once the page under way answers
    const QDateTime first = !older_.isEmpty()      ? older_.first().start
                            : !day_bars_.isEmpty() ? day_bars_.first().start
                                                   : QDateTime();
    const auto before = krx_quant::trader_before(marks_latest_ ? QDateTime() : marks_from_, first);
    if (!before)
        return; // no bar yet, or read back to the first: asked again when bars come
    if (!before->isValid())
        marks_latest_ = false;
    marks_paging_ = true;
    service_->read_trader_marks(QString::fromLatin1(krx_quant::kTraderSymbol), *before, krx_quant::kTraderPage);
}

void KrxTraderLive::forget_chart() {
    day_bars_.clear();
    forget_older();
    stale_ = false;
    reading_bars_ = false; // the reads under way were dropped with the old engine's
    reading_older_ = false;
    view_from_ = krx_quant::kOlderMargin;
    view_width_ = 0;
    marks_.clear();
    marks_from_ = {};
    marks_paging_ = false;
    marks_latest_ = false;
    chart_failure_.clear();
    older_failure_.clear();
    emit chart_changed();
}

void KrxTraderLive::set_decisions_book(const QString& book) {
    if (book == decisions_book_)
        return;
    decisions_book_ = book;
    decisions_.reset(); // another book's are not shown under this one
    decisions_failure_.clear();
    pages_.reset(); // a page under way is the other book's: its answer is not kept
    if (running_)
        read_decisions();
    emit decisions_changed();
}

void KrxTraderLive::read_decisions() {
    if (!running_ || service_->url().isEmpty())
        return;
    if (reading_decisions_) { // once the read under way ends: what came since
        decisions_wanted_ = true;
        return;
    }
    decisions_wanted_ = false;
    pages_.reset();
    decisions_day_ = chart_day();
    reading_decisions_ = service_->read_trader_decisions(decisions_book_, decisions_day_, {}, view::kDecisionPage);
    if (reading_decisions_)
        emit decisions_changed();
}

void KrxTraderLive::check_decisions_day() {
    // Once the engine has them and they were read: an engine without them, or one that failed, is asked
    // again with the chart as the next minute turns, not on each answer of its bars.
    if (!running_ || !decisions_served_ || !decisions_failure_.isEmpty())
        return;
    const QDate day = chart_day();
    const QDate read = reading_decisions_ ? decisions_day_ : decisions_ ? decisions_->day : QDate();
    if (day.isValid() && day != read)
        read_decisions(); // the chart shows another day's bars: that day's decisions
}

QDate KrxTraderLive::chart_day() const {
    if (!day_bars_.isEmpty())
        return kst_day(day_bars_.last());
    return older_.isEmpty() ? QDate() : kst_day(older_.last());
}

void KrxTraderLive::forget_decisions() {
    decisions_.reset();
    pages_.reset();
    decisions_day_ = {};
    decisions_served_ = true;   // the new engine's not known yet
    reading_decisions_ = false; // the read under way was dropped with the old engine's
    decisions_wanted_ = false;
    decisions_failure_.clear();
    emit decisions_changed();
}

void KrxTraderLive::set_trades_policy(std::optional<qint64> policy) {
    if (policy == trades_policy_)
        return;
    trades_policy_ = policy;
    trades_.reset(); // another policy's trades are not shown under this one
    trades_failure_.clear();
    if (running_)
        read_trades();
    emit changed();
}

void KrxTraderLive::read_trades() {
    if (reading_trades_) { // the answer under way is the other choice's: asked again once it is in
        trades_wanted_ = true;
        return;
    }
    trades_wanted_ = false;
    reading_trades_ = true;
    trades_asked_policy_ = trades_policy_;
    service_->read_trader_trades(kTradesShown, trades_policy_);
}

bool KrxTraderLive::pause(bool sell_now, const Confirm& confirm) {
    if (sending_ || !view::pause_action(sell_now, state_).enabled)
        return false;
    // The engine whose state is shown and asked about: the pause goes to it, or nowhere.
    const QString shown = service_->url();
    const int engine = engine_;
    if (KrxQuantService::base_url() != shown) // another KRX screen set another engine since it was read
        return refuse_pause();
    if (!confirm || !confirm(view::pause_question(sell_now, state_)))
        return false; // asked, and not confirmed: nothing is sent
    // The confirm window runs its own event loop: a poll meanwhile may have read a new address's engine.
    if (engine != engine_ || service_->url() != shown || KrxQuantService::base_url() != shown)
        return refuse_pause();
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

bool KrxTraderLive::refuse_pause() {
    if (running_)
        read(); // the new engine's, what was read of the other dropped
    pause_failed_ = true;
    pause_note_ = QStringLiteral("엔진 주소가 바뀌었습니다. 보내지 않았습니다. 새 엔진의 상태를 읽은 뒤에 다시 누르세요.");
    emit changed();
    return false;
}

QString KrxTraderLive::failure() const {
    QStringList parts;
    if (!state_failure_.isEmpty())
        parts << state_failure_;
    if (!days_failure_.isEmpty() && !parts.contains(days_failure_))
        parts << QStringLiteral("날마다: %1").arg(days_failure_);
    if (!trades_failure_.isEmpty() && !parts.contains(trades_failure_))
        parts << QStringLiteral("매매: %1").arg(trades_failure_);
    if (!policies_failure_.isEmpty() && !parts.contains(policies_failure_))
        parts << QStringLiteral("정책: %1").arg(policies_failure_);
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
        reading_policies_ = false;
        trades_wanted_ = false;
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
    if ((!state_ || history_wanted_) && !reading_days_ && !reading_trades_ && !reading_policies_)
        read_history();
    emit changed();
}

void KrxTraderLive::check_history() {
    if (!state_)
        return;
    const QString key = view::history_key(*state_);
    if (!history_key_)
        history_key_ = key; // asked with the first state, before it answered: of the same moment
    if (reading_days_ || reading_trades_ || reading_policies_) {
        if (*history_key_ != key) // changed since they were asked: their answer is stale
            history_wanted_ = true;
        return;
    }
    const bool stale = !history_asked_at_.isValid() || history_asked_at_.msecsTo(now_()) >= view::kIdleMs;
    // A failure is asked again with the next state's answer, not at once: no loop on an engine that fails.
    if (history_wanted_ || !days_ || !trades_ || !policies_ || !days_failure_.isEmpty() || !trades_failure_.isEmpty() ||
        !policies_failure_.isEmpty() || *history_key_ != key || stale)
        read_history();
}

void KrxTraderLive::read_history() {
    history_wanted_ = false;
    history_asked_at_ = now_();
    history_key_ = state_ ? std::optional(view::history_key(*state_)) : std::nullopt;
    reading_days_ = true;
    service_->read_trader_days();
    read_trades();
    reading_policies_ = true;
    service_->read_trader_policies();
}

void KrxTraderLive::history_done() {
    emit changed();
    if (!running_)
        return;
    // Asked for again while they were read (read_now, a pause sent, a state changed meanwhile): now.
    if (history_wanted_ && !reading_days_ && !reading_trades_ && !reading_policies_)
        read_history();
    else if (trades_wanted_ && !reading_trades_) // another policy chosen while the trades were read
        read_trades();
}

void KrxTraderLive::schedule() {
    if (running_)
        timer_->start(next_ms());
}

void KrxTraderLive::drop() {
    ++engine_;
    state_.reset();
    days_.reset();
    trades_.reset();
    policies_.reset();
    trades_policy_.reset(); // another engine's versions: the champion's trades again
    trades_asked_policy_.reset();
    trades_wanted_ = false;
    history_key_.reset();
    history_asked_at_ = {};
    state_failure_.clear();
    days_failure_.clear();
    trades_failure_.clear();
    policies_failure_.clear();
    pause_note_.clear();
    pause_failed_ = false;
    answered_at_ = {};
    forget_chart();
    forget_decisions();
}

} // namespace fincept::services
