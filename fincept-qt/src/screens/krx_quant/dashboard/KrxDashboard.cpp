#include "screens/krx_quant/dashboard/KrxDashboard.h"

#include "screens/dashboard/canvas/DashboardTemplates.h"
#include "screens/dashboard/canvas/WidgetRegistry.h"
#include "screens/krx_quant/dashboard/KrxDashboardWidgets.h"

namespace fincept::screens::krx_dashboard {

// The names and descriptions are Korean, like the KRX screen's text: the Korean pack does not
// hold them, and translate() gives them back as they are (custom/README.md, 한국어 언어 팩).
void register_widgets(WidgetRegistry& registry) {
    // 12-column grid: default_w, default_h, min_w, min_h
    registry.register_widget({"krx_signals", QStringLiteral("KRX 지금 신호"), kCategory,
                              QStringLiteral("종목별 최종 합의와 신호, 직전 채점 결과 (KRX 화면의 신호 규칙)"), 7, 4, 4,
                              3, [](const QJsonObject&) { return new KrxSignalsWidget; }});
    registry.register_widget({"krx_leaderboard", QStringLiteral("KRX 순위표"), kCategory,
                              QStringLiteral("알고리즘 정답률과 거래일 lift 하한, 이겨야 하는 기준 예측. 톱니로 종류, "
                                             "기간, 종목"),
                              7, 5, 4, 3,
                              [](const QJsonObject& config) { return new KrxLeaderboardWidget(config); }});
    registry.register_widget({"krx_engine_status", QStringLiteral("KRX 엔진 상태"), kCategory,
                              QStringLiteral("장 단계, KIS 연결, 오늘 특수 상황, 지난밤 학습, CPU 예산"), 5, 4, 3, 3,
                              [](const QJsonObject&) { return new KrxStatusWidget; }});
    registry.register_widget({"krx_open_to_close", QStringLiteral("KRX 오늘 시가→종가"), kCategory,
                              QStringLiteral("08:50 시가→종가 예측과 장 마감 뒤 결과"), 5, 4, 3, 3,
                              [](const QJsonObject&) { return new KrxOpenToCloseWidget; }});
}

DashboardTemplate dashboard_template() {
    const auto item = [](const char* id, int x, int y, int w, int h) {
        GridItem i;
        i.id = id;
        i.cell = {x, y, w, h, 3, 3};
        return i;
    };
    return {"krx",
            QStringLiteral("KRX"),
            QStringLiteral("KRX 엔진의 지금 신호, 순위표, 엔진 상태, 오늘 시가→종가"),
            {
                item("krx_signals", 0, 0, 7, 4),
                item("krx_engine_status", 7, 0, 5, 4),
                item("krx_leaderboard", 0, 4, 7, 5),
                item("krx_open_to_close", 7, 4, 5, 5),
            }};
}

} // namespace fincept::screens::krx_dashboard
