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
    FINCEPT_REFERENCE_CATALOG="${PROJECT_SOURCE_DIR}/translations/fincept_zh_CN.ts")
qt6_add_resources(tst_krx_korean "krx_korean_i18n"
    PREFIX "/i18n"
    BASE "${PROJECT_BINARY_DIR}"
    FILES ${FINCEPT_QM_FILES})
