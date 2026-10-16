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
/// day, week, month and year buttons, the chart style's menu, the indicators button (#222: it
/// asks the screen to open the indicators panel) and the drawing menu (#224: the twelve tools and
/// the magnet), and at the right end the trash (#224: the symbol's drawings, all of them) and the
/// options menu with the day separators, the special situations' bands (#236) and, while a style
/// without a time axis is shown (#225), its size. It only shows what the screen says and tells it
/// what was picked; the screen keeps the choices. In a narrow panel the indicators and drawing
/// buttons lose their labels. Cmd+D (Ctrl+D on Windows) opens the drawing menu and a letter in it
/// picks a tool (#241).
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
    /// The size of the style shown when it has no time axis (krx-quant #225): the one the symbol set
    /// (``kept``; none: the default) and ATR(14) of the view's bars (``atr``; none: not counted yet),
    /// in the options menu below the day separators. Hidden for a style with a time axis.
    void show_brick_size(std::optional<double> kept, std::optional<double> atr);
    /// The drawing tool chosen (krx-quant #224): the drawing button filled with its icon; none:
    /// the cursor.
    void show_tool(std::optional<krx_quant::DrawingTool> tool);
    void show_magnet(bool on);
    /// The symbol's drawings: the trash is off without any, and on bricks (krx-quant #225), where they
    /// are hidden.
    void show_drawings(int count);
    /// Where the indicators panel opens: under the indicators button (global).
    QPoint indicators_corner() const;
    /// Cmd+D (Ctrl+D on Windows) with the keyboard anywhere in ``area`` (the screen) opens the
    /// drawing menu as a click on its button does, or closes it (krx-quant #241). Not while the
    /// button is off (a style without a time axis).
    void take_keys(QWidget* area);
    /// Paint in the theme's colors now.
    void restyle();
    QSize sizeHint() const override;

  signals:
    void view_chosen(int view);
    void style_chosen(krx_quant::ChartStyle style);
    void day_separator_toggled(bool on);
    /// The options menu's `특수 상황 표시` (krx-quant #236): the special situations' bands on or off.
    void special_toggled(bool on);
    /// A size set for ``style``, a style without a time axis (krx-quant #225), in the options menu:
    /// won, or the line break's lines; 0: back to its default (ATR(14), 3 lines).
    void brick_size_chosen(krx_quant::ChartStyle style, double size);
    /// The indicators button was pressed (krx-quant #222).
    void indicators_requested();
    /// A drawing tool was picked from the drawing menu (krx-quant #224).
    void tool_chosen(krx_quant::DrawingTool tool);
    void magnet_toggled(bool on);
    /// The trash was pressed: the screen asks before it removes the symbol's drawings.
    void trash_requested();

  protected:
    void resizeEvent(QResizeEvent* event) override;
    /// A letter in the open drawing menu (krx-quant #241, krx_quant::tool_letter, kMagnetLetter)
    /// closes it and picks that tool or turns the magnet, as a click on its item does.
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    /// The drawing menu opened under its button, or closed while it is open.
    void toggle_drawing_menu();
    /// The indicators and drawing buttons with their labels while the row fits, else icons only.
    void fit_labels();
    /// The left group from the left end, cut off where the right group (trash, options) starts.
    void place();
    void paint_icons();
    /// The day separators' item and the special situations' (krx-quant #236): on in a minute view
    /// with a time axis.
    void show_day_separator_state();

    QWidget* left_clip_ = nullptr;   ///< clips the left group
    QWidget* left_ = nullptr;        ///< the views, the style, the indicators and drawing buttons
    QWidget* right_ = nullptr;       ///< the trash and the options
    QToolButton* minutes_ = nullptr; ///< the minute views' drop-down, labelled with the last one picked
    QMenu* minutes_menu_ = nullptr;
    QList<QPushButton*> candle_buttons_; ///< 일 주 월 년 (property "view")
    QToolButton* style_ = nullptr;       ///< the chart style's menu, showing the style's icon
    QMenu* style_menu_ = nullptr;
    QToolButton* indicators_ = nullptr;
    QToolButton* drawing_ = nullptr; ///< the drawing menu, filled while a tool is chosen
    QMenu* drawing_menu_ = nullptr;
    QAction* magnet_ = nullptr;
    QToolButton* trash_ = nullptr;
    QToolButton* options_ = nullptr;
    QAction* day_separator_ = nullptr;
    QAction* special_ = nullptr;         ///< the special situations' bands (krx-quant #236)
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
    std::optional<krx_quant::DrawingTool> tool_shown_;
    int drawings_shown_ = 0; ///< the symbol's drawings, as the screen last said
    int wide_ = 0;           ///< the row's width with the labels
    bool narrow_ = false;
};

} // namespace fincept::screens
