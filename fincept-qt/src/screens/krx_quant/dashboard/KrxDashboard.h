#pragma once

// What the dashboard's upstream files call for the KRX widgets (codeyoma/krx-quant#302): kept to
// declarations so WidgetRegistry.cpp and DashboardTemplates.cpp take one include and one line each.

namespace fincept::screens {
class WidgetRegistry;
struct DashboardTemplate;
} // namespace fincept::screens

namespace fincept::screens::krx_dashboard {

/// Register the KRX widgets: krx_signals, krx_leaderboard, krx_engine_status, krx_open_to_close.
void register_widgets(WidgetRegistry& registry);

/// The "KRX" dashboard template: the four KRX widgets.
DashboardTemplate dashboard_template();

} // namespace fincept::screens::krx_dashboard
