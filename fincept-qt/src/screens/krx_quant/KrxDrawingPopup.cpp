#include "screens/krx_quant/KrxDrawingPopup.h"

#include "screens/krx_quant/KrxSwatches.h"
#include "ui/theme/Theme.h"

#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace fincept::screens {

using krx_quant::DrawingTool;
using krx_quant::LineStyle;

namespace {

/// A 16×16 (or ``width``×16) icon drawn by ``draw``, sharp on a high-density screen.
QIcon paint(const std::function<void(QPainter&)>& draw, int width = 16) {
    QPixmap pixmap(width * 2, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    draw(p);
    return QIcon(pixmap);
}

QIcon swatch_icon(const QString& color) {
    return paint([color](QPainter& p) {
        p.setPen(QPen(QColor(ui::colors::BORDER_MED()), 1));
        p.setBrush(QColor(color));
        p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 2.5, 2.5);
    });
}

/// A line ``width`` thick in ``style``, 28 wide.
QIcon line_icon(int width, LineStyle style) {
    return paint(
        [width, style](QPainter& p) {
            QPen pen(QColor(ui::colors::TEXT_PRIMARY()), width, Qt::SolidLine, Qt::FlatCap);
            if (style == LineStyle::Dotted)
                pen.setDashPattern({1, 2});
            else if (style == LineStyle::Dashed)
                pen.setDashPattern({4, 3});
            p.setPen(pen);
            p.drawLine(QPointF(2, 8), QPointF(26, 8));
        },
        28);
}

QToolButton* choice(const QIcon& icon, const QString& tip) {
    auto* button = new QToolButton;
    button->setObjectName("krxDrawingChoice");
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(icon);
    button->setToolTip(tip);
    return button;
}

QLabel* label(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName("krxDrawingLabel");
    return l;
}

} // namespace

KrxDrawingPopup::KrxDrawingPopup(QWidget* parent) : QFrame(parent, Qt::Popup) {
    setObjectName("krxDrawingPopup");
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(10, 8, 10, 10);
    column->setSpacing(6);
    title_ = new QLabel;
    title_->setObjectName("krxDrawingTitle");
    column->addWidget(title_);

    // A text's text (krx-quant #224): each key shows on the chart; Enter closes.
    text_label_ = label(tr("내용"));
    column->addWidget(text_label_);
    text_ = new QLineEdit;
    text_->setMaxLength(krx_quant::kMaxDrawingText);
    text_->setPlaceholderText(tr("쓸 글"));
    connect(text_, &QLineEdit::textEdited, this, [this](const QString& text) {
        drawing_.text = text;
        changed();
    });
    connect(text_, &QLineEdit::returnPressed, this, &QWidget::hide);
    column->addWidget(text_);

    column->addWidget(label(tr("색")));
    auto* colors = new QGridLayout;
    colors->setSpacing(2);
    const QStringList& all = krx_swatches();
    for (qsizetype i = 0; i < all.size(); ++i) {
        auto* swatch = choice(swatch_icon(all[i]), all[i]);
        swatch->setIconSize(QSize(16, 16));
        swatch->setProperty("color", all[i]);
        connect(swatch, &QToolButton::clicked, this, [this, color = all[i]]() {
            drawing_.color = color;
            changed();
        });
        swatches_ << swatch;
        colors->addWidget(swatch, static_cast<int>(i / 9), static_cast<int>(i % 9));
    }
    column->addLayout(colors);

    width_label_ = label(tr("굵기"));
    column->addWidget(width_label_);
    auto* widths = new QHBoxLayout;
    widths->setSpacing(2);
    for (int w = krx_quant::kMinDrawingWidth; w <= krx_quant::kMaxDrawingWidth; ++w) {
        auto* button = choice(line_icon(w, LineStyle::Solid), tr("%1px").arg(w));
        button->setIconSize(QSize(28, 16));
        button->setProperty("lineWidth", w);
        connect(button, &QToolButton::clicked, this, [this, w]() {
            drawing_.width = w;
            changed();
        });
        widths_ << button;
        widths->addWidget(button);
    }
    widths->addStretch(1);
    column->addLayout(widths);

    style_label_ = label(tr("선 종류"));
    column->addWidget(style_label_);
    styles_row_ = new QWidget;
    auto* styles = new QHBoxLayout(styles_row_);
    styles->setContentsMargins(0, 0, 0, 0);
    styles->setSpacing(2);
    for (const LineStyle style : krx_quant::kLineStyles) {
        auto* button = choice(line_icon(2, style), krx_quant::line_style_title(style));
        button->setIconSize(QSize(28, 16));
        button->setText(krx_quant::line_style_title(style));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setProperty("style", krx_quant::line_style_key(style));
        connect(button, &QToolButton::clicked, this, [this, style]() {
            drawing_.style = style;
            changed();
        });
        styles_ << button;
        styles->addWidget(button);
    }
    styles->addStretch(1);
    column->addWidget(styles_row_);

    remove_ = new QPushButton(tr("삭제"));
    remove_->setObjectName("krxDrawingRemove");
    remove_->setFocusPolicy(Qt::NoFocus);
    remove_->setToolTip(tr("이 그린 것을 지운다. 차트에서 고른 뒤 Delete나 Backspace를 눌러도 된다."));
    connect(remove_, &QPushButton::clicked, this, [this]() {
        const int id = drawing_.id;
        hide();
        emit remove_requested(id);
    });
    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    foot->addWidget(remove_);
    column->addLayout(foot);
    restyle();
}

