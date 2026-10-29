#include "screens/krx_quant/KrxTraderScreen.h"

#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/KrxTraderLive.h"
#include "ui/theme/Theme.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace {

using krx_quant::trader::Tone;
using krx_tables::cell;
using krx_tables::kNumber;
namespace view = krx_quant::trader;

/// The trades table's columns.
enum TradeColumn {
    kEntry,
    kExit,
    kQuantity,
    kReason,
    kCost,
    kNet,
    kCell,
    kProbability,
    kPStar,
    kLow,
    kMode,
    kTradeColumns
};
/// Rows the trades table shows before it scrolls on its own.
constexpr int kTradesShown = 15;

QString tone_color(Tone tone) {
    switch (tone) {
        case Tone::Plain:
            return ui::colors::TEXT_PRIMARY();
        case Tone::Dim:
            return ui::colors::TEXT_DIM();
        case Tone::Positive:
            return ui::colors::POSITIVE();
        case Tone::Negative:
            return ui::colors::NEGATIVE();
        case Tone::Warning:
            return ui::colors::WARNING();
        case Tone::Live: // the chart's colour for the live account's trades (codeyoma/krx-quant#398)
            return ui::colors::AMBER();
        case Tone::Simulated:
            return ui::colors::CYAN();
    }
    return ui::colors::TEXT_PRIMARY();
}

QPushButton* tool_button(const QString& text, const QString& tip) {
    auto* button = new QPushButton(text);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    return button;
}

/// A table as tall as its rows (at most ``rows`` of them, then it scrolls): the screen scrolls, not
/// each table.
void fit_height(QTableWidget* table, int rows = -1) {
    table->resizeRowsToContents();
    const auto* header = table->horizontalHeader();
    int height = header->isHidden() ? 0 : std::max(header->height(), header->sizeHint().height());
    const auto* vertical = table->verticalHeader();
    if (rows >= 0 && table->rowCount() > rows) {
        height += vertical->sectionPosition(rows);
        table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    } else {
        height += vertical->length();
        table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    table->setFixedHeight(height + 2 * table->frameWidth() + 4);
}

QTableWidgetItem* toned(const QString& text, Tone tone, const QString& tip = {},
                        Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* item = cell(text, align);
    item->setForeground(QColor(tone_color(tone)));
    item->setToolTip(tip);
    return item;
}

/// A badge's look: its colour on a faint fill of it, a border of it.
QString badge_style(Tone tone) {
    QColor fill(tone_color(tone));
    fill.setAlphaF(0.14f);
    return QString("color:%1;background:%2;border:1px solid %1;border-radius:3px;padding:1px 8px;font-weight:700;")
        .arg(tone_color(tone), fill.name(QColor::HexArgb));
}

} // namespace

KrxTraderDaysChart::KrxTraderDaysChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(220);
}

void KrxTraderDaysChart::set_points(const QList<krx_quant::trader::DayPoint>& points) {
    points_ = points;
    hover_ = -1;
    update();
}

QRectF KrxTraderDaysChart::plot() const {
    return QRectF(rect()).adjusted(84, 12, -16, -26);
}

int KrxTraderDaysChart::day_at(double x) const {
    const QRectF area = plot();
    if (points_.isEmpty() || x < area.left() || x > area.right())
        return -1;
    const double step = area.width() / static_cast<double>(points_.size());
    const auto index = static_cast<int>((x - area.left()) / step);
    return index >= 0 && index < points_.size() ? index : -1;
}

void KrxTraderDaysChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(ui::colors::BG_BASE()));
    const QRectF area = plot();
    p.setPen(QColor(ui::colors::BORDER_DIM()));
    p.drawRect(area);
    if (points_.isEmpty()) {
        p.setPen(QColor(ui::colors::TEXT_DIM()));
        p.drawText(area, Qt::AlignCenter, tr("거래일이 없습니다"));
        return;
    }
    // One scale for the bars and the running sum, its zero in view.
    double low = 0, high = 0;
    for (const auto& point : points_) {
        low = std::min({low, point.net, point.sum});
        high = std::max({high, point.net, point.sum});
    }
    if (high - low < 1)
        high = low + 1;
    const double span = high - low;
    const auto y_of = [&](double value) { return area.bottom() - 6 - (area.height() - 12) * (value - low) / span; };
    const double step = area.width() / static_cast<double>(points_.size());
    const auto x_of = [&](qsizetype i) { return area.left() + step * (static_cast<double>(i) + 0.5); };

    p.setPen(QPen(QColor(ui::colors::BORDER_MED()), 1, Qt::DashLine));
    p.drawLine(QPointF(area.left(), y_of(0)), QPointF(area.right(), y_of(0)));
    p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
    for (const double value : {high, 0.0, low})
        p.drawText(QRectF(0, y_of(value) - 9, area.left() - 6, 18), Qt::AlignRight | Qt::AlignVCenter,
                   view::signed_won_text(value));

    // The bars: each day's net, a hovered one brighter.
    const double width = std::max(2.0, std::min(36.0, step * 0.6));
    for (qsizetype i = 0; i < points_.size(); ++i) {
        const auto& point = points_[i];
        QColor color(point.net < 0 ? ui::colors::NEGATIVE() : ui::colors::POSITIVE());
        if (i != hover_)
            color.setAlphaF(0.7f);
        const double top = std::min(y_of(point.net), y_of(0));
        const double bottom = std::max(y_of(point.net), y_of(0));
        p.fillRect(QRectF(x_of(i) - width / 2, top, width, std::max(1.0, bottom - top)), color);
        if (point.halted) { // a loss limit reached that day
            p.setPen(QPen(QColor(ui::colors::NEGATIVE()), 1.5));
            p.drawRect(QRectF(x_of(i) - width / 2 - 2, area.top() + 2, width + 4, area.height() - 4));
        }
    }
    // The running sum.
    p.setPen(QPen(QColor(ui::colors::AMBER()), 1.6));
    QPainterPath path;
    for (qsizetype i = 0; i < points_.size(); ++i) {
        const QPointF at(x_of(i), y_of(points_[i].sum));
        i == 0 ? path.moveTo(at) : path.lineTo(at);
    }
    p.drawPath(path);
    p.setBrush(QColor(ui::colors::AMBER()));
    for (qsizetype i = 0; i < points_.size(); ++i)
        p.drawEllipse(QPointF(x_of(i), y_of(points_[i].sum)), 2.5, 2.5);
    p.setBrush(Qt::NoBrush);

    // The days under it: at most about ten labels.
    p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
    const qsizetype every = std::max<qsizetype>(1, (points_.size() + 9) / 10);
    for (qsizetype i = 0; i < points_.size(); i += every)
        p.drawText(QRectF(x_of(i) - 30, area.bottom() + 4, 60, 18), Qt::AlignHCenter | Qt::AlignTop,
                   points_[i].day.toString("MM-dd"));
}

void KrxTraderDaysChart::mouseMoveEvent(QMouseEvent* event) {
    const int index = day_at(event->position().x());
    if (index != hover_) {
        hover_ = index;
        update();
    }
    if (index >= 0)
        QToolTip::showText(event->globalPosition().toPoint(), points_[index].tip, this);
    else
        QToolTip::hideText();
}

void KrxTraderDaysChart::leaveEvent(QEvent*) {
    hover_ = -1;
    QToolTip::hideText();
    update();
}

