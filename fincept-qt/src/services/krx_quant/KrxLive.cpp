#include "services/krx_quant/KrxLive.h"

#include "services/krx_quant/ChartStudies.h"

#include <QTimeZone>

#include <algorithm>
#include <utility>

namespace fincept::services {

namespace {

constexpr int kTickMs = 5000;
// With the stream connected the API is read less often: it only catches what the stream missed.
constexpr int kRefreshEvery = 6; ///< ticks between full reads (30 s)
constexpr int kChartEvery = 12;  ///< 60 s
constexpr int kMacroEvery = 60;  ///< 5 min
constexpr int kLeaderboardDelayMs = 3000;
/// The most days of special situations the engine gives in one answer (krx-quant #236: more is 400).
constexpr int kSpecialDays = 60;
/// The most days of daily supply data the engine gives in one answer (krx-quant #226: more is 400).
constexpr int kSupplyDays = 4000;
/// A week, month or year view reads the daily supply data from this many days before its first bar
/// (krx-quant #226): its flows add up over each period only when the days read reach before it
/// (ChartStudies' supply_values), and the first period starts on a weekend or holiday as often as
/// not. Longer than any break in trading (Chuseok 2017: 11 days without a trading day).
constexpr int kSupplyLead = 14;
/// A bar window is read again this long after the stream touched it (krx-quant #255): a bar close
/// brings a prediction of each predictor within seconds.
constexpr int kBarDelayMs = 1000;

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
        else
            read_chart(); // another was chosen meanwhile
    });
    connect(service_, &KrxQuantService::older_ready, this,
            [this](const QString& symbol, const QList<krx_quant::Bar>& bars) {
                if (symbol != selected_)
                    return read_older(); // another was chosen meanwhile: its own, if its view asked
                oldest_ = bars.isEmpty();
                older_ = krx_quant::join_bars(bars, older_);
                emit chart_changed();
                ensure_marks();      // their icons' predictions, if not read along (krx-quant #239)
                read_kind_history(); // the kind's over the bars now shown too (krx-quant #182)
                read_trader();       // the trader's buys and sells over them (krx-quant #398)
                read_history();      // more for the indicators (krx-quant #222)
                read_special(false); // the special situations of the days now held (krx-quant #236)
                read_supply(false);  // and their daily supply data (krx-quant #226)
            });
    connect(service_, &KrxQuantService::predictions_ready, this,
            [this](const QString& symbol, const QDateTime& before, const QList<krx_quant::Prediction>& predictions,
                   bool all) {
                marks_paging_ = false;
                if (symbol == selected_) {
                    board_.set_history(symbol, predictions);
                    // Read before where they were read from: unless another read moved that since.
                    if (before == (all ? full_from_ : finals_from_))
                        marks_read(predictions, all);
                    emit marks_changed();
                }
                ensure_marks(); // back to the first bar shown, or another symbol's
            });
    connect(service_, &KrxQuantService::predictions_failed, this, [this](const QString& symbol, const QString& reason) {
        marks_paging_ = false; // asked again when the view moves or a bar close comes
        if (symbol == selected_)
            emit older_failed(reason);
    });
    connect(service_, &KrxQuantService::older_failed, this, [this](const QString& symbol, const QString& reason) {
        if (symbol != selected_)
            return read_older();                         // another was chosen meanwhile
        if (!krx_quant::candle_view(view_minutes_))      // not since the view changed to candles
            emit older_failed(reason);
    });
    connect(
        service_, &KrxQuantService::candles_ready, this,
        [this](const QString& symbol, const QString& period, const QDateTime& before, const krx_quant::Candles& read) {
            if (symbol == selected_) {
                int view = krx_quant::kDayView;
                for (const int v : {krx_quant::kWeekView, krx_quant::kMonthView, krx_quant::kYearView})
                    if (krx_quant::candle_period(v) == period)
                        view = v;
                Candles& kept = candles_[view];
                // Over those kept: the latest page replaces its own candles and keeps the
                // pages read before; a page before the first goes in front.
                kept.read = krx_quant::join_bars(kept.read, read.candles);
                if (!read.venue.isEmpty())
                    candle_venue_ = read.venue;
                if (!before.isValid())
                    kept.day = engine_day();
                if (before.isValid() || kept.read.isEmpty())
                    kept.oldest = read.candles.isEmpty();
                emit chart_changed();
                read_kind_history(); // a daily kind's over the days now held (krx-quant #264)
            }
            set_view(view_minutes_); // what else the view needs (one read at a time), or another symbol's
        });
    connect(service_, &KrxQuantService::candles_failed, this,
            [this](const QString& symbol, const QString& period, const QString& reason) {
                // The failure is the view's own when it reads that period: its own, or the days
                // a longer view is made of. Shown then, and asked again on the chart's next read
                // (at once would ask again and again).
                const bool own = krx_quant::candle_view(view_minutes_) &&
                                 (period == krx_quant::candle_period(view_minutes_) || period == QLatin1String("day"));
                if (symbol == selected_ && own)
                    return emit older_failed(reason);
                set_view(view_minutes_); // another symbol or view was chosen meanwhile: its reads, waiting till now
            });
    connect(service_, &KrxQuantService::special_ready, this,
            [this](const QString& symbol, const QDate& from, const QDate& to,
                   const QList<krx_quant::SpecialWindow>& windows) {
                if (symbol == selected_) {
                    special_read_ = std::pair(from, to);
                    if (windows != special_) {
                        special_ = windows;
                        emit special_changed();
                    }
                }
                read_special(false); // another symbol's, or more days held meanwhile
            });
    connect(service_, &KrxQuantService::special_failed, this,
            [this](const QString& symbol, const QDate&, const QDate&, const QString&) {
                // The bands are left as they were; asked again within a minute. Another symbol's at once.
                if (symbol != selected_)
                    read_special(false);
            });
    connect(service_, &KrxQuantService::supply_ready, this,
            [this](const QString& symbol, const QDate& from, const QDate& to, const QList<krx_quant::SupplyDay>& days,
                   bool served) {
                if (symbol == selected_) {
                    supply_read_ = std::pair(from, to);
                    if (days != supply_ || served != supply_served_) {
                        supply_ = days;
                        supply_served_ = served;
                        emit supply_changed();
                    }
                }
                read_supply(false); // another symbol's, or more days held meanwhile
            });
    connect(service_, &KrxQuantService::supply_failed, this,
            [this](const QString& symbol, const QDate& from, const QDate& to, const QString&) {
                // The values are left as they were. The days wanted now failed: asked again within 5
                // minutes. Another symbol's, or days no longer wanted (the view or the bars held changed
                // while they were read, and that read was skipped as one was under way): at once.
                if (symbol != selected_ || supply_days() != std::optional(std::pair(from, to)))
                    read_supply(false);
            });
    connect(service_, &KrxQuantService::algorithms_ready, this,
            [this](const QMap<QString, krx_quant::AlgorithmInfo>& algorithms) {
                algorithms_ = algorithms;
                algorithms_failure_.clear();
                emit algorithms_changed();
            });
    connect(service_, &KrxQuantService::algorithms_failed, this, [this](const QString& reason) {
        algorithms_.clear(); // not shown as the engine's while they cannot be read
        algorithms_failure_ = reason;
        emit algorithms_changed();
    });
    connect(service_, &KrxQuantService::kinds_ready, this, [this](const QList<krx_quant::KindInfo>& kinds) {
        const auto described = kind_info();
        const QString shown = kind_;
        const QStringList windows = window_kinds();
        kinds_ = kinds;
        emit kinds_changed();
        apply_kind();
        if (window_kinds() != windows) { // the bar windows' kinds (krx-quant #255): read again
            for (auto& watch : bars_watched_)
                watch.settled = false;
            read_bars_due();
        }
        if (kind_ == shown && kind_info() != described) {
            // The same kind, described anew (an engine upgrade: its title, classes or
            // baselines): what shows it is rebuilt with the new description.
            emit kind_changed();
            emit kind_predictions_changed();
            read_leaderboard();
        }
    });
    connect(service_, &KrxQuantService::kinds_failed, this, [this](const QString&) {
        // An engine before krx-quant #179, or one not answering: the direction alone.
        const bool windows = !window_kinds().isEmpty();
        kinds_.reset();
        emit kinds_changed();
        apply_kind();
        if (windows) { // the bar windows read without them now (krx-quant #255)
            for (auto& watch : bars_watched_)
                watch.settled = false;
            read_bars_due();
        }
    });
    connect(service_, &KrxQuantService::kind_latest_ready, this, [this](const krx_quant::KindLatest& latest) {
        if (latest.symbol != selected_ || latest.kind != kind_)
            return read_kind_latest(); // another was chosen meanwhile
        kind_board_.set_latest(latest);
        emit kind_predictions_changed();
    });
    connect(service_, &KrxQuantService::kind_latest_failed, this,
            [this](const QString& symbol, const QString& kind, const QString&) {
                if (symbol != selected_ || kind != kind_)
                    read_kind_latest(); // another was chosen meanwhile; a failure shows as no prediction
            });
    connect(service_, &KrxQuantService::kind_history_ready, this,
            [this](const QString& symbol, const QString& kind, const QDateTime&,
                   const QList<krx_quant::KindPrediction>& predictions) {
                kind_paging_ = false;
                if (symbol == selected_ && kind == kind_) {
                    kind_board_.set_history(symbol, predictions);
                    const auto described = kind_info();
                    kind_from_ = described && krx_quant::is_daily(*described) // by days (krx-quant #264)
                                     ? krx_quant::daily_from_after(kind_from_, predictions)
                                     : krx_quant::kind_from_after(kind_from_, predictions, oldest_close());
                    emit kind_predictions_changed();
                }
                read_kind_history(); // what is still to read, or another symbol's or kind's
            });
    connect(service_, &KrxQuantService::kind_history_failed, this,
            [this](const QString& symbol, const QString& kind, const QString&) {
                kind_paging_ = false; // asked again on the next chart read
                if (symbol != selected_ || kind != kind_)
                    read_kind_history(true);
            });
    // The trader's buys and sells (krx-quant #398): the chosen symbol's, if the trader's.
    connect(service_, &KrxQuantService::trader_ready, this,
            [this](const QString& symbol, const QDateTime& before, const QList<krx_quant::TraderMark>& marks) {
                trader_paging_ = false;
                if (symbol == selected_) {
                    const bool back = before.isValid();
                    // More since the last read than a page holds: those held and the latest page do not
                    // meet, so all are read again from this page back to the first bar.
                    if (!back && krx_quant::trader_gap(trader_marks_, marks))
                        forget_trader();
                    // A page back was asked a millisecond past where they were read from (trader_before):
                    // unless reading again from scratch moved that since.
                    if (!back || (trader_from_.isValid() && before == trader_from_.addMSecs(1)))
                        trader_from_ = krx_quant::trader_from_after(trader_from_, marks, back);
                    if (const auto joined = krx_quant::join_trader_marks(trader_marks_, marks);
                        joined != trader_marks_) {
                        trader_marks_ = joined;
                        emit marks_changed();
                    }
                }
                read_trader(); // back to the first bar held, or another symbol's
            });
    connect(service_, &KrxQuantService::trader_failed, this, [this](const QString& symbol, const QString&) {
        trader_paging_ = false; // asked again on the next chart read; the marks shown stay
        if (symbol != selected_)
            read_trader(true); // another was chosen meanwhile
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
        QMap<QString, QString> standings;
        for (const auto* entries : {&leaderboard.ranked, &leaderboard.unranked})
            for (const auto& e : *entries)
                if (!e.status.isEmpty())
                    standings.insert(e.algorithm, e.status);
        if (leaderboard.kind != QLatin1String(krx_quant::kDirectionKind)) {
            // Another kind's (krx-quant #182): its hover's statuses; the direction's stay.
            if (leaderboard.kind == kind_ && standings != kind_standings_) {
                kind_standings_ = standings;
                emit kind_predictions_changed();
            }
        } else if (standings != standings_) {
            standings_ = standings;
            emit marks_changed(); // the hover's statuses
        }
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
    // The bar windows (krx-quant #255): each read until nothing more is to come for it.
    connect(service_, &KrxQuantService::bar_ready, this, [this](const krx_quant::BarRead& read) {
        const auto watch = bars_watched_.find(krx_quant::bar_key(read.symbol, read.bar_close));
        if (watch == bars_watched_.end())
            return; // its window closed meanwhile
        watch->settled = krx_quant::bar_settled(read, engine_now());
        emit bar_changed(read);
    });
    connect(service_, &KrxQuantService::bar_failed, this,
            [this](const QString& symbol, const QDateTime& bar_close, const QString& reason) {
                if (bars_watched_.contains(krx_quant::bar_key(symbol, bar_close)))
                    emit bar_failed(symbol, bar_close, reason);
            });
    connect(service_, &KrxQuantService::base_url_changed, this, [this](const QString& url) {
        marks_paging_ = false; // its page was dropped with the old engine's reads
        trader_paging_ = false;
        algorithms_.clear(); // the old engine's; the new one's come with the details
        algorithms_failure_.clear();
        emit algorithms_changed();
        // The old engine's kinds go: the direction until the new one says what it makes (krx-quant #182).
        kind_paging_ = false;
        kinds_.reset();
        emit kinds_changed();
        apply_kind();
        service_->read_kinds();
        stream_->open(url); // also when another KRX panel applied an address
        // The bar windows (krx-quant #255): read from the new engine.
        for (auto& watch : bars_watched_)
            watch.settled = false;
        read_bars_due();
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
    bars_soon_.setSingleShot(true);
    bars_soon_.setInterval(kBarDelayMs);
    connect(&bars_soon_, &QTimer::timeout, this, [this]() {
        for (const QString& key : std::exchange(bars_due_, {}))
            read_bar(key);
    });
}

void KrxLive::start() {
    service_->refresh(); // reads the address from the settings; opens the stream on it
    service_->read_kinds();
    service_->read_macro();
    read_leaderboard();
    timer_.start();
}

void KrxLive::apply_base_url(const QString& url) {
    const QString before = KrxQuantService::base_url();
    KrxQuantService::set_base_url(url);
    board_ = {};
    board_.focus(selected_);
    kind_board_.forget();
    kind_from_ = {};
    kind_paging_ = false;
    marks_paging_ = false; // the restart below drops a page under way: none would answer
    forget_trader();       // the old engine's trader (krx-quant #398)
    trader_paging_ = false;
    bars_.clear();
    day_read_ = false;
    live_.reset();
    forget_older(); // the old engine's
    special_.clear();
    special_read_.reset();
    supply_.clear(); // and its daily supply data (krx-quant #226): the new one may give it
    supply_read_.reset();
    supply_served_ = true;
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
    board_.focus(symbol);
    kind_board_.focus(symbol);
    kind_from_ = {};
    live_.reset();
    pinned_ = {}; // another symbol: its latest
    pinned_predictions_.clear();
    emit pinned_changed();
    bars_.clear();
    day_read_ = false;
    forget_older();
    forget_trader();  // the trader's symbol alone has them (krx-quant #398)
    special_.clear(); // its own are read with its bars (krx-quant #236)
    special_read_.reset();
    supply_.clear(); // likewise its daily supply data (krx-quant #226)
    supply_read_.reset();
    emit selected_changed();
    emit chart_changed();
    emit predictions_changed();
    emit kind_predictions_changed();
    read_chart();
    read_kind_latest();
}

void KrxLive::set_leaderboard_query(const KrxQuantService::LeaderboardQuery& query) {
    query_ = query;
    query_.kind = kind_; // the kind shown's
    read_leaderboard();
}

void KrxLive::set_kind(const QString& saved) {
    saved_kind_ = saved;
    apply_kind();
}

void KrxLive::apply_kind() {
    const QString kind = krx_quant::shown_kind(saved_kind_, krx_quant::kind_choices(kinds_));
    if (kind == kind_)
        return;
    kind_ = kind;
    kind_board_.set_kind(kind == QLatin1String(krx_quant::kDirectionKind) ? QString() : kind);
    kind_board_.focus(selected_);
    kind_standings_.clear();
    kind_from_ = {};
    query_.kind = kind;
    emit kind_changed();
    emit kind_predictions_changed();
    read_leaderboard();
    read_kind_latest();
    read_kind_history(true);
}

void KrxLive::read_kind_latest() {
    if (kind_ != QLatin1String(krx_quant::kDirectionKind) && !selected_.isEmpty())
        service_->read_kind_latest(selected_, kind_);
}

void KrxLive::read_kind_history(bool latest) {
    if (kind_ == QLatin1String(krx_quant::kDirectionKind) || selected_.isEmpty())
        return;
    kind_latest_page_ |= latest;
    if (kind_paging_)
        return; // asked again once it answers
    const auto described = kind_info();
    const bool daily = described && krx_quant::is_daily(*described);
    std::optional<krx_quant::KindRead> read;
    if (daily) {
        // Once a day (krx-quant #264): by days, back to the first day the chart holds in any view.
        read = krx_quant::daily_read(kind_from_, first_day_held(), kind_latest_page_);
    } else {
        read = krx_quant::kind_read(kind_from_, oldest_close());
        if (kind_latest_page_ && oldest_close().isValid())
            read = krx_quant::KindRead{}; // the latest bar closes again: their answers since
    }
    if (!read)
        return;
    if (!read->before.isValid())
        kind_latest_page_ = false;
    kind_paging_ = true;
    service_->read_kind_history(selected_, kind_, read->before, read->limit);
}

void KrxLive::set_details(const QStringList& algorithms) {
    const QStringList before = std::exchange(details_, algorithms);
    bool opened = false;
    for (const auto& algorithm : details_)
        if (!before.contains(algorithm)) {
            service_->read_stats(algorithm, query_);
            opened = true;
        }
    if (opened)
        service_->read_algorithms();
}

void KrxLive::set_bars(const QList<QPair<QString, QDateTime>>& bars) {
    QMap<QString, BarWatch> watched;
    for (const auto& [symbol, bar_close] : bars) {
        const QString key = krx_quant::bar_key(symbol, bar_close);
        watched.insert(key, bars_watched_.value(key, BarWatch{symbol, bar_close, false}));
    }
    const QMap<QString, BarWatch> before = std::exchange(bars_watched_, watched);
    for (auto it = bars_watched_.cbegin(); it != bars_watched_.cend(); ++it)
        if (!before.contains(it.key()))
            read_bar(it.key()); // a window opened: its bar at once
    for (auto it = bars_due_.begin(); it != bars_due_.end();)
        it = bars_watched_.contains(*it) ? std::next(it) : bars_due_.erase(it);
}

QStringList KrxLive::window_kinds() const {
    QStringList out;
    for (const auto& kind : krx_quant::bar_kinds())
        if (krx_quant::kind_named(kinds_, kind))
            out << kind;
    return out;
}

void KrxLive::read_bar(const QString& key) {
    if (const auto watch = bars_watched_.constFind(key); watch != bars_watched_.cend())
        service_->read_bar(watch->symbol, watch->bar_close, window_kinds());
}

void KrxLive::read_bars_due() {
    for (auto it = bars_watched_.cbegin(); it != bars_watched_.cend(); ++it)
        if (!it->settled)
            read_bar(it.key());
}

void KrxLive::bars_touched(const krx_quant::Event& event) {
    if (bars_watched_.isEmpty())
        return;
    const auto touch = [this](const QString& symbol, const QDateTime& bar_close) {
        for (auto it = bars_watched_.cbegin(); it != bars_watched_.cend(); ++it)
            // Its own bar close's (a late model, an outcome, an answer), or a later one of its
            // symbol: another of its four minutes is over, its bar in.
            if (!it->settled && it->symbol == symbol && bar_close >= it->bar_close)
                bars_due_.insert(it.key());
    };
    for (const auto& p : event.predictions)
        touch(p.symbol, p.bar_close);
    for (const auto& p : event.kind_predictions)
        touch(p.symbol, p.bar_close);
    for (const auto& a : event.kind_answers)
        touch(a.symbol, a.bar_close);
    if (!bars_due_.isEmpty() && !bars_soon_.isActive())
        bars_soon_.start();
}

void KrxLive::set_selection(const QStringList& selection) {
    if (selection == selection_)
        return;
    selection_ = selection;
    emit marks_changed();
    ensure_marks();
}

bool KrxLive::all_predictors() const {
    return !selection_.isEmpty() && view_minutes_ == 1;
}

QDate KrxLive::first_day_held() const {
    const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    QDate first;
    const auto earlier = [&first, &kst](const QList<krx_quant::Bar>& bars) {
        if (bars.isEmpty())
            return;
        const QDate day = bars.first().start.toTimeZone(kst).date();
        if (!first.isValid() || day < first)
            first = day;
    };
    earlier(older_);
    earlier(bars_);
    earlier(candles_.value(krx_quant::kDayView).read);
    return first;
}

QDateTime KrxLive::oldest_close() const {
    const QDateTime first = !older_.isEmpty()  ? older_.first().start
                            : !bars_.isEmpty() ? bars_.first().start
                                               : QDateTime();
    return first.isValid() ? first.addSecs(60) : QDateTime();
}

void KrxLive::ensure_marks(bool ahead) {
    if (marks_paging_ || view_minutes_ != 1 || selected_.isEmpty())
        return; // one page at a time; the wider views draw no icons
    const bool all = all_predictors();
    if (const auto before = krx_quant::marks_before(all ? full_from_ : finals_from_, oldest_close(), ahead))
        marks_paging_ = service_->read_predictions(selected_, *before, krx_quant::kPredictionPage, all);
}

void KrxLive::marks_read(const QList<krx_quant::Prediction>& page, bool all) {
    if (!all) {
        finals_from_ = krx_quant::finals_from_after(finals_from_, page);
        return;
    }
    full_from_ = krx_quant::full_from_after(full_from_, page);
    // Every predictor's holds the final consensus's and "늘 보합"'s too.
    if (!finals_from_.isValid() || full_from_ < finals_from_)
        finals_from_ = full_from_;
}

void KrxLive::read_chart() {
    if (!selected_.isEmpty())
        service_->read_chart(selected_, all_predictors());
    read_kind_history(true); // the kind's latest bar closes with the chart's (krx-quant #182)
    read_trader(true);       // the trader's latest buys and sells with them (krx-quant #398)
}

void KrxLive::read_trader(bool latest) {
    if (selected_ != QLatin1String(krx_quant::kTraderSymbol))
        return;
    trader_latest_ |= latest;
    if (trader_paging_)
        return; // asked again once it answers
    const QDateTime first = !older_.isEmpty()  ? older_.first().start
                            : !bars_.isEmpty() ? bars_.first().start
                                               : QDateTime();
    // The latest page again: the buys and sells since, and the sells of trades open then.
    const auto before = krx_quant::trader_before(trader_latest_ ? QDateTime() : trader_from_, first);
    if (!before)
        return; // no bar yet, or read back to the first: asked again when bars come
    if (!before->isValid())
        trader_latest_ = false;
    trader_paging_ = true;
    service_->read_trader_marks(selected_, *before, krx_quant::kTraderPage);
}

void KrxLive::forget_trader() {
    trader_marks_.clear();
    trader_from_ = {};
}

void KrxLive::view_from(double from, double to, int minutes) {
    view_from_ = from;
    view_width_ = to - from;
    view_minutes_ = minutes;
    read_older();
}

void KrxLive::read_older() {
    const int minutes = view_minutes_;
    if (krx_quant::candle_view(minutes)) {
        const Candles kept = candles_.value(minutes);
        if (selected_.isEmpty() || kept.oldest)
            return;
        // The first candle shown: the earliest the engine stored, else (a longer view with none
        // stored yet) the first made of the days; none: the latest are read.
        const QList<krx_quant::Bar> shown = kept.read.isEmpty() ? view_candles(minutes) : kept.read;
        const QDateTime first = shown.isEmpty() ? QDateTime() : shown.first().start;
        if (const auto before = krx_quant::older_candles(first, view_from_, view_width_))
            service_->read_candles(selected_, krx_quant::candle_period(minutes), *before);
        return;
    }
    if (selected_.isEmpty() || oldest_)
        return;
    // The first bar shown: the earliest read before, else today's first.
    const QDateTime first = !older_.isEmpty()  ? older_.first().start
                            : !bars_.isEmpty() ? bars_.first().start
                                               : QDateTime();
    if (const auto read = krx_quant::older_read(first, view_from_, minutes, view_width_))
        read_bars(*read);
}

void KrxLive::read_bars(const krx_quant::OlderRead& read) {
    // Their icons' predictions at the same time, apart (krx-quant #239).
    if (service_->read_older(selected_, read))
        ensure_marks(true);
}

void KrxLive::set_view(int minutes) {
    if (minutes != view_minutes_) {
        day_target_ = {}; // another first screen
        zero_target_ = {};
    }
    view_minutes_ = minutes;
    ensure_marks(); // back to 1-minute bars: the icons' predictions over those read meanwhile
    const bool candles = krx_quant::candle_view(minutes);
    if (candles && !selected_.isEmpty()) {
        read_latest(krx_quant::kDayView); // a longer view's last periods are made of the days
        read_latest(minutes);             // skipped while the days are read: asked again on their answer
    }
    // Nothing today yet (before the open, a holiday): the last day's bars, also in a minute view
    // chosen after the chart was read in a candle view. Before the day is read, its answer does it.
    read_supply(false); // the days of this view's bars held (krx-quant #226)
    if (!candles && day_read_ && bars_.isEmpty() && older_.isEmpty()) {
        view_from_ = 0;
        return read_older();
    }
    read_history();
}

void KrxLive::set_history(int bars, const QDateTime& back_to, bool whole_days, bool cumulative) {
    history_ = bars;
    history_back_to_ = back_to;
    if (whole_days && !whole_days_)
        day_target_ = {}; // worked out from the screen now
    whole_days_ = whole_days;
    if (cumulative && !cumulative_)
        zero_target_ = {}; // worked out from the bars held now
    cumulative_ = cumulative;
    read_history();
}

bool KrxLive::reached_start(int view) const {
    return krx_quant::candle_view(view) ? candles_.value(view).oldest : oldest_;
}

void KrxLive::read_history() {
    if (selected_.isEmpty() || (history_ <= 0 && !history_back_to_.isValid() && !whole_days_ && !cumulative_))
        return;
    // Back to the earliest of the moments wanted. The AD line's and the OBV's 0 (krx-quant #223) is
    // worked out once from the bars first held (their last bar's date or year), not from the first
    // bar held, so the reads do not keep moving back.
    const auto back_to_with = [this](QDateTime back_to, const QList<krx_quant::Bar>& held) {
        if (cumulative_ && !zero_target_.isValid())
            zero_target_ = krx_quant::cumulative_start(held, view_minutes_);
        if (cumulative_ && zero_target_.isValid() && (!back_to.isValid() || zero_target_ < back_to))
            back_to = zero_target_;
        return back_to;
    };
    if (krx_quant::candle_view(view_minutes_)) {
        // The candles the engine stored, a page before the first (once the latest are read).
        const Candles kept = candles_.value(view_minutes_);
        if (kept.oldest || kept.read.isEmpty())
            return;
        const QList<krx_quant::Bar> shown = view_candles(view_minutes_);
        if (cumulative_ && !zero_target_.isValid() && shown.isEmpty())
            return; // the 0's year is the last candle's, made of the days: asked again once they come
        const int have = static_cast<int>(std::max(shown.size(), kept.read.size()));
        const QDateTime back_to = back_to_with(history_back_to_, shown);
        if (const auto before = krx_quant::history_candles(kept.read.first().start, have, history_, back_to))
            service_->read_candles(selected_, krx_quant::candle_period(view_minutes_), *before);
        return;
    }
    if (oldest_)
        return;
    const QDateTime first = !older_.isEmpty()  ? older_.first().start
                            : !bars_.isEmpty() ? bars_.first().start
                                               : QDateTime();
    const QList<krx_quant::Bar> held = bars();
    // The VWAP's days on the first screen whole: back past its earliest date's start, worked out
    // once from that screen (not from the first bar held, or the reads would keep moving back).
    if (whole_days_ && !day_target_.isValid())
        day_target_ = krx_quant::first_screen_day(held, view_minutes_);
    QDateTime back_to = back_to_with(history_back_to_, held);
    if (whole_days_ && day_target_.isValid() && (!back_to.isValid() || day_target_ < back_to))
        back_to = day_target_;
    const int have = static_cast<int>(krx_quant::group_bars(held, view_minutes_).size());
    if (const auto read = krx_quant::history_read(first, have, history_, view_minutes_, back_to))
        read_bars(*read);
}

std::optional<std::pair<QDate, QDate>> KrxLive::special_days() const {
    const QDateTime first = !older_.isEmpty()  ? older_.first().start
                            : !bars_.isEmpty() ? bars_.first().start
                                               : QDateTime();
    if (selected_.isEmpty() || !first.isValid())
        return std::nullopt;
    const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    const QDate from = first.toTimeZone(kst).date();
    const QDate last = (!bars_.isEmpty() ? bars_.last().start : older_.last().start).toTimeZone(kst).date();
    const QDate to = std::max(last, engine_day());
    // The engine answers at most kSpecialDays at a time: bars held further back have no bands.
    return std::pair(std::max(from, to.addDays(1 - kSpecialDays)), to);
}

void KrxLive::read_special(bool always) {
    const auto days = special_days();
    if (!days || (!always && days == special_read_))
        return;
    service_->read_special(selected_, days->first, days->second);
}

std::optional<std::pair<QDate, QDate>> KrxLive::supply_days() const {
    if (selected_.isEmpty())
        return std::nullopt;
    QDateTime first;
    QDateTime last;
    if (krx_quant::candle_view(view_minutes_)) {
        const QList<krx_quant::Bar> candles = view_candles(view_minutes_);
        if (!candles.isEmpty()) {
            first = candles.first().start;
            last = candles.last().start;
        }
    } else if (!older_.isEmpty() || !bars_.isEmpty()) {
        first = !older_.isEmpty() ? older_.first().start : bars_.first().start;
        last = !bars_.isEmpty() ? bars_.last().start : older_.last().start;
    }
    if (!first.isValid())
        return std::nullopt;
    const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    QDate from = first.toTimeZone(kst).date();
    if (view_minutes_ > krx_quant::kDayView) // the days before the first period, for its sums
        from = from.addDays(-kSupplyLead);
    const QDate to = std::max(last.toTimeZone(kst).date(), engine_day());
    // The engine answers at most kSupplyDays at a time: bars held further back have no values.
    return std::pair(std::max(from, to.addDays(1 - kSupplyDays)), to);
}

void KrxLive::read_supply(bool always) {
    const auto days = supply_days();
    if (!days || (!always && days == supply_read_))
        return;
    service_->read_supply(selected_, days->first, days->second);
}

void KrxLive::read_latest(int view) {
    // At first (unless the engine has none), and again on each new engine day, when the day
    // before's candles are the engine's (stored after its 20:30), no longer made of its bars.
    const Candles kept = candles_.value(view);
    if ((kept.read.isEmpty() && !kept.oldest) || kept.day != engine_day())
        service_->read_candles(selected_, krx_quant::candle_period(view), QDateTime());
}

QList<krx_quant::Bar> KrxLive::view_candles(int view) const {
    // Nothing until the engine's latest candles of the view (and of the days a longer view
    // is made of) have answered: the chart fits the first bars it draws, and fitted on
    // today's candle alone it would keep that one candle's width for the years read after.
    const auto answered = [this](int v) {
        const Candles kept = candles_.value(v);
        return kept.day.isValid() || kept.oldest;
    };
    if (!answered(krx_quant::kDayView) || !answered(view))
        return {};
    const QList<krx_quant::Bar> days =
        krx_quant::day_candles(candles_.value(krx_quant::kDayView).read, bars_, candle_venue_);
    return view == krx_quant::kDayView ? days : krx_quant::period_candles(candles_.value(view).read, days, view);
}

QDate KrxLive::engine_day() const {
    return engine_now().toTimeZone(QTimeZone::fromSecondsAheadOfUtc(9 * 3600)).date();
}

void KrxLive::forget_older() {
    older_.clear();
    oldest_ = false;
    day_target_ = {}; // another symbol's first screen
    zero_target_ = {};
    full_from_ = {};
    finals_from_ = {};
    candles_.clear();
    candle_venue_.clear();
    view_from_ = krx_quant::kOlderMargin;
    view_width_ = 0;
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
    if (!details_.isEmpty())
        service_->read_algorithms(); // what each open window says about its algorithm (krx-quant #163)
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
    // The bar windows (krx-quant #255) stay open beside other screens too: without the stream they
    // are read here until settled; with it, each minute as well (the engine clock alone settles one
    // the engine never scores).
    if (!live || ticks_ % kChartEvery == 0)
        read_bars_due();
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
    // With the stream the bar being made comes as it moves and the finished bars on each new
    // bar close (krx-quant #110); without it they are read here.
    if (!live && ticks_ % kChartEvery == 0)
        read_chart();
    // The special situations over the days held (krx-quant #236): a halt or a VI shows within a minute.
    if (ticks_ % kChartEvery == 0)
        read_special();
    // The daily supply data (krx-quant #226) changes twice a day (the engine's 20:30 and morning reads).
    if (ticks_ % kMacroEvery == 0) {
        service_->read_macro();
        read_supply();
    }
}

void KrxLive::read_everything() {
    service_->refresh();
    service_->read_kinds();
    read_chart();
    read_leaderboard();
    service_->read_macro();
    read_bars_due(); // what the stream did not bring them meanwhile (krx-quant #255)
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
    if (snapshot.status.now.isValid() && board_.rewind_to(snapshot.status.now)) {
        bars_.clear();
        day_read_ = false;
        live_.reset();
        full_from_ = {};
        finals_from_ = {};
        forget_trader(); // read again with the chart
    }
    if (snapshot.status.now.isValid() && kind_board_.rewind_to(snapshot.status.now))
        kind_from_ = {};
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
    emit marks_changed();
    read_kind_latest(); // the kind's with the direction's (krx-quant #182)
    on_status(snapshot.status, true);
}

void KrxLive::on_status(const krx_quant::EngineStatus& status, bool with_snapshot) {
    const bool recovering = !failure_.isEmpty();
    failure_.clear();
    status_ = status;
    set_engine_clock(status.now);
    if (status.now.isValid() && board_.rewind_to(status.now)) {
        bars_.clear();
        day_read_ = false;
        live_.reset();
        full_from_ = {};
        finals_from_ = {};
        forget_trader();
        emit predictions_changed();
        emit chart_changed();
        read_chart();
    }
    if (status.now.isValid() && kind_board_.rewind_to(status.now)) {
        kind_from_ = {};
        emit kind_predictions_changed();
    }
    if (recovering) {
        // Back after a failure: what was not read meanwhile is read again.
        if (!with_snapshot)
            service_->refresh();
        service_->read_kinds();
        service_->read_macro();
        read_leaderboard();
        read_chart();
    }
    emit status_changed();
}

void KrxLive::on_failure(const QString& reason) {
    status_.reset();
    failure_ = reason;
    read_errors_.clear();
    board_.forget_predictions(); // old predictions must not pass for current ones
    kind_board_.forget();
    kind_from_ = {};
    bars_.clear();
    day_read_ = false;
    live_.reset();
    forget_older();
    forget_trader();  // not shown as the engine's while it cannot be read (krx-quant #398)
    special_.clear(); // not shown as the engine's while it cannot be read (krx-quant #236)
    special_read_.reset();
    supply_.clear(); // likewise its daily supply data (krx-quant #226)
    supply_read_.reset();
    emit predictions_changed();
    emit kind_predictions_changed();
    emit chart_changed();
    emit status_changed();
}

void KrxLive::on_chart(const QString& symbol, const QList<krx_quant::Bar>& bars,
                       const QList<krx_quant::Prediction>& predictions, bool all) {
    if (symbol != selected_)
        return read_chart(); // another was chosen meanwhile
    bars_ = bars;
    day_read_ = true;
    // The finished bars, then the one being made: only today's (the engine's day), as a
    // bar kept from before a rewind (a demo started again) or from yesterday is not.
    const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    if (live_ && live_->start.toTimeZone(kst).date() == engine_now().toTimeZone(kst).date())
        krx_quant::merge_live(bars_, *live_);
    else
        live_.reset();
    board_.set_history(symbol, predictions);
    // The latest bar closes: every predictor's from the earliest read on, or, read with the
    // final consensus alone, no longer all of them up to now (krx-quant #159); the final
    // consensus's and "늘 보합"'s either way (krx-quant #239).
    if (!all)
        full_from_ = {};
    marks_read(predictions, all);
    emit chart_changed();
    emit marks_changed();
    ensure_marks();
    read_kind_history(); // the kind's over the bars now shown (krx-quant #182)
    read_trader();       // the trader's buys and sells over them (krx-quant #398)
    // The view's candles, unless read (a symbol or an engine changed); in a minute view the bars
    // before that the indicators want (krx-quant #222), or with nothing today yet the last day's.
    set_view(view_minutes_); // also the daily supply data of the days held, unless read (krx-quant #226)
    read_special(false);     // the special situations of the days held, unless read (krx-quant #236)
}

void KrxLive::on_event(const krx_quant::Event& event) {
    bars_touched(event); // any kind's, any symbol's (krx-quant #255)
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
        case krx_quant::Event::Type::Bar:
            // The chosen symbol's bar being made (krx-quant #110); another's is not drawn.
            if (event.symbol == selected_ && !selected_.isEmpty() && krx_quant::merge_live(bars_, event.bar)) {
                live_ = event.bar; // only one drawn: a dropped (older) one never comes back on a read
                emit live_changed();
            }
            return;
        case krx_quant::Event::Type::KindPrediction:
        case krx_quant::Event::Type::KindAnswer: {
            // Another kind's (krx-quant #182): the kind shown's alone; the direction's are not touched.
            if (!kind_board_.apply(event))
                return;
            set_engine_clock(event.at);
            if (event.type == krx_quant::Event::Type::KindAnswer)
                leaderboard_soon_.start(); // its leaderboard, once its answers are in
            const bool chosen =
                std::any_of(event.kind_predictions.cbegin(), event.kind_predictions.cend(),
                            [this](const krx_quant::KindPrediction& p) { return p.symbol == selected_; }) ||
                std::any_of(event.kind_answers.cbegin(), event.kind_answers.cend(),
                            [this](const krx_quant::KindAnswerAt& a) { return a.symbol == selected_; });
            if (chosen)
                emit kind_predictions_changed();
            return;
        }
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
    const bool chosen = std::any_of(event.predictions.cbegin(), event.predictions.cend(),
                                    [this](const krx_quant::Prediction& p) { return p.symbol == selected_; });
    if (chosen)
        emit marks_changed(); // only the chosen symbol's draw again (krx-quant #159)
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
        read_chart(); // a new bar close: a new bar
}

} // namespace fincept::services
