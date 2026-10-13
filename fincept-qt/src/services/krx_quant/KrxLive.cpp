#include "services/krx_quant/KrxLive.h"

#include <utility>

namespace fincept::services {

namespace {

constexpr int kTickMs = 5000;
// With the stream connected the API is read less often: it only catches what the stream missed.
constexpr int kRefreshEvery = 6; ///< ticks between full reads (30 s)
constexpr int kChartEvery = 12;  ///< 60 s
constexpr int kMacroEvery = 60;  ///< 5 min
constexpr int kLeaderboardDelayMs = 3000;

/// The final consensus among a symbol's latest predictions, if there.
std::optional<krx_quant::Prediction> final_of(const krx_quant::Latest& latest) {
    for (const auto& p : latest.predictions)
        if (p.algorithm == "consensus-final")
            return p;
    return std::nullopt;
}

} // namespace

KrxLive::KrxLive(QObject* parent) : QObject(parent) {
    qRegisterMetaType<krx_quant::Signal>();
    service_ = new KrxQuantService(this);
    stream_ = new KrxStream(this);

    connect(service_, &KrxQuantService::snapshot_ready, this, &KrxLive::on_snapshot);
    connect(service_, &KrxQuantService::refresh_failed, this, &KrxLive::on_failure);
    connect(service_, &KrxQuantService::status_ready, this,
            [this](const krx_quant::EngineStatus& status) { on_status(status, false); });
    connect(service_, &KrxQuantService::chart_ready, this, &KrxLive::on_chart);
    connect(service_, &KrxQuantService::chart_failed, this, [this](const QString& symbol, const QString& reason) {
        if (symbol == selected_)
            emit chart_failed(reason);
        else if (!selected_.isEmpty())
            service_->read_chart(selected_); // another was chosen meanwhile
    });
    connect(
        service_, &KrxQuantService::older_ready, this,
        [this](const QString& symbol, const QList<krx_quant::Bar>& bars, const QList<krx_quant::Prediction>& finals) {
            if (symbol != selected_)
                return view_from(view_from_, view_minutes_); // another was chosen meanwhile: its own, if its view asked
            oldest_ = bars.isEmpty();
            older_ = krx_quant::join_bars(bars, older_);
            board_.set_history(symbol, finals);
            emit chart_changed();
        });
    connect(service_, &KrxQuantService::older_failed, this, [this](const QString& symbol, const QString& reason) {
        if (symbol == selected_)
            emit older_failed(reason);
        else
            view_from(view_from_, view_minutes_); // another was chosen meanwhile
    });
    connect(service_, &KrxQuantService::macro_ready, this, [this](const krx_quant::Macro& macro) {
        macro_ = macro;
        macro_failure_.clear();
        emit macro_changed(macro);
    });
    connect(service_, &KrxQuantService::macro_failed, this, [this](const QString& reason) {
        macro_.reset();
        macro_failure_ = reason;
        emit macro_failed(reason);
    });
    connect(service_, &KrxQuantService::leaderboard_ready, this, [this](const krx_quant::Leaderboard& leaderboard) {
        if (query_.matches(leaderboard))
            emit leaderboard_changed(leaderboard);
        else
            service_->read_leaderboard(query_); // another was asked for meanwhile
    });
    connect(service_, &KrxQuantService::leaderboard_failed, this, &KrxLive::leaderboard_failed);
    connect(service_, &KrxQuantService::stats_ready, this, [this](const krx_quant::AlgorithmStats& stats) {
        if (!details_.contains(stats.algorithm))
            return; // its window closed meanwhile
        if (query_.matches(stats))
            emit stats_changed(stats);
        else
            service_->read_stats(stats.algorithm, query_); // another query was chosen meanwhile
    });
    connect(service_, &KrxQuantService::close_ready, this,
            [this](const QString& symbol, const QDateTime& bar_close, const QList<krx_quant::Prediction>& made,
                   const QMap<QString, QString>& statuses) {
                if (symbol != selected_ || bar_close != pinned_)
                    return; // another was chosen meanwhile
                pinned_predictions_ = made;
                statuses_ = statuses;
                emit pinned_changed();
            });
    connect(service_, &KrxQuantService::close_failed, this,
            [this](const QString& symbol, const QDateTime& bar_close, const QString& reason) {
                if (symbol == selected_ && bar_close == pinned_)
                    emit pinned_failed(reason);
            });
    connect(service_, &KrxQuantService::stats_failed, this, [this](const QString& algorithm, const QString& reason) {
        if (details_.contains(algorithm))
            emit stats_failed(algorithm, reason);
    });
    connect(service_, &KrxQuantService::base_url_changed, this, [this](const QString& url) {
        stream_->open(url); // also when another KRX panel applied an address
        // The old engine's macro row goes; the new engine's is read.
        macro_.reset();
        macro_failure_.clear();
        service_->read_macro();
        emit base_url_changed(url);
    });

    connect(stream_, &KrxStream::connected, this, [this]() {
        stream_state_ = tr("스트림 연결됨");
        emit status_changed();
        read_everything(); // the stream sends what happens from now on: what happened before is read again
    });
    connect(stream_, &KrxStream::disconnected, this, [this](const QString& reason, int retry_in) {
        stream_state_ = retry_in < 0 ? reason : tr("스트림 끊김: %1 · %2초 뒤 다시 연결").arg(reason).arg(retry_in);
        emit status_changed();
    });
    connect(stream_, &KrxStream::event_received, this, &KrxLive::on_event);

    timer_.setInterval(kTickMs);
    connect(&timer_, &QTimer::timeout, this, &KrxLive::tick);
    leaderboard_soon_.setSingleShot(true);
    leaderboard_soon_.setInterval(kLeaderboardDelayMs);
    connect(&leaderboard_soon_, &QTimer::timeout, this, &KrxLive::read_leaderboard);
}

void KrxLive::start() {
    service_->refresh(); // reads the address from the settings; opens the stream on it
    service_->read_macro();
    read_leaderboard();
    timer_.start();
}

void KrxLive::apply_base_url(const QString& url) {
    const QString before = KrxQuantService::base_url();
    KrxQuantService::set_base_url(url);
    board_ = {};
    bars_.clear();
    forget_older(); // the old engine's
    status_.reset();
    macro_.reset(); // the new engine's is read below
    macro_failure_.clear();
    failure_.clear();
    read_errors_.clear();
    emit predictions_changed();
    emit chart_changed();
    emit status_changed();
    if (KrxQuantService::base_url() == before)
        stream_->open(before); // the same address again: connect again too
    service_->restart();       // a new one opens the stream through base_url_changed
    service_->read_macro();
    read_leaderboard();
}

void KrxLive::set_shown(bool shown) {
    shown_ = shown;
    if (shown)
        read_everything();
}

void KrxLive::select(const QString& symbol) {
    if (symbol == selected_)
        return;
    selected_ = symbol;
    pinned_ = {}; // another symbol: its latest
    pinned_predictions_.clear();
    emit pinned_changed();
    bars_.clear();
    forget_older();
    emit chart_changed();
    emit predictions_changed();
    if (!symbol.isEmpty())
        service_->read_chart(symbol);
}

void KrxLive::set_leaderboard_query(const KrxQuantService::LeaderboardQuery& query) {
    query_ = query;
    read_leaderboard();
}

void KrxLive::set_details(const QStringList& algorithms) {
    const QStringList before = std::exchange(details_, algorithms);
    for (const auto& algorithm : details_)
        if (!before.contains(algorithm))
            service_->read_stats(algorithm, query_);
}

void KrxLive::view_from(double from, int minutes) {
    view_from_ = from;
    view_minutes_ = minutes;
    if (selected_.isEmpty() || oldest_)
        return;
    // The first bar shown: the earliest read before, else today's first.
    const QDateTime first = !older_.isEmpty()  ? older_.first().start
                            : !bars_.isEmpty() ? bars_.first().start
                                               : QDateTime();
    if (const auto read = krx_quant::older_read(first, from, minutes))
        service_->read_older(selected_, *read);
}

void KrxLive::forget_older() {
    older_.clear();
    oldest_ = false;
    view_from_ = krx_quant::kOlderMargin;
}

void KrxLive::pin(const QDateTime& bar_close) {
    pinned_ = bar_close;
    pinned_predictions_.clear();
    emit pinned_changed();
    if (pinned_.isValid() && !selected_.isEmpty())
        service_->read_close(selected_, pinned_);
}

void KrxLive::read_details() {
    for (const auto& algorithm : details_)
        service_->read_stats(algorithm, query_);
}

void KrxLive::set_alerts(bool on, const krx_quant::SignalRule& rule) {
    alerts_on_ = on;
    alerts_.set_rule(rule);
}

QDateTime KrxLive::engine_now() const {
    return QDateTime::currentDateTime().addMSecs(clock_offset_ms_);
}

void KrxLive::tick() {
    ++ticks_;
    const bool live = stream_->is_connected();
    if (!shown_) {
        // Only for the signal alerts: without the stream, the predictions are read here; and
        // an address another KRX panel applied is followed, not the old engine's stream.
        if ((alerts_on_ && !live) || KrxQuantService::base_url() != service_->url())
            service_->refresh();
        // Detail windows stay open beside other screens: without the stream (whose outcomes
        // read them with the leaderboard), they are read here.
        if (!live)
            read_details();
        return;
    }
    if (!live || ticks_ % kRefreshEvery == 0)
        service_->refresh();
    else
        service_->read_status();
    // Without the stream no outcome tells when the leaderboard changed: it is read each tick.
    if (!live || ticks_ % kChartEvery == 0)
        read_leaderboard();
    if (ticks_ % kChartEvery == 0 && !selected_.isEmpty())
        service_->read_chart(selected_); // and on each new bar close
    if (ticks_ % kMacroEvery == 0)
        service_->read_macro();
}

void KrxLive::read_everything() {
    service_->refresh();
    if (!selected_.isEmpty())
        service_->read_chart(selected_);
    read_leaderboard();
    service_->read_macro();
}

void KrxLive::read_leaderboard() {
    service_->read_leaderboard(query_);
    read_details();
    if (pinned_.isValid() && !selected_.isEmpty()) // its outcomes come in as they are scored
        service_->read_close(selected_, pinned_);
}

void KrxLive::on_snapshot(const KrxQuantService::Snapshot& snapshot) {
    read_errors_ = snapshot.errors;
    // An engine whose clock went back (a demo started again) takes the screen back too.
    if (snapshot.status.now.isValid() && board_.rewind_to(snapshot.status.now))
        bars_.clear();
    const QDateTime shown_before = board_.latest(selected_).bar_close;
    board_.set_symbols(snapshot.symbols);
    if (snapshot.members)
        board_.set_members(*snapshot.members);
    for (const auto& latest : snapshot.latest)
        board_.set_latest(latest);
    for (const auto& latest : snapshot.latest)
        if (const auto final = final_of(board_.latest(latest.symbol)))
            consider(*final, snapshot.status.now);

    const bool listed = std::any_of(snapshot.symbols.begin(), snapshot.symbols.end(),
                                    [this](const krx_quant::SymbolQuote& q) { return q.symbol == selected_; });
    if (!listed && !snapshot.symbols.isEmpty())
        select(snapshot.symbols.first().symbol);
    else
        follow_bar_close(shown_before);
    emit predictions_changed();
    on_status(snapshot.status, true);
}

void KrxLive::on_status(const krx_quant::EngineStatus& status, bool with_snapshot) {
    const bool recovering = !failure_.isEmpty();
    failure_.clear();
    status_ = status;
    set_engine_clock(status.now);
    if (status.now.isValid() && board_.rewind_to(status.now)) {
        bars_.clear();
        emit predictions_changed();
        emit chart_changed();
        if (!selected_.isEmpty())
            service_->read_chart(selected_);
    }
    if (recovering) {
        // Back after a failure: what was not read meanwhile is read again.
        if (!with_snapshot)
            service_->refresh();
        service_->read_macro();
        read_leaderboard();
        if (!selected_.isEmpty())
            service_->read_chart(selected_);
    }
    emit status_changed();
}

void KrxLive::on_failure(const QString& reason) {
    status_.reset();
    failure_ = reason;
    read_errors_.clear();
    board_.forget_predictions(); // old predictions must not pass for current ones
    bars_.clear();
    forget_older();
    emit predictions_changed();
    emit chart_changed();
    emit status_changed();
}

void KrxLive::on_chart(const QString& symbol, const QList<krx_quant::Bar>& bars,
                       const QList<krx_quant::Prediction>& finals) {
    if (symbol != selected_) {
        if (!selected_.isEmpty())
            service_->read_chart(selected_); // another was chosen meanwhile
        return;
    }
    bars_ = bars;
    board_.set_history(symbol, finals);
    emit chart_changed();
    if (bars_.isEmpty() && older_.isEmpty())
        view_from(0, view_minutes_); // nothing today yet (before the open): the last day's bars
}

void KrxLive::on_event(const krx_quant::Event& event) {
    switch (event.type) {
        case krx_quant::Event::Type::Health:
            if (status_) {
                status_->kis_connected = event.kis_connected;
                status_->feeds = event.feeds;
                emit status_changed();
            }
            return;
        case krx_quant::Event::Type::Lifecycle:
            board_.apply(event);
            leaderboard_soon_.start(); // its status in the leaderboard
            emit predictions_changed();
            return;
        case krx_quant::Event::Type::Other:
            return;
        case krx_quant::Event::Type::Prediction:
        case krx_quant::Event::Type::Consensus:
        case krx_quant::Event::Type::Outcome:
            break;
    }
    set_engine_clock(event.at);
    const QDateTime shown_before = board_.latest(selected_).bar_close;
    for (const auto& final : board_.apply(event))
        consider(final, event.at);
    if (event.type == krx_quant::Event::Type::Outcome)
        leaderboard_soon_.start(); // outcomes come a bar close at a time: one read after them
    follow_bar_close(shown_before);
    emit predictions_changed();
}

void KrxLive::consider(const krx_quant::Prediction& final, const QDateTime& now) {
    if (!alerts_on_)
        return;
    if (const auto signal = alerts_.consider(final, board_.pooled(final.symbol, final.bar_close), now))
        emit signal_raised(*signal);
}

void KrxLive::set_engine_clock(const QDateTime& now) {
    if (now.isValid())
        clock_offset_ms_ = now.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch();
}

void KrxLive::follow_bar_close(const QDateTime& shown_before) {
    if (shown_ && !selected_.isEmpty() && board_.latest(selected_).bar_close != shown_before)
        service_->read_chart(selected_); // a new bar close: a new bar
}

} // namespace fincept::services