KrxTraderScreen::KrxTraderScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("krxTraderScreen");
    live_ = new services::KrxTraderLive(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = krx_panel::header(
        tr("KRX 자동 매매"),
        tr("엔진의 자동 매매 trader(005930, 삼중 장벽)의 모드, 상태, 잔고, 포지션, 손익, 한도와 매매입니다. trader가 "
           "연속매매 동안 포지션이 있으면 2초마다, 없으면 5초마다, 그 밖에는 5분마다 다시 읽고, 숨으면 읽지 않습니다. "
           "이 화면에서 할 수 있는 쓰기는 정지 두 가지뿐이고, 재개는 서버에서만 됩니다."));
    dot_ = new QLabel;
    dot_->setFixedSize(8, 8);
    header_.row->insertWidget(2, dot_);
    mode_badge_ = new QLabel;
    header_.row->insertWidget(3, mode_badge_);
    status_badge_ = new QLabel;
    header_.row->insertWidget(4, status_badge_);
    header_line_ = new QLabel;
    header_.row->insertWidget(5, header_line_);
    reload_ = tool_button(tr("새로 읽기"), tr("지금 다시 읽습니다(날마다와 매매도)"));
    header_.controls->addWidget(reload_);
    connect(reload_, &QPushButton::clicked, this, [this] { live_->read_now(); });
    layout->addWidget(header_.bar);

    failure_ = new QLabel;
    failure_->setContentsMargins(12, 4, 12, 4);
    failure_->setWordWrap(true);
    failure_->hide();
    layout->addWidget(failure_);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget;
    body->setObjectName("krxTraderBody");
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(1);

    // The cards: the capital, the cash, the position (twice as wide), the P&Ls and the limits.
    auto* cards = new QWidget;
    auto* cards_layout = new QHBoxLayout(cards);
    cards_layout->setContentsMargins(0, 0, 0, 0);
    cards_layout->setSpacing(1);
    for (int i = 0; i < 7; ++i) {
        CardView card;
        card.box = new QFrame;
        card.box->setObjectName("krxTraderCard");
        card.box->setAttribute(Qt::WA_StyledBackground);
        auto* card_layout = new QVBoxLayout(card.box);
        card_layout->setContentsMargins(12, 8, 12, 10);
        card_layout->setSpacing(2);
        card.title = new QLabel;
        card.value = new QLabel;
        card.lines = new QLabel;
        card.lines->setWordWrap(true);
        card_layout->addWidget(card.title);
        card_layout->addWidget(card.value);
        card_layout->addWidget(card.lines);
        card_layout->addStretch(1);
        cards_layout->addWidget(card.box, i == 2 ? 2 : 1);
        cards_ << card;
    }
    body_layout->addWidget(cards);

    auto* middle = new QHBoxLayout;
    middle->setContentsMargins(0, 0, 0, 0);
    middle->setSpacing(1);
    days_header_ = krx_panel::header(
        tr("날마다 순손익"),
        tr("trader가 돈 거래일마다 청산한 매매의 순손익(수수료와 매도세를 뺌)을 막대로, 보이는 첫날부터의 합을 선으로 "
           "그립니다. 손실 한도에 닿은 날은 빨간 테두리입니다. 날에 마우스를 올리면 그날의 한도 사용과 상태가 나옵니다. "
           "모드마다 따로입니다."));
    mode_box_ = new QComboBox;
    mode_box_->setToolTip(tr("모드"));
    days_header_.controls->addWidget(mode_box_);
    connect(mode_box_, &QComboBox::activated, this, [this](int index) {
        mode_ = mode_box_->itemData(index).toString();
        fill_days();
    });
    auto* days_body = new QWidget;
    auto* days_layout = new QVBoxLayout(days_body);
    days_layout->setContentsMargins(0, 0, 0, 0);
    days_layout->setSpacing(0);
    days_line_ = new QLabel;
    days_line_->setContentsMargins(12, 6, 12, 2);
    days_layout->addWidget(days_line_);
    chart_ = new KrxTraderDaysChart;
    days_layout->addWidget(chart_, 1);
    middle->addWidget(krx_panel::panel(days_header_, days_body), 3);

    pause_header_ = krx_panel::header(
        tr("정지"), tr("화면에서 할 수 있는 쓰기는 이 둘뿐입니다(엔진의 POST /v1/trader/pause). 둘 다 확인 창을 거치고, "
                       "trader가 1초 안에 읽습니다. 다시 켜는 것은 서버에서만 됩니다."));
    auto* pause_body = new QWidget;
    auto* pause_layout = new QVBoxLayout(pause_body);
    pause_layout->setContentsMargins(12, 8, 12, 8);
    pause_layout->setSpacing(6);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    pause_entries_ = new QPushButton(tr("새 진입 정지"));
    pause_entries_->setObjectName(kPauseEntriesName);
    pause_entries_->setCursor(Qt::PointingHandCursor);
    pause_sell_ = new QPushButton(tr("정지하고 즉시 매도"));
    pause_sell_->setObjectName(kPauseSellName);
    pause_sell_->setCursor(Qt::PointingHandCursor);
    buttons->addWidget(pause_entries_);
    buttons->addWidget(pause_sell_);
    buttons->addStretch(1);
    pause_layout->addLayout(buttons);
    connect(pause_entries_, &QPushButton::clicked, this, [this] { ask_pause(false); });
    connect(pause_sell_, &QPushButton::clicked, this, [this] { ask_pause(true); });
    resume_note_ = new QLabel(view::resume_note());
    resume_note_->setWordWrap(true);
    pause_layout->addWidget(resume_note_);
    pause_note_ = new QLabel;
    pause_note_->setWordWrap(true);
    pause_note_->hide();
    pause_layout->addWidget(pause_note_);
    auto* recent = new QLabel(tr("최근 요청"));
    recent->setObjectName("krxTraderRecent");
    pause_layout->addWidget(recent);
    controls_ = krx_tables::table({tr("시각"), tr("요청"), tr("누가"), tr("trader")});
    controls_->horizontalHeader()->setStretchLastSection(true);
    if (auto* item = controls_->horizontalHeaderItem(2))
        item->setToolTip(tr("화면: 이 화면의 정지. 서버: 서버의 krx-quant trade resume·rearm"));
    if (auto* item = controls_->horizontalHeaderItem(3))
        item->setToolTip(tr("trader가 그 요청을 읽은 때. trader가 멈춰 있으면 다시 뜰 때 읽습니다."));
    pause_layout->addWidget(controls_);
    pause_layout->addStretch(1);
    middle->addWidget(krx_panel::panel(pause_header_, pause_body), 2);
    body_layout->addLayout(middle);

    trades_header_ = krx_panel::header(
        tr("매매"),
        tr("최근 매매 100개, 진입이 늦은 것부터입니다. 진입은 매도 1호가에 사고, 목표가(익절), 손절가(손절), 시간 장벽(시간 "
           "청산) 가운데 먼저 닿은 것에 팝니다. 정지하고 즉시 매도(정지 매도)와 손실 한도(하루·누적 한도)도 그때의 매수 "
           "1호가에 팝니다. 비용은 수수료 양쪽과 매도세입니다. 줄의 툴팁에 기준가, 장벽, 정한 때, 크기가 있습니다."));
    trades_line_ = new QLabel;
    trades_header_.controls->addWidget(trades_line_);
    trades_ = krx_tables::table({tr("진입"), tr("청산"), tr("수량"), tr("청산 이유"), tr("비용"), tr("순손익"), tr("칸"),
                                 tr("확률"), tr("p*"), tr("하한"), tr("모드")});
    trades_->horizontalHeader()->setStretchLastSection(false);
    const QList<std::pair<int, QString>> tips{
        {kEntry, tr("매수 체결 시각과 가격(매도 1호가)")},
        {kExit, tr("매도 체결 시각과 가격. 아직 들고 있으면 보유 중")},
        {kReason, tr("익절(목표가), 손절(손절가), 시간 청산(시간 장벽), 정지 매도(화면의 정지하고 즉시 매도), 하루 한도, "
                     "누적 한도")},
        {kCost, tr("매수·매도 수수료와 매도세(원). 들고 있는 동안은 매수 수수료만")},
        {kNet, tr("매도 금액 − 매수 금액 − 비용(원). 들고 있는 동안은 지금 매수 1호가에 팔 때의 평가")},
        {kCell, tr("산 칸: 예측 종류와 예측자(비용 지도의 자리: 합의, 1위, 기준)")},
        {kProbability, tr("그 예측의 오름 확률(방향은 상승, 장벽은 위 먼저)과 사는 문턱 θ")},
        {kPStar, tr("비용 지도가 그 칸을 고를 때 시장가 왕복의 손익분기 맞힘률")},
        {kLow, tr("그 칸의 맞힘률 95% 하한(Wilson). p*를 넘는 칸만 매매합니다")},
        {kMode, tr("모의(내부 모의), KIS 모의, 실전")},
    };
    for (const auto& [column, tip] : tips)
        if (auto* item = trades_->horizontalHeaderItem(column))
            item->setToolTip(tip);
    body_layout->addWidget(krx_panel::panel(trades_header_, trades_));
    body_layout->addStretch(1); // the policy versions and the live gate go here (codeyoma/krx-quant#401)
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, [this] {
        show_dot();
        fill_cards(); // the time left to the time barrier
    });
    connect(live_, &services::KrxTraderLive::changed, this, [this] { render(); });
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this] { refresh_theme(); });
    refresh_theme();
}

void KrxTraderScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    live_->start();
    clock_timer_->start();
}

void KrxTraderScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    live_->stop();
    clock_timer_->stop();
}

void KrxTraderScreen::refresh_theme() {
    setStyleSheet(QString("#krxTraderScreen,#krxTraderBody{background:%1;}"
                          "#krxTraderScreen QLabel{background:transparent;color:%2;}"
                          "#krxTraderCard{background:%4;}"
                          "#krxTraderScreen QPushButton{background:transparent;color:%2;border:1px solid transparent;"
                          "padding:1px 6px;font-weight:700;}"
                          "#krxTraderScreen QPushButton:hover{border-color:%3;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED(),
                           ui::colors::BG_SURFACE()));
    for (const auto* header : {&header_, &days_header_, &pause_header_, &trades_header_})
        header->restyle();
    for (auto* table : {controls_, trades_})
        krx_tables::restyle(table);
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    for (auto* label : {header_line_, days_line_, resume_note_, trades_line_})
        label->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    if (auto* recent = findChild<QLabel*>("krxTraderRecent"))
        recent->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::TEXT_SECONDARY()));
    // The pause buttons stand out from the flat ones: framed, the sale's in the loss colour.
    const QString button("QPushButton#%1{color:%2;border:1px solid %2;border-radius:3px;padding:4px 12px;}"
                         "QPushButton#%1:hover{background:%3;}"
                         "QPushButton#%1:disabled{color:%4;border-color:%5;}");
    pause_entries_->setStyleSheet(button.arg(kPauseEntriesName, ui::colors::WARNING(), ui::colors::BG_RAISED(),
                                             ui::colors::TEXT_DIM(), ui::colors::BORDER_DIM()));
    pause_sell_->setStyleSheet(button.arg(kPauseSellName, ui::colors::NEGATIVE(), ui::colors::BG_RAISED(),
                                          ui::colors::TEXT_DIM(), ui::colors::BORDER_DIM()));
    dot_->setProperty("krxColor", QVariant());
    render();
}

