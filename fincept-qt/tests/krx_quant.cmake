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

# src/services/krx_quant/AppRelease.{h,cpp} — which personal build is newer, and the newest one
# on the fork's Releases page, for the app's one-click update. Qt Core only.
fincept_add_test(tst_krx_app_release
    tst_krx_app_release.cpp
    "${PROJECT_SOURCE_DIR}/src/services/krx_quant/AppRelease.cpp")
