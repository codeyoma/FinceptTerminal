#pragma once
// fork addition (codeyoma/krx-quant#142): valuation comparisons require matching source units.
#include "core/currency/fx/FxRates.h"

#include <QString>

#include <cmath>
#include <optional>

namespace fincept::screens::equity {

inline QString resolved_quote_type(const QString& quote_type, const QString& info_type = {}) {
    const QString type = quote_type.toUpper();
    return type.isEmpty() || type == QLatin1String("UNKNOWN") ? info_type.toUpper() : type;
}

// Futures may quote index/bond points despite a USD contract currency.
inline bool monetary_quote_type(const QString& type) {
    return currency::monetary_quote_type(type);
}

// Yahoo's quote unit does not establish the unit of aggregate market capitalization.
// For pence-quoted instruments, keep the aggregate unconverted until its own unit is known.
inline QString capitalization_currency(const QString& quote_currency) {
    return quote_currency == QLatin1String("GBp") || quote_currency == QLatin1String("GBX") ? QString()
                                                                                            : quote_currency;
}

// Keep case significant: Yahoo's GBp is one penny; GBP is one pound.
// Missing units are not evidence that two financial quantities are comparable.
inline bool same_valuation_currency(const QString& quote_currency, const QString& reporting_currency) {
    return !quote_currency.isEmpty() && quote_currency == reporting_currency;
}

inline std::optional<double> valuation_margin(double price, const QString& price_currency, double intrinsic,
                                              const QString& intrinsic_currency) {
    if (!same_valuation_currency(price_currency, intrinsic_currency) || !std::isfinite(price) ||
        !std::isfinite(intrinsic) || price <= 0.0 || intrinsic <= 0.0)
        return std::nullopt;
    const double margin = (intrinsic - price) / price * 100.0;
    return std::isfinite(margin) ? std::optional<double>(margin) : std::nullopt;
}

} // namespace fincept::screens::equity
