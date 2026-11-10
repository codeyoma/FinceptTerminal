#include "screens/krx_quant/KrxVerdictScreen.h"

#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/KrxVerdictLive.h"
#include "services/krx_quant/VerdictView.h"
#include "ui/theme/Theme.h"

#include <QFrame>
#include <QHeaderView>
#include <QMouseEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace fincept::screens {

namespace {

using krx_tables::cell;
using krx_tables::kNumber;
namespace view = krx_quant::judgment;

QString call_color(view::Call call) {
    switch (call) {
        case view::Call::Buy:
            return ui::colors::POSITIVE();
        case view::Call::Sell:
            return ui::colors::NEGATIVE();
        case view::Call::Wait:
            break;
    }
    return ui::colors::TEXT_SECONDARY();
}

/// A table as tall as its rows: the screen scrolls, not each table.
void fit_height(QTableWidget* table) {
    table->resizeRowsToContents();
    const auto* header = table->horizontalHeader();
    int height = header->isHidden() ? 0 : std::max(header->height(), header->sizeHint().height());
    height += table->verticalHeader()->length();
    table->setFixedHeight(height + 2 * table->frameWidth() + 4);
}

} // namespace

/// A symbol's card: its call, large, over its strength or reason, its holding time, target and stop,
/// and the expected net per horizon. A click chooses the symbol whose members are shown.
class KrxVerdictCard : public QFrame {
  public:
    explicit KrxVerdictCard(std::function<void(const QString&)> chosen) : chosen_(std::move(chosen)) {
        setObjectName("krxVerdictCard");
        setAttribute(Qt::WA_StyledBackground);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QObject::tr("누르면 아래에 이 종목의 참여 알고리즘을 보입니다"));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(4);
        title_ = new QLabel;
        word_ = new QLabel;
        reason_ = new QLabel;
        reason_->setWordWrap(true);
        hold_ = new QLabel;
        levels_ = new QLabel;
        horizons_ = krx_tables::table({QObject::tr("지평"), QObject::tr("먼저 닿을 확률"), QObject::tr("기대 순손익")});
        horizons_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        horizons_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        horizons_->horizontalHeader()->setStretchLastSection(true);
        horizons_->setSelectionMode(QAbstractItemView::NoSelection);
        horizons_->viewport()->installEventFilter(this); // a click on the table chooses too
        as_of_ = new QLabel;
        for (QWidget* w : {static_cast<QWidget*>(title_), static_cast<QWidget*>(word_),
                           static_cast<QWidget*>(reason_), static_cast<QWidget*>(hold_),
                           static_cast<QWidget*>(levels_), static_cast<QWidget*>(horizons_),
                           static_cast<QWidget*>(as_of_)})
            layout->addWidget(w);
        layout->addStretch(1);
    }

    void show_verdict(const krx_quant::SymbolVerdict& v, const QString& title, bool selected) {
        symbol_ = v.symbol;
        const int size = ui::ThemeManager::instance().current_font().pixelSize();
        setStyleSheet(QString("#krxVerdictCard{background:%1;border:%2px solid %3;}"
                              "#krxVerdictCard QLabel{background:transparent;color:%4;}")
                          .arg(ui::colors::BG_SURFACE(), selected ? "2" : "1",
                               selected ? ui::colors::AMBER() : ui::colors::BORDER_DIM(), ui::colors::TEXT_PRIMARY()));
        title_->setText(title);
        title_->setStyleSheet(QString("font-weight:700;color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        word_->setText(view::word(v));
        word_->setStyleSheet(
            QString("font-size:%1px;font-weight:800;color:%2;").arg(size * 2 + 8).arg(call_color(view::call(v))));
        reason_->setText(view::reason_text(v));
        const QString hold = view::hold_text(v);
        hold_->setText(hold);
        hold_->setVisible(!hold.isEmpty());
        hold_->setStyleSheet("font-weight:700;");
        const QString levels = view::levels_text(v);
        levels_->setText(levels);
        levels_->setVisible(!levels.isEmpty());
        const auto lines = view::horizon_lines(v);
        horizons_->setRowCount(static_cast<int>(lines.size()));
        for (int r = 0; r < lines.size(); ++r) {
            const auto& line = lines[r];
            const QStringList texts{line.horizon, line.odds, line.net};
            for (int c = 0; c < texts.size(); ++c) {
                auto* item = cell(texts[c], c == 0 ? Qt::AlignLeft | Qt::AlignVCenter : kNumber);
                item->setToolTip(line.tip);
                if (line.chosen) {
                    QFont bold = item->font();
                    bold.setBold(true);
                    item->setFont(bold);
                }
                if (c == 2 && (line.positive || line.negative))
                    item->setForeground(QColor(line.positive ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()));
                horizons_->setItem(r, c, item);
            }
        }
        horizons_->setVisible(!lines.isEmpty());
        krx_tables::restyle(horizons_);
        fit_height(horizons_);
        as_of_->setText(view::as_of_text(v));
        as_of_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_DIM()));
    }

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        QFrame::mousePressEvent(event);
        chosen_(symbol_);
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonPress)
            chosen_(symbol_);
        return QFrame::eventFilter(watched, event);
    }

  private:
    std::function<void(const QString&)> chosen_;
    QString symbol_;
    QLabel* title_ = nullptr;
    QLabel* word_ = nullptr;
    QLabel* reason_ = nullptr;
    QLabel* hold_ = nullptr;
    QLabel* levels_ = nullptr;
    QTableWidget* horizons_ = nullptr;
    QLabel* as_of_ = nullptr;
};

