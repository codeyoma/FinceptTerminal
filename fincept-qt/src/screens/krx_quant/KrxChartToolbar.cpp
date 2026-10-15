#include "screens/krx_quant/KrxChartToolbar.h"

#include "ui/theme/Theme.h"

#include <QAction>
#include <QActionGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>

#include <algorithm>
#include <functional>

namespace fincept::screens {

using krx_quant::ChartStyle;
using krx_quant::DrawingTool;

namespace {

/// The colors the icons are painted in.
struct Ink {
    QColor text;
    QColor up;
    QColor down;
};

/// A 16×16 icon drawn by ``draw``, sharp on a high-density screen.
QIcon paint(const std::function<void(QPainter&)>& draw) {
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    draw(p);
    return QIcon(pixmap);
}

QPen pen(const QColor& color, double width = 1.4) {
    return QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

/// A candle at ``x`` (its wick from ``top`` to ``bottom``, its body from ``body_top`` to
/// ``body_bottom``), filled or hollow.
void candle(QPainter& p, double x, double top, double bottom, double body_top, double body_bottom, const QColor& color,
            bool filled = true) {
    p.setPen(pen(color, 1.2));
    p.drawLine(QPointF(x, top), QPointF(x, body_top));
    p.drawLine(QPointF(x, body_bottom), QPointF(x, bottom));
    const QRectF body(x - 2.2, body_top, 4.4, body_bottom - body_top);
    p.setBrush(filled ? QBrush(color) : Qt::NoBrush);
    p.drawRect(body);
}

const QList<QPointF> kZigzag{{1.5, 12}, {5.5, 7}, {9.5, 10}, {14.5, 3.5}};

void polyline(QPainter& p, const QList<QPointF>& points, const QColor& color, double width = 1.6) {
    p.setPen(pen(color, width));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(points.data(), static_cast<int>(points.size()));
}

QIcon style_icon(ChartStyle style, const Ink& ink) {
    return paint([&](QPainter& p) {
        switch (style) {
            case ChartStyle::Bar:
                p.setPen(pen(ink.up, 1.3));
                p.drawLine(QPointF(5, 3), QPointF(5, 13));
                p.drawLine(QPointF(2.5, 10), QPointF(5, 10));
                p.drawLine(QPointF(5, 5), QPointF(7.5, 5));
                p.setPen(pen(ink.down, 1.3));
                p.drawLine(QPointF(11, 3), QPointF(11, 13));
                p.drawLine(QPointF(8.5, 5.5), QPointF(11, 5.5));
                p.drawLine(QPointF(11, 11), QPointF(13.5, 11));
                break;
            case ChartStyle::Candle:
                candle(p, 5, 2.5, 13.5, 5, 11, ink.up);
                candle(p, 11, 2.5, 13.5, 4, 9.5, ink.down);
                break;
            case ChartStyle::Line:
                polyline(p, kZigzag, ink.up);
                break;
            case ChartStyle::Area: {
                QPainterPath fill;
                fill.moveTo(kZigzag.first().x(), 15);
                for (const QPointF& point : kZigzag)
                    fill.lineTo(point);
                fill.lineTo(kZigzag.last().x(), 15);
                fill.closeSubpath();
                QColor soft = ink.up;
                soft.setAlphaF(0.35f);
                p.fillPath(fill, soft);
                polyline(p, kZigzag, ink.up);
                break;
            }
            case ChartStyle::Renko:
                p.setPen(Qt::NoPen);
                p.setBrush(ink.up);
                p.drawRect(QRectF(1, 10, 3.5, 3.5));
                p.drawRect(QRectF(4.5, 6.5, 3.5, 3.5));
                p.drawRect(QRectF(8, 3, 3.5, 3.5));
                p.setBrush(ink.down);
                p.drawRect(QRectF(11.5, 6.5, 3.5, 3.5));
                break;
            case ChartStyle::Kagi:
                polyline(p, {{2, 14}, {2, 6}, {6, 6}, {6, 11}, {10, 11}, {10, 2.5}, {14, 2.5}, {14, 8}}, ink.up, 1.5);
                break;
            case ChartStyle::PointFigure:
                p.setPen(pen(ink.up, 1.2));
                for (const double y : {3.0, 7.0, 11.0}) {
                    p.drawLine(QPointF(2.5, y), QPointF(5.5, y + 3));
                    p.drawLine(QPointF(5.5, y), QPointF(2.5, y + 3));
                }
                p.setPen(pen(ink.down, 1.2));
                p.setBrush(Qt::NoBrush);
                for (const double y : {5.0, 9.0})
                    p.drawEllipse(QPointF(11.5, y + 1.5), 1.6, 1.6);
                break;
            case ChartStyle::LineBreak:
                p.setPen(Qt::NoPen);
                p.setBrush(ink.up);
                p.drawRect(QRectF(1, 9, 4, 4.5));
                p.drawRect(QRectF(6, 4, 4, 5));
                p.setBrush(ink.down);
                p.drawRect(QRectF(11, 6, 4, 7));
                break;
            case ChartStyle::HeikinAshi:
                candle(p, 4, 6, 14, 8, 12.5, ink.up);
                candle(p, 8, 4, 12, 5.5, 9, ink.up);
                candle(p, 12, 2, 9, 3, 6.5, ink.up);
                break;
            case ChartStyle::Hollow:
                candle(p, 5, 2.5, 13.5, 5, 11, ink.up, false);
                candle(p, 11, 2.5, 13.5, 4, 9.5, ink.down);
                break;
            case ChartStyle::Baseline: {
                QPen dashed = pen(ink.text, 1);
                dashed.setDashPattern({2, 2});
                p.setPen(dashed);
                p.drawLine(QPointF(1, 8), QPointF(15, 8));
                const QList<QPointF> line{{1.5, 11}, {5, 4}, {9, 11.5}, {14.5, 5}};
                p.save();
                p.setClipRect(QRectF(0, 0, 16, 8));
                polyline(p, line, ink.up);
                p.restore();
                p.save();
                p.setClipRect(QRectF(0, 8, 16, 8));
                polyline(p, line, ink.down);
                p.restore();
                break;
            }
        }
    });
}

QIcon plus_icon(const Ink& ink) {
    return paint([&](QPainter& p) {
        p.setPen(pen(ink.text, 1.5));
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
    });
}

QIcon drawing_icon(const Ink& ink) {
    return paint([&](QPainter& p) {
        p.setPen(pen(ink.text, 1.3));
        p.drawLine(QPointF(3.5, 12.5), QPointF(12.5, 3.5));
        p.setBrush(ink.text);
        p.drawEllipse(QPointF(3.5, 12.5), 1.6, 1.6);
        p.drawEllipse(QPointF(12.5, 3.5), 1.6, 1.6);
    });
}

void dot(QPainter& p, const QPointF& at, const QColor& color, double radius = 1.6) {
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(at, radius, radius);
    p.restore();
}

/// The drawing tools' icons (krx-quant #224), in ``color``.
QIcon tool_icon(DrawingTool tool, const QColor& color) {
    return paint([&](QPainter& p) {
        p.setPen(pen(color, 1.3));
        p.setBrush(Qt::NoBrush);
        switch (tool) {
            case DrawingTool::TrendLine:
                p.drawLine(QPointF(3, 13), QPointF(13, 3));
                dot(p, {3, 13}, color);
                dot(p, {13, 3}, color);
                break;
            case DrawingTool::Ray:
                p.drawLine(QPointF(3, 13), QPointF(15, 1));
                dot(p, {3, 13}, color);
                dot(p, {8, 8}, color);
                break;
            case DrawingTool::Horizontal:
                p.drawLine(QPointF(1, 8), QPointF(15, 8));
                dot(p, {8, 8}, color);
                break;
            case DrawingTool::Vertical:
                p.drawLine(QPointF(8, 1), QPointF(8, 15));
                dot(p, {8, 8}, color);
                break;
            case DrawingTool::Cross:
                p.drawLine(QPointF(1, 8), QPointF(15, 8));
                p.drawLine(QPointF(8, 1), QPointF(8, 15));
                dot(p, {8, 8}, color);
                break;
            case DrawingTool::Rectangle:
                p.drawRect(QRectF(2.5, 4, 11, 8));
                dot(p, {2.5, 4}, color);
                dot(p, {13.5, 12}, color);
                break;
            case DrawingTool::Ellipse:
                p.drawEllipse(QRectF(1.5, 3.5, 13, 9));
                break;
            case DrawingTool::Pitchfork:
                p.drawLine(QPointF(2, 14), QPointF(14, 2));
                p.drawLine(QPointF(4, 7), QPointF(9.5, 1.5));
                p.drawLine(QPointF(9, 12), QPointF(14.5, 6.5));
                p.drawLine(QPointF(4, 7), QPointF(9, 12));
                dot(p, {2, 14}, color);
                break;
            case DrawingTool::FibRetracement:
                for (const double y : {2.5, 6.0, 9.0, 13.5})
                    p.drawLine(QPointF(2, y), QPointF(14, y));
                break;
            case DrawingTool::FibChannel:
                p.drawLine(QPointF(1, 9), QPointF(8, 2));
                p.drawLine(QPointF(4, 12), QPointF(11, 5));
                p.drawLine(QPointF(7, 15), QPointF(14, 8));
                break;
            case DrawingTool::FibFan:
                for (const QPointF to :
                     {QPointF(14, 2), QPointF(14, 6.5), QPointF(14, 11), QPointF(9.5, 2), QPointF(5, 2)})
                    p.drawLine(QPointF(2, 14), to);
                break;
            case DrawingTool::Text:
                p.setPen(pen(color, 1.8));
                p.drawLine(QPointF(3, 3), QPointF(13, 3));
                p.drawLine(QPointF(8, 3), QPointF(8, 13.5));
                break;
        }
    });
}

QIcon magnet_icon(const Ink& ink) {
    return paint([&](QPainter& p) {
        QPainterPath u;
        u.moveTo(4, 2);
        u.lineTo(4, 8.5);
        u.arcTo(QRectF(4, 4.5, 8, 8), 180, 180);
        u.lineTo(12, 2);
        p.setPen(QPen(ink.text, 2.4, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(u);
        p.setPen(QPen(ink.down, 2.4, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(4, 2), QPointF(4, 4.5));
        p.drawLine(QPointF(12, 2), QPointF(12, 4.5));
    });
}

/// What a tool draws, on its menu entry.
QString tool_tip(DrawingTool tool) {
    switch (tool) {
        case DrawingTool::TrendLine:
            return QObject::tr("두 점을 잇는 선");
        case DrawingTool::Ray:
            return QObject::tr("첫 점에서 둘째 점 쪽으로 끝없이 뻗는 선");
        case DrawingTool::Horizontal:
            return QObject::tr("한 점의 가격에 긋는 가로선. 가격 축에 그 가격이 나온다.");
        case DrawingTool::Vertical:
            return QObject::tr("한 점의 시각에 긋는 세로선. 시간 축에 그 시각이 나온다.");
        case DrawingTool::Cross:
            return QObject::tr("한 점에서 긋는 가로선과 세로선");
        case DrawingTool::Rectangle:
            return QObject::tr("두 점을 마주 보는 꼭짓점으로 하는 사각형");
        case DrawingTool::Ellipse:
            return QObject::tr("두 점이 이루는 사각형 안의 타원");
        case DrawingTool::Pitchfork:
            return QObject::tr(
                "세 점: 첫 점에서 둘째·셋째 점의 가운데로 뻗는 중심선과, 둘째·셋째 점에서 나란히 뻗는 두 갈래");
        case DrawingTool::FibRetracement:
            return QObject::tr("두 점 사이를 0 · 0.236 · 0.382 · 0.5 · 0.618 · 0.786 · 1로 나눈 가격(0이 둘째 점)");
        case DrawingTool::FibChannel:
            return QObject::tr("세 점: 첫 두 점의 선과, 셋째 점까지 0.236–1 비율로 나란한 선들");
        case DrawingTool::FibFan:
            return QObject::tr("두 점: 첫 점에서 가격과 시간의 0.25–1 지점으로 뻗는 선들");
        case DrawingTool::Text:
            return QObject::tr("한 점에 글을 쓴다. 찍으면 입력창이 열린다.");
    }
    return {};
}

QIcon trash_icon(const Ink& ink) {
    return paint([&](QPainter& p) {
        p.setPen(pen(ink.text, 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(2.5, 4), QPointF(13.5, 4));
        p.drawPolyline(QPolygonF{{6, 4}, {6, 2.2}, {10, 2.2}, {10, 4}});
        p.drawPolyline(QPolygonF{{4, 4}, {4.8, 14}, {11.2, 14}, {12, 4}});
        p.drawLine(QPointF(6.7, 6.5), QPointF(6.9, 11.5));
        p.drawLine(QPointF(9.3, 6.5), QPointF(9.1, 11.5));
    });
}

QIcon gear_icon(const Ink& ink) {
    return paint([&](QPainter& p) {
        QPainterPath gear;
        gear.addEllipse(QPointF(8, 8), 4.6, 4.6);
        for (int i = 0; i < 8; ++i) {
            QPainterPath tooth;
            tooth.addRect(QRectF(-1.3, -7, 2.6, 3));
            gear = gear.united(QTransform().translate(8, 8).rotate(i * 45.0).map(tooth));
        }
        QPainterPath hole;
        hole.addEllipse(QPointF(8, 8), 2, 2);
        p.fillPath(gear.subtracted(hole), ink.text);
    });
}

void repolish(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

} // namespace

KrxChartToolbar::KrxChartToolbar(QWidget* parent) : QWidget(parent) {
    // Its width never widens the chart's panel: a narrow one drops the labels, then cuts the
    // left group off at its right end. The trash and the options stay at the right end.
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    left_clip_ = new QWidget(this);
    left_ = new QWidget(left_clip_);
    right_ = new QWidget(this);
    auto* row = new QHBoxLayout(left_);
    row->setContentsMargins(6, 3, 6, 3);
    row->setSpacing(2);
    auto* end = new QHBoxLayout(right_);
    end->setContentsMargins(0, 3, 6, 3);
    end->setSpacing(2);

    // The minute views (krx-quant #109) in a drop-down, named by the last one picked.
    minutes_ = new QToolButton;
    minutes_->setObjectName("krxMinutes");
    minutes_->setPopupMode(QToolButton::InstantPopup);
    minutes_->setFocusPolicy(Qt::NoFocus);
    minutes_->setToolTip(tr("분봉. 3분 이상은 1분봉을 묶은 봉이고, 아이콘은 1분 보기에만 있다."));
    minutes_menu_ = new QMenu(minutes_);
    minutes_menu_->setToolTipsVisible(true);
    auto* minute_group = new QActionGroup(minutes_menu_);
    for (const int minutes : krx_quant::kMinuteViews) {
        QAction* action = minutes_menu_->addAction(tr("%1분").arg(minutes));
        action->setCheckable(true);
        action->setData(minutes);
        action->setToolTip(minutes == 1 ? tr("1분봉과 최종 합의 아이콘")
                                        : tr("1분봉을 %1분씩 묶은 봉. 아이콘은 1분 보기에만 있다.").arg(minutes));
        minute_group->addAction(action);
        connect(action, &QAction::triggered, this, [this, minutes]() { emit view_chosen(minutes); });
    }
    minutes_->setMenu(minutes_menu_);
    row->addWidget(minutes_);

    // The day, week, month and year views (krx-quant #111, #112), as before.
    const QList<QPair<int, QString>> candles{{krx_quant::kDayView, tr("일")},
                                             {krx_quant::kWeekView, tr("주")},
                                             {krx_quant::kMonthView, tr("월")},
                                             {krx_quant::kYearView, tr("년")}};
    for (const auto& [view, name] : candles) {
        auto* button = new QPushButton(name);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setProperty("view", view);
        button->setToolTip(
            view == krx_quant::kDayView ? tr("일봉. 오늘 봉은 오늘의 1분봉으로 만든다.")
            : view == krx_quant::kWeekView ? tr("주봉(월요일 시작). 이번 주 봉은 일봉과 오늘의 1분봉으로 만든다.")
            : view == krx_quant::kMonthView ? tr("월봉(1일 시작). 이번 달 봉은 일봉과 오늘의 1분봉으로 만든다.")
                                            : tr("년봉(1월 1일 시작). 올해 봉은 일봉과 오늘의 1분봉으로 만든다."));
        connect(button, &QPushButton::clicked, this, [this, view]() {
            show_view(view_); // checked as shown until the screen says otherwise
            emit view_chosen(view);
        });
        candle_buttons_ << button;
        row->addWidget(button);
    }

    auto* divider = new QFrame;
    divider->setObjectName("krxDivider");
    divider->setFixedSize(1, 14);
    row->addSpacing(4);
    row->addWidget(divider);
    row->addSpacing(4);

    // The chart style (krx-quant #221): its icon, and the styles in a menu.
    style_ = new QToolButton;
    style_->setObjectName("krxStyle");
    style_->setPopupMode(QToolButton::InstantPopup);
    style_->setFocusPolicy(Qt::NoFocus);
    style_->setToolTip(tr("차트 모양"));
    style_menu_ = new QMenu(style_);
    style_menu_->setToolTipsVisible(true);
    auto* style_group = new QActionGroup(style_menu_);
    for (const ChartStyle style : krx_quant::kChartStyles) {
        QAction* action = style_menu_->addAction(krx_quant::style_title(style));
        action->setCheckable(true);
        action->setIconVisibleInMenu(true); // macOS hides menu icons by default
        action->setData(krx_quant::style_key(style));
        action->setEnabled(krx_quant::drawable(style));
        if (!krx_quant::drawable(style))
            action->setToolTip(tr("준비 중"));
        style_group->addAction(action);
        connect(action, &QAction::triggered, this, [this, style]() { emit style_chosen(style); });
    }
    style_->setMenu(style_menu_);
    row->addWidget(style_);

    // The indicators (krx-quant #222): the screen opens their panel under the button.
    indicators_ = new QToolButton;
    indicators_->setObjectName("krxIndicators");
    indicators_->setText(tr("보조지표"));
    indicators_->setToolTip(tr("보조지표"));
    connect(indicators_, &QToolButton::clicked, this, &KrxChartToolbar::indicators_requested);
    // The drawing tools and the magnet (krx-quant #224): a tool picked draws with the next clicks on
    // the chart, then the chart is back at the cursor.
    drawing_ = new QToolButton;
    drawing_->setObjectName("krxDrawing");
    drawing_->setText(tr("그리기"));
    drawing_->setPopupMode(QToolButton::InstantPopup);
    drawing_menu_ = new QMenu(drawing_);
    drawing_menu_->setToolTipsVisible(true);
    for (const DrawingTool tool : krx_quant::kDrawingTools) {
        QAction* action = drawing_menu_->addAction(krx_quant::tool_title(tool));
        action->setCheckable(true);
        action->setIconVisibleInMenu(true);
        action->setData(krx_quant::tool_key(tool));
        action->setToolTip(tool_tip(tool));
        connect(action, &QAction::triggered, this, [this, tool]() {
            show_tool(tool_shown_); // checked as the screen says
            emit tool_chosen(tool);
        });
    }
    drawing_menu_->addSeparator();
    magnet_ = drawing_menu_->addAction(tr("자석"));
    magnet_->setCheckable(true);
    magnet_->setIconVisibleInMenu(true);
    magnet_->setToolTip(
        tr("찍거나 끄는 점이 가장 가까운 봉의 시가·고가·저가·종가 중 가까운 값에 붙는다. 모든 종목에 하나다."));
    connect(magnet_, &QAction::toggled, this, &KrxChartToolbar::magnet_toggled);
    drawing_->setMenu(drawing_menu_);
    for (auto* button : {indicators_, drawing_}) {
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        row->addWidget(button);
    }

    // The symbol's drawings, all of them (krx-quant #224): the screen asks first.
    trash_ = new QToolButton;
    trash_->setObjectName("krxTrash");
    trash_->setFocusPolicy(Qt::NoFocus);
    connect(trash_, &QToolButton::clicked, this, &KrxChartToolbar::trash_requested);
    end->addWidget(trash_);

    // The options (krx-quant #221): the day separators, for every symbol.
    options_ = new QToolButton;
    options_->setObjectName("krxOptions");
    options_->setPopupMode(QToolButton::InstantPopup);
    options_->setFocusPolicy(Qt::NoFocus);
    options_->setToolTip(tr("옵션"));
    auto* options_menu = new QMenu(options_);
    options_menu->setToolTipsVisible(true);
    day_separator_ = options_menu->addAction(tr("날짜 구분선"));
    day_separator_->setCheckable(true);
    connect(day_separator_, &QAction::toggled, this, &KrxChartToolbar::day_separator_toggled);
    options_->setMenu(options_menu);
    end->addWidget(options_);

    show_view(view_);
    show_style(style_shown_);
    show_tool(std::nullopt);
    show_drawings(0);
    restyle();
}

void KrxChartToolbar::show_view(int view) {
    view_ = view;
    const bool candles = krx_quant::candle_view(view);
    if (!candles)
        last_minutes_ = view;
    minutes_->setText(tr("%1분 ▾").arg(last_minutes_));
    minutes_->setProperty("active", !candles);
    repolish(minutes_);
    for (QAction* action : minutes_menu_->actions())
        action->setChecked(!candles && action->data().toInt() == view);
    for (auto* button : candle_buttons_)
        button->setChecked(button->property("view").toInt() == view);
    // The day separators are the minute views' (krx-quant #221).
    day_separator_->setEnabled(!candles);
    day_separator_->setToolTip(candles ? tr("분봉에서만") : tr("분봉에서 날짜가 바뀐 뒤 첫 봉에 회색 띠를 칠한다."));
    if (!narrow_)
        wide_ = 0; // measured again with the new label
    fit_labels();
    place();
}

void KrxChartToolbar::show_style(ChartStyle style) {
    style_shown_ = style;
    for (QAction* action : style_menu_->actions())
        action->setChecked(action->data().toString() == krx_quant::style_key(style));
    style_->setToolTip(tr("차트 모양: %1").arg(krx_quant::style_title(style)));
    // A style without a time axis hides the drawings (krx-quant #220, #225).
    const bool timeless = krx_quant::timeless(style);
    drawing_->setEnabled(!timeless);
    if (timeless)
        drawing_->setToolTip(tr("그리기: 시간축 없는 차트에서는 쓰지 않는다"));
    else
        show_tool(tool_shown_);
    paint_icons();
}

void KrxChartToolbar::show_tool(std::optional<DrawingTool> tool) {
    tool_shown_ = tool;
    for (QAction* action : drawing_menu_->actions())
        if (action != magnet_ && action->isCheckable())
            action->setChecked(tool && action->data().toString() == krx_quant::tool_key(*tool));
    drawing_->setProperty("active", tool.has_value());
    repolish(drawing_);
    if (drawing_->isEnabled())
        drawing_->setToolTip(
            tool ? tr("그리기: %1. 차트를 눌러 점을 찍는다. Esc로 취소한다.").arg(krx_quant::tool_title(*tool))
                 : tr("그리기"));
    paint_icons();
}

void KrxChartToolbar::show_magnet(bool on) {
    const QSignalBlocker quiet(magnet_); // shown, not toggled
    magnet_->setChecked(on);
}

void KrxChartToolbar::show_drawings(int count) {
    trash_->setEnabled(count > 0);
    trash_->setToolTip(count > 0 ? tr("그린 것 모두 지우기 (%1개)").arg(count)
                                 : tr("그린 것 모두 지우기: 그린 것이 없다"));
}

QPoint KrxChartToolbar::indicators_corner() const {
    return indicators_->mapToGlobal(QPoint(0, indicators_->height() + 2));
}

void KrxChartToolbar::show_day_separator(bool on) {
    const QSignalBlocker quiet(day_separator_); // shown, not toggled
    day_separator_->setChecked(on);
}

void KrxChartToolbar::paint_icons() {
    const Ink ink{QColor(ui::colors::TEXT_PRIMARY()), QColor(ui::colors::POSITIVE()), QColor(ui::colors::NEGATIVE())};
    style_->setIcon(style_icon(style_shown_, ink));
    for (QAction* action : style_menu_->actions())
        if (const auto style = krx_quant::parse_style(action->data().toString()))
            action->setIcon(style_icon(*style, ink));
    indicators_->setIcon(plus_icon(ink));
    // While a tool is chosen, its icon on the filled button (krx-quant #224).
    drawing_->setIcon(tool_shown_ ? tool_icon(*tool_shown_, QColor(ui::colors::BG_BASE())) : drawing_icon(ink));
    for (QAction* action : drawing_menu_->actions())
        if (const auto tool = krx_quant::parse_tool(action->data().toString()))
            action->setIcon(tool_icon(*tool, ink.text));
    magnet_->setIcon(magnet_icon(ink));
    trash_->setIcon(trash_icon(ink));
    options_->setIcon(gear_icon(ink));
}

void KrxChartToolbar::restyle() {
    // As the views' buttons were (krx-quant #109): flat, the view chosen filled amber.
    setStyleSheet(
        QString("QPushButton,QToolButton{background:transparent;color:%1;border:1px solid transparent;"
                "padding:2px 6px;font-weight:700;}"
                "QToolButton::menu-indicator{image:none;width:0;}"
                "QPushButton:checked,QToolButton[active=\"true\"]{color:%4;background:%2;border:1px solid %2;}"
                "QPushButton:hover:!checked,QToolButton:hover:enabled{color:%3;border-color:%5;}"
                "QToolButton[active=\"true\"]:hover{color:%4;}"
                "QPushButton:disabled,QToolButton:disabled{color:%6;}"
                "#krxDivider{background:%5;}"
                "QMenu::item:disabled{color:%6;}"
                "QMenu::item:checked{color:%2;font-weight:700;}") // with an icon, a menu has no check mark
            .arg(ui::colors::TEXT_TERTIARY(), ui::colors::AMBER(), ui::colors::TEXT_PRIMARY(), ui::colors::BG_BASE(),
                 ui::colors::BORDER_MED(), ui::colors::TEXT_DIM()));
    paint_icons();
    if (!narrow_)
        wide_ = 0; // the bold labels measured again
    updateGeometry();
    fit_labels();
    place();
}

QSize KrxChartToolbar::sizeHint() const {
    const QSize left = left_->sizeHint();
    const QSize right = right_->sizeHint();
    return {left.width() + right.width(), std::max(left.height(), right.height())};
}

void KrxChartToolbar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    fit_labels();
    place();
}

void KrxChartToolbar::fit_labels() {
    if (!narrow_ && wide_ == 0)
        wide_ = sizeHint().width();
    const bool narrow = width() < wide_;
    if (narrow == narrow_)
        return;
    narrow_ = narrow;
    for (auto* button : {indicators_, drawing_})
        button->setToolButtonStyle(narrow ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    left_->adjustSize();
}

void KrxChartToolbar::place() {
    const int right = right_->sizeHint().width();
    const int left = std::max(0, width() - right);
    right_->setGeometry(left, 0, right, height());
    left_clip_->setGeometry(0, 0, left, height());
    left_->setGeometry(0, 0, left_->sizeHint().width(), height()); // past the clip: cut off
}

} // namespace fincept::screens
