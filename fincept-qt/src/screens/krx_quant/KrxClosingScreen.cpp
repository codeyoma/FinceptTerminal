#include "screens/krx_quant/KrxClosingScreen.h"

#include "screens/krx_quant/KrxSymbolCell.h"
#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/ClosingView.h"
#include "services/krx_quant/KrxClosingLive.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::screens {

namespace {

using krx_quant::closing::Side;
using krx_tables::cell;
using krx_tables::kNumber;
namespace view = krx_quant::closing;

/// The days table's columns; the demand's are hidden for a symbol without products.
enum DayColumn {
    kDate,
    kPeriod,
    kDemand,
    kRatio,
    kAuction,
    kAuctionSource, ///< where the day's closing auction value came from (codeyoma/krx-quant#368)
    kAgree,
    kNextGap,
    kNextClose,
    kSpecial,
    kSource,
    kDayColumns
};
/// Rows the days table shows before it scrolls on its own: the page stays short enough to reach the
/// summary under it.
constexpr int kDaysShown = 15;

QPushButton* tool_button(const QString& text, const QString& tip) {
    auto* button = new QPushButton(text);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    return button;
}

QString side_color(Side side) {
    switch (side) {
        case Side::Buy:
            return ui::colors::POSITIVE();
        case Side::Sell:
            return ui::colors::NEGATIVE();
        case Side::None:
            break;
    }
    return ui::colors::TEXT_PRIMARY();
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

QTableWidgetItem* side_cell(const QString& text, Side side, const QString& tip = {}) {
    auto* item = cell(text, kNumber);
    item->setForeground(QColor(side_color(side)));
    item->setToolTip(tip);
    return item;
}

} // namespace

KrxClosingScreen::KrxClosingScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("krxClosingScreen");
    live_ = new services::KrxClosingLive(this);
    logos_ = new KrxLogos(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = krx_panel::header(
        tr("KRX 마감 흐름"),
        tr("단일종목 ±2배 ETF(005930·000660 기초 16종)가 장 마감에 노출을 순자산의 L배로 맞추려고 사고파는 양, "
           "A × (L² − L) × r을 공식으로 계산한 값과 그 뒤 종가 단일가·다음날에 일어난 일입니다. 009150은 상품이 "
           "없는 대조군입니다. 연속매매 동안은 60초마다, 그 밖에는 열 때와 날이 바뀔 때 다시 읽습니다. 예측 신호가 "
           "아닙니다."));
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
    krx_label_ = new QLabel(QString::fromUtf8(view::kKrxLabel));
    krx_label_->setToolTip(tr("보이는 순자산의 일부가 한국거래소(KRX) Open API의 ETF 일별매매정보에서 왔습니다"));
    krx_label_->hide();
    reload_ = tool_button(tr("새로 읽기"), tr("지금 다시 읽습니다(지난날 표도)"));
    header_.controls->addWidget(krx_label_);
    header_.controls->addWidget(reload_);
    connect(reload_, &QPushButton::clicked, this, [this] { live_->read_now(); });
    connect(symbol_box_, &QComboBox::activated, this,
            [this](int index) { live_->set_symbol(symbol_box_->itemData(index).toString()); });
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
    body->setObjectName("krxClosingBody");
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(1);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(1);
    card_header_ = krx_panel::header(
        tr("오늘의 마감 수요"),
        tr("상품 k의 수요 d = A × (L² − L) × r: 2배는 2·A·r, −2배는 6·A·r이고 둘 다 r과 같은 쪽입니다(+ 매수, "
           "− 매도). A는 전 거래일 순자산, r은 기준가 ÷ 전 거래일 KRX 종가 − 1입니다."));
    auto* card_body = new QWidget;
    auto* card_layout = new QVBoxLayout(card_body);
    card_layout->setContentsMargins(0, 0, 0, 0);
    card_layout->setSpacing(0);
    headline_ = new QLabel;
    headline_->setContentsMargins(12, 8, 12, 8);
    headline_->setWordWrap(true);
    card_layout->addWidget(headline_);
    card_ = krx_tables::table({QString(), QString()});
    card_->horizontalHeader()->hide();
    card_->horizontalHeader()->setStretchLastSection(true);
    card_layout->addWidget(card_);
    notes_ = new QLabel;
    notes_->setContentsMargins(12, 6, 12, 8);
    notes_->setWordWrap(true);
    card_layout->addWidget(notes_);
    card_layout->addStretch(1);
    top->addWidget(krx_panel::panel(card_header_, card_body), 2);

    products_header_ = krx_panel::header(
        tr("상품"), tr("이 종목을 기초로 한 단일종목 ±2배 ETF. 순자산을 받지 못한 상품은 \"없음\"이고 합에서 "
                       "빠집니다. 인버스(−2배)는 계수가 6이라 오른 날 마감 매수가 큽니다."));
    products_ = krx_tables::table({tr("코드"), tr("이름"), tr("배율"), tr("구조"), tr("계수 L²−L"), tr("전일 순자산"),
                                   tr("날짜"), tr("출처"), tr("수요")});
    products_->horizontalHeader()->setStretchLastSection(false);
    if (auto* item = products_->horizontalHeaderItem(4))
        item->setToolTip(tr("수요 = 순자산 × 계수 × r. 2배는 2, −2배(인버스)는 6"));
    if (auto* item = products_->horizontalHeaderItem(6))
        item->setToolTip(tr("순자산이 며칠 자 값인지(전 거래일의 것)"));
    auto* products_body = new QWidget; // the table as tall as its rows, the rest of the row's height under it
    auto* products_layout = new QVBoxLayout(products_body);
    products_layout->setContentsMargins(0, 0, 0, 0);
    products_layout->setSpacing(0);
    products_layout->addWidget(products_);
    products_layout->addStretch(1);
    products_panel_ = krx_panel::panel(products_header_, products_body);
    top->addWidget(products_panel_, 3);
    products_filler_ = new QWidget; // in the products' place for a symbol without them: the card keeps its width
    products_filler_->hide();
    top->addWidget(products_filler_, 3);
    body_layout->addLayout(top);

    days_header_ = krx_panel::header(
        tr("지난날"),
        tr("날마다 15:20 수요와 단일가 비율, 종가 단일가 움직임(종가 ÷ 15:20 직전가 − 1), 수요와 단일가 방향이 "
           "같았는지, 다음날 시가 갭과 종가 → 종가입니다. 정의는 구조 실측(structure-check, #270)과 같습니다. "
           "엔진이 매일 20:30 뒤에 한 번 계산합니다."));
    days_line_ = new QLabel;
    days_header_.controls->addWidget(days_line_);
    days_ = krx_tables::table({tr("날짜"), tr("기간"), tr("15:20 수요"), tr("단일가 비율"), tr("단일가 움직임"),
                               tr("체결대금"), tr("방향"), tr("다음날 갭"), tr("다음날 종가"), tr("특수 상황"),
                               tr("수요 출처")});
    days_->horizontalHeader()->setStretchLastSection(false);
    if (auto* item = days_->horizontalHeaderItem(kRatio))
        item->setToolTip(tr("현물+선물형 수요 ÷ 그날 전 20거래일의 종가 단일가 체결대금 중앙값: 상한. 날마다의 "
                            "체결대금은 녹화한 KRX 체결, 녹화가 없으면 KRX 1분봉의 단일가 봉, 그것도 없으면 통합 "
                            "1분봉의 단일가 봉입니다. 체결대금이 있는 날이 없으면 분모가 없습니다(체결대금 없음)."));
    if (auto* item = days_->horizontalHeaderItem(kAuctionSource))
        item->setToolTip(tr("그날 종가 단일가 체결대금의 출처: 녹화(녹화한 KRX 체결, 가격 × 수량, 2026-10-12부터), "
                            "KRX 봉(KRX 1분봉의 단일가 봉, 거래량 × 종가), 통합 봉(통합 1분봉의 단일가 봉). 녹화가 "
                            "있던 9일에는 두 봉의 값이 녹화와 1% 안에서 같았습니다(엔진의 실측, 2026-10-23)."));
    if (auto* item = days_->horizontalHeaderItem(kSource))
        item->setToolTip(tr("15:20 수요를 어떻게 얻었는지(저장한 15:20 값, 1분봉으로 계산한 값). 칸의 툴팁에 그날 "
                            "순자산의 출처가 있습니다."));
    if (auto* item = days_->horizontalHeaderItem(kAgree))
        item->setToolTip(tr("sign(15:20 수요) = sign(단일가 움직임). 수요가 없는 날은 그날 흐름과 견줍니다(흐름)"));
    if (auto* item = days_->horizontalHeaderItem(kSpecial))
        item->setToolTip(tr("그날 시작한 VI·서킷브레이커·사이드카: 단일가가 길어졌을 수 있습니다"));
    body_layout->addWidget(krx_panel::panel(days_header_, days_));

    summary_header_ = krx_panel::header(
        tr("기간 요약"),
        tr("기간(상장 전, 상장 뒤, 규제 뒤, 애프터마켓 뒤)과 비율 3분위마다 단일가 방향 일치 비율, 다음날 반대 비율, "
           "표본 수입니다. 특수 상황이 있던 날을 넣은 것과 뺀 것을 둘 다 봅니다. 20일 미만의 칸은 흐립니다."));
    QStringList summary_columns{tr("묶음"), tr("날")};
    summary_columns << view::summary_headers();
    summary_ = krx_tables::table(summary_columns);
    summary_->horizontalHeader()->setStretchLastSection(false);
    body_layout->addWidget(krx_panel::panel(summary_header_, summary_));
    body_layout->addStretch(1);
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, [this] { show_dot(); });
    connect(live_, &services::KrxClosingLive::changed, this, [this] { render(); });
    connect(logos_, &KrxLogos::arrived, this, [this] { fill_symbols(); });
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this] { refresh_theme(); });
    refresh_theme();
}

void KrxClosingScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    live_->start();
    clock_timer_->start();
}

void KrxClosingScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    live_->stop();
    clock_timer_->stop();
}

void KrxClosingScreen::refresh_theme() {
    setStyleSheet(QString("#krxClosingScreen,#krxClosingBody{background:%1;}"
                          "#krxClosingScreen QLabel{background:transparent;color:%2;}"
                          "#krxClosingScreen QPushButton{background:transparent;color:%2;border:1px solid transparent;"
                          "padding:1px 6px;font-weight:700;}"
                          "#krxClosingScreen QPushButton:hover{border-color:%3;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED()));
    for (const auto* header : {&header_, &card_header_, &products_header_, &days_header_, &summary_header_})
        header->restyle();
    for (auto* table : {card_, products_, days_, summary_})
        krx_tables::restyle(table);
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    for (auto* label : {day_line_, days_line_, notes_})
        label->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    krx_label_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::AMBER()));
    dot_->setProperty("krxColor", QVariant());
    render();
}

void KrxClosingScreen::render() {
    const auto& f = live_->flow();
    const QString failure = live_->failure();
    failure_->setText(
        f && !failure.isEmpty()
            ? tr("읽지 못함: %1 (아래는 %2에 읽은 것)").arg(failure, live_->answered_at().toString("HH:mm:ss"))
            : failure);
    failure_->setVisible(!failure.isEmpty());
    day_line_->setText(f ? view::day_line(*f) : (live_->reading() ? tr("읽는 중") : QString()));
    krx_label_->setVisible(view::shows_krx(f, live_->days()));
    show_dot();
    fill_symbols();
    fill_card();
    fill_products();
    fill_days();
    fill_summary();
}

