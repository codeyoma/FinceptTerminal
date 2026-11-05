#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/TraderView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace fincept::services {
class KrxTraderLive;
}

namespace fincept::screens {

class KrxChart;

/// The days' net P&L of one mode (codeyoma/krx-quant#397): a bar a day (green a gain, red a loss) and
/// the running sum as a line, on one scale with its zero; a day where a loss limit was reached is
/// marked. Hovering a day shows its tooltip (trader::DayPoint::tip). QPainter.
class KrxTraderDaysChart : public QWidget {
    Q_OBJECT
  public:
    explicit KrxTraderDaysChart(QWidget* parent = nullptr);
    void set_points(const QList<krx_quant::trader::DayPoint>& points);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    QRectF plot() const;
    /// The day under ``x``, as an index of the points; -1 outside.
    int day_at(double x) const;

    QList<krx_quant::trader::DayPoint> points_;
    int hover_ = -1;
};

/// KRX auto trader (fork addition, codeyoma/krx-quant#397, #401, see custom/README.md): the engine's auto
/// trader of 005930 (krx-quant design.md 14장). The header has its mode (모의, KIS 모의, 실전), status
/// (도는 중, 새 진입 정지, 정지, 하루 한도, 누적 한도), champion and live gate's progress; the cards its
/// capital, cash, position (with its target, stop and the time left to its time barrier), today's and the
/// cumulative net P&L and the loss limits' use; then 005930's 1-minute chart with the trader's buys and
/// sells (codeyoma/krx-quant#537: KrxChart, its candles and those marks alone, and their hover lines; only a
/// fit button over it, and the switch of the hollow marks of a book's calls that traded nothing,
/// codeyoma/krx-quant#601), a book's decisions of the day (the practice's first) with their totals by
/// reason, each day's net P&L, the two pause buttons ("새 진입
/// 정지", "정지하고 즉시 매도", each behind a confirm window) with the recent requests, the trades table
/// (the champion's, or a policy version's chosen above it), and the policy versions (champion,
/// challengers, waiting, former, dropped: their values, paper records and standing) with the gate in
/// full and the champion's history. There is no resume button: resuming is the server's alone, and the
/// screen says so. It draws what its KrxTraderLive holds, in Fincept's panels, tables, fonts and theme.
/// The text is Korean.
class KrxTraderScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KrxTraderScreen(QWidget* parent = nullptr);

    /// The pause buttons' object names (krx-capture presses one to show its confirm window).
    static constexpr const char* kPauseEntriesName = "krxTraderPauseEntries";
    static constexpr const char* kPauseSellName = "krxTraderPauseSell";
    /// The tables' object names (tests find them).
    static constexpr const char* kTradesName = "krxTraderTrades";
    static constexpr const char* kControlsName = "krxTraderControls";
    static constexpr const char* kPoliciesName = "krxTraderPolicies";
    static constexpr const char* kHistoryName = "krxTraderHistory";
    /// The trades' policy choice (codeyoma/krx-quant#401): krx-capture and the tests choose in it.
    static constexpr const char* kTradesPolicyName = "krxTraderTradesPolicy";
    /// The champion and the live gate in the header, and the gate in full over the policy table.
    static constexpr const char* kGateName = "krxTraderGate";
    static constexpr const char* kGateLineName = "krxTraderGateLine";
    /// The challengers' cohort over the policy table (codeyoma/krx-quant#473).
    static constexpr const char* kCohortLineName = "krxTraderCohortLine";
    /// "연습" beside the mode badge while the mock book practises (codeyoma/krx-quant#427).
    static constexpr const char* kPracticeName = "krxTraderPractice";
    /// The 1-minute chart and its header's "읽지 못함" (codeyoma/krx-quant#537).
    static constexpr const char* kBarsChartName = "krxTraderBarsChart";
    static constexpr const char* kBarsFailureName = "krxTraderBarsFailure";
    /// The chart's switch of the hollow marks of the calls that traded nothing (codeyoma/krx-quant#601).
    static constexpr const char* kSkipsName = "krxTraderSkips";
    /// The decisions panel (codeyoma/krx-quant#601): its book choice, header line, "읽지 못함", totals and table.
    static constexpr const char* kDecisionsBookName = "krxTraderDecisionsBook";
    static constexpr const char* kDecisionsLineName = "krxTraderDecisionsLine";
    static constexpr const char* kDecisionsFailureName = "krxTraderDecisionsFailure";
    static constexpr const char* kDecisionsTotalsName = "krxTraderDecisionsTotals";
    static constexpr const char* kDecisionsName = "krxTraderDecisions";

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    /// A card's widgets.
    struct CardView {
        QWidget* box = nullptr;
        QLabel* title = nullptr;
        QLabel* value = nullptr;
        QLabel* lines = nullptr;
    };

