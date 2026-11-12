#include "screens/krx_quant/KrxChartToolbar.h"

#include "ui/theme/Theme.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <functional>

namespace fincept::screens {

using krx_quant::ChartStyle;

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

/// What a style without a time axis draws (krx-quant #225), on its menu entry.
QString style_tip(ChartStyle style) {
    const QString bricks =
        QObject::tr(" 시간축 없는 차트: 벽돌 순서로 그리고, 축과 십자선에는 벽돌이 끝난 시각을 보인다.");
    switch (style) {
        case ChartStyle::Renko:
            return QObject::tr("종가가 박스만큼 움직일 때마다 벽돌을 쌓는다(되돌 때는 두 칸). 박스는 ATR(14)이고 톱니 "
                               "메뉴에서 바꾼다.") +
                   bricks;
        case ChartStyle::Kagi:
            return QObject::tr("종가가 반전폭만큼 되돌면 꺾는 선. 앞 어깨를 넘으면 굵게, 앞 허리 아래로 가면 가늘게 "
                               "그린다. 반전폭은 ATR(14)이고 톱니 메뉴에서 바꾼다.") +
                   bricks;
        case ChartStyle::PointFigure:
            return QObject::tr("종가가 박스를 채울 때마다 X(상승)나 O(하락)를 쌓고, 세 칸 되돌면 다음 칸으로 넘어간다. "
                               "박스는 ATR(14)이고 톱니 메뉴에서 바꾼다.") +
                   bricks;
        case ChartStyle::LineBreak:
            return QObject::tr("종가가 마지막 줄을 넘으면 줄을 더하고, 마지막 세 줄을 넘어 되돌면 반대 줄을 긋는다. 줄 "
                               "수는 톱니 메뉴에서 바꾼다.") +
                   bricks;
        default:
            return {};
    }
}

QIcon plus_icon(const Ink& ink) {
    return paint([&](QPainter& p) {
        p.setPen(pen(ink.text, 1.5));
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
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
    // left group off at its right end. The SMT toggle and the options stay at the right end.
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
        action->setToolTip(style_tip(style)); // the styles without a time axis (krx-quant #225)
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
    indicators_->setFocusPolicy(Qt::NoFocus);
    indicators_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    row->addWidget(indicators_);

    // The SMT divergences (codeyoma/krx-quant#522): every symbol's, filled while on. At the right end, so a
    // narrow panel that cuts the left group off still shows it.
    smt_ = new QToolButton;
    smt_->setObjectName("krxSmt");
    smt_->setText(tr("SMT"));
    smt_->setCheckable(true);
    smt_->setFocusPolicy(Qt::NoFocus);
    connect(smt_, &QToolButton::toggled, this, [this](bool on) {
        show_day_separator_state(); // filled while on
        emit smt_toggled(on);
    });
    end->addWidget(smt_);

    // The options (krx-quant #221): the day separators and the special situations' bands (krx-quant
    // #236), each for every symbol.
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
    special_ = options_menu->addAction(tr("특수 상황 표시"));
    special_->setCheckable(true);
    special_->setChecked(true);
    connect(special_, &QAction::toggled, this, &KrxChartToolbar::special_toggled);
    // While a style without a time axis is shown (krx-quant #225), its size: the box, the reversal or
    // the lines, and ATR(14) for the default.
    brick_divider_ = options_menu->addSeparator();
    auto* brick_holder = new QWidget(options_menu);
    brick_holder->setObjectName("krxBrickRow");
    auto* brick_line = new QHBoxLayout(brick_holder);
    brick_line->setContentsMargins(10, 4, 10, 4);
    brick_line->setSpacing(6);
    brick_title_ = new QLabel;
    brick_size_ = new QSpinBox;
    brick_size_->setKeyboardTracking(false); // said once typed (Enter, or leaving it), or stepped
    brick_size_->setAccelerated(true);
    brick_size_->setMinimumWidth(110);
    brick_atr_ = new QCheckBox;
    brick_line->addWidget(brick_title_);
    brick_line->addWidget(brick_size_, 1);
    brick_line->addWidget(brick_atr_);
    brick_row_ = new QWidgetAction(options_menu);
    brick_row_->setDefaultWidget(brick_holder);
    options_menu->addAction(brick_row_);
    connect(brick_size_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { emit brick_size_chosen(style_shown_, value); });
    connect(brick_atr_, &QCheckBox::toggled, this,
            [this](bool on) { emit brick_size_chosen(style_shown_, on ? 0 : brick_size_->value()); });
    options_->setMenu(options_menu);
    end->addWidget(options_);

    show_view(view_);
    show_style(style_shown_);
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
    show_day_separator_state();
    wide_ = 0; // measured again with the new label
    fit_labels();
    place();
}

void KrxChartToolbar::show_day_separator_state() {
    // The day separators are the minute views' (krx-quant #221) with a time axis (krx-quant #225).
    const bool candles = krx_quant::candle_view(view_);
    const bool bricks = krx_quant::timeless(style_shown_);
    day_separator_->setEnabled(!candles && !bricks);
    day_separator_->setToolTip(bricks    ? tr("분봉에서만. 시간축 없는 차트에서는 쓰지 않는다.")
                               : candles ? tr("분봉에서만")
                                         : tr("분봉에서 날짜가 바뀐 뒤 첫 봉에 밝은 회색 띠를 칠한다."));
    // The special situations' bands likewise (krx-quant #236).
    special_->setEnabled(!candles && !bricks);
    special_->setToolTip(
        bricks    ? tr("분봉에서만. 시간축 없는 차트에서는 쓰지 않는다.")
        : candles ? tr("분봉에서만")
                  : tr("분봉에서 엔진이 아는 서킷브레이커·사이드카(시장 전체)와 이 종목의 VI 구간을 옅은 띠와 "
                       "이름표로 칠하고, 그 안의 봉 호버에 적는다."));
    // The SMT divergences are the 1-minute view's (codeyoma/krx-quant#522), filled while on there.
    const bool minute = view_ == 1 && !bricks;
    smt_->setEnabled(minute);
    smt_->setProperty("active", minute && smt_->isChecked());
    repolish(smt_);
    smt_->setToolTip(
        bricks    ? tr("SMT 다이버전스: 1분 보기에서만. 시간축 없는 차트에서는 쓰지 않는다.")
        : !minute ? tr("SMT 다이버전스: 1분 보기에서만")
                  : tr("SMT 다이버전스. 이 종목이 든 쌍(005930·000660, 005930·KOSPI200 선물, 000660·KOSPI200 "
                       "선물)에서 한쪽만 직전 L분(15분, 30분)의 고점이나 저점을 넘은 봉에 표시한다. 하락 SMT는 봉 "
                       "위 ▼, 상승 SMT는 봉 아래 ▲다. 찬 표시는 엔진이 장중에 판정한 것, 속이 빈 표시(▽ △)는 지난날을 "
                       "다시 계산한 것이다. 호버에 상대 종목, L, 넘은 쪽, 출처가 있다. 모든 종목에 하나다."));
}

void KrxChartToolbar::show_brick_size(std::optional<double> kept, std::optional<double> atr) {
    brick_kept_ = kept;
    brick_atr_value_ = atr;
    const bool bricks = krx_quant::timeless(style_shown_);
    brick_divider_->setVisible(bricks);
    brick_row_->setVisible(bricks);
    if (!bricks)
        return;
    const QSignalBlocker quiet_size(brick_size_); // shown, not chosen
    const QSignalBlocker quiet_atr(brick_atr_);
    const bool lines = style_shown_ == ChartStyle::LineBreak;
    brick_title_->setText(style_shown_ == ChartStyle::Kagi ? tr("반전폭")
                          : style_shown_ == ChartStyle::PointFigure
                              ? tr("박스 (반전 %1칸)").arg(krx_quant::kFigureReversal)
                          : lines ? tr("줄 수")
                                  : tr("박스"));
    brick_size_->setRange(lines ? krx_quant::kMinBreakLines : 1,
                          lines ? krx_quant::kMaxBreakLines : krx_quant::kMaxBrickSize);
    brick_size_->setSuffix(lines ? tr(" 줄") : tr(" 원"));
    const double shown = kept.value_or(lines ? krx_quant::kBreakLines : atr.value_or(1));
    brick_size_->setValue(static_cast<int>(std::lround(shown)));
    brick_size_->setToolTip(lines ? tr("라인 브레이크가 되돌 때 넘어야 하는 줄 수. 이 종목에 남는다.")
                                  : tr("이 종목에 남는다. 바꾸면 ATR(14) 대신 이 값을 쓴다."));
    brick_atr_->setVisible(!lines);
    brick_atr_->setChecked(!kept);
    brick_atr_->setText(atr ? tr("ATR(14) %L1원").arg(static_cast<qlonglong>(*atr)) : tr("ATR(14)"));
    brick_atr_->setToolTip(tr("지금 보기의 봉 중 마지막 봉(만들어지는 중일 수 있다)을 뺀 봉으로 센 ATR(14)의 마지막 "
                              "값(원 단위로 반올림). 새 봉이 시작되거나 앞의 봉을 더 읽으면 다시 센다. 켜면 이 값을 "
                              "쓴다."));
}

void KrxChartToolbar::show_style(ChartStyle style) {
    style_shown_ = style;
    show_day_separator_state();
    show_brick_size(brick_kept_, brick_atr_value_);
    for (QAction* action : style_menu_->actions())
        action->setChecked(action->data().toString() == krx_quant::style_key(style));
    style_->setToolTip(tr("차트 모양: %1").arg(krx_quant::style_title(style)));
    paint_icons();
}

QPoint KrxChartToolbar::indicators_corner() const {
    return indicators_->mapToGlobal(QPoint(0, indicators_->height() + 2));
}

void KrxChartToolbar::show_day_separator(bool on) {
    const QSignalBlocker quiet(day_separator_); // shown, not toggled
    day_separator_->setChecked(on);
}

void KrxChartToolbar::show_special(bool on) {
    const QSignalBlocker quiet(special_); // shown, not toggled
    special_->setChecked(on);
}

void KrxChartToolbar::show_smt(bool on) {
    {
        const QSignalBlocker quiet(smt_); // shown, not toggled
        smt_->setChecked(on);
    }
    show_day_separator_state();
}

void KrxChartToolbar::paint_icons() {
    const Ink ink{QColor(ui::colors::TEXT_PRIMARY()), QColor(ui::colors::POSITIVE()), QColor(ui::colors::NEGATIVE())};
    style_->setIcon(style_icon(style_shown_, ink));
    for (QAction* action : style_menu_->actions())
        if (const auto style = krx_quant::parse_style(action->data().toString()))
            action->setIcon(style_icon(*style, ink));
    indicators_->setIcon(plus_icon(ink));
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
                "#krxBrickRow QLabel,#krxBrickRow QCheckBox{color:%3;background:transparent;}"
                "QMenu::item:checked{color:%2;font-weight:700;}" // with an icon, a menu has no check mark
                "QMenu::item:checked:disabled{color:%6;font-weight:400;}")
            .arg(ui::colors::TEXT_TERTIARY(), ui::colors::AMBER(), ui::colors::TEXT_PRIMARY(), ui::colors::BG_BASE(),
                 ui::colors::BORDER_MED(), ui::colors::TEXT_DIM()));
    paint_icons();
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
    if (wide_ == 0) {
        // Measured with the labels, also while they are hidden.
        if (narrow_)
            show_labels(true);
        wide_ = sizeHint().width();
        if (narrow_)
            show_labels(false);
    }
    const bool narrow = width() < wide_;
    if (narrow == narrow_)
        return;
    narrow_ = narrow;
    show_labels(!narrow);
    left_->adjustSize();
}

void KrxChartToolbar::show_labels(bool on) {
    indicators_->setToolButtonStyle(on ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
}

void KrxChartToolbar::place() {
    const int right = right_->sizeHint().width();
    const int left = std::max(0, width() - right);
    right_->setGeometry(left, 0, right, height());
    left_clip_->setGeometry(0, 0, left, height());
    left_->setGeometry(0, 0, left_->sizeHint().width(), height()); // past the clip: cut off
}

} // namespace fincept::screens
