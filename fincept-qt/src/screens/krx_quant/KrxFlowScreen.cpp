#include "screens/krx_quant/KrxFlowScreen.h"

#include "screens/krx_quant/KrxSymbolCell.h"
#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/KrxFlowLive.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace {

using krx_quant::flow::Series;
using krx_tables::cell;
using krx_tables::kNumber;
namespace view = krx_quant::flow;

constexpr std::array<Series, view::kSeries> kAll = {Series::Price, Series::Program, Series::Member,
                                                    Series::EstimateForeign, Series::EstimateInstitution};

QPushButton* tool_button(const QString& text, const QString& tip) {
    auto* button = new QPushButton(text);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    return button;
}

QString change_color(const std::optional<double>& value) {
    if (!value || *value == 0)
        return ui::colors::TEXT_PRIMARY();
    return *value > 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE();
}

} // namespace

// ---- Chart ---------------------------------------------------------------------------------------

KrxFlowChart::KrxFlowChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(260);
    setToolTip(tr("끌어서 그 구간을 확대합니다. 두 번 누르면 하루 전체로 돌아갑니다."));
}

QString KrxFlowChart::color_of(Series series) {
    switch (series) {
        case Series::Price:
            return ui::colors::TEXT_PRIMARY();
        case Series::Program:
            return ui::colors::AMBER();
        case Series::Member:
            return ui::colors::INFO();
        case Series::EstimateForeign:
            return ui::colors::CYAN();
        case Series::EstimateInstitution:
            return ui::colors::WARNING();
    }
    return ui::colors::TEXT_PRIMARY();
}

void KrxFlowChart::set_flow(const std::optional<krx_quant::Flow>& flow, const QDateTime& now) {
    const bool same_day = flow && flow_ && flow->symbol == flow_->symbol && flow->day == flow_->day;
    flow_ = flow;
    now_ = now;
    if (!same_day && flow_) { // another symbol or day: the session so far
        from_ = flow_->open.toSecsSinceEpoch();
        to_ = whole_span();
    }
    hover_ = -1;
    update();
}

void KrxFlowChart::set_shown(Series series, bool shown) {
    shown_[static_cast<std::size_t>(series)] = shown;
    update();
}

void KrxFlowChart::restyle() {
    update();
}

QRectF KrxFlowChart::plot() const {
    return QRectF(rect()).adjusted(70, 10, -28, -24);
}

double KrxFlowChart::x_of(qint64 at) const {
    const QRectF area = plot();
    const auto span = static_cast<double>(std::max<qint64>(to_ - from_, 1));
    return area.left() + area.width() * static_cast<double>(at - from_) / span;
}

qint64 KrxFlowChart::at_of(double x) const {
    const QRectF area = plot();
    const double share = std::clamp((x - area.left()) / std::max(area.width(), 1.0), 0.0, 1.0);
    return from_ + static_cast<qint64>(std::llround(share * static_cast<double>(to_ - from_)));
}

int KrxFlowChart::minute_at(double x) const {
    if (!flow_ || flow_->minutes.isEmpty() || !plot().contains(QPointF(x, plot().center().y())))
        return -1;
    const qint64 at = at_of(x);
    const qint64 first = flow_->minutes.first().at.toSecsSinceEpoch();
    const auto index = static_cast<int>((at - first) / 60);
    return index >= 0 && index < flow_->minutes.size() ? index : -1;
}

void KrxFlowChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(ui::colors::BG_BASE()));
    const QRectF area = plot();
    p.setPen(QColor(ui::colors::BORDER_DIM()));
    p.drawRect(area);
    if (!flow_) {
        p.setPen(QColor(ui::colors::TEXT_DIM()));
        p.drawText(area, Qt::AlignCenter, tr("읽는 중…"));
        return;
    }
    if (!flow_->recorded) {
        p.setPen(QColor(ui::colors::TEXT_DIM()));
        p.drawText(area, Qt::AlignCenter, tr("이 날의 녹화가 서버에 없습니다"));
    }
    // The special windows: shaded under the lines.
    for (const auto& w : flow_->special) {
        const double left = std::max(x_of(w.start.toSecsSinceEpoch()), area.left());
        const double right = std::min(x_of((w.end.isValid() ? w.end : flow_->close).toSecsSinceEpoch()), area.right());
        if (right > left) {
            QColor shade(ui::colors::AMBER());
            shade.setAlphaF(0.10f);
            p.fillRect(QRectF(left, area.top(), right - left, area.height()), shade);
        }
    }
    // The time axis: every 30 minutes.
    p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
    QDateTime tick = flow_->open;
    while (tick <= flow_->close) {
        const qint64 at = tick.toSecsSinceEpoch();
        if (at >= from_ && at <= to_) {
            const double x = x_of(at);
            p.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + 4));
            p.drawText(QRectF(x - 30, area.bottom() + 4, 60, 18), Qt::AlignHCenter | Qt::AlignTop,
                       krx_quant::kst_text(tick, "HH:mm"));
        }
        tick = tick.addSecs(30 * 60);
    }
    // Each series on its own scale over the span shown (the header's boxes name them, in their colors).
    for (const Series series : kAll) {
        if (!shown_[static_cast<std::size_t>(series)])
            continue;
        const auto segments = view::segments(*flow_, series, now_);
        double low = 0, high = 0;
        bool any = false;
        for (const auto& segment : segments)
            for (const auto& point : segment)
                if (point.at >= from_ && point.at <= to_) {
                    low = any ? std::min(low, point.value) : point.value;
                    high = any ? std::max(high, point.value) : point.value;
                    any = true;
                }
        const QString color = color_of(series);
        if (!any)
            continue;
        if (series != Series::Price) { // a flow's sign matters: keep its zero in view
            low = std::min(low, 0.0);
            high = std::max(high, 0.0);
        }
        const double span = std::max(high - low, 1e-9);
        const auto y_of = [&](double value) { return area.bottom() - 6 - (area.height() - 12) * (value - low) / span; };
        if (series != Series::Price) {
            QPen zero(QColor(color), 1, Qt::DashLine);
            zero.setColor(QColor(color).darker(160));
            p.setPen(zero);
            p.drawLine(QPointF(area.left(), y_of(0)), QPointF(area.right(), y_of(0)));
        } else {
            p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
            p.drawText(QRectF(0, y_of(high) - 9, area.left() - 6, 18), Qt::AlignRight | Qt::AlignVCenter,
                       view::value_text(high, series));
            p.drawText(QRectF(0, y_of(low) - 9, area.left() - 6, 18), Qt::AlignRight | Qt::AlignVCenter,
                       view::value_text(low, series));
        }
        p.setPen(QPen(QColor(color), series == Series::Price ? 1.4 : 1.2));
        p.save();
        p.setClipRect(area);
        for (const auto& segment : segments) {
            QPainterPath path;
            for (qsizetype i = 0; i < segment.size(); ++i) {
                const QPointF point(x_of(segment[i].at), y_of(segment[i].value));
                i == 0 ? path.moveTo(point) : path.lineTo(point);
            }
            if (segment.size() == 1)
                p.drawEllipse(path.currentPosition(), 1.5, 1.5);
            p.drawPath(path);
        }
        p.restore();
    }
    // The drag being made.
    if (drag_from_ >= 0 && drag_to_ >= 0) {
        QColor band(ui::colors::TEXT_PRIMARY());
        band.setAlphaF(0.08f);
        p.fillRect(QRectF(std::min(drag_from_, drag_to_), area.top(), std::abs(drag_to_ - drag_from_), area.height()),
                   band);
    }
    // The hover: a line at the minute and its values.
    if (hover_ >= 0) {
        const double x = x_of(flow_->minutes[hover_].at.toSecsSinceEpoch());
        p.setPen(QPen(QColor(ui::colors::BORDER_MED()), 1, Qt::DotLine));
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        const QStringList lines = view::hover_lines(*flow_, hover_);
        const int line_height = p.fontMetrics().height();
        int width = 0;
        for (const auto& line : lines)
            width = std::max(width, p.fontMetrics().horizontalAdvance(line));
        QRectF box(x + 10, area.top() + 8, width + 16, line_height * lines.size() + 10);
        if (box.right() > area.right())
            box.moveRight(x - 10);
        p.fillRect(box, QColor(ui::colors::BG_RAISED()));
        p.setPen(QColor(ui::colors::BORDER_MED()));
        p.drawRect(box);
        for (qsizetype i = 0; i < lines.size(); ++i) {
            p.setPen(QColor(i == 0 ? ui::colors::TEXT_SECONDARY() : color_of(kAll[static_cast<std::size_t>(i - 1)])));
            p.drawText(QPointF(box.left() + 8, box.top() + 5 + line_height * (i + 1) - 4), lines[i]);
        }
    }
}

