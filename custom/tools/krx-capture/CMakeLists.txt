# Fork addition (custom/README.md, "화면 캡처"): renders the KRX screen against an engine,
# such as `krx-quant demo-engine`, and saves it as a PNG for the screen PRs. A project of
# its own, so nothing here touches the app's build; it compiles the screen's sources from
# fincept-qt/src with the same Qt.
#
#   cmake -S custom/tools/krx-capture -B fincept-qt/build/krx-capture -G Ninja \
#         -DCMAKE_PREFIX_PATH=<Qt 6.8.3 for the platform>
#   cmake --build fincept-qt/build/krx-capture
cmake_minimum_required(VERSION 3.21)
project(krx_capture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(Qt6 QUIET COMPONENTS WebSockets)
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets WebChannel)

# macOS 14+ has no AGL framework, which Qt's WrapOpenGL still names: drop it, as the app's
# CMakeLists.txt does.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_gl_libs)
        list(FILTER _gl_libs EXCLUDE REGEX "AGL")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_gl_libs}")
    endif()
endif()

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../fincept-qt/src")

add_executable(krx_capture
    main.cpp
    AppConfigIni.cpp
    "${SRC}/core/logging/Logger.cpp"
    "${SRC}/network/http/HttpClient.cpp"
    "${SRC}/screens/krx_quant/KrxAlerts.cpp"
    "${SRC}/screens/krx_quant/KrxChart.cpp"
    "${SRC}/screens/krx_quant/KrxLeaderboard.cpp"
    "${SRC}/screens/krx_quant/KrxQuantScreen.cpp"
    "${SRC}/screens/krx_quant/KrxStatsWindow.cpp"
    "${SRC}/services/krx_quant/Board.cpp"
    "${SRC}/services/krx_quant/ChartModel.cpp"
    "${SRC}/services/krx_quant/EngineData.cpp"
    "${SRC}/services/krx_quant/KindView.cpp"
    "${SRC}/services/krx_quant/KrxLive.cpp"
    "${SRC}/services/krx_quant/KrxQuantService.cpp"
    "${SRC}/services/krx_quant/KrxStream.cpp"
    "${SRC}/services/krx_quant/Signals.cpp"
    "${SRC}/services/krx_quant/StatsView.cpp"
    "${SRC}/ui/notifications/NotificationService.cpp"
    "${SRC}/ui/tables/DataTable.cpp"
    "${SRC}/ui/theme/Theme.cpp"
    "${SRC}/ui/theme/ThemeManager.cpp"
    "${SRC}/ui/widgets/LoadingOverlay.cpp"
    # Headers with Q_OBJECT, for moc.
    "${SRC}/screens/krx_quant/KrxAlerts.h"
    "${SRC}/screens/krx_quant/KrxChart.h"
    "${SRC}/screens/krx_quant/KrxLeaderboard.h"
    "${SRC}/screens/krx_quant/KrxQuantScreen.h"
    "${SRC}/screens/krx_quant/KrxStatsWindow.h"
    "${SRC}/services/krx_quant/KrxLive.h"
    "${SRC}/services/krx_quant/KrxQuantService.h"
    "${SRC}/services/krx_quant/KrxStream.h"
    "${SRC}/ui/notifications/NotificationService.h"
    "${SRC}/ui/tables/DataTable.h"
    "${SRC}/ui/theme/ThemeManager.h"
    "${SRC}/ui/widgets/LoadingOverlay.h")
target_include_directories(krx_capture PRIVATE "${SRC}")
target_link_libraries(krx_capture PRIVATE Qt6::Widgets Qt6::Network)
# The chart page, as the app bundles it (fincept-qt/CMakeLists.txt, "krx_chart").
qt_add_resources(krx_capture "krx_chart"
    PREFIX "/krx/chart"
    BASE "${SRC}/screens/krx_quant/chart"
    FILES
        "${SRC}/screens/krx_quant/chart/index.html"
        "${SRC}/screens/krx_quant/chart/lightweight-charts.standalone.production.js")
if(Qt6WebEngineWidgets_FOUND AND Qt6WebChannel_FOUND)
    target_link_libraries(krx_capture PRIVATE Qt6::WebEngineWidgets Qt6::WebChannel)
    target_compile_definitions(krx_capture PRIVATE HAS_QT_WEBENGINE)
endif()
if(TARGET Qt6::WebSockets)
    target_link_libraries(krx_capture PRIVATE Qt6::WebSockets)
    target_compile_definitions(krx_capture PRIVATE HAS_QT_WEBSOCKETS)
endif()