    void refresh_theme();
    void render();
    void show_dot();
    void fill_header();
    void fill_cards();
    /// The 1-minute chart: the bars and the trader's marks held, and why a read of them failed; while
    /// shown, the hollow marks of the book's calls that traded nothing (codeyoma/krx-quant#601).
    void fill_bars();
    /// The decisions panel (codeyoma/krx-quant#601): the book's decisions of the day and their totals.
    void fill_decisions();
    void fill_days();
    void fill_pause();
    void fill_trades();
    void fill_policies();
    void ask_pause(bool sell_now);

    services::KrxTraderLive* live_ = nullptr;
    QTimer* clock_timer_ = nullptr;
    krx_panel::Header header_;
    QLabel* dot_ = nullptr;
    QLabel* mode_badge_ = nullptr;
    QLabel* practice_badge_ = nullptr; ///< "연습" beside the mode while the mock book practises (#427)
    QLabel* status_badge_ = nullptr;
    QLabel* header_line_ = nullptr;
    QLabel* gate_ = nullptr; ///< the champion and the live gate's progress (codeyoma/krx-quant#400)
    QPushButton* reload_ = nullptr;
    QLabel* failure_ = nullptr;
    QList<CardView> cards_;
    krx_panel::Header bars_header_;
    QLabel* bars_failure_ = nullptr;
    KrxChart* bars_chart_ = nullptr;
    QCheckBox* skips_ = nullptr; ///< the hollow marks shown (codeyoma/krx-quant#601)
    krx_panel::Header decisions_header_;
    QLabel* decisions_failure_ = nullptr;
    QLabel* decisions_line_ = nullptr;
    QComboBox* book_box_ = nullptr;
    QLabel* totals_ = nullptr;
    QTableWidget* decisions_ = nullptr;
    krx_panel::Header days_header_;
    QComboBox* mode_box_ = nullptr;
    QLabel* days_line_ = nullptr;
    KrxTraderDaysChart* chart_ = nullptr;
    QString mode_; ///< the mode the curve shows; empty: the default one
    krx_panel::Header pause_header_;
    QPushButton* pause_entries_ = nullptr;
    QPushButton* pause_sell_ = nullptr;
    QLabel* resume_note_ = nullptr;
    QLabel* pause_note_ = nullptr;
    QTableWidget* controls_ = nullptr;
    krx_panel::Header trades_header_;
    QComboBox* policy_box_ = nullptr; ///< whose trades (codeyoma/krx-quant#401)
    QList<krx_quant::trader::PolicyChoice> choices_; ///< what policy_box_ lists now
    QLabel* trades_line_ = nullptr;
    QTableWidget* trades_ = nullptr;
    krx_panel::Header policies_header_;
    QLabel* policies_line_ = nullptr;
    QLabel* gate_line_ = nullptr;
    QLabel* cohort_line_ = nullptr; ///< codeyoma/krx-quant#473
    QTableWidget* policies_ = nullptr;
    QTableWidget* history_ = nullptr;
};

} // namespace fincept::screens
