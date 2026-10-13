#include "screens/portfolio/PortfolioPresentation.h"

#include <QCoreApplication>
#include <QDebug>

#include <cmath>
#include <limits>

using namespace fincept::portfolio;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            qCritical() << message;
            ++failures;
        }
    };
    PortfolioSummary source;
    source.portfolio.currency = "USD";
    HoldingWithQuote holding;
    holding.symbol = "AAPL";
    holding.quantity = 2;
    holding.avg_buy_price = 80;
    holding.current_price = 100;
    holding.market_value = 200;
    holding.cost_basis = 160;
    holding.unrealized_pnl = 40;
    holding.unrealized_pnl_percent = 25;
    holding.day_change = 5;
    holding.weight = 100;
    source.holdings = {holding};
    source.total_market_value = 200;
    source.total_cost_basis = 160;
    source.total_unrealized_pnl = 40;
    source.total_unrealized_pnl_percent = 25;
    source.total_day_change = 10;
    source.total_day_change_percent = 5.26;
    PortfolioSnapshot snapshot;
    snapshot.total_value = 180;
    snapshot.total_cost_basis = 160;
    snapshot.total_pnl = 20;
    snapshot.total_pnl_percent = 12.5;
    snapshot.snapshot_date = "2026-10-12";
    const QVector<PortfolioSnapshot> snapshots{snapshot};
    const QHash<QString, PortfolioQuoteUnit> currencies{{"AAPL", {"USD", true}}};

    const auto won = present_portfolio(source, snapshots, currencies, "KRW", 1400.0);
    check(won.source_verified && won.converted, "Known homogeneous source must convert");
    check(won.monetary_available, "Verified homogeneous monetary panels must remain available");
    check(won.summary.portfolio.currency == "KRW" && won.summary.total_market_value == 280000,
          "NAV and its currency must convert together");
    check(won.summary.holdings.first().avg_buy_price == 112000 &&
              won.summary.holdings.first().unrealized_pnl == 56000 && won.summary.total_day_change == 14000,
          "Cost, P&L and day change must use the same rate");
    check(won.snapshots.first().total_value == 180 && won.snapshots.first().total_cost_basis == 160,
          "Current holding currencies do not prove historical snapshot provenance");
    check(won.summary.holdings.first().quantity == 2 && won.summary.holdings.first().weight == 100 &&
              won.summary.total_unrealized_pnl_percent == 25 && won.snapshots.first().total_pnl_percent == 12.5,
          "Quantities, weights and return percentages must remain unchanged");
    check(source.portfolio.currency == "USD" && source.total_market_value == 200 &&
              source.holdings.first().avg_buy_price == 80 && snapshots.first().total_value == 180,
          "Rendering must never mutate source summaries or snapshots");
    check(present_portfolio(source, snapshots, currencies, "KRW", 1400.0).summary.total_market_value == 280000,
          "Repeated rendering must not compound conversion");

    auto mixed = source;
    auto foreign = holding;
    foreign.symbol = "RELIANCE.NS";
    foreign.current_price = 5000;
    foreign.market_value = 5000;
    foreign.quantity = 1;
    mixed.holdings.append(foreign);
    mixed.total_market_value += foreign.market_value;
    auto mixed_currencies = currencies;
    mixed_currencies.insert(foreign.symbol, {"INR", true});
    const auto blocked = present_portfolio(mixed, snapshots, mixed_currencies, "KRW", 1400.0);
    check(!blocked.source_verified && !blocked.converted && blocked.summary.portfolio.currency == "USD" &&
              blocked.summary.total_market_value == mixed.total_market_value &&
              blocked.snapshots.first().total_value == 180,
          "Mixed source currencies must never relabel or convert aggregates");
    check(!blocked.monetary_available,
          "Mixed source currencies must hide monetary aggregate panels instead of displaying a raw sum");
    check(!present_portfolio(source, snapshots, {}, "KRW", 1400.0).monetary_available,
          "Unknown sources must hide monetary aggregate panels");
    check(!present_portfolio(source, snapshots, {}, "KRW", 1400.0).source_verified,
          "Unknown quote currency must block aggregate conversion");
    const QHash<QString, PortfolioQuoteUnit> index_units{{"AAPL", {"USD", false}}};
    check(!present_portfolio(source, snapshots, index_units, "KRW", 1400.0).source_verified,
          "INDEX or unknown quote type must not convert even when metadata contains USD");
    check(!present_portfolio(source, snapshots, index_units, "KRW", 1400.0).monetary_available,
          "Nonmonetary quote units must not expose monetary aggregate panels");
    auto unlabelled = source;
    unlabelled.portfolio.currency.clear();
    check(!present_portfolio(unlabelled, snapshots, currencies, "KRW", 1400.0).source_verified,
          "Missing portfolio currency must never imply USD");
    const auto unavailable = present_portfolio(source, snapshots, currencies, "KRW", std::nullopt);
    check(unavailable.source_verified && !unavailable.converted && unavailable.summary.portfolio.currency == "USD",
          "Missing FX must retain the verified native denomination");
    check(unavailable.monetary_available, "Missing FX may still show verified native monetary values");
    check(
        !present_portfolio(source, snapshots, currencies, "KRW", -1.0).converted &&
            !present_portfolio(source, snapshots, currencies, "KRW", std::numeric_limits<double>::infinity()).converted,
        "Invalid FX rates must not reach the display");
    auto enormous = source;
    enormous.holdings.first().market_value = std::numeric_limits<double>::max();
    const auto overflow = present_portfolio(enormous, snapshots, currencies, "KRW", 1400.0);
    check(!overflow.converted && overflow.summary.portfolio.currency == "USD" &&
              overflow.summary.total_market_value == source.total_market_value &&
              overflow.summary.holdings.first().market_value == enormous.holdings.first().market_value,
          "Overflow in any monetary field must preserve the entire native display");
    auto live = source;
    update_native_quote(live.holdings, "AAPL", 120, 20);
    const auto refreshed = present_portfolio(live, snapshots, currencies, "KRW", 1400.0);
    check(live.holdings.first().current_price == 120 && live.holdings.first().market_value == 240 &&
              live.holdings.first().cost_basis == 160 && live.holdings.first().unrealized_pnl == 80,
          "Live ticks must update raw caches without converting cost basis");
    check(refreshed.summary.holdings.first().current_price == 168000 &&
              refreshed.summary.holdings.first().unrealized_pnl == 112000,
          "FX repaint after a broker tick must keep the latest exact native price");
    return failures == 0 ? 0 : 1;
}