void KrxClosingScreen::show_dot() {
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
        tip += tr("\n다음 읽기: %1초 뒤").arg(live_->next_ms() / 1000);
    if (dot_->property("krxColor").toString() != color) {
        dot_->setStyleSheet(QString("background:%1;border-radius:4px;").arg(color));
        dot_->setProperty("krxColor", color);
    }
    dot_->setToolTip(tip);
}

void KrxClosingScreen::fill_symbols() {
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

void KrxClosingScreen::fill_card() {
    card_->setRowCount(0);
    const auto& f = live_->flow();
    if (!f) {
        headline_->clear();
        notes_->clear();
        fit_height(card_);
        return;
    }
    const int size = ui::ThemeManager::instance().current_font().pixelSize() + 6;
    headline_->setText(view::headline(*f));
    const bool demand = f->demand.has_value();
    headline_->setStyleSheet(QString("color:%1;font-size:%2px;font-weight:700;")
                                 .arg(demand ? side_color(view::side_of(f->demand)) : ui::colors::TEXT_SECONDARY())
                                 .arg(demand ? size : size - 6));
    headline_->setToolTip(demand ? tr("두 구조의 합(현물+선물형 + 선물형)") : QString());
    for (const auto& line : view::card_lines(*f)) {
        const int r = card_->rowCount();
        card_->insertRow(r);
        auto* label = cell(line.label);
        label->setToolTip(line.tip);
        auto* value = cell(line.value, kNumber);
        value->setToolTip(line.tip);
        value->setForeground(QColor(line.dim ? ui::colors::TEXT_DIM() : side_color(line.side)));
        card_->setItem(r, 0, label);
        card_->setItem(r, 1, value);
    }
    fit_height(card_);
    notes_->setText(view::notes(*f).join('\n'));
}

void KrxClosingScreen::fill_products() {
    products_->setRowCount(0);
    const auto& f = live_->flow();
    const bool listed = !f || !f->products.isEmpty(); // 009150: no products to list
    products_panel_->setVisible(listed);
    products_filler_->setVisible(!listed);
    if (!f) {
        fit_height(products_);
        return;
    }
    for (const auto& row : view::product_rows(*f)) {
        const int r = products_->rowCount();
        products_->insertRow(r);
        const QStringList cells{row.code,        row.name,       row.leverage,        row.structure,
                                row.coefficient, row.net_assets, row.net_assets_date, row.source,
                                row.demand};
        for (int c = 0; c < cells.size(); ++c) {
            auto* item = cell(cells[c], c >= 4 && c != 7 ? kNumber : Qt::AlignLeft | Qt::AlignVCenter);
            item->setToolTip(row.tip);
            if (row.missing && (c == 5 || c == 8))
                item->setForeground(QColor(ui::colors::TEXT_DIM()));
            products_->setItem(r, c, item);
        }
        if (row.inverse) { // the coefficient of 6, where a rise means a large buy at the close
            auto* coefficient = products_->item(r, 4);
            coefficient->setForeground(QColor(ui::colors::AMBER()));
            QFont bold = coefficient->font();
            bold.setBold(true);
            coefficient->setFont(bold);
        }
        if (!row.missing)
            products_->item(r, 8)->setForeground(QColor(side_color(row.side)));
        if (row.source == QString::fromUtf8(view::kKrxLabel))
            products_->item(r, 7)->setForeground(QColor(ui::colors::AMBER()));
    }
    fit_height(products_);
}

void KrxClosingScreen::fill_days() {
    days_->setRowCount(0);
    const auto& d = live_->days();
    const auto& f = live_->flow();
    // A symbol without products (009150, f->reason "no_products"): the auction and the next day only.
    const bool has_products = !f || !f->products.isEmpty();
    for (const int column : {kDemand, kRatio, kSource})
        days_->setColumnHidden(column, !has_products);
    days_line_->setText(d ? view::days_line(*d) : QString());
    if (!d) {
        fit_height(days_, kDaysShown);
        return;
    }
    for (const auto& row : view::day_rows(*d, has_products)) {
        const int r = days_->rowCount();
        days_->insertRow(r);
        days_->setItem(r, kDate, cell(row.date));
        days_->setItem(r, kPeriod, cell(row.period));
        auto* demand = side_cell(row.demand, row.side, row.demand_tip);
        if (row.side == Side::None)
            demand->setForeground(QColor(ui::colors::TEXT_DIM()));
        days_->setItem(r, kDemand, demand);
        auto* ratio = cell(row.ratio, kNumber);
        ratio->setToolTip(row.ratio_tip);
        if (row.ratio_missing)
            ratio->setForeground(QColor(ui::colors::TEXT_DIM()));
        days_->setItem(r, kRatio, ratio);
        days_->setItem(r, kAuction, side_cell(row.auction, row.auction_side, row.auction_tip));
        auto* auction_source = cell(row.auction_source);
        auction_source->setToolTip(row.auction_source_tip);
        if (row.auction_source != QLatin1String("녹화"))
            auction_source->setForeground(QColor(ui::colors::TEXT_SECONDARY()));
        days_->setItem(r, kAuctionSource, auction_source);
        auto* agree = cell(row.agree);
        agree->setToolTip(row.agree_tip);
        agree->setForeground(QColor(!row.agreed   ? ui::colors::TEXT_DIM()
                                    : *row.agreed ? ui::colors::POSITIVE()
                                                  : ui::colors::NEGATIVE()));
        days_->setItem(r, kAgree, agree);
        days_->setItem(r, kNextGap, cell(row.next_gap, kNumber));
        days_->setItem(r, kNextClose, cell(row.next_close, kNumber));
        auto* special = cell(row.special);
        if (!row.special.isEmpty())
            special->setForeground(QColor(ui::colors::AMBER()));
        days_->setItem(r, kSpecial, special);
        auto* source = cell(row.source);
        source->setToolTip(row.source_tip); // whose net assets (codeyoma/krx-quant#368)
        days_->setItem(r, kSource, source);
    }
    fit_height(days_, kDaysShown);
}

void KrxClosingScreen::fill_summary() {
    summary_->setRowCount(0);
    const auto& d = live_->days();
    if (d) {
        for (const auto& row : view::summary_rows(*d)) {
            const int r = summary_->rowCount();
            summary_->insertRow(r);
            auto* group = cell(row.group);
            group->setToolTip(row.group_tip);
            if (row.without_special)
                group->setForeground(QColor(ui::colors::TEXT_SECONDARY()));
            summary_->setItem(r, 0, group);
            auto* scope = cell(row.without_special ? tr("특수 상황 뺌") : tr("전체"));
            scope->setToolTip(row.without_special ? tr("VI·서킷브레이커·사이드카가 있던 날을 뺀 것") : QString());
            summary_->setItem(r, 1, scope);
            for (int c = 0; c < row.cells.size(); ++c) {
                auto* item = cell(row.cells[c].text, kNumber);
                item->setToolTip(row.cells[c].tip);
                if (row.cells[c].thin)
                    item->setForeground(QColor(ui::colors::TEXT_DIM()));
                summary_->setItem(r, 2 + c, item);
            }
        }
    }
    fit_height(summary_);
}

} // namespace fincept::screens