void KrxFlowChart::mouseMoveEvent(QMouseEvent* event) {
    const double x = event->position().x();
    if (drag_from_ >= 0)
        drag_to_ = x;
    const int index = minute_at(x);
    if (index != hover_ || drag_from_ >= 0) {
        hover_ = index;
        update();
    }
}

void KrxFlowChart::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && plot().contains(event->position())) {
        drag_from_ = event->position().x();
        drag_to_ = -1;
    }
}

void KrxFlowChart::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && drag_from_ >= 0) {
        const double to = event->position().x();
        if (std::abs(to - drag_from_) > 8) { // a drag, not a click
            const qint64 a = at_of(std::min(drag_from_, to)), b = at_of(std::max(drag_from_, to));
            if (b - a >= 5 * 60) { // at least five minutes
                from_ = a;
                to_ = b;
            }
        }
        drag_from_ = drag_to_ = -1;
        update();
    }
}

void KrxFlowChart::mouseDoubleClickEvent(QMouseEvent*) {
    if (flow_) {
        from_ = flow_->open.toSecsSinceEpoch();
        to_ = whole_span();
        update();
    }
}

qint64 KrxFlowChart::whole_span() const {
    // While the session goes on, up to half an hour past now: the rest of the day would be empty.
    const QDateTime as_of = flow_->as_of;
    if (as_of > flow_->open && as_of < flow_->close)
        return std::min(as_of.addSecs(30 * 60), flow_->close).toSecsSinceEpoch();
    return flow_->close.toSecsSinceEpoch();
}

void KrxFlowChart::leaveEvent(QEvent*) {
    hover_ = -1;
    update();
}

// ---- Screen --------------------------------------------------------------------------------------

KrxFlowScreen::KrxFlowScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("krxFlowScreen");
    live_ = new services::KrxFlowLive(this);
    logos_ = new KrxLogos(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ =
        krx_panel::header(tr("KRX 수급 흐름"),
                          tr("한 종목의 그날 장중 수급: 외국인·기관 추정 가집계, 프로그램매매, 외국계 회원사를 1분 "
                             "가격 위에 겹쳐 봅니다. "
                             "장중에는 30초마다, 그 밖에는 5분마다 다시 읽습니다. 관찰용이고 예측 신호가 아닙니다."));
    dot_ = new QLabel;
    dot_->setFixedSize(8, 8);
    header_.row->insertWidget(2, dot_);
    symbol_box_ = new QComboBox;
    symbol_box_->setToolTip(tr("종목"));
    symbol_box_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    symbol_box_->setMinimumContentsLength(14);
    header_.row->insertWidget(3, symbol_box_);
    day_line_ = new QLabel;
    header_.row->insertWidget(4, day_line_);
    previous_ = tool_button(QStringLiteral("◀"), tr("전 거래일 쪽으로 하루"));
    next_ = tool_button(QStringLiteral("▶"), tr("다음 날"));
    today_ = tool_button(tr("오늘"), tr("엔진의 오늘(휴장이면 지난 거래일)"));
    reload_ = tool_button(tr("새로 읽기"), tr("지금 다시 읽습니다"));
    for (auto* button : {previous_, next_, today_, reload_})
        header_.controls->addWidget(button);
    connect(previous_, &QPushButton::clicked, this, [this] { move_day(-1); });
    connect(next_, &QPushButton::clicked, this, [this] { move_day(1); });
    connect(today_, &QPushButton::clicked, this, [this] { move_day(0); });
    connect(reload_, &QPushButton::clicked, this, [this] { live_->read_now(); });
    connect(symbol_box_, &QComboBox::activated, this,
            [this](int index) { live_->set_symbol(symbol_box_->itemData(index).toString()); });
    layout->addWidget(header_.bar);

    failure_ = new QLabel;
    failure_->setContentsMargins(12, 4, 12, 4);
    failure_->setWordWrap(true);
    failure_->hide();
    layout->addWidget(failure_);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(1);

    chart_header_ = krx_panel::header(
        tr("흐름"),
        tr("각 흐름은 그날 누적이고 자기 눈금입니다(가격은 왼쪽). 점선은 그 흐름의 0, 주황 음영은 특수 상황입니다. "
           "값이 없는 분은 선을 끊습니다. 추정은 들어온 때부터 다음 구간까지의 계단입니다."));
    chart_header_.controls->setSpacing(12);
    for (const Series series : kAll) {
        auto* toggle = new QCheckBox(view::series_label(series));
        toggle->setChecked(true);
        toggle->setToolTip(tr("단위: %1").arg(view::series_unit(series)));
        connect(toggle, &QCheckBox::toggled, this, [this, series](bool on) { chart_->set_shown(series, on); });
        toggles_ << toggle;
        chart_header_.controls->addWidget(toggle);
    }
    chart_ = new KrxFlowChart;
    body->addWidget(krx_panel::panel(chart_header_, chart_), 3);

    auto* side = new QWidget;
    auto* side_layout = new QVBoxLayout(side);
    side_layout->setContentsMargins(0, 0, 0, 0);
    side_layout->setSpacing(1);
    totals_header_ =
        krx_panel::header(tr("지금까지의 합"), tr("각 흐름의 그날 마지막 값과 전 거래일 같은 시각까지의 값"));
    auto* totals_body = new QWidget;
    auto* totals_layout = new QVBoxLayout(totals_body);
    totals_layout->setContentsMargins(0, 0, 0, 0);
    totals_layout->setSpacing(0);
    totals_table_ = krx_tables::table({tr("흐름"), tr("오늘"), tr("전일")});
    totals_layout->addWidget(totals_table_);
    final_ = new QLabel;
    final_->setContentsMargins(12, 4, 12, 6);
    final_->setWordWrap(true);
    final_->setToolTip(tr("장중 가집계는 증권사가 입력한 추정치라 확정 일별 수급(20:30 뒤)과 다릅니다"));
    totals_layout->addWidget(final_);
    side_layout->addWidget(krx_panel::panel(totals_header_, totals_body));

    members_header_ =
        krx_panel::header(tr("회원사 상위 5"), tr("그날 마지막 회원사 기록의 매도·매수 상위 창구. 외국계는 주황"));
    auto* members_body = new QWidget;
    auto* members_layout = new QHBoxLayout(members_body);
    members_layout->setContentsMargins(0, 0, 0, 0);
    members_layout->setSpacing(1);
    sellers_ = krx_tables::table({tr("매도"), tr("수량")});
    buyers_ = krx_tables::table({tr("매수"), tr("수량")});
    members_layout->addWidget(sellers_);
    members_layout->addWidget(buyers_);
    side_layout->addWidget(krx_panel::panel(members_header_, members_body), 1);
    body->addWidget(side, 2);
    layout->addLayout(body, 1);

    for (auto* table : {totals_table_, sellers_, buyers_})
        table->horizontalHeader()->setStretchLastSection(false);

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, [this] { show_dot(); });
    connect(live_, &services::KrxFlowLive::changed, this, [this] { render(); });
    connect(logos_, &KrxLogos::arrived, this, [this] { fill_symbols(); });
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this] { refresh_theme(); });
    refresh_theme();
}

void KrxFlowScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    live_->start();
    clock_timer_->start();
}

void KrxFlowScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    live_->stop();
    clock_timer_->stop();
}

void KrxFlowScreen::refresh_theme() {
    setStyleSheet(QString("#krxFlowScreen{background:%1;}"
                          "#krxFlowScreen QLabel,#krxFlowScreen QCheckBox{background:transparent;color:%2;}"
                          "#krxFlowScreen QPushButton{background:transparent;color:%2;border:1px solid transparent;"
                          "padding:1px 6px;font-weight:700;}"
                          "#krxFlowScreen QPushButton:hover{border-color:%3;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED()));
    for (const auto* header : {&header_, &chart_header_, &totals_header_, &members_header_})
        header->restyle();
    for (auto* table : {totals_table_, sellers_, buyers_})
        krx_tables::restyle(table);
    for (qsizetype i = 0; i < toggles_.size(); ++i)
        toggles_[i]->setStyleSheet(
            QString("QCheckBox{color:%1;font-weight:700;spacing:5px;padding-right:12px;}"
                    "QCheckBox::indicator{width:10px;height:10px;border:1px solid %2;background:%3;}"
                    "QCheckBox::indicator:checked{background:%1;border-color:%1;}")
                .arg(KrxFlowChart::color_of(kAll[static_cast<std::size_t>(i)]), ui::colors::BORDER_MED(),
                     ui::colors::BG_SURFACE()));
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    day_line_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    dot_->setProperty("krxColor", QVariant());
    chart_->restyle();
    render();
}

void KrxFlowScreen::render() {
    const auto& f = live_->flow();
    const QString failure = live_->failure();
    failure_->setText(
        f ? tr("읽지 못함: %1 (아래는 %2에 읽은 것)").arg(failure, live_->answered_at().toString("HH:mm:ss"))
          : failure);
    failure_->setVisible(!failure.isEmpty());
    day_line_->setText(f ? view::day_line(*f) : QString());
    show_dot();
    fill_symbols();
    chart_->set_flow(f, f ? f->as_of : QDateTime());
    fill_totals();
    fill_members();
}

void KrxFlowScreen::show_dot() {
    QString color = ui::colors::TEXT_DIM();
    QString tip = tr("읽는 중");
    if (!live_->failure().isEmpty()) {
        color = ui::colors::NEGATIVE();
        tip = tr("읽지 못함: %1").arg(live_->failure());
    } else if (live_->flow()) {
        color = ui::colors::POSITIVE();
        tip = tr("엔진 응답 %1 · 엔진 시계 %2")
                  .arg(live_->answered_at().toString("HH:mm:ss"),
                       krx_quant::kst_text(live_->flow()->as_of, "MM-dd HH:mm:ss"));
    }
    if (live_->running() && !live_->reading())
        tip += tr("\n다음 읽기: %1초마다").arg(live_->next_ms() / 1000);
    if (dot_->property("krxColor").toString() != color) {
        dot_->setStyleSheet(QString("background:%1;border-radius:4px;").arg(color));
        dot_->setProperty("krxColor", color);
    }
    dot_->setToolTip(tip);
}

void KrxFlowScreen::fill_symbols() {
    const QSignalBlocker quiet(symbol_box_); // a refill is not a choice
    symbol_box_->clear();
    const int size = symbol_box_->fontMetrics().height();
    symbol_box_->setIconSize(QSize(size, size));
    for (const auto& quote : live_->symbols()) {
        const QString text =
            quote.name.isEmpty() ? quote.symbol : QStringLiteral("%1 %2").arg(quote.name, quote.symbol);
        symbol_box_->addItem(logos_->logo(quote.logo_url, quote.name, size, devicePixelRatioF()), text, quote.symbol);
        if (quote.symbol == live_->symbol())
            symbol_box_->setCurrentIndex(symbol_box_->count() - 1);
    }
}

void KrxFlowScreen::fill_totals() {
    totals_table_->setRowCount(0);
    final_->clear();
    const auto& f = live_->flow();
    if (!f)
        return;
    if (auto* header = totals_table_->horizontalHeaderItem(2))
        header->setText(view::previous_header(*f));
    for (const auto& row : view::total_rows(*f)) {
        const int r = totals_table_->rowCount();
        totals_table_->insertRow(r);
        totals_table_->setItem(r, 0, cell(row.flow));
        auto* today = cell(row.today, kNumber);
        today->setForeground(QColor(change_color(row.value)));
        totals_table_->setItem(r, 1, today);
        totals_table_->setItem(r, 2, cell(row.previous, kNumber));
    }
    final_->setText(view::final_line(*f));
}

void KrxFlowScreen::fill_members() {
    for (auto* table : {sellers_, buyers_})
        table->setRowCount(0);
    const auto& f = live_->flow();
    if (!f || !f->members) {
        members_header_.title->setText(tr("회원사 상위 5"));
        return;
    }
    members_header_.title->setText(tr("회원사 상위 5 · %1%2")
                                       .arg(krx_quant::kst_text(f->members->received_at, "HH:mm"),
                                            f->members->source == QLatin1String("rest") ? tr(" (REST)") : QString()));
    const QList<std::pair<QTableWidget*, QList<krx_quant::FlowMember>>> sides = {{sellers_, f->members->sell},
                                                                                 {buyers_, f->members->buy}};
    for (const auto& [table, members] : sides) {
        for (const auto& row : view::member_rows(members)) {
            const int r = table->rowCount();
            table->insertRow(r);
            auto* name = cell(row.name);
            auto* qty = cell(row.qty, kNumber);
            if (row.foreign) {
                name->setForeground(QColor(ui::colors::AMBER()));
                name->setToolTip(tr("외국계 회원사"));
            }
            table->setItem(r, 0, name);
            table->setItem(r, 1, qty);
        }
    }
}

void KrxFlowScreen::move_day(int offset) {
    if (offset == 0)
        return live_->set_day({});
    const auto& f = live_->flow();
    const QDate from = live_->day().isValid() ? live_->day() : (f ? f->day : QDate::currentDate());
    QDate day = from.addDays(offset);
    while (day.dayOfWeek() > 5) // weekends: the engine answers 404; holidays it reports as such
        day = day.addDays(offset);
    live_->set_day(day);
}

} // namespace fincept::screens
