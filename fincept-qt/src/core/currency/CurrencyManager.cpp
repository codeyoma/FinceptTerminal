#include "core/currency/CurrencyManager.h"

#include "core/logging/Logger.h"
#include "storage/repositories/SettingsRepository.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <memory>

namespace fincept::currency {

CurrencyManager& CurrencyManager::instance() {
    static CurrencyManager s;
    return s;
}

QVector<CurrencyInfo> CurrencyManager::available() {
    return {
        {"USD", "$", "US Dollar"},
        {"EUR", QString::fromUtf8("\xe2\x82\xac"), "Euro"},         // €
        {"GBP", QString::fromUtf8("\xc2\xa3"), "British Pound"},    // £
        {"JPY", QString::fromUtf8("\xc2\xa5"), "Japanese Yen"},     // ¥
        {"INR", QString::fromUtf8("\xe2\x82\xb9"), "Indian Rupee"}, // ₹
        {"CNY", QString::fromUtf8("\xc2\xa5"), "Chinese Yuan"},     // ¥
        {"CHF", "Fr", "Swiss Franc"},
        {"CAD", "$", "Canadian Dollar"},
        {"AUD", "$", "Australian Dollar"},
        // fork addition (codeyoma/krx-quant#138): Korean won in the shared display-currency list.
        {"KRW", QString::fromUtf8("\xe2\x82\xa9"), "South Korean Won"}, // ₩
    };
}

QString CurrencyManager::symbol_for(const QString& code) {
    const auto symbol = currency_symbol(code);
    return symbol.isEmpty() ? code : symbol;
}

void CurrencyManager::initialize() {
    if (initialized_)
        return;
    initialized_ = true;
    const auto r = SettingsRepository::instance().get("general.currency", "USD");
    if (r.is_ok()) {
        for (const auto& currency : available())
            if (currency.code == r.value())
                code_ = currency.code;
    }
    // The main settings DB is already open; CacheDatabase opens later at startup.
    const auto cached = SettingsRepository::instance().get("general.fx_rates_v1");
    if (cached.is_ok() && cached.value().size() <= FxRates::max_bytes) {
        const auto parsed =
            FxRates::from_json(QJsonDocument::fromJson(cached.value().toUtf8()).object(), QDate::currentDate());
        if (parsed) {
            rates_ = *parsed;
            cached_ = true;
        }
    }
    last_usable_ = rates_usable();
    connect(&refresh_timer_, &QTimer::timeout, this, &CurrencyManager::refresh_rates);
    refresh_timer_.start(6 * 60 * 60 * 1000);
    connect(&expiry_timer_, &QTimer::timeout, this, [this] {
        if (last_usable_ != rates_usable()) {
            last_usable_ = rates_usable();
            emit display_changed();
            emit rates_status_changed();
        }
    });
    expiry_timer_.start(60 * 1000);
    QTimer::singleShot(0, this, &CurrencyManager::refresh_rates);
    LOG_INFO("Currency", QString("Preferred currency: %1").arg(code_));
}

DisplayAmount CurrencyManager::display_amount(double value, const QString& source) const {
    return rates_.convert(value, source, code_, QDate::currentDate());
}

void CurrencyManager::refresh_rates() {
    if (refreshing_)
        return;
    refreshing_ = true;
    rates_error_.clear();
    emit rates_status_changed();
    if (!network_)
        network_ = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral("https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml")));
    // Never forward credentials or follow a redirect to another host.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setRawHeader("Accept", "application/xml,text/xml");
    auto* reply = network_->get(request);
    reply->setReadBufferSize(FxRates::max_bytes + 1);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes] {
        bytes->append(reply->read(FxRates::max_bytes + 1 - bytes->size()));
        if (bytes->size() > FxRates::max_bytes) {
            reply->setProperty("fxOversize", true);
            reply->abort();
        }
    });
    QTimer::singleShot(20000, reply, [reply] {
        if (reply->isRunning())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes] {
        const bool was_usable = rates_usable();
        const bool response_ok = reply->error() == QNetworkReply::NoError &&
                                 reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                                 !reply->property("fxOversize").toBool();
        const auto parsed = response_ok ? FxRates::from_ecb_xml(*bytes, QDate::currentDate()) : std::nullopt;
        if (parsed && (!was_usable || parsed->date() >= rates_.date())) {
            const bool changed = rates_.to_json() != parsed->to_json();
            rates_ = *parsed;
            cached_ = false;
            const auto saved = SettingsRepository::instance().set(
                "general.fx_rates_v1",
                QString::fromUtf8(QJsonDocument(rates_.to_json()).toJson(QJsonDocument::Compact)), "general");
            if (saved.is_err())
                LOG_WARN("Currency", "Could not persist reference rates");
            last_usable_ = rates_usable();
            if (changed || was_usable != last_usable_)
                emit display_changed();
        } else {
            rates_error_ = response_ok ? QStringLiteral("invalid") : QStringLiteral("network");
            cached_ = rates_usable();
            if (last_usable_ != rates_usable()) {
                last_usable_ = rates_usable();
                emit display_changed();
            }
        }
        refreshing_ = false;
        reply->deleteLater();
        emit rates_status_changed();
    });
}

void CurrencyManager::set_currency(const QString& code) {
    if (code == code_ || symbol_for(code) == code) // unchanged, or unknown code
        return;
    const auto saved = SettingsRepository::instance().set("general.currency", code, "general");
    if (saved.is_err()) {
        LOG_WARN("Currency", "Could not persist preferred currency");
        return;
    }
    code_ = code;
    LOG_INFO("Currency", QString("Currency \xe2\x86\x92 %1").arg(code)); // →
    emit currency_changed(code_, symbol());
    emit display_changed();
}

} // namespace fincept::currency
