#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/TraderView.h"

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

/// KRX auto trader (fork addition, codeyoma/krx-quant#397, see custom/README.md): the engine's auto
/// trader of 005930 (krx-quant design.md 14장). The header has its mode (모의, KIS 모의, 실전) and status
/// (도는 중, 새 진입 정지, 정지, 하루 한도, 누적 한도); the cards its capital, cash, position (with its
/// target, stop and the time left to its time barrier), today's and the cumulative net P&L and the
/// loss limits' use; then each day's net P&L, the two pause buttons ("새 진입 정지", "정지하고 즉시
/// 매도", each behind a confirm window) with the recent requests, and the trades table. There is no
/// resume button: resuming is the server's alone, and the screen says so. It draws what its
/// KrxTraderLive holds, in Fincept's panels, tables, fonts and theme. The text is Korean.
///
/// Room is left under the trades for the policy versions and the live gate's progress
/// (codeyoma/krx-quant#401).
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
    void fill_days();
    void fill_pause();
    void fill_trades();
    void ask_pause(bool sell_now);

    services::KrxTraderLive* live_ = nullptr;
    QTimer* clock_timer_ = nullptr;
    krx_panel::Header header_;
    QLabel* dot_ = nullptr;
    QLabel* mode_badge_ = nullptr;
    QLabel* status_badge_ = nullptr;
    QLabel* header_line_ = nullptr;
    QPushButton* reload_ = nullptr;
    QLabel* failure_ = nullptr;
    QList<CardView> cards_;
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
    QLabel* trades_line_ = nullptr;
    QTableWidget* trades_ = nullptr;
};

} // namespace fincept::screens
