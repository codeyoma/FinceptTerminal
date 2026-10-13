#include "core/currency/fx/FxRates.h"

#include <QCoreApplication>
#include <QLocale>
#include <QXmlStreamReader>

#include <cmath>

namespace fincept::currency {
namespace {
bool valid_code(const QString& code) {
    if (code.size() != 3)
        return false;
    for (const QChar c : code)
        if (c < u'A' || c > u'Z')
            return false;
    return true;
}
bool valid_rate(double rate) {
    return std::isfinite(rate) && rate > 0;
}
struct Unit {
    QString code;
    double factor = 1;
};
Unit unit(const QString& source) {
    // Yahoo's GBp is pence, NOT the ISO code GBP. Check before any case normalization.
    if (source == "GBp" || source == "GBX")
        return {"GBP", 0.01};
    return {source, 1};
}
} // namespace

bool FxRates::usable(const QDate& today) const {
    const auto age = date_.daysTo(today);
    return today.isValid() && date_.isValid() && age >= 0 && age <= max_age_days && rates_.size() > 1;
}

std::optional<FxRates> FxRates::from_ecb_xml(const QByteArray& data, const QDate& today) {
    if (data.isEmpty() || data.size() > max_bytes)
        return std::nullopt;
    QXmlStreamReader xml(data);
    FxRates result;
    result.rates_.insert("EUR", 1);
    int depth = 0;
    int dated_depth = -1;
    bool saw_date = false;
    bool saw_root = false;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
            return std::nullopt;
        if (xml.isStartElement()) {
            ++depth;
            if (depth == 1) {
                if (xml.name() != u"Envelope")
                    return std::nullopt;
                saw_root = true;
            }
            if (xml.name() != u"Cube")
                continue;
            const auto attrs = xml.attributes();
            if (attrs.hasAttribute("time")) {
                if (saw_date)
                    return std::nullopt;
                saw_date = true;
                dated_depth = depth;
                result.date_ = QDate::fromString(attrs.value("time").toString(), Qt::ISODate);
            } else if (attrs.hasAttribute("currency")) {
                if (dated_depth < 0 || depth != dated_depth + 1)
                    return std::nullopt;
                const QString code = attrs.value("currency").toString();
                bool ok = false;
                const double rate = attrs.value("rate").toDouble(&ok);
                if (!valid_code(code) || result.rates_.contains(code) || !ok || !valid_rate(rate))
                    return std::nullopt;
                result.rates_.insert(code, rate);
            }
        } else if (xml.isEndElement()) {
            if (depth == dated_depth)
                dated_depth = -1;
            --depth;
        }
    }
    if (xml.hasError() || !saw_root || !saw_date || !result.usable(today))
        return std::nullopt;
    return result;
}

std::optional<FxRates> FxRates::from_json(const QJsonObject& object, const QDate& today) {
    if (object.value("version").toDouble() != 1 || object.value("source").toString() != "ECB")
        return std::nullopt;
    FxRates result;
    result.date_ = QDate::fromString(object.value("date").toString(), Qt::ISODate);
    const auto rates = object.value("rates").toObject();
    if (rates.isEmpty() || rates.size() > 200)
        return std::nullopt;
    for (auto it = rates.begin(); it != rates.end(); ++it) {
        if (!valid_code(it.key()) || !it.value().isDouble() || !valid_rate(it.value().toDouble()))
            return std::nullopt;
        result.rates_.insert(it.key(), it.value().toDouble());
    }
    if (result.rates_.value("EUR") != 1 || !result.usable(today))
        return std::nullopt;
    return result;
}

QJsonObject FxRates::to_json() const {
    QJsonObject rates;
    for (auto it = rates_.begin(); it != rates_.end(); ++it)
        rates.insert(it.key(), it.value());
    return {{"version", 1}, {"source", "ECB"}, {"date", date_.toString(Qt::ISODate)}, {"rates", rates}};
}

DisplayAmount FxRates::convert(double amount, const QString& source, const QString& target, const QDate& today) const {
    DisplayAmount result{amount, source, false, false, {}};
    if (!std::isfinite(amount) || source.isEmpty() || target.isEmpty())
        return result;
    const auto from = unit(source);
    const auto to = unit(target);
    // Identity and minor-unit conversion do not require an exchange-rate download.
    if (from.code == to.code) {
        const double value = amount * from.factor / to.factor;
        if (std::isfinite(value))
            result = {value, target, true, source != target, {}};
        return result;
    }
    if (!usable(today) || !rates_.contains(from.code) || !rates_.contains(to.code))
        return result;
    const double value = amount * (from.factor * rates_.value(to.code) / rates_.value(from.code) / to.factor);
    if (!std::isfinite(value))
        return result;
    return {value, target, true, true, date_};
}

QString currency_symbol(const QString& code) {
    if (code == "USD" || code == "CAD" || code == "AUD")
        return "$";
    if (code == "EUR")
        return QString::fromUtf8("€");
    if (code == "GBP")
        return QString::fromUtf8("£");
    if (code == "JPY" || code == "CNY")
        return QString::fromUtf8("¥");
    if (code == "INR")
        return QString::fromUtf8("₹");
    if (code == "KRW")
        return QString::fromUtf8("₩");
    if (code == "CHF")
        return "Fr";
    return {};
}

QString format_display_amount(const DisplayAmount& amount, bool compact, int precision) {
    if (!std::isfinite(amount.value))
        return QString::fromUtf8("—");
    const QString sign = amount.value < 0 ? QStringLiteral("-") : QString();
    const double value = std::abs(amount.value);
    QString number;
    if (compact && value >= 1e3) {
        if (value >= 1e9)
            number = QString::number(value / 1e9, 'f', 1) + "B";
        else if (value >= 1e6)
            number = QString::number(value / 1e6, 'f', 1) + "M";
        else
            number = QString::number(value / 1e3, 'f', 0) + "K";
    } else {
        int digits =
            precision < 0 ? (amount.currency == "KRW" || amount.currency == "JPY" ? 0 : 2) : qBound(0, precision, 8);
        if (value > 0 && value < 1 && precision < 0)
            digits = qBound(digits, static_cast<int>(-std::floor(std::log10(value))) + 2, 8);
        number = QLocale(QLocale::English).toString(value, 'f', digits);
        if (value > 0 && value < 1e-8)
            number = QString::number(value, 'g', 3);
    }
    const QString suffix =
        amount.currency.isEmpty() ? QCoreApplication::translate("Currency", "(native)") : amount.currency;
    return sign + currency_symbol(amount.currency) + number + " " + suffix;
}
} // namespace fincept::currency
