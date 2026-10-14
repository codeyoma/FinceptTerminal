#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/KrxLive.h"

#include <QComboBox>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

#include <optional>
#include <utility>

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
    /// The table while a bar close is pinned (a chart icon clicked).
    void fill_pinned();
    /// The panel of the kind shown besides the direction (krx-quant #182): its latest bar close's
    /// predictions, or the pinned one's with their results.
    void fill_kind();
    /// The kind selector's buttons as the engine's kinds and the kind shown say (krx-quant #182).
    void show_kinds();
    /// Another kind is shown: the panel, the leaderboard and the chart's hover boxes follow.
    void on_kind_changed();
    /// The kind panel's title: the symbol, the bar close, the countdown or the answer, k and 2k.
    void kind_title();
    void fill_chart();
    /// The selected consensus's score over the bars in view, on the chart's header (krx-quant #159).
    void show_score();
    /// Show the chart as bars of ``minutes`` (krx-quant #109), and keep the choice.
    void set_view(int minutes);
    void open_detail(const QString& algorithm);
    /// What the engine says about ``algorithm`` (krx-quant #163); none for a baseline or a
    /// consensus, or before it answered.
    /// Why the description of ``algorithm`` could not be read; empty for another kind's baseline,
    /// described by GET /v1/kinds whatever /v1/algorithms answered (krx-quant #182).
    QString about_failure(const QString& algorithm) const;
    std::optional<krx_quant::AlgorithmInfo> about_of(const QString& algorithm) const;
    void update_countdowns();
    /// Paint in the theme's colors and font now (Fincept's theme, font or density changed).
    void refresh_theme();
    /// Whether ``bar_close`` is of the engine's today (not the last session's, shown before the open).
    bool is_today(const QDateTime& bar_close) const;

    services::KrxLive* live_ = nullptr;
    krx_panel::Header header_; ///< the screen's: its title, the engine dot and address
    QLabel* dot_ = nullptr;
    QWidget* kind_bar_ = nullptr;      ///< the kind selector, after the title (krx-quant #182)
    QList<QPushButton*> kind_buttons_; ///< 방향, 장벽, 변동폭 (property "kind")
    QLineEdit* url_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* macro_label_ = nullptr;
    QLabel* special_label_ = nullptr;
    krx_panel::Header symbols_header_;
    QTableWidget* symbols_table_ = nullptr;
    krx_panel::Header chart_header_;             ///< titled with the chosen symbol
    QLabel* chart_failure_ = nullptr;            ///< on the chart's header, while the bars cannot be read
    QLabel* chart_score_ = nullptr;              ///< on the chart's header: the selected consensus's score
    QTimer* chart_soon_ = nullptr;               ///< draws the chart once the chosen symbol's predictions settle
    std::pair<qint64, qint64> view_range_{0, 0}; ///< the bars in view, by time (chart_time)
    KrxChart* chart_ = nullptr;
    QWidget* chart_views_ = nullptr;       ///< the row of view buttons above the chart
    QList<QPushButton*> view_buttons_;     ///< the minute views', then the day view's (property "view")
    int minutes_ = 1;                      ///< the chart's view
    krx_panel::Header predictions_header_; ///< titled with the bar close and the countdown
    QTableWidget* predictions_table_ = nullptr;
    QPushButton* latest_ = nullptr; ///< back to the latest bar close, while one is pinned
    KrxLeaderboard* leaderboard_ = nullptr;
    QHash<QString, KrxStatsWindow*> stats_windows_; ///< open, by predictor
    KrxAlerts* alerts_ = nullptr;
    QWidget* body_ = nullptr;
    ui::LoadingOverlay* loading_ = nullptr; ///< over the body until the engine first answers
    QTimer* clock_timer_ = nullptr;         ///< the countdowns and the engine clock, every second
};

} // namespace fincept::screens
