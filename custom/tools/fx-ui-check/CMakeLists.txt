cmake_minimum_required(VERSION 3.21)
project(base_widget_check LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOGEN_PARALLEL 1)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Widgets Sql)
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    list(FILTER _gl_libs EXCLUDE REGEX "AGL")
    set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_gl_libs}")
endif()
get_filename_component(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../fincept-qt/src" ABSOLUTE)
add_executable(base_widget_check main.cpp market_signal_stub.cpp context_menu_stub.cpp
    "${SRC}/services/backtesting/BacktestingService.h"
    "${SRC}/screens/dashboard/widgets/BaseWidget.cpp"
    "${SRC}/screens/dashboard/widgets/BaseWidget.h"
    "${SRC}/screens/dashboard/widgets/LoadingOverlay.cpp"
    "${SRC}/services/markets/MarketDataService.h"
    "${SRC}/ui/theme/Theme.cpp"
    "${SRC}/ui/theme/ThemeManager.cpp"
    "${SRC}/core/logging/Logger.cpp"
    # Upstream v4.5.1: BaseWidget publishes navigation on the bus and leaves the hub
    # on hide. Both depend on the logger only, so the real ones are linked.
    "${SRC}/core/events/EventBus.cpp"
    "${SRC}/datahub/DataHub.cpp")
target_include_directories(base_widget_check PRIVATE "${SRC}")
# Qt Sql only for the repository headers BaseWidget includes (upstream v4.5.1).
target_link_libraries(base_widget_check PRIVATE Qt6::Widgets Qt6::Sql)