KrxVerdictScreen::KrxVerdictScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("krxVerdictScreen");
    live_ = new services::KrxVerdictLive(this);
    live_->run_while_shown(this); // read only while the screen is seen

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = krx_panel::header(
        tr("KRX 종합 판단"),
        tr("종목마다 이 봉 마감에 예측을 낸 장벽 알고리즘을 모두 합친 판정입니다: 사세요, 파세요, 기다리세요. "
           "매수면 기대 순손익이 가장 큰 지평의 최대 보유 시간과 목표가·손절가를 보입니다. 엔진이 봉 마감마다 "
           "만든 판정을 그대로 보이기만 하고, 주문하지 않습니다. 장중(평일 08:59–15:40)에는 15초마다, 그 밖에는 "
           "5분마다 다시 읽습니다."));
    dot_ = new QLabel;
    dot_->setFixedSize(8, 8);
    header_.row->insertWidget(2, dot_);
    reload_ = new QPushButton(tr("새로 읽기"));
    reload_->setToolTip(tr("지금 다시 읽습니다"));
    reload_->setCursor(Qt::PointingHandCursor);
    reload_->setFlat(true);
    header_.controls->addWidget(reload_);
    connect(reload_, &QPushButton::clicked, this, [this] { live_->read_now(); });
    layout->addWidget(header_.bar);

    failure_ = new QLabel;
    failure_->setContentsMargins(12, 2, 12, 4);
    failure_->setWordWrap(true);
    failure_->hide();
    layout->addWidget(failure_);
    as_of_ = new QLabel;
    as_of_->setContentsMargins(12, 4, 12, 2);
    layout->addWidget(as_of_);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget;
    body->setObjectName("krxVerdictBody");
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(1);

    cards_header_ = krx_panel::header(
        tr("종목별 판단"),
        tr("기대 순손익은 그 지평에 지금 매도 1호가로 산 한 주의 것입니다(목표·손절·시간 만료 셋을 자동 매매의 왕복 "
           "비용으로 셉니다). 굵은 줄이 판정이 기대는 지평입니다."));
    auto* cards = new QWidget;
    auto* cards_layout = new QVBoxLayout(cards);
    cards_layout->setContentsMargins(0, 0, 0, 0);
    cards_layout->setSpacing(0);
    empty_ = new QLabel;
    empty_->setContentsMargins(12, 8, 12, 8);
    empty_->setWordWrap(true);
    cards_layout->addWidget(empty_);
    cards_row_ = new QHBoxLayout;
    cards_row_->setContentsMargins(0, 0, 0, 0);
    cards_row_->setSpacing(1);
    cards_layout->addLayout(cards_row_);
    body_layout->addWidget(krx_panel::panel(cards_header_, cards));

    auto* lower = new QWidget;
    auto* lower_row = new QHBoxLayout(lower);
    lower_row->setContentsMargins(0, 0, 0, 0);
    lower_row->setSpacing(1);

    members_header_ = krx_panel::header(
        tr("참여 알고리즘"),
        tr("판정에 든 장벽 알고리즘마다 자기 세 확률과 기울기입니다. 계열마다 같은 무게를 받고 계열 안에서 나눕니다. "
           "증거는 그 종류의 예측 가운데 정답이 난 수입니다. 상태가 검증 중이면 아직 수습입니다."));
    auto* members_body = new QWidget;
    auto* members_layout = new QVBoxLayout(members_body);
    members_layout->setContentsMargins(0, 0, 0, 0);
    members_layout->setSpacing(0);
    members_empty_ = new QLabel;
    members_empty_->setContentsMargins(12, 8, 12, 8);
    members_empty_->setWordWrap(true);
    members_layout->addWidget(members_empty_);
    members_ = krx_tables::table({tr("알고리즘"), tr("종류·지평"), tr("계열"), tr("상태"), tr("증거"), tr("위 먼저"),
                                  tr("시간 만료"), tr("아래 먼저"), tr("기울기"), tr("무게")});
    members_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    members_->horizontalHeader()->setStretchLastSection(true);
    members_layout->addWidget(members_);
    members_layout->addStretch(1);
    lower_row->addWidget(krx_panel::panel(members_header_, members_body), 3);

    side_header_ = krx_panel::header(
        tr("방향·변동폭 예측"),
        tr("판정에 섞지 않고 옆에 보이는 예측입니다: 방향 알고리즘(4분), 최종 합의, 변동폭."));
    auto* side_body = new QWidget;
    auto* side_layout = new QVBoxLayout(side_body);
    side_layout->setContentsMargins(0, 0, 0, 0);
    side_layout->setSpacing(0);
    side_ = krx_tables::table({tr("예측자"), tr("종류"), tr("상태"), tr("예측")});
    side_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    side_->horizontalHeader()->setStretchLastSection(true);
    side_layout->addWidget(side_);
    side_layout->addStretch(1);
    lower_row->addWidget(krx_panel::panel(side_header_, side_body), 2);
    body_layout->addWidget(lower);
    body_layout->addStretch(1);
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, [this] { show_dot(); });
    connect(live_, &services::KrxVerdictLive::changed, this, [this] { render(); });
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this] { refresh_theme(); });
    refresh_theme();
}

void KrxVerdictScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    clock_timer_->start();
}

void KrxVerdictScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    clock_timer_->stop();
}

void KrxVerdictScreen::refresh_theme() {
    setStyleSheet(QString("#krxVerdictScreen,#krxVerdictBody{background:%1;}"
                          "#krxVerdictScreen QLabel{background:transparent;color:%2;}"
                          "#krxVerdictScreen QPushButton{background:transparent;color:%2;border:1px solid transparent;"
                          "padding:1px 6px;font-weight:700;}"
                          "#krxVerdictScreen QPushButton:hover{border-color:%3;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED()));
    for (const auto* header : {&header_, &cards_header_, &members_header_, &side_header_})
        header->restyle();
    for (auto* table : {members_, side_})
        krx_tables::restyle(table);
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    for (auto* label : {as_of_, empty_, members_empty_})
        label->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    dot_->setProperty("krxColor", QVariant());
    render();
}

void KrxVerdictScreen::render() {
    const auto& verdicts = live_->verdicts();
    const QString failure = view::failure_text(live_->failure());
    failure_->setText(verdicts ? tr("읽지 못함: %1 (아래는 %2에 읽은 것)")
                                     .arg(failure, live_->answered_at().toString("HH:mm:ss"))
                               : failure);
    failure_->setVisible(!failure.isEmpty());
    if (verdicts)
        as_of_->setText(tr("판정 규칙 %1판 · %2에 읽음").arg(verdicts->version).arg(live_->answered_at().toString("HH:mm:ss")));
    else
        as_of_->setText(live_->reading() ? tr("읽는 중") : QString());
    show_dot();
    fill_cards();
    fill_members();
    fill_side();
}

void KrxVerdictScreen::show_dot() {
    QString color = ui::colors::TEXT_DIM();
    QString tip = tr("읽는 중");
    if (!live_->failure().isEmpty()) {
        color = ui::colors::NEGATIVE();
        tip = tr("읽지 못함: %1").arg(live_->failure());
    } else if (live_->verdicts()) {
        color = ui::colors::POSITIVE();
        tip = tr("엔진 응답 %1").arg(live_->answered_at().toString("HH:mm:ss"));
    }
    if (live_->running() && !live_->reading())
        tip += tr("\n다음 읽기: %1초마다").arg(live_->next_ms() / 1000);
    if (dot_->property("krxColor").toString() != color) {
        dot_->setStyleSheet(QString("background:%1;border-radius:4px;").arg(color));
        dot_->setProperty("krxColor", color);
    }
    dot_->setToolTip(tip);
}

void KrxVerdictScreen::fill_cards() {
    const auto& verdicts = live_->verdicts();
    const int wanted = verdicts ? static_cast<int>(verdicts->verdicts.size()) : 0;
    empty_->setText(verdicts && wanted == 0 ? view::empty_text() : QString());
    empty_->setVisible(verdicts && wanted == 0);
    while (cards_.size() > wanted) {
        auto* card = cards_.takeLast();
        cards_row_->removeWidget(card);
        card->deleteLater();
    }
    while (cards_.size() < wanted) {
        auto* card = new KrxVerdictCard([this](const QString& symbol) { live_->set_symbol(symbol); });
        cards_ << card;
        cards_row_->addWidget(card, 1);
    }
    const auto chosen = live_->chosen();
    for (int i = 0; i < wanted; ++i) {
        const auto& v = verdicts->verdicts[i];
        cards_[i]->show_verdict(v, view::card_title(v.symbol, live_->symbols()),
                                chosen && chosen->symbol == v.symbol);
    }
}

void KrxVerdictScreen::fill_members() {
    members_->setRowCount(0);
    const auto chosen = live_->chosen();
    members_header_.title->setText(chosen ? tr("참여 알고리즘 · %1").arg(view::card_title(chosen->symbol, live_->symbols()))
                                          : tr("참여 알고리즘"));
    const auto rows = chosen ? view::member_rows(*chosen) : QList<view::MemberRow>{};
    members_empty_->setText(chosen && rows.isEmpty() ? view::no_members_text(*chosen) : QString());
    members_empty_->setVisible(chosen && rows.isEmpty());
    members_->setVisible(!rows.isEmpty());
    for (const auto& row : rows) {
        const int r = members_->rowCount();
        members_->insertRow(r);
        const QStringList texts{row.name, row.kind, row.family, row.status, row.evidence,
                                row.up,   row.timeout, row.down, row.lean,  row.weight};
        for (int c = 0; c < texts.size(); ++c) {
            auto* item = cell(texts[c], c >= 4 && c != 8 ? kNumber : Qt::AlignLeft | Qt::AlignVCenter);
            item->setToolTip(row.tip);
            if (!row.counted)
                item->setForeground(QColor(ui::colors::TEXT_DIM()));
            else if (c == 3 && row.active)
                item->setForeground(QColor(ui::colors::POSITIVE()));
            members_->setItem(r, c, item);
        }
    }
    fit_height(members_);
}

void KrxVerdictScreen::fill_side() {
    side_->setRowCount(0);
    const auto chosen = live_->chosen();
    const auto rows = chosen ? view::side_rows(*chosen) : QList<view::SideRow>{};
    for (const auto& row : rows) {
        const int r = side_->rowCount();
        side_->insertRow(r);
        side_->setItem(r, 0, cell(row.name));
        side_->setItem(r, 1, cell(row.kind));
        side_->setItem(r, 2, cell(row.status));
        side_->setItem(r, 3, cell(row.output));
    }
    fit_height(side_);
}

} // namespace fincept::screens
