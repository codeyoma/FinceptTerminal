#include "screens/krx_quant/KrxBriefingScreen.h"

#include "screens/krx_quant/KrxSymbolCell.h"
#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/BriefingView.h"
#include "services/krx_quant/KrxBriefingLive.h"
#include "ui/theme/Theme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::screens {

namespace {

using krx_tables::cell;
using krx_tables::kNumber;
namespace view = krx_quant::briefing;

QString change_color(const std::optional<double>& pct) {
    if (!pct || *pct == 0)
        return ui::colors::TEXT_PRIMARY();
    return *pct > 0 ? ui::colors::POSITIVE() : ui::colors::NEGATIVE();
}

QTableWidgetItem* number_cell(const QString& text, const std::optional<double>& pct = std::nullopt,
                              const QString& tip = {}) {
    auto* item = cell(text, kNumber);
    if (pct)
        item->setForeground(QColor(change_color(pct)));
    item->setToolTip(tip);
    return item;
}

QPushButton* tool_button(const QString& text, const QString& tip) {
    auto* button = new QPushButton(text);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    return button;
}

} // namespace

/// The night session's minutes as a line, last price on the right (codeyoma/krx-quant#304).
class KrxNightLine : public QWidget {
  public:
    using QWidget::QWidget;

    void set_minutes(QList<krx_quant::NightMinute> minutes, const std::optional<double>& pct) {
        minutes_ = std::move(minutes);
        color_ = change_color(pct);
        update();
    }

    QSize sizeHint() const override { return {240, 56}; }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (minutes_.size() < 2)
            return;
        const auto [low, high] = std::minmax_element(minutes_.begin(), minutes_.end(),
                                                     [](const auto& a, const auto& b) { return a.price < b.price; });
        const double span = std::max(high->price - low->price, 1e-9);
        const qint64 start = minutes_.first().at.toSecsSinceEpoch();
        const auto length = static_cast<double>(std::max<qint64>(minutes_.last().at.toSecsSinceEpoch() - start, 1));
        const QRectF area = QRectF(rect()).adjusted(4, 6, -4, -6);
        QPainterPath path;
        for (int i = 0; i < minutes_.size(); ++i) {
            const auto since = static_cast<double>(minutes_[i].at.toSecsSinceEpoch() - start);
            const double x = area.left() + area.width() * since / length;
            const double y = area.bottom() - area.height() * (minutes_[i].price - low->price) / span;
            i == 0 ? path.moveTo(x, y) : path.lineTo(x, y);
        }
        p.setPen(QPen(QColor(color_), 1.5));
        p.drawPath(path);
    }

  private:
    QList<krx_quant::NightMinute> minutes_;
    QString color_;
};

