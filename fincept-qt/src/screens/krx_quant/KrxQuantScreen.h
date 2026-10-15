#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/ChartLook.h"
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
class KrxChartToolbar;
class KrxDrawingPopup;
class KrxLeaderboard;
class KrxStatsWindow;
class KrxStudyPanel;

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
    /// Show the chart as bars of ``minutes`` (krx-quant #109), and keep the choice for the symbol
    /// chosen (krx-quant #221).
    void set_view(int minutes);
    /// Draw the chart in ``style`` (krx-quant #221), and keep the choice for the symbol chosen. A
    /// style without a time axis (krx-quant #225) puts the drawing tool down and reads the bars its
    /// bricks start with; going to or from one fits the chart again.
    void set_style(krx_quant::ChartStyle style);
    /// The options menu's size for the style shown when it has no time axis (krx-quant #225).
    void show_brick_size();
    /// ``size`` set for ``style`` in the options menu (0: its default), kept for the symbol chosen.
    void set_brick_size(krx_quant::ChartStyle style, double size);
    /// The symbol chosen now: its chart's view and style as it kept them (krx-quant #221), its
    /// indicators and the anchored VWAP's anchor (krx-quant #222), and its drawings (krx-quant #224).
    void show_symbol_prefs();
    /// Draw the chart's indicators as ``set`` says (krx-quant #222), and keep them for the symbol
    /// chosen. The anchored VWAP turned on without an anchor waits for a click on the chart.
    void set_studies(const krx_quant::StudySet& set);
    /// Open the indicators panel under its button, ``study`` picked (none: as it was).
    void open_studies(std::optional<krx_quant::Study> study = std::nullopt);
    /// The anchored VWAP's bar was chosen at ``time`` (chart_time): kept for the symbol as a moment.
    void set_anchor(qint64 time);
    /// Tell KrxLive how many bars the indicators want, and back to the anchor (krx-quant #222).
    void want_history();
    /// The symbol's drawings changed (krx-quant #224): kept for it, drawn (``select``: the one to
    /// select, as KrxChart::set_drawings), and the trash on while there are any.
    void drawings_changed(int select = 0);
    /// The drawing ``id`` gone (the trash, Delete, the popup's `삭제`, a text left empty).
    void remove_drawing(int id);
    /// A drawing tool chosen (krx-quant #224); none: back at the cursor.
    void set_tool(std::optional<krx_quant::DrawingTool> tool);
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
    /// The slot of the engine's clock the chart was last drawn at (krx-quant #238, krx_quant::clock_slot).
    std::optional<qint64> clock_slot_drawn_;
    KrxChart* chart_ = nullptr;
    KrxChartToolbar* chart_toolbar_ = nullptr; ///< the row above the chart (krx-quant #221)
    int minutes_ = 1;                          ///< the chart's view
    krx_quant::ChartLook look_;                ///< its style, day separators (#221), indicators (#222), drawings (#224)
    std::optional<double> brick_atr_;          ///< ATR(14) the chart last counted for the bricks' default (#225)
    KrxStudyPanel* study_panel_ = nullptr;     ///< the indicators panel (krx-quant #222)
    KrxDrawingPopup* drawing_popup_ = nullptr; ///< a drawing's colour, width, line style and text (krx-quant #224)
    krx_panel::Header predictions_header_;     ///< titled with the bar close and the countdown
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
