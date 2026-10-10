#include "services/krx_quant/KrxQuantService.h"

#include "core/config/AppConfig.h"
#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"

#include <QPointer>
#include <QUrl>

#include <memory>

namespace fincept::services {

namespace {

constexpr auto kBaseUrlKey = "krx_quant/base_url";
constexpr auto kDefaultBaseUrl = "http://krx-quant:8080";

QString reason_of(const std::string& error) {
    const int status = HttpClient::status_from_error(error);
    const QString message = HttpClient::message_from_error(error);
    if (status == 0 && message.isEmpty())
        return QStringLiteral("엔진에 연결할 수 없습니다. 엔진 주소와 Tailscale 연결을 확인하세요.");
    if (status == 0)
        return QStringLiteral("엔진에 연결할 수 없습니다: %1").arg(message);
    return QStringLiteral("엔진 오류 (HTTP %1): %2").arg(status).arg(message);
}

} // namespace

KrxQuantService& KrxQuantService::instance() {
    static KrxQuantService s;
    return s;
}

KrxQuantService::KrxQuantService() {
    qRegisterMetaType<Snapshot>();
}

QString KrxQuantService::base_url() const {
    return AppConfig::instance().get(kBaseUrlKey, kDefaultBaseUrl).toString();
}

void KrxQuantService::set_base_url(const QString& url) {
    QString trimmed = url.trimmed();
    while (trimmed.endsWith('/'))
        trimmed.chop(1);
    AppConfig::instance().set(kBaseUrlKey, trimmed.isEmpty() ? QString(kDefaultBaseUrl) : trimmed);
}

void KrxQuantService::refresh() {
    if (!in_flight_)
        restart();
}

void KrxQuantService::finish_with(const Snapshot& snapshot) {
    in_flight_ = false;
    emit snapshot_ready(snapshot);
}

void KrxQuantService::fail_with(const QString& reason) {
    in_flight_ = false;
    emit refresh_failed(reason);
}

void KrxQuantService::restart() {
    in_flight_ = true;
    const int generation = ++generation_;
    QPointer<KrxQuantService> self = this;
    HttpClient::instance().get(
        base_url() + "/v1/status",
        [self, generation](Result<QJsonDocument> result) {
            if (!self || generation != self->generation_)
                return;
            if (result.is_err()) {
                self->fail_with(reason_of(result.error()));
                return;
            }
            auto status = krx_quant::parse_status(result.value());
            if (status.is_err()) {
                self->fail_with(QString::fromStdString(status.error()));
                return;
            }
            Snapshot snapshot;
            snapshot.status = status.value();
            self->read_symbols(generation, snapshot);
        },
        this);
}

void KrxQuantService::read_symbols(int generation, Snapshot snapshot) {
    QPointer<KrxQuantService> self = this;
    HttpClient::instance().get(
        base_url() + "/v1/symbols",
        [self, generation, snapshot](Result<QJsonDocument> result) mutable {
            if (!self || generation != self->generation_)
                return;
            if (result.is_err()) {
                self->fail_with(reason_of(result.error()));
                return;
            }
            auto symbols = krx_quant::parse_symbols(result.value());
            if (symbols.is_err()) {
                self->fail_with(QString::fromStdString(symbols.error()));
                return;
            }
            snapshot.symbols = symbols.value();
            self->read_latest(generation, snapshot);
        },
        this);
}

void KrxQuantService::read_latest(int generation, Snapshot snapshot) {
    if (snapshot.symbols.isEmpty()) {
        finish_with(snapshot);
        return;
    }
    // Every symbol is read at once; the snapshot goes out when the last answer is in.
    auto shared = std::make_shared<Snapshot>(std::move(snapshot));
    auto waiting = std::make_shared<qsizetype>(shared->symbols.size());
    QPointer<KrxQuantService> self = this;
    for (const auto& quote : shared->symbols) {
        const QString symbol = quote.symbol;
        const QString url =
            base_url() + "/v1/symbols/" + QString::fromUtf8(QUrl::toPercentEncoding(symbol)) + "/latest";
        HttpClient::instance().get(
            url,
            [self, generation, shared, waiting, symbol](Result<QJsonDocument> result) {
                if (!self || generation != self->generation_)
                    return;
                if (result.is_err()) {
                    shared->errors << QStringLiteral("%1: %2").arg(symbol, reason_of(result.error()));
                } else if (auto latest = krx_quant::parse_latest(result.value()); latest.is_ok()) {
                    shared->latest.insert(symbol, latest.value());
                } else {
                    shared->errors << QStringLiteral("%1: %2").arg(symbol, QString::fromStdString(latest.error()));
                }
                if (--*waiting == 0) {
                    if (!shared->errors.isEmpty())
                        LOG_WARN("KrxQuant", shared->errors.join("; "));
                    self->finish_with(*shared);
                }
            },
            this);
    }
}

} // namespace fincept::services