KrxBriefingScreen::KrxBriefingScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("krxBriefingScreen");
    live_ = new services::KrxBriefingLive(this);
    logos_ = new KrxLogos(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = krx_panel::header(
        tr("KRX 장전 브리핑"),
        tr("개장 전 그날의 재료: 밤사이 미국과 국면, 야간선물, 종목마다 장 마감 뒤·프리마켓·예상 시가·"
           "전일 수급, 08:50 시가→종가 예측과 그 입력, 오늘 일정. 07:00–09:10에는 30초마다, "
           "그 밖에는 5분마다 다시 읽습니다. 판단 재료이고 검증된 신호가 아닙니다."));
    dot_ = new QLabel;
    dot_->setFixedSize(8, 8);
    header_.row->insertWidget(2, dot_); // right after the title
    headline_ = new QLabel;
    header_.row->insertWidget(3, headline_);
    previous_ = tool_button(QStringLiteral("◀"), tr("전날"));
    next_ = tool_button(QStringLiteral("▶"), tr("다음 날"));
    today_ = tool_button(tr("오늘"), tr("엔진의 오늘(휴장이면 다음 거래일)"));
    reload_ = tool_button(tr("새로 읽기"), tr("지금 다시 읽습니다"));
    for (auto* button : {previous_, next_, today_, reload_})
        header_.controls->addWidget(button);
    connect(previous_, &QPushButton::clicked, this, [this] { move_day(-1); });
    connect(next_, &QPushButton::clicked, this, [this] { move_day(1); });
    connect(today_, &QPushButton::clicked, this, [this] { move_day(0); });
    connect(reload_, &QPushButton::clicked, this, [this] { live_->read_now(); });
    layout->addWidget(header_.bar);

    failure_ = new QLabel;
    failure_->setContentsMargins(12, 4, 12, 4);
    failure_->setWordWrap(true);
    failure_->hide();
    layout->addWidget(failure_);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(1);

    // Overnight US and the day's regime.
    us_header_ =
        krx_panel::header(tr("밤사이 미국 · 국면"), tr("그날 아침 07:30 매크로 읽기와 미국 일봉(그날 전 마지막 5개)"));
    auto* us_body = new QWidget;
    auto* us_layout = new QVBoxLayout(us_body);
    us_layout->setContentsMargins(0, 0, 0, 0);
    us_layout->setSpacing(0);
    us_table_ = krx_tables::table({tr("지표"), tr("마지막 종가"), tr("등락"), tr("최근 5일 등락(%)")});
    us_layout->addWidget(us_table_, 1);
    regime_ = new QLabel;
    regime_->setContentsMargins(12, 4, 12, 6);
    regime_->setWordWrap(true);
    us_layout->addWidget(regime_);
    grid->addWidget(krx_panel::panel(us_header_, us_body), 0, 0);

    // The KOSPI200 night session.
    night_header_ =
        krx_panel::header(tr("KOSPI200 야간선물"), tr("전 거래일 저녁에 시작한 야간 세션(18:00–06:00)의 녹화"));
    auto* night_body = new QWidget;
    auto* night_layout = new QVBoxLayout(night_body);
    night_layout->setContentsMargins(12, 6, 12, 6);
    night_text_ = new QLabel;
    night_text_->setWordWrap(true);
    night_layout->addWidget(night_text_);
    night_line_ = new KrxNightLine;
    night_layout->addWidget(night_line_, 1);
    grid->addWidget(krx_panel::panel(night_header_, night_body), 0, 1);

    // Each symbol.
    symbols_header_ = krx_panel::header(
        tr("종목"), tr("전일 KRX 종가 대비. 장 마감 뒤는 전일 20:00 전 마지막 통합 1분봉, 프리마켓은 cox와 같은 규칙"
                       "(08:40–08:50, 없으면 08:00–08:30), 예상 시가는 시가 단일가(개장 30분 전부터)의 마지막 예상가"));
    symbols_table_ =
        krx_tables::table({tr("종목"), tr("전일 종가"), tr("장 마감 뒤"), tr("NXT 프리마켓"), tr("KRX 예상 시가"),
                           tr("외국인"), tr("기관"), tr("외국인 5일"), tr("기관 5일")});
    symbols_table_->setItemDelegateForColumn(0, new KrxSymbolDelegate(symbols_table_));
    grid->addWidget(krx_panel::panel(symbols_header_, symbols_table_), 1, 0, 1, 2);

    // The 08:50 open-to-close forecast and its inputs.
    forecast_header_ = krx_panel::header(tr("08:50 시가→종가"));
    forecast_table_ =
        krx_tables::table({tr("종목"), tr("예측자"), tr("상승"), tr("보합"), tr("하락"), tr("보합 폭"), tr("결과")});
    forecast_table_->setItemDelegateForColumn(0, new KrxSymbolDelegate(forecast_table_));
    grid->addWidget(krx_panel::panel(forecast_header_, forecast_table_), 2, 0);
    inputs_header_ = krx_panel::header(
        tr("입력 기여"),
        tr("고른 종목의 08:50 예측이 입력마다 받은 몫: 상승·하락 점수(보합 대비 로짓)에 더한 값. 큰 순서. "
           "종목·예측 표의 줄을 누르면 그 종목을 봅니다."));
    inputs_table_ = krx_tables::table({tr("입력"), tr("값"), tr("표준화"), tr("상승 점수"), tr("하락 점수"), tr("쪽")});
    grid->addWidget(krx_panel::panel(inputs_header_, inputs_table_), 2, 1);
    for (auto* table : {symbols_table_, forecast_table_})
        connect(table, &QTableWidget::cellClicked, this, [this, table](int row, int) {
            if (auto* item = table->item(row, 0)) {
                selected_ = item->data(Qt::UserRole).toString();
                fill_inputs();
            }
        });

    // The day's calendar.
    events_header_ = krx_panel::header(tr("오늘 일정"), tr("경제 캘린더(그날 KST)와 그날의 표시"));
    auto* events_body = new QWidget;
    auto* events_layout = new QVBoxLayout(events_body);
    events_layout->setContentsMargins(0, 0, 0, 0);
    events_layout->setSpacing(0);
    flags_ = new QLabel;
    flags_->setContentsMargins(12, 4, 12, 4);
    events_layout->addWidget(flags_);
    events_table_ =
        krx_tables::table({tr("시각"), tr("나라"), tr("일정"), tr("중요도"), tr("예상"), tr("이전"), tr("실제")});
    events_layout->addWidget(events_table_, 1);
    grid->addWidget(krx_panel::panel(events_header_, events_body), 3, 0, 1, 2);

    grid->setRowStretch(0, 2);
    grid->setRowStretch(1, 2);
    grid->setRowStretch(2, 3);
    grid->setRowStretch(3, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid, 1);

    // Columns as wide as their contents, none stretched to the edge: a number column at the end would
    // otherwise sit far from its header's neighbours.
    for (auto* table : {us_table_, symbols_table_, forecast_table_, inputs_table_, events_table_})
        table->horizontalHeader()->setStretchLastSection(false);

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, [this] { show_dot(); });
    connect(live_, &services::KrxBriefingLive::changed, this, [this] { render(); });
    connect(logos_, &KrxLogos::arrived, this, [this] {
        fill_symbols();
        fill_forecasts();
    });
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this] { refresh_theme(); });
    refresh_theme();
}

void KrxBriefingScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    live_->start();
    clock_timer_->start();
}

void KrxBriefingScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    live_->stop();
    clock_timer_->stop();
}

void KrxBriefingScreen::refresh_theme() {
    setStyleSheet(QString("#krxBriefingScreen{background:%1;}"
                          "#krxBriefingScreen QLabel{background:transparent;color:%2;}"
                          "#krxBriefingScreen QPushButton{background:transparent;color:%2;border:1px solid transparent;"
                          "padding:1px 6px;font-weight:700;}"
                          "#krxBriefingScreen QPushButton:hover{border-color:%3;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED()));
    for (const auto* header :
         {&header_, &us_header_, &night_header_, &symbols_header_, &forecast_header_, &inputs_header_, &events_header_})
        header->restyle();
    for (auto* table : {us_table_, symbols_table_, forecast_table_, inputs_table_, events_table_})
        krx_tables::restyle(table);
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    headline_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    dot_->setProperty("krxColor", QVariant()); // the dot in the new colors
    render();                                  // the cells' colors are the theme's when they are filled
}

void KrxBriefingScreen::render() {
    const auto& b = live_->briefing();
    const QString failure = live_->failure();
    failure_->setText(
        b ? tr("읽지 못함: %1 (아래는 %2에 읽은 것)").arg(failure, live_->answered_at().toString("HH:mm:ss"))
          : failure);
    failure_->setVisible(!failure.isEmpty());
    headline_->setText(b ? view::headline(*b) : QString());
    show_dot();
    fill_us();
    if (b) {
        const auto line = view::night_line(b->night);
        night_text_->setText(line.text);
        night_text_->setToolTip(line.tip);
        night_header_.title->setText(tr("KOSPI200 야간선물 · %1 밤").arg(b->night.evening.toString("MM-dd")));
        night_line_->set_minutes(b->night.minutes, line.pct);
    } else {
        night_text_->clear();
        night_line_->set_minutes({}, std::nullopt);
    }
    fill_symbols();
    fill_forecasts();
    fill_inputs();
    fill_events();
}

void KrxBriefingScreen::show_dot() {
    QString color = ui::colors::TEXT_DIM();
    QString tip = tr("읽는 중");
    if (!live_->failure().isEmpty()) {
        color = ui::colors::NEGATIVE();
        tip = tr("읽지 못함: %1").arg(live_->failure());
    } else if (live_->briefing()) {
        color = ui::colors::POSITIVE();
        tip = tr("엔진 응답 %1 · 엔진 시계 %2")
                  .arg(live_->answered_at().toString("HH:mm:ss"),
                       krx_quant::kst_text(live_->briefing()->as_of, "MM-dd HH:mm:ss"));
    }
    if (live_->running() && !live_->reading())
        tip += tr("\n다음 읽기: %1초마다").arg(live_->next_ms() / 1000);
    if (dot_->property("krxColor").toString() != color) {
        dot_->setStyleSheet(QString("background:%1;border-radius:4px;").arg(color));
        dot_->setProperty("krxColor", color);
    }
    dot_->setToolTip(tip);
}

QTableWidgetItem* KrxBriefingScreen::symbol_item(QTableWidget* table, const QString& symbol) {
    const auto quote = live_->symbols().value(symbol);
    auto* item = cell(quote.name.isEmpty() ? symbol : quote.name);
    item->setData(Qt::UserRole, symbol);
    if (!quote.name.isEmpty())
        item->setData(KrxSymbolDelegate::kCodeRole, symbol);
    if (!quote.logo_url.isEmpty() || !quote.name.isEmpty()) {
        const int size = table->fontMetrics().height(); // as high as the name
        table->setIconSize(QSize(size, size));
        item->setIcon(logos_->logo(quote.logo_url, quote.name, size, devicePixelRatioF()));
    }
    return item;
}

void KrxBriefingScreen::fill_us() {
    us_table_->setRowCount(0);
    regime_->clear();
    const auto& b = live_->briefing();
    if (!b)
        return;
    for (const auto& row : view::us_rows(*b)) {
        const int r = us_table_->rowCount();
        us_table_->insertRow(r);
        us_table_->setItem(r, 0, cell(row.name));
        us_table_->setItem(r, 1, number_cell(row.last, std::nullopt, row.tip));
        us_table_->setItem(r, 2, number_cell(row.change, row.pct));
        us_table_->setItem(r, 3, cell(row.days));
    }
    const auto& macro = b->macro;
    if (macro.regime.isEmpty()) {
        regime_->setText(tr("국면: 그날 매크로 읽기 없음"));
        return;
    }
    regime_->setText(tr("국면 %1 (%2 %3 결정)%4")
                         .arg(krx_quant::regime_label(macro.regime), macro.day,
                              krx_quant::kst_text(macro.decided_at, "HH:mm"),
                              macro.reasons.isEmpty() ? QString() : " · " + macro.reasons.join(" · ")));
}

void KrxBriefingScreen::fill_symbols() {
    const QSignalBlocker quiet(symbols_table_);
    symbols_table_->setRowCount(0);
    const auto& b = live_->briefing();
    if (!b)
        return;
    for (const auto& row : view::symbol_rows(*b)) {
        const int r = symbols_table_->rowCount();
        symbols_table_->insertRow(r);
        symbols_table_->setItem(r, 0, symbol_item(symbols_table_, row.symbol));
        symbols_table_->setItem(r, 1, number_cell(row.previous_close));
        symbols_table_->setItem(r, 2, number_cell(row.after_hours, row.after_hours_pct, row.after_hours_tip));
        symbols_table_->setItem(r, 3, number_cell(row.premarket, row.premarket_pct, row.premarket_tip));
        symbols_table_->setItem(r, 4, number_cell(row.expected, row.expected_pct, row.expected_tip));
        symbols_table_->setItem(r, 5, number_cell(row.foreign, std::nullopt, row.supply_tip));
        symbols_table_->setItem(r, 6, number_cell(row.institution, std::nullopt, row.supply_tip));
        symbols_table_->setItem(r, 7, number_cell(row.foreign_sum, std::nullopt, row.supply_tip));
        symbols_table_->setItem(r, 8, number_cell(row.institution_sum, std::nullopt, row.supply_tip));
    }
}

void KrxBriefingScreen::fill_forecasts() {
    const QSignalBlocker quiet(forecast_table_);
    forecast_table_->setRowCount(0);
    const auto& b = live_->briefing();
    forecast_header_.title->setText(tr("08:50 시가→종가"));
    if (!b || !b->open_to_close)
        return;
    const auto& otc = *b->open_to_close;
    const bool previous = otc.previous;
    if (otc.day.isValid())
        forecast_header_.title->setText(
            tr("08:50 시가→종가 · %1%2").arg(otc.day.toString("MM-dd"), previous ? tr(" (지난 거래일)") : QString()));
    for (const auto& row : view::forecast_rows(*b)) {
        const int r = forecast_table_->rowCount();
        forecast_table_->insertRow(r);
        forecast_table_->setItem(r, 0, symbol_item(forecast_table_, row.symbol));
        forecast_table_->setItem(
            r, 1, cell(row.status.isEmpty() ? row.algorithm : tr("%1 (%2)").arg(row.algorithm, row.status)));
        const QList<std::pair<QString, krx_quant::Direction>> classes = {{row.up, krx_quant::Direction::Up},
                                                                         {row.flat, krx_quant::Direction::Flat},
                                                                         {row.down, krx_quant::Direction::Down}};
        for (int i = 0; i < classes.size(); ++i) {
            auto* item = cell(classes[i].first, kNumber);
            if (row.lean == classes[i].second) {
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
            forecast_table_->setItem(r, 2 + i, item);
        }
        forecast_table_->setItem(r, 5, cell(row.band, kNumber));
        forecast_table_->setItem(r, 6, cell(row.result));
        if (previous)
            for (int c = 0; c < forecast_table_->columnCount(); ++c)
                if (auto* item = forecast_table_->item(r, c))
                    item->setForeground(QColor(ui::colors::TEXT_DIM()));
    }
}

void KrxBriefingScreen::fill_inputs() {
    inputs_table_->setRowCount(0);
    const auto& b = live_->briefing();
    inputs_header_.title->setText(tr("입력 기여"));
    if (!b || b->symbols.isEmpty())
        return;
    const QString symbol = selected_.isEmpty() ? b->symbols.first().symbol : selected_;
    const auto inputs = view::inputs_of(*b, symbol);
    const auto quote = live_->symbols().value(symbol);
    inputs_header_.title->setText(tr("입력 기여 · %1").arg(quote.name.isEmpty() ? symbol : quote.name));
    if (!inputs)
        return;
    inputs_header_.title->setText(
        tr("입력 기여 · %1 · %2")
            .arg(quote.name.isEmpty() ? symbol : quote.name, krx_quant::predictor_label(inputs->algorithm)));
    for (const auto& row : view::input_rows(*inputs)) {
        const int r = inputs_table_->rowCount();
        inputs_table_->insertRow(r);
        auto* name = cell(row.name);
        name->setToolTip(row.key);
        inputs_table_->setItem(r, 0, name);
        inputs_table_->setItem(r, 1, cell(row.value, kNumber));
        inputs_table_->setItem(r, 2, cell(row.z, kNumber));
        inputs_table_->setItem(r, 3, cell(row.up, kNumber));
        inputs_table_->setItem(r, 4, cell(row.down, kNumber));
        auto* lean = cell(row.lean);
        if (row.lean == tr("상승 쪽"))
            lean->setForeground(QColor(ui::colors::POSITIVE()));
        else if (row.lean == tr("하락 쪽"))
            lean->setForeground(QColor(ui::colors::NEGATIVE()));
        inputs_table_->setItem(r, 5, lean);
    }
}

void KrxBriefingScreen::fill_events() {
    events_table_->setRowCount(0);
    flags_->clear();
    const auto& b = live_->briefing();
    if (!b)
        return;
    const QStringList flags = view::flags(*b);
    flags_->setText(flags.isEmpty() ? tr("오늘 표시 없음") : flags.join("  ·  "));
    flags_->setStyleSheet(
        QString("color:%1;font-weight:700;").arg(flags.isEmpty() ? ui::colors::TEXT_TERTIARY() : ui::colors::AMBER()));
    for (const auto& row : view::event_rows(*b)) {
        const int r = events_table_->rowCount();
        events_table_->insertRow(r);
        const QStringList cells = {row.time,     row.country,  row.title, row.importance,
                                   row.forecast, row.previous, row.actual};
        for (int c = 0; c < cells.size(); ++c) {
            auto* item = cell(cells[c], c >= 4 ? kNumber : Qt::AlignLeft | Qt::AlignVCenter);
            if (row.high && c == 3)
                item->setForeground(QColor(ui::colors::AMBER()));
            events_table_->setItem(r, c, item);
        }
    }
}

void KrxBriefingScreen::move_day(int offset) {
    if (offset == 0)
        return live_->set_day({});
    const auto& b = live_->briefing();
    const QDate from = live_->day().isValid() ? live_->day() : (b ? b->day : QDate::currentDate());
    live_->set_day(from.addDays(offset));
}

} // namespace fincept::screens
