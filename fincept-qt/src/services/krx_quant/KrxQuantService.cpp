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

QString symbol_path(const QString& symbol, const QString& rest) {
    return "/v1/symbols/" + QString::fromUtf8(QUrl::toPercentEncoding(symbol)) + rest;
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
        if (answer.is_err())
            return fail_with(krx_failure(answer.error()));
        auto status = krx_quant::parse_status(answer.value());
        if (status.is_err())
            return fail_with(QString::fromStdString(status.error()));
        Snapshot snapshot;
        snapshot.status = status.value();
        read_symbols(snapshot);
    });
}

void KrxQuantService::restart() {
    ++generation_; // every answer still under way is dropped
    for (bool& busy : busy_)
        busy = false;
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
        if (answer.is_err())
            return fail_with(krx_failure(answer.error()));
        auto symbols = krx_quant::parse_symbols(answer.value());
        if (symbols.is_err())
            return fail_with(QString::fromStdString(symbols.error()));
        snapshot.symbols = symbols.value();
        read_members(snapshot);
    });
}

void KrxQuantService::read_members(Snapshot snapshot) {
    get("/v1/consensus", [this, snapshot](Result<QJsonDocument> answer) mutable {
        if (answer.is_err()) {
            snapshot.errors << QStringLiteral("합의 구성원: %1").arg(krx_failure(answer.error()));
        } else if (auto consensus = krx_quant::parse_consensus(answer.value()); consensus.is_ok()) {
            snapshot.members = consensus.value().members;
        } else {
            snapshot.errors << QStringLiteral("합의 구성원: %1").arg(QString::fromStdString(consensus.error()));
        }
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
            if (answer.is_err()) {
                shared->errors << QStringLiteral("%1: %2").arg(symbol, krx_failure(answer.error()));
            } else if (auto latest = krx_quant::parse_latest(answer.value()); latest.is_ok()) {
                shared->latest.insert(symbol, latest.value());
            } else {
                shared->errors << QStringLiteral("%1: %2").arg(symbol, QString::fromStdString(latest.error()));
            }
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

void KrxQuantService::fail_with(const QString& reason) {
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
        if (answer.is_err())
            return fail_with(krx_failure(answer.error()));
        auto status = krx_quant::parse_status(answer.value());
        if (status.is_err())
            return fail_with(QString::fromStdString(status.error()));
        emit status_ready(status.value());
    });
}

void KrxQuantService::read_chart(const QString& symbol) {
    if (!begin(kChart))
        return;
    get(symbol_path(symbol, "/bars"), [this, symbol](Result<QJsonDocument> answer) {
        if (answer.is_err()) {
            end(kChart);
            return emit chart_failed(symbol, krx_failure(answer.error()));
        }
        auto bars = krx_quant::parse_bars(answer.value());
        if (bars.is_err()) {
            end(kChart);
            return emit chart_failed(symbol, QString::fromStdString(bars.error()));
        }
        const QString path =
            symbol_path(symbol, QString("/predictions?limit=%1&algorithm=consensus-final").arg(kFinalsLimit));
        get(path, [this, symbol, bars = bars.value()](Result<QJsonDocument> answer) {
            end(kChart);
            if (answer.is_err())
                return emit chart_failed(symbol, krx_failure(answer.error()));
            auto finals = krx_quant::parse_predictions(answer.value());
            if (finals.is_err())
                return emit chart_failed(symbol, QString::fromStdString(finals.error()));
            emit chart_ready(symbol, bars, finals.value());
        });
    });
}

void KrxQuantService::read_leaderboard(const LeaderboardQuery& query) {
    if (!begin(kLeaderboard))
        return;
    QUrlQuery q;
    q.addQueryItem("symbol", query.symbol);
    q.addQueryItem("period", query.period);
    q.addQueryItem("basis", query.basis);
    q.addQueryItem("special", query.special ? "include" : "exclude");
    get("/v1/leaderboard?" + q.toString(QUrl::FullyEncoded), [this](Result<QJsonDocument> answer) {
        end(kLeaderboard);
        if (answer.is_err())
            return emit leaderboard_failed(krx_failure(answer.error()));
        auto board = krx_quant::parse_leaderboard(answer.value());
        if (board.is_err())
            return emit leaderboard_failed(QString::fromStdString(board.error()));
        emit leaderboard_ready(board.value());
    });
}

void KrxQuantService::read_macro() {
    if (!begin(kMacro))
        return;
    get("/v1/macro", [this](Result<QJsonDocument> answer) {
        end(kMacro);
        if (answer.is_err())
            return emit macro_failed(krx_failure(answer.error()));
        auto macro = krx_quant::parse_macro(answer.value());
        if (macro.is_err())
            return emit macro_failed(QString::fromStdString(macro.error()));
        emit macro_ready(macro.value());
    });
}

} // namespace fincept::services
