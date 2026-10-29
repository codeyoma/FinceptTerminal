# Fork addition (custom/README.md): the KRX prediction screen's suites. Included
# from tests/CMakeLists.txt right after fincept_add_test(), away from the end of
# that file where upstream adds its suites, so upstream merges do not conflict.

# src/services/krx_quant/EngineData.{h,cpp} — how the KRX prediction screen (fork addition,
# custom/README.md) reads the prediction engine's answers. Qt Core only.
fincept_add_test(tst_krx_engine_data
    tst_krx_engine_data.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/Board.{h,cpp} — what the KRX screen shows, from the engine's answers
# and its stream in whatever order they come. Qt Core only.
fincept_add_test(tst_krx_board
    tst_krx_board.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/Signals.{h,cpp} — when the KRX screen raises a signal alert: the
# engine's Telegram signal rule with the screen's threshold and N. Qt Core only.
fincept_add_test(tst_krx_signals
    tst_krx_signals.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/StatsView.{h,cpp} — what the KRX screen's leaderboard and an
# algorithm's detail window show of the engine's stats. Qt Core only.
fincept_add_test(tst_krx_stats_view
    tst_krx_stats_view.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/StatsView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/ChartModel.{h,cpp} — what the KRX screen's chart draws: its bars and
# the final consensus's icons, worked out so its page only draws (codeyoma/krx-quant#105). Qt Core only.
fincept_add_test(tst_krx_chart_model
    tst_krx_chart_model.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/ChartLook.{h,cpp} — how the KRX screen's chart looks: its styles, the
# Heikin-Ashi bars, the baseline's base, the day separators and each symbol's chart settings
# (codeyoma/krx-quant#221). Qt Core only.
fincept_add_test(tst_krx_chart_look
    tst_krx_chart_look.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/AppRelease.{h,cpp} — which personal build is newer, and the newest one
# on the fork's Releases page, for the app's one-click update. Qt Core only.
fincept_add_test(tst_krx_app_release
    tst_krx_app_release.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/AppRelease.cpp")

# src/services/krx_quant/LocalCliLlm.{h,cpp}, LocalCliRunner.{h,cpp} — Fincept's LLM rounds through
# the local Claude Code or Codex CLI (codeyoma/krx-quant#96): arguments, reading each CLI's output,
# and running a stand-in CLI. Qt Core only.
fincept_add_test(tst_krx_local_cli_llm
    tst_krx_local_cli_llm.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/LocalCliLlm.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/LocalCliRunner.cpp")

# Fork addition (codeyoma/krx-quant#132): test the actual compiled language resource.
fincept_add_test(tst_krx_korean tst_krx_korean.cpp)
target_compile_definitions(tst_krx_korean PRIVATE
    FINCEPT_REFERENCE_CATALOG="${PROJECT_SOURCE_DIR}/translations/fincept_zh_CN.ts"
    FINCEPT_KOREAN_CATALOG="${PROJECT_SOURCE_DIR}/translations/fincept_ko_KR.ts")
qt6_add_resources(tst_krx_korean "krx_korean_i18n"
    PREFIX "/i18n"
    BASE "${PROJECT_BINARY_DIR}"
    FILES ${FINCEPT_QM_FILES})

# fork addition (codeyoma/krx-quant#142): deterministic FX parsing and conversion.
fincept_add_test(tst_fx_rates tst_fx_rates.cpp
    "${PROJECT_SOURCE_DIR}/src/core/currency/fx/FxRates.cpp")
fincept_add_test(tst_portfolio_presentation tst_portfolio_presentation.cpp)
fincept_add_test(tst_equity_valuation_units tst_equity_valuation_units.cpp)

# src/services/krx_quant/EngineCalendar.{h,cpp} — the dashboard's Economic Calendar read from the KRX
# engine's /v1/calendar instead of Fincept's credit-charged endpoint (codeyoma/krx-quant#150). Qt Core only.
fincept_add_test(tst_krx_engine_calendar
    tst_krx_engine_calendar.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineCalendar.cpp")

# src/services/krx_quant/KindView.{h,cpp} — what the KRX screen shows of a prediction kind besides the
# direction (codeyoma/krx-quant#182): the kind selector, the range's panel and hover boxes. Qt Core only.
fincept_add_test(tst_krx_kind_view
    tst_krx_kind_view.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KindView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/ChartStudies.{h,cpp} — the KRX chart's indicators: their settings as each symbol
# keeps them, their values, what the page draws and its legend, and the bars to read for them
# (codeyoma/krx-quant#222). Qt Core only.
fincept_add_test(tst_krx_chart_studies
    tst_krx_chart_studies.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/ChartDrawings.{h,cpp} — the KRX chart's drawings: the tools, a symbol's drawings as
# kept, where a moment lies on each view's bars and back, and the magnet (codeyoma/krx-quant#224). Qt Core only.
fincept_add_test(tst_krx_chart_drawings
    tst_krx_chart_drawings.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/ChartBricks.{h,cpp} — the KRX chart's styles without a time axis: renko, kagi, point
# and figure and line break of the view's bars, their sizes and where they start, and the indicators on
# them (codeyoma/krx-quant#225). Qt Core only.
fincept_add_test(tst_krx_chart_bricks
    tst_krx_chart_bricks.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartBricks.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/ChartSlots.{h,cpp} — the minute views' empty bars for the minutes without trades
# and the special situations' bands and hover lines over them (codeyoma/krx-quant#236), with what places
# by time on them (the drawings' positions) and what computes on prices beside them (the indicators).
# Qt Core only.
fincept_add_test(tst_krx_chart_slots
    tst_krx_chart_slots.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/BarView.{h,cpp} — what a bar's window shows (codeyoma/krx-quant#255): a 1-minute bar
# double-clicked, its prices and special situations, every prediction of its bar close, the answer in detail,
# the mini chart of its four minutes and when the window stops reading. Qt Core only.
fincept_add_test(tst_krx_bar_view
    tst_krx_bar_view.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/BarView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KindView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/KrxLive.{h,cpp} — what the KRX screen reads and when, against a stand-in
# engine on this computer: before the open a minute view shows the last trading day's bars however
# the screen got there. It needs Qt Network: what it tests is the reads the screen sends. The
# settings are a temporary file's (the test defines AppConfig); no WebSockets, so no stream.
fincept_add_test(tst_krx_live
    tst_krx_live.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxLive.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxLive.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxStream.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxStream.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KindView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/BarView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.h"
    "${PROJECT_SOURCE_DIR}/src/core/logging/Logger.cpp")
target_link_libraries(tst_krx_live PRIVATE Qt6::Network)

# src/screens/krx_quant/chart/index.html as src/screens/krx_quant/KrxChart.{h,cpp} drives it: the
# first screen fits the latest bars however the bars read before them come in, and a view moved
# by hand stays. With the keyboard on the chart, Cmd+D still opens the toolbar's drawing menu
# (codeyoma/krx-quant#241). Unlike the suites above it needs Widgets and Qt WebEngine: what it tests is the
# page running TradingView Lightweight Charts, which only a web view runs. It opens a window and
# skips itself offscreen or without the WebEngine runtime.
if(HAS_WEBENGINE)
    fincept_add_test(tst_krx_chart_page
        tst_krx_chart_page.cpp
        "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/KrxChart.cpp"
        "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/KrxChart.h"
        "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/KrxChartToolbar.cpp"
        "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/KrxChartToolbar.h"
        "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartBricks.cpp"
        "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
        "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
        "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
        "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
        "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
        "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
        "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
        "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui/theme/Theme.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui/theme/ThemeManager.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui/theme/ThemeManager.h"
        "${PROJECT_SOURCE_DIR}/src/core/logging/Logger.cpp")
    target_link_libraries(tst_krx_chart_page PRIVATE Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel)
    target_compile_definitions(tst_krx_chart_page PRIVATE HAS_QT_WEBENGINE)
    # The page, as the app bundles it (fincept-qt/CMakeLists.txt, "krx_chart").
    qt6_add_resources(tst_krx_chart_page "krx_chart_page"
        PREFIX "/krx/chart"
        BASE "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/chart"
        FILES
            "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/chart/index.html"
            "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/chart/lightweight-charts.standalone.production.js")
endif()

# src/screens/krx_quant/KrxChartToolbar.{h,cpp} — the row above the KRX chart: Cmd+D (Ctrl+D) opens its
# drawing menu and a letter in it picks a tool (codeyoma/krx-quant#241). It needs Widgets and Qt's
# shortcuts, so it opens a window and skips itself offscreen or where no window becomes active.
fincept_add_test(tst_krx_chart_toolbar
    tst_krx_chart_toolbar.cpp
    "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/KrxChartToolbar.cpp"
    "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/KrxChartToolbar.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartBricks.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme/Theme.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme/ThemeManager.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme/ThemeManager.h"
    "${PROJECT_SOURCE_DIR}/src/core/logging/Logger.cpp")
target_link_libraries(tst_krx_chart_toolbar PRIVATE Qt6::Widgets)

# src/services/krx_quant/DashboardView.{h,cpp} — what the dashboard's KRX widgets show, from the
# engine's answers (codeyoma/krx-quant#302). Qt Core only.
fincept_add_test(tst_krx_dashboard_view
    tst_krx_dashboard_view.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/DashboardView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KindView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/StatsView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp")

# src/services/krx_quant/BriefingView.{h,cpp} and KrxBriefingLive.{h,cpp} — what the KRX pre-open
# briefing screen (codeyoma/krx-quant#304) reads of GET /v1/briefing, what it shows of it and when it
# reads it, the last against a stand-in engine on this computer (Qt Network; the settings are a
# temporary file's: the test defines AppConfig).
fincept_add_test(tst_krx_briefing
    tst_krx_briefing.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/BriefingView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxBriefingLive.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxBriefingLive.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KindView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/BarView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/StatsView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.h"
    "${PROJECT_SOURCE_DIR}/src/core/logging/Logger.cpp")
target_link_libraries(tst_krx_briefing PRIVATE Qt6::Network)

# src/services/krx_quant/FlowView.{h,cpp} and KrxFlowLive.{h,cpp} — what the KRX flow screen
# (codeyoma/krx-quant#306) reads of GET /v1/symbols/{symbol}/flow, what it draws and shows of it and
# when it reads it, the last against a stand-in engine on this computer (Qt Network; the settings are
# a temporary file's: the test defines AppConfig).
fincept_add_test(tst_krx_flow
    tst_krx_flow.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/FlowView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxFlowLive.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxFlowLive.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KindView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/BarView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/StatsView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.h"
    "${PROJECT_SOURCE_DIR}/src/core/logging/Logger.cpp")
target_link_libraries(tst_krx_flow PRIVATE Qt6::Network)

# src/services/krx_quant/CostView.{h,cpp} and KrxCostLive.{h,cpp} — what the KRX horizon and cost
# screen (codeyoma/krx-quant#307) reads of GET /v1/cost-map, what it shows of it and when it reads it,
# the last against a stand-in engine on this computer (Qt Network; the settings are a temporary file's:
# the test defines AppConfig).
fincept_add_test(tst_krx_cost
    tst_krx_cost.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/CostView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxCostLive.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxCostLive.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KindView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/BarView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/StatsView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.h"
    "${PROJECT_SOURCE_DIR}/src/core/logging/Logger.cpp")
target_link_libraries(tst_krx_cost PRIVATE Qt6::Network)

# src/services/krx_quant/ClosingView.{h,cpp} and KrxClosingLive.{h,cpp} — what the KRX closing flow
# screen (codeyoma/krx-quant#305) reads of GET /v1/symbols/{symbol}/closing-flow and …/closing-flow/days,
# what it shows of them and when it reads them, the last against a stand-in engine on this computer
# (Qt Network; the settings are a temporary file's: the test defines AppConfig).
fincept_add_test(tst_krx_closing
    tst_krx_closing.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ClosingView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxClosingLive.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxClosingLive.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KindView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/BarView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/StatsView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.h"
    "${PROJECT_SOURCE_DIR}/src/core/logging/Logger.cpp")
target_link_libraries(tst_krx_closing PRIVATE Qt6::Network)

# src/services/krx_quant/TraderView.{h,cpp} and KrxTraderLive.{h,cpp} — what the KRX auto trader screen
# (codeyoma/krx-quant#397) reads of GET /v1/trader, /v1/trader/days and /v1/trader/trades, what it shows of
# them, when it reads them, and that its pause (POST /v1/trader/pause) is sent as JSON only after a yes, the
# last against a stand-in engine on this computer (Qt Network; the settings are a temporary file's: the test
# defines AppConfig).
fincept_add_test(tst_krx_trader
    tst_krx_trader.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/TraderView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxTraderLive.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxTraderLive.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KindView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/BarView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/StatsView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.h"
    "${PROJECT_SOURCE_DIR}/src/core/logging/Logger.cpp")
target_link_libraries(tst_krx_trader PRIVATE Qt6::Network)

# src/screens/krx_quant/KrxTraderScreen.{h,cpp} — the KRX auto trader screen itself (codeyoma/krx-quant#397)
# against a stand-in engine on this computer: a poll leaves its trades table where the reader is
# (krx_tables::refill), and it has the two pause buttons and no resume button. Widgets and Qt Network; it
# runs on the offscreen platform too (the settings are a temporary file's: the test defines AppConfig).
fincept_add_test(tst_krx_trader_screen
    tst_krx_trader_screen.cpp
    "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/KrxTraderScreen.cpp"
    "${PROJECT_SOURCE_DIR}/src/screens/krx_quant/KrxTraderScreen.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/TraderView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxTraderLive.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxTraderLive.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KrxQuantService.h"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/KindView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/BarView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartSlots.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartDrawings.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartLook.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartStudies.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/ChartModel.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Signals.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/Board.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/StatsView.cpp"
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/EngineData.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/tables/DataTable.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme/Theme.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme/ThemeManager.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme/ThemeManager.h"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.cpp"
    "${PROJECT_SOURCE_DIR}/src/network/http/HttpClient.h"
    "${PROJECT_SOURCE_DIR}/src/core/logging/Logger.cpp")
target_link_libraries(tst_krx_trader_screen PRIVATE Qt6::Widgets Qt6::Network)

# Upstream v4.5.1 made src/trading/OrderValidator.cpp read the risk limits through
# SettingsRepository (Qt Sql and the app database), but tst_order_validator below in
# tests/CMakeLists.txt still links Qt Core and that one source, so it does not compile.
# Upstream builds no tests in CI; this fork's release build builds and runs every suite.
# Leave the suite out (not built, ctest reports it as disabled) instead of editing the
# upstream registration, until upstream fixes it (codeyoma/FinceptTerminal#36). Deferred
# because the target is created after this file is included.
function(krx_skip_broken_upstream_suites)
    if(TARGET tst_order_validator)
        set_target_properties(tst_order_validator PROPERTIES EXCLUDE_FROM_ALL TRUE)
        set_tests_properties(tst_order_validator PROPERTIES DISABLED TRUE)
    endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" CALL krx_skip_broken_upstream_suites)
