// The real header and moc provide the signal. Only external data-service
// construction/Producer behavior is stubbed, so no network/SQLite is involved.
#include "services/markets/MarketDataService.h"
namespace fincept::services {
MarketDataService& MarketDataService::instance() {
    static MarketDataService service;
    return service;
}
MarketDataService::MarketDataService() = default;
QStringList MarketDataService::topic_patterns() const { return {}; }
void MarketDataService::refresh(const QStringList&) {}
int MarketDataService::max_requests_per_sec() const { return 0; }
}
