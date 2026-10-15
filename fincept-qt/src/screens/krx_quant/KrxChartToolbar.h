#pragma once

#include "services/krx_quant/ChartLook.h"

#include <QList>
#include <QWidget>

#include <optional>

class QAction;
class QMenu;
class QPushButton;
class QToolButton;

namespace fincept::screens {

/// The row above the KRX chart (krx-quant #220, #221): the minute views in a drop-down, the
/// day, week, month and year buttons, the chart style's menu, the indicators button (#222: it
/// asks the screen to open the indicators panel) and the drawing menu (#224: the twelve tools and
/// the magnet), and at the right end the trash (#224: the symbol's drawings, all of them) and the
/// options menu with the day separators. It only shows what the screen says and tells it what was
/// picked; the screen keeps the choices. In a narrow panel the indicators and drawing buttons lose
/// their labels.
class KrxChartToolbar : public QWidget {
    Q_OBJECT
  public:
    explicit KrxChartToolbar(QWidget* parent = nullptr);

    /// The view shown: a minute view checks the drop-down (labelled with it), a candle view its button.
    void show_view(int view);
    void show_style(krx_quant::ChartStyle style);
    void show_day_separator(bool on);
    /// The drawing tool chosen (krx-quant #224): the drawing button filled with its icon; none:
    /// the cursor.
    void show_tool(std::optional<krx_quant::DrawingTool> tool);
    void show_magnet(bool on);
    /// The symbol's drawings: the trash is off without any.
    void show_drawings(int count);
    /// Where the indicators panel opens: under the indicators button (global).
    QPoint indicators_corner() const;
    /// Paint in the theme's colors now.
    void restyle();
    QSize sizeHint() const override;

  signals:
    void view_chosen(int view);
    void style_chosen(krx_quant::ChartStyle style);
    void day_separator_toggled(bool on);
    /// The indicators button was pressed (krx-quant #222).
    void indicators_requested();
    /// A drawing tool was picked from the drawing menu (krx-quant #224).
    void tool_chosen(krx_quant::DrawingTool tool);
    void magnet_toggled(bool on);
    /// The trash was pressed: the screen asks before it removes the symbol's drawings.
    void trash_requested();

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    /// The indicators and drawing buttons with their labels while the row fits, else icons only.
    void fit_labels();
    /// The left group from the left end, cut off where the right group (trash, options) starts.
    void place();
    void paint_icons();

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
    int view_ = 1;
    int last_minutes_ = 1; ///< the minute view the drop-down names
    krx_quant::ChartStyle style_shown_ = krx_quant::ChartStyle::Candle;
    std::optional<krx_quant::DrawingTool> tool_shown_;
    int wide_ = 0; ///< the row's width with the labels
    bool narrow_ = false;
};

} // namespace fincept::screens
