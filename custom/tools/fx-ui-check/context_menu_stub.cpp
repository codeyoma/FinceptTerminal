// Upstream v4.5.1 gave BaseWidget a symbol context menu that reads the saved
// watchlists and hands a symbol to the Backtesting screen. The check never opens
// that menu, so both services are stubbed: no SQLite database, no Python.
#include "services/backtesting/BacktestingService.h"
#include "storage/repositories/WatchlistRepository.h"

namespace fincept::services::backtest {
BacktestingService& BacktestingService::instance() {
    static BacktestingService service;
    return service;
}
BacktestingService::BacktestingService(QObject* parent) : QObject(parent) {}
BacktestingService::~BacktestingService() = default;
void BacktestingService::set_pending_portfolio_config(const QJsonObject&) {}
} // namespace fincept::services::backtest

namespace fincept {
WatchlistRepository& WatchlistRepository::instance() {
    static WatchlistRepository repository;
    return repository;
}
Result<QVector<Watchlist>> WatchlistRepository::list_all() {
    return Result<QVector<Watchlist>>::ok({});
}
Result<QVector<WatchlistStock>> WatchlistRepository::get_stocks(const QString&) {
    return Result<QVector<WatchlistStock>>::ok({});
}
Result<void> WatchlistRepository::add_stock(const QString&, const QString&, const QString&, const QString&) {
    return Result<void>::err("stub");
}
} // namespace fincept
