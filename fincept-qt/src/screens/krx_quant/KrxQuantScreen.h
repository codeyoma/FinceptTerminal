#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/KrxLive.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace fincept::ui {
class LoadingOverlay;
}

namespace fincept::screens {

class KrxAlerts;
class KrxChart;
class KrxLeaderboard;
class KrxStatsWindow;

/// KRX 4-minute predictions (fork addition, see custom/README.md): the prediction engine's
/// status and macro row, each symbol's latest predictions, the chosen symbol's chart with
/// the final consensus over it, the leaderboard with a predictor's detail window, and
/// signal alerts. It renders what its KrxLive says and passes on the user's choices, in
/// Fincept's panels, tables, fonts and theme. The text is Korean.
class KrxQuantScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KrxQuantScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void apply_base_url();
    void show_status();
    /// The macro row: the regime, its reasons and the USD futures (live once they trade).
    void show_macro();
    /// The header's engine dot and its tooltip (the refresh, the engine clock, what failed).
    void show_dot();
    void fill_symbols();
    void fill_predictions();
    void fill_chart();
    void open_detail(const QString& algorithm);
    void update_countdowns();
    /// Paint in the theme's colors and font now (Fincept's theme, font or density changed).
    void refresh_theme();
    /// Whether ``bar_close`` is of the engine's today (not the last session's, shown before the open).
    bool is_today(const QDateTime& bar_close) const;

    services::KrxLive* live_ = nullptr;
    krx_panel::Header header_; ///< the screen's: its title, the engine dot and address
    QLabel* dot_ = nullptr;
    QLineEdit* url_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* macro_label_ = nullptr;
    QLabel* special_label_ = nullptr;
    krx_panel::Header symbols_header_;
    QTableWidget* symbols_table_ = nullptr;
    krx_panel::Header chart_header_; ///< titled with the chosen symbol
    QComboBox* window_combo_ = nullptr;
    KrxChart* chart_ = nullptr;
    krx_panel::Header predictions_header_; ///< titled with the bar close and the countdown
    QTableWidget* predictions_table_ = nullptr;
    KrxLeaderboard* leaderboard_ = nullptr;
    KrxStatsWindow* stats_window_ = nullptr; ///< made when first asked for
    KrxAlerts* alerts_ = nullptr;
    QWidget* body_ = nullptr;
    ui::LoadingOverlay* loading_ = nullptr; ///< over the body until the engine first answers
    QTimer* clock_timer_ = nullptr;         ///< the countdowns and the engine clock, every second
};

} // namespace fincept::screens
