#pragma once

#include "services/krx_quant/ChartDrawings.h"

#include <QFrame>
#include <QList>

class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace fincept::screens {

/// The popup of a drawing on the KRX chart (krx-quant #224), opened by a double-click on it and
/// when a text is placed: its colour (the palette), its width (a text's size), its line style
/// (실선, 점선, 파선) and a text's text, and `삭제`. It shows the drawing the screen gives it and
/// says each change at once; the screen keeps it for the symbol and draws it.
class KrxDrawingPopup : public QFrame {
    Q_OBJECT
  public:
    explicit KrxDrawingPopup(QWidget* parent = nullptr);

    /// Show ``drawing`` with the popup's top left at ``at`` (global), kept on that screen. A text's
    /// text has the keyboard, all of it selected to be typed over.
    void open_for(const krx_quant::Drawing& drawing, const QPoint& at);
    /// The drawing shown (its id; 0 before one is).
    int drawing_id() const { return drawing_.id; }
    /// Paint in the theme's colours now.
    void restyle();

  signals:
    /// Its look or text changed: the drawing as it is now.
    void drawing_changed(const krx_quant::Drawing& drawing);
    void remove_requested(int id);
    /// It closed (a click outside, Enter in the text, Esc).
    void closed(int id);

  protected:
    void hideEvent(QHideEvent* event) override;

  private:
    /// The buttons checked as the drawing shown is.
    void show_values();
    void changed();

    krx_quant::Drawing drawing_;
    QLabel* title_ = nullptr;
    QLabel* text_label_ = nullptr;
    QLineEdit* text_ = nullptr;
    QLabel* width_label_ = nullptr;
    QLabel* style_label_ = nullptr;
    QWidget* styles_row_ = nullptr;
    QList<QToolButton*> swatches_; ///< property "color"
    QList<QToolButton*> widths_;   ///< property "lineWidth"
    QList<QToolButton*> styles_;   ///< property "style" (line_style_key)
    QPushButton* remove_ = nullptr;
};

} // namespace fincept::screens