void KrxDrawingPopup::open_for(const krx_quant::Drawing& drawing, const QPoint& at) {
    drawing_ = drawing;
    const bool text = drawing.tool == DrawingTool::Text;
    title_->setText(krx_quant::tool_title(drawing.tool));
    text_label_->setVisible(text);
    text_->setVisible(text);
    text_->setText(drawing.text);
    width_label_->setText(text ? tr("크기") : tr("굵기"));
    style_label_->setVisible(!text); // a text has no line
    styles_row_->setVisible(!text);
    show_values();
    adjustSize();
    // Kept on the screen it opens on.
    QPoint top_left = at + QPoint(8, 8);
    if (const QScreen* screen = QGuiApplication::screenAt(at)) {
        const QRect room = screen->availableGeometry();
        top_left.setX(std::clamp(top_left.x(), room.left(), std::max(room.left(), room.right() - width())));
        top_left.setY(std::clamp(top_left.y(), room.top(), std::max(room.top(), room.bottom() - height())));
    }
    move(top_left);
    show();
    raise();
    if (text) {
        text_->setFocus(Qt::PopupFocusReason);
        text_->selectAll();
    }
}

void KrxDrawingPopup::show_values() {
    for (auto* swatch : swatches_)
        swatch->setChecked(swatch->property("color").toString().compare(drawing_.color, Qt::CaseInsensitive) == 0);
    for (auto* button : widths_)
        button->setChecked(button->property("lineWidth").toInt() == drawing_.width);
    for (auto* button : styles_)
        button->setChecked(button->property("style").toString() == krx_quant::line_style_key(drawing_.style));
}

void KrxDrawingPopup::changed() {
    show_values();
    emit drawing_changed(drawing_);
}

void KrxDrawingPopup::hideEvent(QHideEvent* event) {
    QFrame::hideEvent(event);
    emit closed(drawing_.id);
}

void KrxDrawingPopup::restyle() {
    setStyleSheet(QString("#krxDrawingPopup{background:%1;border:1px solid %2;}"
                          "#krxDrawingTitle{color:%3;font-weight:700;}"
                          "#krxDrawingLabel{color:%4;}"
                          "#krxDrawingChoice{background:transparent;color:%3;border:1px solid transparent;"
                          "padding:2px;}"
                          "#krxDrawingChoice:hover{border-color:%2;}"
                          "#krxDrawingChoice:checked{border-color:%5;}"
                          "#krxDrawingRemove{color:%6;background:transparent;border:1px solid %2;padding:3px 12px;}"
                          "#krxDrawingRemove:hover{border-color:%6;}"
                          "QLineEdit{background:%7;color:%3;border:1px solid %2;padding:3px 5px;}"
                          "QLineEdit:focus{border-color:%5;}")
                      .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_MED(), ui::colors::TEXT_PRIMARY(),
                           ui::colors::TEXT_TERTIARY(), ui::colors::AMBER(), ui::colors::NEGATIVE(),
                           ui::colors::BG_BASE()));
    // The line icons are in the theme's text colour.
    for (auto* button : widths_)
        button->setIcon(line_icon(button->property("lineWidth").toInt(), LineStyle::Solid));
    for (auto* button : styles_)
        button->setIcon(line_icon(2, *krx_quant::parse_line_style(button->property("style").toString())));
}

} // namespace fincept::screens
