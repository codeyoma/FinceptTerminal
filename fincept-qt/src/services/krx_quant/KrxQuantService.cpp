#include "services/krx_quant/KrxQuantService.h"

#include "core/config/AppConfig.h"
#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"

#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>

#include <memory>
#include <optional>

namespace fincept::services {

namespace {

constexpr auto kBaseUrlKey = "krx_quant/base_url";
/// Bar closes of the final consensus the chart asks for: a whole day (09:01–15:16) and more.
constexpr int kFinalsLimit = 400;
/// Bar closes of every predictor's the chart asks for while models are selected (krx-quant
/// #159): the last hour, about ten predictors each. Those before are read again a page at a
/// time (KrxLive), and the stream brings the rest as it comes.
constexpr int kAllLimit = 60;

QString symbol_path(const QString& symbol, const QString& rest) {
    return "/v1/symbols/" + QString::fromUtf8(QUrl::toPercentEncoding(symbol)) + rest;
}

/// An answer read with ``parse``; a request that failed, why in Korean.
template <typename T>
Result<T> parsed(const Result<QJsonDocument>& answer, Result<T> (*parse)(const QJsonDocument&)) {
    if (answer.is_err())
        return Result<T>::err(krx_failure(answer.error()).toStdString());
    return parse(answer.value());
}

QString why(const std::string& error) {
    return QString::fromStdString(error);
}

/// The leaderboard's query string, also an algorithm's stats'.
QString query_string(const KrxQuantService::LeaderboardQuery& query) {
    QUrlQuery q;
    q.addQueryItem("symbol", query.symbol);
    q.addQueryItem("period", query.period);
    q.addQueryItem("special", query.special_value());
    return q.toString(QUrl::FullyEncoded);
}

/// The leaderboard's query string: an algorithm's stats', and the kind ranked unless the
/// direction (krx-quant #182: asked as before, so an older engine answers it alike).
QString leaderboard_string(const KrxQuantService::LeaderboardQuery& query) {
    QString string = query_string(query);
    if (query.kind != QLatin1String("direction"))
        string += "&kind=" + QString::fromUtf8(QUrl::toPercentEncoding(query.kind));
    return string;
}

/// A path under a symbol's kind (krx-quant #179): /v1/symbols/{symbol}/kinds/{kind}…
QString kind_path(const QString& symbol, const QString& kind, const QString& rest) {
    return symbol_path(symbol, "/kinds/" + QString::fromUtf8(QUrl::toPercentEncoding(kind)) + rest);
}

} // namespace

QString krx_failure(const std::string& error) {
    return krx_quant::failure_text(HttpClient::status_from_error(error), HttpClient::message_from_error(error));
}

KrxQuantService::KrxQuantService(QObject* parent) : QObject(parent) {
    qRegisterMetaType<Snapshot>();
}

QString KrxQuantService::base_url() {
    return AppConfig::instance().get(kBaseUrlKey).toString();
}

void KrxQuantService::set_base_url(const QString& url) {
    QString trimmed = url.trimmed();
    while (trimmed.endsWith('/'))
        trimmed.chop(1);
    AppConfig::instance().set(kBaseUrlKey, trimmed);
}

void KrxQuantService::refresh() {
    if (base_url() != url_)
        return restart();
    if (!begin(kRefresh))
        return;
    get("/v1/status", [this](Result<QJsonDocument> answer) {
        auto status = parsed(answer, krx_quant::parse_status);
        if (status.is_err())
            return fail_refresh(why(status.error()));
        Snapshot snapshot;
        snapshot.status = status.value();
        read_symbols(snapshot);
    });
}

void KrxQuantService::restart() {
    rebind();
    refresh();
}

void KrxQuantService::rebind() {
    ++generation_; // every answer still under way is dropped
    for (bool& busy : busy_)
        busy = false;
    next_leaderboard_.reset();
    next_cost_map_.reset();
    next_close_.reset();
    stats_reads_.clear();
    bar_reads_.clear();
    if (const QString url = base_url(); url != url_) {
        url_ = url;
        emit base_url_changed(url_);
    }
}

bool KrxQuantService::begin(Kind kind) {
    if (url_.isEmpty()) {
        if (kind == kRefresh || kind == kStatus)
            emit refresh_failed(QStringLiteral("엔진 주소를 입력하세요."));
        return false;
    }
    if (busy_[kind])
        return false;
    busy_[kind] = true;
    return true;
}

void KrxQuantService::get(const QString& path, Step step) {
    const int generation = generation_;
    // `this` as the context: the answer is dropped if the screen (and this service) is gone.
    HttpClient::instance().get(
        url_ + path,
        [this, generation, step = std::move(step)](Result<QJsonDocument> answer) {
            if (generation == generation_) // a restart since drops it
                step(std::move(answer));
        },
        this);
}

void KrxQuantService::read_symbols(Snapshot snapshot) {
    get("/v1/symbols", [this, snapshot](Result<QJsonDocument> answer) mutable {
        auto symbols = parsed(answer, krx_quant::parse_symbols);
        if (symbols.is_err())
            return fail_refresh(why(symbols.error()));
        snapshot.symbols = symbols.value();
        read_members(snapshot);
    });
}

void KrxQuantService::read_members(Snapshot snapshot) {
    get("/v1/consensus", [this, snapshot](Result<QJsonDocument> answer) mutable {
        // Without the members the predictions still show; the signals wait for them.
        if (auto members = parsed(answer, krx_quant::parse_members); members.is_ok())
            snapshot.members = members.value();
        else
            snapshot.errors << QStringLiteral("합의 구성원: %1").arg(why(members.error()));
        read_latest(snapshot);
    });
}

void KrxQuantService::read_latest(Snapshot snapshot) {
    if (snapshot.symbols.isEmpty())
        return finish_with(snapshot);
    // Every symbol is read at once; the snapshot goes out when the last answer is in.
    auto shared = std::make_shared<Snapshot>(std::move(snapshot));
    auto waiting = std::make_shared<qsizetype>(shared->symbols.size());
    for (const auto& quote : shared->symbols) {
        const QString symbol = quote.symbol;
        get(symbol_path(symbol, "/latest"), [this, shared, waiting, symbol](Result<QJsonDocument> answer) {
            if (auto latest = parsed(answer, krx_quant::parse_latest); latest.is_ok())
                shared->latest.insert(symbol, latest.value());
            else
                shared->errors << QStringLiteral("%1: %2").arg(symbol, why(latest.error()));
            if (--*waiting == 0)
                finish_with(*shared);
        });
    }
}

void KrxQuantService::finish_with(const Snapshot& snapshot) {
    end(kRefresh);
    if (!snapshot.errors.isEmpty())
        LOG_WARN("KrxQuant", snapshot.errors.join("; "));
    emit snapshot_ready(snapshot);
}

void KrxQuantService::fail_refresh(const QString& reason) {
    end(kRefresh);
    LOG_WARN("KrxQuant", reason);
    emit refresh_failed(reason);
}

void KrxQuantService::read_status() {
    if (base_url() != url_)
        return restart();
    if (!begin(kStatus))
        return;
    get("/v1/status", [this](Result<QJsonDocument> answer) {
        end(kStatus);
        auto status = parsed(answer, krx_quant::parse_status);
        if (status.is_err()) {
            LOG_WARN("KrxQuant", why(status.error()));
            return emit refresh_failed(why(status.error()));
        }
        emit status_ready(status.value());
    });
}

void KrxQuantService::read_chart_predictions(const QString& symbol, const QString& query, bool all,
                                             std::function<void(Result<QList<krx_quant::Prediction>>)> done) {
    using Predictions = QList<krx_quant::Prediction>;
    if (all) {
        get(symbol_path(symbol, "/predictions?" + query),
            [done](Result<QJsonDocument> answer) { done(parsed(answer, krx_quant::parse_predictions)); });
        return;
    }
    // The final consensus's and "늘 보합"'s at once (krx-quant #239), together once both answered.
    struct Pair {
        std::optional<Result<Predictions>> finals, flats;
    };
    auto pair = std::make_shared<Pair>();
    auto join = [pair, done]() {
        if (!pair->finals || !pair->flats)
            return;
        if (pair->finals->is_err())
            return done(*pair->finals);
        if (pair->flats->is_err())
            return done(*pair->flats);
        done(Result<Predictions>::ok(pair->finals->value() + pair->flats->value()));
    };
    get(symbol_path(symbol, "/predictions?algorithm=consensus-final&" + query),
        [pair, join](Result<QJsonDocument> answer) {
            pair->finals = parsed(answer, krx_quant::parse_predictions);
            join();
        });
    get(symbol_path(symbol, "/predictions?algorithm=baseline-flat&" + query),
        [pair, join](Result<QJsonDocument> answer) {
            pair->flats = parsed(answer, krx_quant::parse_predictions);
            join();
        });
}

void KrxQuantService::read_chart(const QString& symbol, bool all) {
    if (!begin(kChart))
        return;
    // The bars and their predictions at once (krx-quant #239); the chart once both answered.
    struct Chart {
        std::optional<Result<QList<krx_quant::Bar>>> bars;
        std::optional<Result<QList<krx_quant::Prediction>>> made;
    };
    auto chart = std::make_shared<Chart>();
    auto join = [this, symbol, all, chart]() {
        if (!chart->bars || !chart->made)
            return;
        end(kChart);
        if (chart->bars->is_err())
            return emit chart_failed(symbol, why(chart->bars->error()));
        if (chart->made->is_err())
            return emit chart_failed(symbol, why(chart->made->error()));
        emit chart_ready(symbol, chart->bars->value(), chart->made->value(), all);
    };
    get(symbol_path(symbol, "/bars"), [chart, join](Result<QJsonDocument> answer) {
        chart->bars = parsed(answer, krx_quant::parse_bars);
        join();
    });
    read_chart_predictions(symbol, QString("limit=%1").arg(all ? kAllLimit : kFinalsLimit), all,
                           [chart, join](Result<QList<krx_quant::Prediction>> made) {
                               chart->made = std::move(made);
                               join();
                           });
}

bool KrxQuantService::read_older(const QString& symbol, const krx_quant::OlderRead& read) {
    if (!begin(kOlder))
        return false; // the view moves on: asked again once this one is drawn
    QString path = QString("/bars?limit=%1").arg(read.limit);
    if (read.before.isValid())
        path += "&before=" + QString::fromUtf8(QUrl::toPercentEncoding(read.before.toString(Qt::ISODate)));
    get(symbol_path(symbol, path), [this, symbol](Result<QJsonDocument> answer) {
        end(kOlder);
        auto bars = parsed(answer, krx_quant::parse_bars);
        if (bars.is_err())
            return emit older_failed(symbol, why(bars.error()));
        emit older_ready(symbol, bars.value());
    });
    return true;
}

bool KrxQuantService::read_predictions(const QString& symbol, const QDateTime& before, int limit, bool all) {
    if (!begin(kPredictions))
        return false;
    QString query = QString("limit=%1").arg(limit);
    if (before.isValid())
        query += "&before=" + QString::fromUtf8(QUrl::toPercentEncoding(before.toString(Qt::ISODate)));
    read_chart_predictions(symbol, query, all, [this, symbol, before, all](Result<QList<krx_quant::Prediction>> made) {
        end(kPredictions);
        if (made.is_err())
            return emit predictions_failed(symbol, why(made.error()));
        emit predictions_ready(symbol, before, made.value(), all);
    });
    return true;
}

void KrxQuantService::read_algorithms() {
    if (!begin(kAlgorithms))
        return;
    get("/v1/algorithms", [this](Result<QJsonDocument> answer) {
        end(kAlgorithms);
        auto algorithms = parsed(answer, krx_quant::parse_algorithms);
        if (algorithms.is_err())
            return emit algorithms_failed(why(algorithms.error()));
        emit algorithms_ready(algorithms.value());
    });
}

void KrxQuantService::read_candles(const QString& symbol, const QString& period, const QDateTime& before) {
    if (!begin(kCandles))
        return; // the view moves on: asked again once this one is drawn
    QString path = QString("/candles?period=%1&limit=%2").arg(period).arg(krx_quant::kCandleRead);
    if (before.isValid())
        path += "&before=" + QString::fromUtf8(QUrl::toPercentEncoding(before.toString(Qt::ISODate)));
    get(symbol_path(symbol, path), [this, symbol, period, before](Result<QJsonDocument> answer) {
        end(kCandles);
        auto candles = parsed(answer, krx_quant::parse_candles);
        if (candles.is_err())
            return emit candles_failed(symbol, period, why(candles.error()));
        emit candles_ready(symbol, period, before, candles.value());
    });
}

void KrxQuantService::read_close(const QString& symbol, const QDateTime& bar_close) {
    if (busy_[kClose] && !url_.isEmpty()) {
        // Another icon clicked while one is read: the last clicked is read next.
        next_close_ = std::pair(symbol, bar_close);
        return;
    }
    if (!begin(kClose))
        return;
    const QString at = QString::fromUtf8(QUrl::toPercentEncoding(bar_close.toString(Qt::ISODate)));
    get(symbol_path(symbol, "/predictions?bar_close=" + at), [this, symbol, bar_close](Result<QJsonDocument> answer) {
        auto made = parsed(answer, krx_quant::parse_predictions);
        if (made.is_err()) {
            emit close_failed(symbol, bar_close, why(made.error()));
            return close_done();
        }
        get("/v1/algorithms", [this, symbol, bar_close, made = made.value()](Result<QJsonDocument> answer) {
            // Without the statuses the judgements still show, their statuses blank.
            auto statuses = parsed(answer, krx_quant::parse_statuses);
            emit close_ready(symbol, bar_close, made, statuses.is_ok() ? statuses.value() : QMap<QString, QString>{});
            close_done();
        });
    });
}

void KrxQuantService::close_done() {
    end(kClose);
    if (next_close_) {
        const auto [symbol, bar_close] = *next_close_;
        next_close_.reset();
        read_close(symbol, bar_close);
    }
}

void KrxQuantService::read_leaderboard(const LeaderboardQuery& query) {
    if (busy_[kLeaderboard] && !url_.isEmpty()) {
        // Another is read once the one under way ends; the one under way answers itself.
        next_leaderboard_ = query == leaderboard_asked_ ? std::nullopt : std::optional(query);
        return;
    }
    if (!begin(kLeaderboard))
        return;
    leaderboard_asked_ = query;
    get("/v1/leaderboard?" + leaderboard_string(query), [this](Result<QJsonDocument> answer) {
        end(kLeaderboard);
        if (next_leaderboard_) { // asked again meanwhile: this answer, or its failure, is stale
            const LeaderboardQuery next = *next_leaderboard_;
            next_leaderboard_.reset();
            return read_leaderboard(next);
        }
        auto leaderboard = parsed(answer, krx_quant::parse_leaderboard);
        if (leaderboard.is_err())
            return emit leaderboard_failed(why(leaderboard.error()));
        // An engine that does not know the kind asked for answers the direction's (krx-quant #182).
        if (leaderboard.value().kind != leaderboard_asked_.kind)
            return emit leaderboard_failed(
                QStringLiteral("엔진이 이 예측 종류(%1)의 순위표를 주지 않았습니다").arg(leaderboard_asked_.kind));
        emit leaderboard_ready(leaderboard.value());
    });
}

void KrxQuantService::read_stats(const QString& algorithm, const LeaderboardQuery& query) {
    if (url_.isEmpty())
        return;
    if (auto under_way = stats_reads_.find(algorithm); under_way != stats_reads_.end()) {
        // Likewise, for this algorithm.
        under_way->next = query == under_way->asked ? std::nullopt : std::optional(query);
        return;
    }
    stats_reads_.insert(algorithm, {query, std::nullopt});
    const QString path =
        "/v1/algorithms/" + QString::fromUtf8(QUrl::toPercentEncoding(algorithm)) + "/stats?" + query_string(query);
    get(path, [this, algorithm](Result<QJsonDocument> answer) {
        const StatsRead read = stats_reads_.take(algorithm);
        if (read.next) // asked again meanwhile: this answer, or its failure, is stale
            return read_stats(algorithm, *read.next);
        auto stats = parsed(answer, krx_quant::parse_stats);
        if (stats.is_err())
            return emit stats_failed(algorithm, why(stats.error()));
        emit stats_ready(stats.value());
    });
}

void KrxQuantService::read_kinds() {
    if (!begin(kKindList))
        return;
    get("/v1/kinds", [this](Result<QJsonDocument> answer) {
        end(kKindList);
        auto kinds = parsed(answer, krx_quant::parse_kinds);
        if (kinds.is_err())
            return emit kinds_failed(why(kinds.error()));
        emit kinds_ready(kinds.value());
    });
}

void KrxQuantService::read_kind_latest(const QString& symbol, const QString& kind) {
    if (!begin(kKindLatest))
        return;
    get(kind_path(symbol, kind, "/latest"), [this, symbol, kind](Result<QJsonDocument> answer) {
        end(kKindLatest);
        auto latest = parsed(answer, krx_quant::parse_kind_latest);
        if (latest.is_err())
            return emit kind_latest_failed(symbol, kind, why(latest.error()));
        krx_quant::KindLatest read = latest.value();
        read.symbol = symbol;
        read.kind = kind;
        emit kind_latest_ready(read);
    });
}

void KrxQuantService::read_kind_history(const QString& symbol, const QString& kind, const QDateTime& before,
                                        int limit) {
    if (!begin(kKindHistory))
        return;
    QString path = QString("/predictions?limit=%1").arg(limit);
    if (before.isValid())
        path += "&before=" + QString::fromUtf8(QUrl::toPercentEncoding(before.toString(Qt::ISODate)));
    get(kind_path(symbol, kind, path), [this, symbol, kind, before](Result<QJsonDocument> answer) {
        end(kKindHistory);
        auto made = parsed(answer, krx_quant::parse_kind_predictions);
        if (made.is_err())
            return emit kind_history_failed(symbol, kind, why(made.error()));
        emit kind_history_ready(symbol, kind, before, made.value());
    });
}

void KrxQuantService::read_special(const QString& symbol, const QDate& from, const QDate& to) {
    if (!begin(kSpecial))
        return;
    const QString path =
        QStringLiteral("/special?from=%1&to=%2").arg(from.toString(Qt::ISODate), to.toString(Qt::ISODate));
    get(symbol_path(symbol, path), [this, symbol, from, to](Result<QJsonDocument> answer) {
        end(kSpecial);
        // An engine before krx-quant #236 has no such answer: no bands, not a failure to show.
        if (answer.is_err() && HttpClient::status_from_error(answer.error()) == 404)
            return emit special_ready(symbol, from, to, {});
        auto windows = parsed(answer, krx_quant::parse_special);
        if (windows.is_err())
            return emit special_failed(symbol, from, to, why(windows.error()));
        emit special_ready(symbol, from, to, windows.value());
    });
}

void KrxQuantService::read_supply(const QString& symbol, const QDate& from, const QDate& to) {
    if (!begin(kSupply))
        return;
    const QString path =
        QStringLiteral("/supply?from=%1&to=%2").arg(from.toString(Qt::ISODate), to.toString(Qt::ISODate));
    get(symbol_path(symbol, path), [this, symbol, from, to](Result<QJsonDocument> answer) {
        end(kSupply);
        // An engine before krx-quant #226 has no such answer: its supply indicators are greyed.
        if (answer.is_err() && HttpClient::status_from_error(answer.error()) == 404)
            return emit supply_ready(symbol, from, to, {}, false);
        auto days = parsed(answer, krx_quant::parse_supply);
        if (days.is_err())
            return emit supply_failed(symbol, from, to, why(days.error()));
        emit supply_ready(symbol, from, to, days.value(), true);
    });
}

void KrxQuantService::read_bar(const QString& symbol, const QDateTime& bar_close, const QStringList& kinds) {
    if (url_.isEmpty())
        return;
    const QString key = krx_quant::bar_key(symbol, bar_close);
    if (auto under_way = bar_reads_.find(key); under_way != bar_reads_.end()) {
        *under_way = {true, kinds}; // read again once it answers: what came meanwhile
        return;
    }
    bar_reads_.insert(key, {false, kinds});
    struct Parts {
        krx_quant::BarRead read;
        std::optional<QString> failure; ///< its predictions' or bars'
        qsizetype waiting = 0;
    };
    auto parts = std::make_shared<Parts>();
    parts->read.symbol = symbol;
    parts->read.bar_close = bar_close;
    parts->waiting = 4 + kinds.size();
    const auto done = [this, parts, key]() {
        if (--parts->waiting > 0)
            return;
        const BarReadAsked asked = bar_reads_.take(key);
        if (asked.again) // this answer is older than what came meanwhile
            return read_bar(parts->read.symbol, parts->read.bar_close, asked.kinds);
        if (parts->failure)
            return emit bar_failed(parts->read.symbol, parts->read.bar_close, *parts->failure);
        emit bar_ready(parts->read);
    };
    const QString at = QString::fromUtf8(QUrl::toPercentEncoding(bar_close.toString(Qt::ISODate)));
    get(symbol_path(symbol, "/predictions?bar_close=" + at), [parts, done](Result<QJsonDocument> answer) {
        if (auto made = parsed(answer, krx_quant::parse_predictions); made.is_ok())
            parts->read.predictions = made.value();
        else
            parts->failure = why(made.error());
        done();
    });
    get("/v1/algorithms", [parts, done](Result<QJsonDocument> answer) {
        // Without the statuses the predictions still show, their statuses blank.
        if (auto statuses = parsed(answer, krx_quant::parse_statuses); statuses.is_ok())
            parts->read.statuses = statuses.value();
        done();
    });
    // The bar, the four up to its scoring time and those before it: bars that start before that time.
    const QString before = QString::fromUtf8(QUrl::toPercentEncoding(bar_close.addSecs(240).toString(Qt::ISODate)));
    get(symbol_path(symbol, QStringLiteral("/bars?limit=%1&before=%2").arg(krx_quant::kBarReadBars).arg(before)),
        [parts, done](Result<QJsonDocument> answer) {
            if (auto bars = parsed(answer, krx_quant::parse_bars); bars.is_ok())
                parts->read.bars = bars.value();
            else if (!parts->failure)
                parts->failure = why(bars.error());
            done();
        });
    const QString day = bar_close.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(9 * 3600)).date().toString(Qt::ISODate);
    get(symbol_path(symbol, QStringLiteral("/special?from=%1&to=%1").arg(day)),
        [parts, done](Result<QJsonDocument> answer) {
            // An engine before krx-quant #236 has none, and a failure leaves none: the bar still shows.
            if (auto windows = parsed(answer, krx_quant::parse_special); windows.is_ok())
                parts->read.special = windows.value();
            done();
        });
    for (const QString& kind : kinds) {
        get(kind_path(symbol, kind, "/predictions?bar_close=" + at), [parts, done, kind](Result<QJsonDocument> answer) {
            if (auto made = parsed(answer, krx_quant::parse_kind_predictions); made.is_ok())
                parts->read.kinds.insert(kind, made.value());
            else
                parts->read.kind_failures.insert(kind, why(made.error()));
            done();
        });
    }
}