void KrxTraderScreen::render() {
    const QString failure = live_->failure();
    failure_->setText(live_->state() && !failure.isEmpty()
                          ? tr("읽지 못함: %1 (아래는 %2에 읽은 것)").arg(failure, live_->answered_at().toString("HH:mm:ss"))
                          : failure);
    failure_->setVisible(!failure.isEmpty());
    show_dot();
    fill_header();
    fill_cards();
    fill_days();
    fill_pause();
    fill_trades();
}

void KrxTraderScreen::show_dot() {
    QString color = ui::colors::TEXT_DIM();
    QString tip = tr("읽는 중");
    if (!live_->failure().isEmpty()) {
        color = ui::colors::NEGATIVE();
        tip = tr("읽지 못함: %1").arg(live_->failure());
    } else if (live_->state()) {
        color = ui::colors::POSITIVE();
        tip = tr("엔진 응답 %1").arg(live_->answered_at().toString("HH:mm:ss"));
    }
    if (live_->running() && !live_->reading())
        tip += tr("\n다음 읽기: %1초 뒤").arg(live_->next_ms() / 1000);
    if (dot_->property("krxColor").toString() != color) {
        dot_->setStyleSheet(QString("background:%1;border-radius:4px;").arg(color));
        dot_->setProperty("krxColor", color);
    }
    dot_->setToolTip(tip);
}

