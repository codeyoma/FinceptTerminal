#include "screens/krx_quant/KrxCostScreen.h"

#include "screens/krx_quant/KrxStatsWindow.h"
#include "screens/krx_quant/KrxSymbolCell.h"
#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/KindView.h"
#include "services/krx_quant/KrxCostLive.h"
#include "ui/theme/Theme.h"

#include <QHeaderView>
#include <QPainter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace fincept::screens {

namespace {

using krx_quant::cost::Cost;
using krx_quant::cost::Role;
using krx_quant::cost::Verdict;
using krx_tables::cell;
using krx_tables::kNumber;
namespace view = krx_quant::cost;

constexpr std::array<Role, view::kRoles> kRoles = {Role::Consensus, Role::Top, Role::Baseline};
constexpr int kFirstRole = 4; ///< the horizon table's column of the first predictor

QPushButton* tool_button(const QString& text, const QString& tip) {
    auto* button = new QPushButton(text);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    return button;
}

/// A cell's verdict color, painted under its text: the global style sheet styles table items, and an
/// item's own background is not painted then.
constexpr int kFillRole = Qt::UserRole + 7;

class FillDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (const QVariant fill = index.data(kFillRole); fill.isValid())
            painter->fillRect(option.rect, QColor(fill.toString()));
        QStyledItemDelegate::paint(painter, option, index);
    }
};

/// A table as tall as its rows: the screen scrolls, not each table.
void fit_height(QTableWidget* table) {
    table->resizeRowsToContents();
    const auto* header = table->horizontalHeader();
    int height = header->isHidden() ? 0 : std::max(header->height(), header->sizeHint().height());
    height += table->verticalHeader()->length();
    table->setFixedHeight(height + 2 * table->frameWidth() + 4);
}

QString verdict_background(Verdict verdict) {
    switch (verdict) {
        case Verdict::Pass:
            return ui::colors::POSITIVE_BG();
        case Verdict::Fail:
            return ui::colors::NEGATIVE_BG();
        case Verdict::Few:
            return ui::colors::BG_RAISED();
        case Verdict::None:
            break;
    }
    return {};
}

} // namespace

