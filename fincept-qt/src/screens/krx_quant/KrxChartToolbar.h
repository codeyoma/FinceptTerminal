#pragma once

#include "services/krx_quant/ChartBricks.h"
#include "services/krx_quant/ChartLook.h"

#include <QList>
#include <QWidget>

#include <optional>

class QAction;
class QCheckBox;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class QToolButton;
class QWidgetAction;

namespace fincept::screens {

/// The row above the KRX chart (krx-quant #220, #221): the minute views in a drop-down, the
/// day, week, month and year buttons, the chart style's menu and the indicators button (#222: it
/// asks the screen to open the indicators panel), and at the right end the SMT divergences' toggle
/// (codeyoma/krx-quant#522) and the options menu with the day separators, the special situations'
/// bands (#236) and, while a style without a time axis is shown (#225), its size. It only shows
/// what the screen says and tells it what was picked; the screen keeps the choices. In a narrow
/// panel the indicators button loses its label.
class KrxChartToolbar : public QWidget {
    Q_OBJECT
  public:
    explicit KrxChartToolbar(QWidget* parent = nullptr);

    /// The view shown: a minute view checks the drop-down (labelled with it), a candle view its button.
    void show_view(int view);
    void show_style(krx_quant::ChartStyle style);
    void show_day_separator(bool on);
    /// Whether the special situations' bands are shown (krx-quant #236): the options menu's check.
    void show_special(bool on);
    /// Whether the SMT divergences are shown (codeyoma/krx-quant#522): the SMT button filled while on, in
    /// the 1-minute view with a time axis (off and dimmed in the others, which draw none).
    void show_smt(bool on);
    /// The size of the style shown when it has no time axis (krx-quant #225): the one the symbol set
    /// (``kept``; none: the default) and ATR(14) of the view's bars (``atr``; none: not counted yet),
    /// in the options menu below the day separators. Hidden for a style with a time axis.
    void show_brick_size(std::optional<double> kept, std::optional<double> atr);
    /// Where the indicators panel opens: under the indicators button (global).
    QPoint indicators_corner() const;
    /// Paint in the theme's colors now.
    void restyle();
    QSize sizeHint() const override;

  signals:
    void view_chosen(int view);
    void style_chosen(krx_quant::ChartStyle style);
    void day_separator_toggled(bool on);
    /// The options menu's `특수 상황 표시` (krx-quant #236): the special situations' bands on or off.
    void special_toggled(bool on);
    /// The SMT button (codeyoma/krx-quant#522): the SMT divergences on or off.
    void smt_toggled(bool on);
    /// A size set for ``style``, a style without a time axis (krx-quant #225), in the options menu:
    /// won, or the line break's lines; 0: back to its default (ATR(14), 3 lines).
    void brick_size_chosen(krx_quant::ChartStyle style, double size);
    /// The indicators button was pressed (krx-quant #222).
    void indicators_requested();

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    /// The indicators button with its label while the row fits, else its icon only.
    void fit_labels();
    /// The indicators button's label shown, or its icon only.
    void show_labels(bool on);
    /// The left group from the left end, cut off where the right group (SMT, options) starts.
    void place();
    void paint_icons();
    /// The day separators' item and the special situations' (krx-quant #236): on in a minute view
    /// with a time axis. The SMT button (codeyoma/krx-quant#522): on in the 1-minute view with one.
    void show_day_separator_state();

    QWidget* left_clip_ = nullptr;   ///< clips the left group
    QWidget* left_ = nullptr;        ///< the views, the style and the indicators button
    QWidget* right_ = nullptr;       ///< the SMT toggle and the options
    QToolButton* minutes_ = nullptr; ///< the minute views' drop-down, labelled with the last one picked
    QMenu* minutes_menu_ = nullptr;
    QList<QPushButton*> candle_buttons_; ///< 일 주 월 년 (property "view")
    QToolButton* style_ = nullptr;       ///< the chart style's menu, showing the style's icon
    QMenu* style_menu_ = nullptr;
    QToolButton* indicators_ = nullptr;
    QToolButton* options_ = nullptr;
    QAction* day_separator_ = nullptr;
    QAction* special_ = nullptr;         ///< the special situations' bands (krx-quant #236)
    QToolButton* smt_ = nullptr;         ///< the SMT divergences (codeyoma/krx-quant#522), checked while on
    QAction* brick_divider_ = nullptr;   ///< above the size, while a style without a time axis is shown
    QWidgetAction* brick_row_ = nullptr; ///< its size (krx-quant #225)
    QLabel* brick_title_ = nullptr;
    QSpinBox* brick_size_ = nullptr;
    QCheckBox* brick_atr_ = nullptr;   ///< the default, ATR(14); not for the line break
    std::optional<double> brick_kept_; ///< as the screen last said
    std::optional<double> brick_atr_value_;
    int view_ = 1;
    int last_minutes_ = 1; ///< the minute view the drop-down names
    krx_quant::ChartStyle style_shown_ = krx_quant::ChartStyle::Candle;
    int wide_ = 0; ///< the row's width with the labels
    bool narrow_ = false;
};

} // namespace fincept::screens