void KrxTraderScreen::fill_header() {
    const auto& s = live_->state();
    if (!s) {
        mode_badge_->hide();
        status_badge_->hide();
        header_line_->setText(live_->reading() ? tr("읽는 중") : QString());
        return;
    }
    for (const auto& [label, badge] : {std::pair{mode_badge_, view::mode_badge(*s)},
                                       std::pair{status_badge_, view::status_badge(*s)}}) {
        label->setText(badge.text);
        label->setToolTip(badge.tip);
        label->setStyleSheet(badge_style(badge.tone));
        label->show();
    }
    header_line_->setText(view::header_line(*s));
}

void KrxTraderScreen::fill_cards() {
    const auto& s = live_->state();
    const krx_quant::TraderState none;
    const QDateTime now = view::trader_now(s ? *s : none, live_->answered_at(), live_->now());
    const QList<view::Card> cards = view::cards(s ? *s : none, now);
    const int size = ui::ThemeManager::instance().current_font().pixelSize();
    // Every second (the time left): a style is set again only when it changes.
    const auto restyle = [](QLabel* label, const QString& style) {
        if (label->property("krxStyle").toString() != style) {
            label->setStyleSheet(style);
            label->setProperty("krxStyle", style);
        }
    };
    for (qsizetype i = 0; i < cards_.size() && i < cards.size(); ++i) {
        const CardView& card = cards_[i];
        card.title->setText(cards[i].title);
        restyle(card.title, QString("color:%1;font-size:%2px;font-weight:700;")
                                .arg(ui::colors::TEXT_SECONDARY())
                                .arg(std::max(9, size - 2)));
        card.value->setText(cards[i].value);
        restyle(card.value,
                QString("color:%1;font-size:%2px;font-weight:700;").arg(tone_color(cards[i].tone)).arg(size + 6));
        card.lines->setText(cards[i].lines.join('\n'));
        restyle(card.lines, QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        card.lines->setVisible(!cards[i].lines.isEmpty());
        card.box->setToolTip(cards[i].tip);
    }
}

void KrxTraderScreen::fill_days() {
    const auto& d = live_->days();
    const QList<krx_quant::TraderDay> days = d ? *d : QList<krx_quant::TraderDay>{};
    const QStringList modes = view::day_modes(days);
    if (!modes.contains(mode_))
        mode_ = view::default_mode(days, live_->state());
    {
        const QSignalBlocker quiet(mode_box_); // a refill is not a choice
        mode_box_->clear();
        for (const auto& mode : modes) {
            mode_box_->addItem(krx_quant::trader_mode_label(mode), mode);
            if (mode == mode_)
                mode_box_->setCurrentIndex(mode_box_->count() - 1);
        }
        mode_box_->setVisible(modes.size() > 1);
    }
    const auto points = view::day_points(days, mode_);
    days_line_->setText(d ? view::days_line(points, mode_) : (live_->reading() ? tr("읽는 중") : QString()));
    chart_->set_points(points);
}

void KrxTraderScreen::fill_pause() {
    const auto& s = live_->state();
    for (const auto& [button, sell_now] : {std::pair{pause_entries_, false}, std::pair{pause_sell_, true}}) {
        const view::Action action = view::pause_action(sell_now, s);
        button->setEnabled(action.enabled && !live_->sending());
        button->setToolTip(action.tip);
    }
    pause_note_->setText(live_->pause_note());
    pause_note_->setStyleSheet(
        QString("color:%1;font-weight:700;").arg(live_->pause_failed() ? ui::colors::NEGATIVE() : ui::colors::WARNING()));
    pause_note_->setVisible(!live_->pause_note().isEmpty());
    controls_->setRowCount(0);
    if (s) {
        for (const auto& row : view::control_rows(*s)) {
            const int r = controls_->rowCount();
            controls_->insertRow(r);
            controls_->setItem(r, 0, cell(row.at));
            controls_->setItem(r, 1, cell(row.what));
            controls_->setItem(r, 2, cell(row.who));
            controls_->setItem(r, 3, toned(row.applied, row.waiting ? Tone::Warning : Tone::Dim));
        }
    }
    fit_height(controls_, 6);
}

void KrxTraderScreen::fill_trades() {
    trades_->setRowCount(0);
    const auto& t = live_->trades();
    trades_line_->setText(t ? tr("%1개").arg(t->size()) : QString());
    if (!t) {
        fit_height(trades_, kTradesShown);
        return;
    }
    for (const auto& row : view::trade_rows(*t, live_->state())) {
        const int r = trades_->rowCount();
        trades_->insertRow(r);
        trades_->setItem(r, kEntry, cell(row.entry, kNumber));
        trades_->setItem(r, kExit, toned(row.exit, row.held ? Tone::Warning : Tone::Plain, {}, kNumber));
        trades_->setItem(r, kQuantity, cell(row.quantity, kNumber));
        trades_->setItem(r, kReason, toned(row.reason, row.reason_tone));
        trades_->setItem(r, kCost, toned(row.cost, Tone::Plain, row.cost_tip, kNumber));
        trades_->setItem(r, kNet, toned(row.net, row.net_tone, row.net_tip, kNumber));
        trades_->setItem(r, kCell, cell(row.cell));
        trades_->setItem(r, kProbability, toned(row.probability, Tone::Plain, row.why_tip, kNumber));
        trades_->setItem(r, kPStar, toned(row.p_star, row.p_star == QLatin1String("—") ? Tone::Dim : Tone::Plain,
                                          row.why_tip, kNumber));
        trades_->setItem(r, kLow,
                         toned(row.low, row.low == QLatin1String("—") ? Tone::Dim : Tone::Plain, row.why_tip, kNumber));
        trades_->setItem(r, kMode, toned(row.mode, row.live ? Tone::Live : Tone::Simulated));
        for (const int column : {int(kEntry), int(kExit), int(kQuantity), int(kCell)})
            trades_->item(r, column)->setToolTip(row.tip);
    }
    fit_height(trades_, kTradesShown);
}

void KrxTraderScreen::ask_pause(bool sell_now) {
    // Asked in a confirm window, its safe answer the default; sent only on its button (KrxTraderLive::pause).
    live_->pause(sell_now, [this, sell_now](const view::Question& question) {
        QMessageBox box(this);
        box.setIcon(sell_now ? QMessageBox::Warning : QMessageBox::Question);
        box.setWindowTitle(question.title);
        box.setText(question.text);
        auto* yes = box.addButton(question.accept, QMessageBox::AcceptRole);
        auto* no = box.addButton(tr("취소"), QMessageBox::RejectRole);
        box.setDefaultButton(no);
        box.setEscapeButton(no);
        box.exec();
        return box.clickedButton() == yes;
    });
}

} // namespace fincept::screens
