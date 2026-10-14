#include "services/krx_quant/KrxQuantService.h"

#include "core/config/AppConfig.h"
#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"

#include <QUrl>
#include <QUrlQuery>

#include <memory>

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
    q.addQueryItem("basis", query.basis);
    q.addQueryItem("special", query.special_value());
    return q.toString(QUrl::FullyEncoded);
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
    ++generation_; // every answer still under way is dropped
    for (bool& busy : busy_)
        busy = false;
    next_leaderboard_.reset();
    next_close_.reset();
    stats_reads_.clear();
    if (const QString url = base_url(); url != url_) {
        url_ = url;
        emit base_url_changed(url_);
    }
    refresh();
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
    get(symbol_path(symbol, "/predictions?algorithm=consensus-final&" + query),
        [this, symbol, query, done](Result<QJsonDocument> answer) {
            auto finals = parsed(answer, krx_quant::parse_predictions);
            if (finals.is_err())
                return done(finals);
            get(symbol_path(symbol, "/predictions?algorithm=baseline-flat&" + query),
                [done, finals = finals.value()](Result<QJsonDocument> answer) {
                    auto flats = parsed(answer, krx_quant::parse_predictions);
                    if (flats.is_err())
                        return done(flats);
                    done(Result<Predictions>::ok(finals + flats.value()));
                });
        });
}

void KrxQuantService::read_chart(const QString& symbol, bool all) {
    if (!begin(kChart))
        return;
    get(symbol_path(symbol, "/bars"), [this, symbol, all](Result<QJsonDocument> answer) {
        auto bars = parsed(answer, krx_quant::parse_bars);
        if (bars.is_err()) {
            end(kChart);
            return emit chart_failed(symbol, why(bars.error()));
        }
        read_chart_predictions(symbol, QString("limit=%1").arg(all ? kAllLimit : kFinalsLimit), all,
                               [this, symbol, all, bars = bars.value()](Result<QList<krx_quant::Prediction>> made) {
                                   end(kChart);
                                   if (made.is_err())
                                       return emit chart_failed(symbol, why(made.error()));
                                   emit chart_ready(symbol, bars, made.value(), all);
                               });
    });
}

void KrxQuantService::read_older(const QString& symbol, const krx_quant::OlderRead& read, bool all) {
    if (!begin(kOlder))
        return; // the view moves on: asked again once this one is drawn
    auto before = [](const QDateTime& at) {
        return at.isValid() ? "&before=" + QString::fromUtf8(QUrl::toPercentEncoding(at.toString(Qt::ISODate)))
                            : QString();
    };
    const QString bars_path = QString("/bars?limit=%1").arg(read.limit) + before(read.before);
    const QString query = QString("limit=%1").arg(read.limit) + before(read.finals_before);
    get(symbol_path(symbol, bars_path), [this, symbol, query, all](Result<QJsonDocument> answer) {
        auto bars = parsed(answer, krx_quant::parse_bars);
        if (bars.is_err()) {
            end(kOlder);
            return emit older_failed(symbol, why(bars.error()));
        }
        read_chart_predictions(symbol, query, all,
                               [this, symbol, all, bars = bars.value()](Result<QList<krx_quant::Prediction>> made) {
                                   end(kOlder);
                                   if (made.is_err())
                                       return emit older_failed(symbol, why(made.error()));
                                   emit older_ready(symbol, bars, made.value(), all);
                               });
    });
}

void KrxQuantService::read_predictions(const QString& symbol, const QDateTime& before, int limit) {
    if (!begin(kPredictions))
        return;
    QString query = QString("limit=%1").arg(limit);
    if (before.isValid())
        query += "&before=" + QString::fromUtf8(QUrl::toPercentEncoding(before.toString(Qt::ISODate)));
    read_chart_predictions(symbol, query, true, [this, symbol, before](Result<QList<krx_quant::Prediction>> made) {
        end(kPredictions);
        if (made.is_err())
            return emit predictions_failed(symbol, why(made.error()));
        emit predictions_ready(symbol, before, made.value());
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
    get("/v1/leaderboard?" + query_string(query), [this](Result<QJsonDocument> answer) {
        end(kLeaderboard);
        if (next_leaderboard_) { // asked again meanwhile: this answer, or its failure, is stale
            const LeaderboardQuery next = *next_leaderboard_;
            next_leaderboard_.reset();
            return read_leaderboard(next);
        }
        auto leaderboard = parsed(answer, krx_quant::parse_leaderboard);
        if (leaderboard.is_err())
            return emit leaderboard_failed(why(leaderboard.error()));
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
