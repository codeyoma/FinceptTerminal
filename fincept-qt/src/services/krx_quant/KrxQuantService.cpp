#include "services/krx_quant/KrxQuantService.h"

#include "core/config/AppConfig.h"
#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"

#include <QUrl>

#include <memory>

namespace fincept::services {

namespace {

constexpr auto kBaseUrlKey = "krx_quant/base_url";

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
    if (!in_flight_ || base_url() != url_)
        restart();
}

void KrxQuantService::restart() {
    const int generation = ++generation_;
    if (const QString url = base_url(); url != url_) {
        url_ = url;
        emit base_url_changed(url_);
    }
    if (url_.isEmpty()) {
        fail_with(QStringLiteral("엔진 주소를 입력하세요."));
        return;
    }
    in_flight_ = true;
    get("/v1/status", generation, [this, generation](Result<QJsonDocument> answer) {
        if (answer.is_err())
            return fail_with(krx_failure(answer.error()));
        auto status = krx_quant::parse_status(answer.value());
        if (status.is_err())
            return fail_with(QString::fromStdString(status.error()));
        Snapshot snapshot;
        snapshot.status = status.value();
        read_symbols(generation, snapshot);
    });
}

void KrxQuantService::get(const QString& path, int generation, Step step) {
    // `this` as the context: the answer is dropped if the screen (and this service) is gone.
    HttpClient::instance().get(
        url_ + path,
        [this, generation, step = std::move(step)](Result<QJsonDocument> answer) {
            if (generation == generation_) // a restart since drops it
                step(std::move(answer));
        },
        this);
}

void KrxQuantService::read_symbols(int generation, Snapshot snapshot) {
    get("/v1/symbols", generation, [this, generation, snapshot](Result<QJsonDocument> answer) mutable {
        if (answer.is_err())
            return fail_with(krx_failure(answer.error()));
        auto symbols = krx_quant::parse_symbols(answer.value());
        if (symbols.is_err())
            return fail_with(QString::fromStdString(symbols.error()));
        snapshot.symbols = symbols.value();
        read_latest(generation, snapshot);
    });
}

void KrxQuantService::read_latest(int generation, Snapshot snapshot) {
    if (snapshot.symbols.isEmpty())
        return finish_with(snapshot);
    // Every symbol is read at once; the snapshot goes out when the last answer is in.
    auto shared = std::make_shared<Snapshot>(std::move(snapshot));
    auto waiting = std::make_shared<qsizetype>(shared->symbols.size());
    for (const auto& quote : shared->symbols) {
        const QString symbol = quote.symbol;
        const QString path = "/v1/symbols/" + QString::fromUtf8(QUrl::toPercentEncoding(symbol)) + "/latest";
        get(path, generation, [this, shared, waiting, symbol](Result<QJsonDocument> answer) {
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
    in_flight_ = false;
    if (!snapshot.errors.isEmpty())
        LOG_WARN("KrxQuant", snapshot.errors.join("; "));
    emit snapshot_ready(snapshot);
}

void KrxQuantService::fail_with(const QString& reason) {
    in_flight_ = false;
    LOG_WARN("KrxQuant", reason);
    emit refresh_failed(reason);
}

} // namespace fincept::services
