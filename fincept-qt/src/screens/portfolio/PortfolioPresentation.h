#pragma once

#include "screens/portfolio/PortfolioTypes.h"

#include <QHash>

#include <cmath>
#include <optional>

namespace fincept::portfolio {

struct PortfolioQuoteUnit {
    QString currency;
    bool monetary = false;
};

struct PortfolioPresentation {
    PortfolioSummary summary;
    QVector<PortfolioSnapshot> snapshots; // Native: historical snapshots have no currency provenance.
    bool source_verified = false;
    bool converted = false;
    bool monetary_available = false; // Native sums are also unsafe when quote/cost units disagree.
};

inline void update_native_quote(QVector<HoldingWithQuote>& holdings, const QString& symbol, double price,
                                double change_percent) {
    if (!std::isfinite(price) || price <= 0 || !std::isfinite(change_percent))
        return;
    for (auto& h : holdings) {
        if (h.symbol != symbol)
            continue;
        h.current_price = price;
        h.market_value = price * h.quantity;
        h.unrealized_pnl = h.market_value - h.cost_basis;
        h.unrealized_pnl_percent = h.cost_basis > 0 ? h.unrealized_pnl / h.cost_basis * 100.0 : 0.0;
        h.day_change_percent = change_percent;
        if (change_percent > -100.0)
            h.day_change = price - price / (1.0 + change_percent / 100.0);
    }
}

inline PortfolioPresentation present_portfolio(const PortfolioSummary& source,
                                               const QVector<PortfolioSnapshot>& snapshots,
                                               const QHash<QString, PortfolioQuoteUnit>& quote_currencies,
                                               const QString& target_currency, std::optional<double> rate) {
    PortfolioPresentation out{source, snapshots, !source.portfolio.currency.isEmpty(), false};
    // The store's prices/costs are declared in the portfolio currency. A quote
    // must confirm that same unit before their differences or totals can be
    // presented in another currency. Metadata absence never means USD.
    for (const auto& h : source.holdings) {
        if (!quote_currencies.value(h.symbol).monetary || quote_currencies.value(h.symbol).currency.isEmpty() ||
            quote_currencies.value(h.symbol).currency != source.portfolio.currency) {
            out.source_verified = false;
            break;
        }
    }
    out.monetary_available = out.source_verified;
    if (!out.source_verified || target_currency.isEmpty() || !rate || !std::isfinite(*rate) || *rate <= 0.0)
        return out;

    out.converted = target_currency != source.portfolio.currency;
    out.summary.portfolio.currency = target_currency;
    bool finite = true;
    auto scale = [&](double& v) {
        v *= *rate;
        finite = finite && std::isfinite(v);
    };
    scale(out.summary.total_market_value);
    scale(out.summary.total_cost_basis);
    scale(out.summary.total_unrealized_pnl);
    scale(out.summary.total_day_change);
    for (auto& h : out.summary.holdings) {
        scale(h.avg_buy_price);
        scale(h.current_price);
        scale(h.market_value);
        scale(h.cost_basis);
        scale(h.unrealized_pnl);
        scale(h.day_change);
    }
    // Today's homogeneous holdings do not establish the unit of older snapshots.
    // Retain the native history until the store has per-snapshot provenance.
    if (!finite)
        return {source, snapshots, out.source_verified, false, out.monetary_available};
    return out;
}

} // namespace fincept::portfolio
