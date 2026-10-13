#pragma once
// fork addition (codeyoma/krx-quant#142): pure, source-aware display conversion.
#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace fincept::currency {
struct DisplayAmount {
    double value = 0;
    QString currency;
    bool available = false; // True only when the requested display currency was reached.
    bool converted = false;
    QDate rate_date;
};

class FxRates {
  public:
    static constexpr int max_age_days = 7;
    static constexpr qsizetype max_bytes = 256 * 1024;
    static std::optional<FxRates> from_ecb_xml(const QByteArray& xml, const QDate& today);
    static std::optional<FxRates> from_json(const QJsonObject& object, const QDate& today);
    QJsonObject to_json() const;
    QDate date() const { return date_; }
    bool usable(const QDate& today) const;
    DisplayAmount convert(double amount, const QString& source, const QString& target, const QDate& today) const;

  private:
    QDate date_;
    QHash<QString, double> rates_; // Units of each currency per EUR; EUR is exactly 1.
};

// Currency metadata alone is insufficient: futures can quote index or bond points.
inline bool monetary_quote_type(const QString& type) {
    const auto normalized = type.toUpper();
    return normalized == "EQUITY" || normalized == "ETF" || normalized == "MUTUALFUND" || normalized == "OPTION" ||
           normalized == "CRYPTOCURRENCY";
}

QString currency_symbol(const QString& code);
// Always identifies the ACTUAL currency, including a failed conversion's original currency.
QString format_display_amount(const DisplayAmount& amount, bool compact = false, int precision = -1);
} // namespace fincept::currency