void KrxQuantService::read_briefing(const QDate& day) {
    if (!begin(kBriefing))
        return;
    const QString path =
        day.isValid() ? "/v1/briefing?day=" + day.toString(Qt::ISODate) : QStringLiteral("/v1/briefing");
    get(path, [this, day](Result<QJsonDocument> answer) {
        end(kBriefing);
        auto briefing = parsed(answer, krx_quant::parse_briefing);
        if (briefing.is_err())
            return emit briefing_failed(day, why(briefing.error()));
        emit briefing_ready(day, briefing.value());
    });
}

void KrxQuantService::read_flow(const QString& symbol, const QDate& day) {
    if (!begin(kFlow))
        return;
    const QString query = day.isValid() ? "/flow?day=" + day.toString(Qt::ISODate) : QStringLiteral("/flow");
    get(symbol_path(symbol, query), [this, symbol, day](Result<QJsonDocument> answer) {
        end(kFlow);
        auto flow = parsed(answer, krx_quant::parse_flow);
        if (flow.is_err())
            return emit flow_failed(symbol, day, why(flow.error()));
        emit flow_ready(symbol, day, flow.value());
    });
}

void KrxQuantService::read_cost_map(const LeaderboardQuery& query) {
    if (busy_[kCostMap] && !url_.isEmpty()) { // read once the one under way ends, as the leaderboard
        next_cost_map_ = query == cost_map_asked_ ? std::nullopt : std::optional(query);
        return;
    }
    if (!begin(kCostMap))
        return;
    cost_map_asked_ = query;
    get("/v1/cost-map?" + query_string(query), [this](Result<QJsonDocument> answer) {
        end(kCostMap);
        if (next_cost_map_) { // asked again meanwhile: this answer, or its failure, is stale
            const LeaderboardQuery next = *next_cost_map_;
            next_cost_map_.reset();
            return read_cost_map(next);
        }
        auto map = parsed(answer, krx_quant::parse_cost_map);
        if (map.is_err())
            return emit cost_map_failed(cost_map_asked_, why(map.error()));
        emit cost_map_ready(cost_map_asked_, map.value());
    });
}

void KrxQuantService::read_symbol_list() {
    if (!begin(kSymbolList))
        return;
    get("/v1/symbols", [this](Result<QJsonDocument> answer) {
        end(kSymbolList);
        auto symbols = parsed(answer, krx_quant::parse_symbols);
        if (symbols.is_err())
            return emit symbols_failed(why(symbols.error()));
        emit symbols_ready(symbols.value());
    });
}

void KrxQuantService::read_macro() {
    if (!begin(kMacro))
        return;
    get("/v1/macro", [this](Result<QJsonDocument> answer) {
        end(kMacro);
        auto macro = parsed(answer, krx_quant::parse_macro);
        if (macro.is_err())
            return emit macro_failed(why(macro.error()));
        emit macro_ready(macro.value());
    });
}

} // namespace fincept::services