KrxCostScreen::KrxCostScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("krxCostScreen");
    live_ = new services::KrxCostLive(this);
    logos_ = new KrxLogos(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = krx_panel::header(
        tr("KRX 지평·비용"),
        tr("어느 지평에서 맞힘률이 왕복 비용을 넘는지 봅니다. 대칭 목표 ±W, 왕복 비용 C면 상승·하락 신호의 "
           "맞힘률이 p* = (W + C) ÷ 2W를 넘어야 본전입니다. 기간·종목·특수 상황은 순위표와 같고, 맞힘률도 "
           "순위표와 같은 숫자입니다. 연속매매 중에는 60초마다, 그 밖에는 10분마다 다시 읽습니다."));
    dot_ = new QLabel;
    dot_->setFixedSize(8, 8);
    header_.row->insertWidget(2, dot_);
    symbol_box_ = new QComboBox;
    symbol_box_->setToolTip(tr("종목"));
    symbol_box_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    symbol_box_->setMinimumContentsLength(12);
    period_box_ = new QComboBox;
    period_box_->setToolTip(tr("기간: 예측이 있는 최근 날"));
    for (const auto& [period, label] :
         {std::pair{"today", tr("오늘")}, std::pair{"5d", tr("5일")}, std::pair{"20d", tr("20일")}})
        period_box_->addItem(label, QString::fromLatin1(period));
    special_box_ = new QCheckBox(tr("특수 상황 포함"));
    special_box_->setToolTip(tr("VI·서킷브레이커·사이드카가 창에 걸린 예측도 셉니다"));
    cost_box_ = new QComboBox;
    cost_box_->addItem(tr("시장가 왕복"), static_cast<int>(Cost::Market));
    cost_box_->addItem(tr("지정가 왕복"), static_cast<int>(Cost::Limit));
    cost_box_->setToolTip(tr("표의 p*와 칸 색이 따르는 왕복 비용: 지정가는 매도세 + 수수료 양쪽, 시장가는 거기에 "
                             "스프레드 1틱"));
    reload_ = tool_button(tr("새로 읽기"), tr("지금 다시 읽습니다"));
    header_.row->insertWidget(3, symbol_box_);
    header_.row->insertWidget(4, period_box_);
    header_.row->insertWidget(5, special_box_);
    header_.controls->addWidget(cost_box_);
    header_.controls->addWidget(reload_);
    connect(symbol_box_, &QComboBox::activated, this, [this] { choose(); });
    connect(period_box_, &QComboBox::activated, this, [this] { choose(); });
    connect(special_box_, &QCheckBox::toggled, this, [this] { choose(); });
    connect(cost_box_, &QComboBox::activated, this,
            [this](int index) { live_->set_cost(static_cast<Cost>(cost_box_->itemData(index).toInt())); });
    connect(reload_, &QPushButton::clicked, this, [this] { live_->read_now(); });
    layout->addWidget(header_.bar);

    as_of_ = new QLabel;
    as_of_->setContentsMargins(12, 4, 12, 2);
    layout->addWidget(as_of_);
    failure_ = new QLabel;
    failure_->setContentsMargins(12, 2, 12, 4);
    failure_->setWordWrap(true);
    failure_->hide();
    layout->addWidget(failure_);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget;
    body->setObjectName("krxCostBody");
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(1);

    cards_header_ = krx_panel::header(
        tr("종목별 비용"), tr("엔진의 가격 규칙(매도세, 수수료 설정, 호가단위표, k, 인버스 비용)을 그대로 씁니다. "
                              "통계와 연구용 손익이 쓰는 비용과 같습니다."));
    auto* cards = new QWidget;
    cards_row_ = new QHBoxLayout(cards);
    cards_row_->setContentsMargins(0, 0, 0, 0);
    cards_row_->setSpacing(1);
    body_layout->addWidget(krx_panel::panel(cards_header_, cards));

    rows_header_ = krx_panel::header(
        tr("지평 표"),
        tr("예측 종류·지평마다 목표 폭 W, 비용 C, 손익분기 p*와 세 예측자의 상승·하락 신호 맞힘률입니다. 칸 색은 "
           "맞힘률의 95% 신뢰구간 하한이 p*를 넘는지입니다(맞힘률 자체가 아니라 하한). 줄을 두 번 누르면 그 "
           "예측자의 상세 통계 창이 열립니다."));
    legend_ = new QLabel;
    rows_header_.controls->addWidget(legend_);
    auto* rows_body = new QWidget;
    auto* rows_layout = new QVBoxLayout(rows_body);
    rows_layout->setContentsMargins(0, 0, 0, 0);
    rows_layout->setSpacing(0);
    QStringList headers{tr("지평"), tr("폭 W"), tr("비용 C"), tr("손익분기")};
    for (const Role role : kRoles)
        headers << view::role_label(role);
    rows_ = krx_tables::table(headers);
    rows_->horizontalHeader()->setStretchLastSection(false);
    rows_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (auto* item = rows_->horizontalHeaderItem(3))
        item->setToolTip(tr("p* = (W + C) ÷ 2W, C/W: 고른 왕복 비용의 것입니다. 시가→종가는 대칭 장벽이 아니라 "
                            "해당 없음입니다."));
    if (auto* item = rows_->horizontalHeaderItem(kFirstRole + 1))
        item->setToolTip(tr("그 종류 순위표의 순위가 가장 높은 알고리즘(합의 빼고)"));
    if (auto* item = rows_->horizontalHeaderItem(kFirstRole + 2))
        item->setToolTip(tr("상승·하락 신호 맞힘률의 신뢰구간 하한이 가장 높은 기준 예측: 실력 없이도 나오는 맞힘률"));
    rows_->setItemDelegate(new FillDelegate(rows_));
    connect(rows_, &QTableWidget::cellDoubleClicked, this, &KrxCostScreen::open_detail);
    rows_layout->addWidget(rows_);
    body_layout->addWidget(krx_panel::panel(rows_header_, rows_body));

    regimes_header_ = krx_panel::header(
        tr("기간 기준율"),
        tr("4분 도달 정답(1분봉 규칙)의 기간별 클래스 비율입니다. structure-check(#270)와 같은 정의로 밤마다 다시 "
           "잽니다. 늘 보합의 맞힘률은 보합 비율이고, 직전 창은 4분 전 창의 정답을 그대로 냈을 때의 맞힘률입니다. "
           "맞힘률 하나는 기준율이 다른 시장들의 평균입니다."));
    regimes_line_ = new QLabel;
    regimes_header_.controls->addWidget(regimes_line_);
    regimes_ = krx_tables::table({tr("종목"), tr("기간"), tr("일수"), tr("창"), tr("상승"), tr("보합(늘 보합)"),
                                  tr("하락"), tr("k"), tr("직전 창")});
    regimes_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    regimes_->horizontalHeader()->setStretchLastSection(false);
    body_layout->addWidget(krx_panel::panel(regimes_header_, regimes_));
    body_layout->addStretch(1);
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, [this] { show_dot(); });
    connect(live_, &services::KrxCostLive::changed, this, [this] { render(); });
    connect(live_, &services::KrxCostLive::stats_ready, this, [this](const krx_quant::AlgorithmStats& stats) {
        if (auto* window = stats_windows_.value(stats.algorithm))
            window->show_stats(stats, live_->kind_info(stats.kind));
    });
    connect(live_, &services::KrxCostLive::stats_failed, this, [this](const QString& algorithm, const QString& reason) {
        if (auto* window = stats_windows_.value(algorithm))
            window->show_failure(reason);
    });
    connect(logos_, &KrxLogos::arrived, this, [this] { fill_choices(); });
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this] { refresh_theme(); });
    refresh_theme();
}

void KrxCostScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    live_->start();
    clock_timer_->start();
}

void KrxCostScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    live_->stop();
    clock_timer_->stop();
}

void KrxCostScreen::refresh_theme() {
    setStyleSheet(QString("#krxCostScreen,#krxCostBody{background:%1;}"
                          "#krxCostScreen QLabel,#krxCostScreen QCheckBox{background:transparent;color:%2;}"
                          "#krxCostScreen QPushButton{background:transparent;color:%2;border:1px solid transparent;"
                          "padding:1px 6px;font-weight:700;}"
                          "#krxCostScreen QPushButton:hover{border-color:%3;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED()));
    for (const auto* header : {&header_, &cards_header_, &rows_header_, &regimes_header_})
        header->restyle();
    for (auto* table : {rows_, regimes_})
        krx_tables::restyle(table);
    for (auto* table : std::as_const(cards_))
        krx_tables::restyle(table);
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    for (auto* label : {as_of_, regimes_line_})
        label->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    legend_->setText(QString("<span style='background:%1'>&nbsp;%2&nbsp;</span> "
                             "<span style='background:%3'>&nbsp;%4&nbsp;</span> "
                             "<span style='background:%5'>&nbsp;%6&nbsp;</span>")
                         .arg(ui::colors::POSITIVE_BG(), view::verdict_text(Verdict::Pass), ui::colors::NEGATIVE_BG(),
                              view::verdict_text(Verdict::Fail), ui::colors::BG_RAISED(),
                              view::verdict_text(Verdict::Few)));
    dot_->setProperty("krxColor", QVariant());
    render();
}

void KrxCostScreen::render() {
    const auto& m = live_->map();
    const QString failure = live_->failure();
    failure_->setText(
        m ? tr("읽지 못함: %1 (아래는 %2에 읽은 것)").arg(failure, live_->answered_at().toString("HH:mm:ss"))
          : failure);
    failure_->setVisible(!failure.isEmpty());
    as_of_->setText(m ? view::as_of_line(*m) : (live_->reading() ? tr("읽는 중") : QString()));
    show_dot();
    fill_choices();
    fill_cards();
    fill_rows();
    fill_regimes();
}

void KrxCostScreen::show_dot() {
    QString color = ui::colors::TEXT_DIM();
    QString tip = tr("읽는 중");
    if (!live_->failure().isEmpty()) {
        color = ui::colors::NEGATIVE();
        tip = tr("읽지 못함: %1").arg(live_->failure());
    } else if (live_->map()) {
        color = ui::colors::POSITIVE();
        tip = tr("엔진 응답 %1 · 엔진 시계 %2")
                  .arg(live_->answered_at().toString("HH:mm:ss"),
                       krx_quant::kst_text(live_->map()->as_of, "MM-dd HH:mm:ss"));
    }
    if (live_->running() && !live_->reading())
        tip += tr("\n다음 읽기: %1초마다").arg(live_->next_ms() / 1000);
    if (dot_->property("krxColor").toString() != color) {
        dot_->setStyleSheet(QString("background:%1;border-radius:4px;").arg(color));
        dot_->setProperty("krxColor", color);
    }
    dot_->setToolTip(tip);
}

void KrxCostScreen::fill_choices() {
    const auto& q = live_->query();
    {
        const QSignalBlocker quiet(symbol_box_); // a refill is not a choice
        symbol_box_->clear();
        const int size = symbol_box_->fontMetrics().height();
        symbol_box_->setIconSize(QSize(size, size));
        symbol_box_->addItem(tr("전체 종목"), QStringLiteral("all"));
        for (const auto& quote : live_->symbols()) {
            const QString text =
                quote.name.isEmpty() ? quote.symbol : QStringLiteral("%1 %2").arg(quote.name, quote.symbol);
            symbol_box_->addItem(logos_->logo(quote.logo_url, quote.name, size, devicePixelRatioF()), text,
                                 quote.symbol);
        }
        symbol_box_->setCurrentIndex(std::max(0, symbol_box_->findData(q.symbol)));
    }
    const QSignalBlocker quiet_period(period_box_);
    period_box_->setCurrentIndex(std::max(0, period_box_->findData(q.period)));
    const QSignalBlocker quiet_special(special_box_);
    special_box_->setChecked(q.special);
    const QSignalBlocker quiet_cost(cost_box_);
    cost_box_->setCurrentIndex(std::max(0, cost_box_->findData(static_cast<int>(live_->cost()))));
}

void KrxCostScreen::choose() {
    auto q = live_->query();
    q.symbol = symbol_box_->currentData().toString();
    q.period = period_box_->currentData().toString();
    q.special = special_box_->isChecked();
    live_->set_query(q);
}

void KrxCostScreen::fill_cards() {
    const auto& m = live_->map();
    const int wanted = m ? static_cast<int>(m->symbols.size()) : 0;
    while (cards_.size() > wanted) {
        auto* table = cards_.takeLast();
        cards_row_->removeWidget(table);
        table->deleteLater();
    }
    while (cards_.size() < wanted) {
        auto* table = krx_tables::table({QString(), QString()});
        table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table->horizontalHeader()->setStretchLastSection(true);
        cards_ << table;
        cards_row_->addWidget(table, 1);
    }
    for (int i = 0; i < wanted; ++i) {
        const auto& symbol = m->symbols[i];
        QTableWidget* table = cards_[i];
        if (auto* header = table->horizontalHeaderItem(0))
            header->setText(view::card_title(symbol));
        if (auto* header = table->horizontalHeaderItem(1))
            header->setText(symbol.live ? tr("실시간") : tr("장 밖"));
        const auto lines = view::card_lines(symbol);
        table->setRowCount(static_cast<int>(lines.size()));
        for (int r = 0; r < lines.size(); ++r) {
            auto* label = cell(lines[r].label);
            auto* value = cell(lines[r].value, kNumber);
            label->setToolTip(lines[r].tip);
            value->setToolTip(lines[r].tip);
            if (lines[r].warn)
                value->setForeground(QColor(ui::colors::NEGATIVE()));
            table->setItem(r, 0, label);
            table->setItem(r, 1, value);
        }
        fit_height(table);
    }
}

void KrxCostScreen::fill_rows() {
    rows_->setRowCount(0);
    const auto& m = live_->map();
    if (!m) {
        fit_height(rows_);
        return;
    }
    const Cost cost = live_->cost();
    if (auto* item = rows_->horizontalHeaderItem(3))
        item->setText(tr("손익분기 (%1)").arg(view::cost_label(cost)));
    for (const auto& row : m->rows) {
        const int r = rows_->rowCount();
        rows_->insertRow(r);
        auto* horizon = cell(view::horizon_text(row));
        horizon->setToolTip(view::horizon_tip(row));
        rows_->setItem(r, 0, horizon);
        rows_->setItem(r, 1, cell(view::width_text(row), kNumber));
        rows_->setItem(r, 2, cell(view::cost_text(row), kNumber));
        auto* breakeven = cell(view::breakeven_text(row, cost), kNumber);
        breakeven->setToolTip(view::breakeven_tip(row));
        rows_->setItem(r, 3, breakeven);
        for (int i = 0; i < view::kRoles; ++i) {
            const auto shown = view::predictor_cell(row, kRoles[static_cast<std::size_t>(i)], cost, m->min_sample);
            auto* item = cell(shown.text);
            item->setToolTip(shown.tip);
            if (const QString background = verdict_background(shown.verdict); !background.isEmpty())
                item->setData(kFillRole, background);
            if (!view::predictor(row, kRoles[static_cast<std::size_t>(i)]))
                item->setForeground(QColor(ui::colors::TEXT_DIM()));
            rows_->setItem(r, kFirstRole + i, item);
        }
    }
    fit_height(rows_);
}

void KrxCostScreen::fill_regimes() {
    regimes_->setRowCount(0);
    const auto& m = live_->map();
    regimes_line_->setText(m ? view::regimes_line(*m) : QString());
    if (m && m->regimes) {
        for (const auto& line : view::regime_lines(*m->regimes)) {
            const int r = regimes_->rowCount();
            regimes_->insertRow(r);
            const QStringList cells{line.symbol, line.period, line.days, line.windows, line.up,
                                    line.flat,   line.down,   line.k,    line.persist};
            for (int c = 0; c < cells.size(); ++c)
                regimes_->setItem(r, c, cell(cells[c], c < 2 ? Qt::AlignLeft | Qt::AlignVCenter : kNumber));
        }
    }
    fit_height(regimes_);
}

void KrxCostScreen::open_detail(int row, int column) {
    const auto& m = live_->map();
    if (!m || row < 0 || row >= m->rows.size())
        return;
    const auto& r = m->rows[row];
    QString algorithm;
    if (column >= kFirstRole && column < kFirstRole + view::kRoles) {
        if (const auto& p = view::predictor(r, kRoles[static_cast<std::size_t>(column - kFirstRole)]))
            algorithm = p->algorithm;
    }
    for (const Role role : {Role::Top, Role::Consensus, Role::Baseline})
        if (algorithm.isEmpty())
            if (const auto& p = view::predictor(r, role))
                algorithm = p->algorithm;
    if (algorithm.isEmpty())
        return;
    KrxStatsWindow* window = stats_windows_.value(algorithm);
    if (!window) { // a window of its own for each predictor, as the 4-minute screen's
        window = new KrxStatsWindow(this);
        stats_windows_.insert(algorithm, window);
        connect(window, &KrxStatsWindow::closed, this, [this, algorithm] {
            if (auto* closed = stats_windows_.take(algorithm))
                closed->deleteLater();
        });
    }
    window->open(algorithm);
    const auto& about = live_->algorithms();
    // Another kind's baseline is described by GET /v1/kinds, as on the 4-minute screen.
    window->show_about(about.contains(algorithm) ? std::optional(about.value(algorithm))
                                                 : krx_quant::kind_baseline_info(live_->kinds(), algorithm));
    live_->read_stats(algorithm);
}

} // namespace fincept::screens
