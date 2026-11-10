#include "screens/krx_quant/KrxActionScreen.h"

#include "core/config/AppConfig.h"
#include "core/events/EventBus.h"
#include "services/krx_quant/ActionView.h"
#include "services/krx_quant/KrxActionLive.h"
#include "ui/notifications/NotificationService.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollArea>

namespace fincept::screens {

namespace {

namespace view = krx_quant::action;

constexpr const char* kSoundKey = "krx_quant/action_sound";

QPushButton* tool_button(const QString& text, const QString& tip) {
    auto* button = new QPushButton(text);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    return button;
}

QString tone_color(const QString& tone) {
    if (tone == QLatin1String("buy"))
        return ui::colors::POSITIVE();
    if (tone == QLatin1String("sell"))
        return ui::colors::NEGATIVE();
    return ui::colors::TEXT_SECONDARY();
}

} // namespace

/// One stock's card: its word, large, the stock, the line, the plan and the record.
class KrxActionCard : public QFrame {
  public:
    explicit KrxActionCard(QWidget* parent = nullptr) : QFrame(parent) {
        setObjectName("krxActionCard");
        setAttribute(Qt::WA_StyledBackground);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(24);
        word_ = new QLabel;
        word_->setObjectName("krxActionWord");
        word_->setMinimumWidth(150);
        word_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(word_, 0, Qt::AlignTop);
        auto* text = new QVBoxLayout;
        text->setSpacing(6);
        title_ = new QLabel;
        title_->setObjectName("krxActionTitle");
        line_ = new QLabel;
        line_->setObjectName("krxActionLine");
        line_->setWordWrap(true);
        plan_ = new QLabel;
        plan_->setObjectName("krxActionPlan");
        plan_->setTextFormat(Qt::PlainText);
        record_ = new QLabel;
        record_->setObjectName("krxActionRecord");
        record_->setWordWrap(true);
        for (auto* label : {title_, line_, plan_, record_})
            text->addWidget(label);
        text->addStretch(1);
        layout->addLayout(text, 1);
    }

    void set(const view::CardText& card) {
        word_->setText(card.word);
        if (tone_ != card.tone) {
            tone_ = card.tone;
            restyle();
        }
        title_->setText(card.title);
        line_->setText(card.line);
        plan_->setText(card.plan.join('\n'));
        plan_->setVisible(!card.plan.isEmpty());
        record_->setText(card.record);
        record_->setToolTip(card.record_tip);
        record_->setVisible(!card.record.isEmpty());
    }

    void restyle() {
        const int base = ui::ThemeManager::instance().current_font().pixelSize();
        setStyleSheet(QString("#krxActionCard{background:%1;border:1px solid %2;}"
                              "#krxActionCard QLabel{background:transparent;}"
                              "#krxActionWord{color:%3;font-size:%4px;font-weight:700;}"
                              "#krxActionTitle{color:%5;font-weight:700;}"
                              "#krxActionLine{color:%6;font-size:%7px;}"
                              "#krxActionPlan{color:%6;}"
                              "#krxActionRecord{color:%5;}")
                          .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), tone_color(tone_))
                          .arg(base * 3)
                          .arg(ui::colors::TEXT_SECONDARY(), ui::colors::TEXT_PRIMARY())
                          .arg(base + 2));
    }

  private:
    QLabel* word_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* line_ = nullptr;
    QLabel* plan_ = nullptr;
    QLabel* record_ = nullptr;
    QString tone_;
};

KrxActionScreen::KrxActionScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("krxActionScreen");
    live_ = new services::KrxActionLive(this);
    live_->run_while_shown(this); // read only while the screen is seen

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = krx_panel::header(
        tr("KRX 할 일"),
        tr("지금 할 일을 wait(기다림), buy(삼), sell(팖) 셋 가운데 하나로 보입니다. 자동 매매가 하는 일을 그대로 "
           "따릅니다: 비용을 빼고도 이익이 난다고 밤마다 검증한 신호로만 사고, 이익 실현 가격·손절 가격·정한 "
           "시각에 팝니다. 투자 조언이 아닙니다."));
    dot_ = new QLabel;
    dot_->setFixedSize(8, 8);
    header_.row->insertWidget(2, dot_); // right after the title
    mode_ = new QLabel;
    header_.row->insertWidget(3, mode_);
    sound_ = new QCheckBox(tr("소리"));
    sound_->setToolTip(tr("buy나 sell로 바뀌면 알림과 함께 소리를 냅니다"));
    sound_->setChecked(AppConfig::instance().get(kSoundKey, true).toBool());
    connect(sound_, &QCheckBox::toggled, this, [](bool on) { AppConfig::instance().set(kSoundKey, on); });
    reload_ = tool_button(tr("새로 읽기"), tr("지금 다시 읽습니다"));
    connect(reload_, &QPushButton::clicked, this, [this] { live_->read_now(); });
    header_.controls->addWidget(sound_);
    header_.controls->addWidget(reload_);
    layout->addWidget(header_.bar);

    failure_ = new QLabel;
    failure_->setContentsMargins(12, 4, 12, 4);
    failure_->setWordWrap(true);
    failure_->hide();
    layout->addWidget(failure_);

    auto* body = new QWidget;
    body->setObjectName("krxActionBody");
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(16, 16, 16, 16);
    body_layout->setSpacing(12);
    empty_ = new QLabel;
    empty_->setWordWrap(true);
    body_layout->addWidget(empty_);
    cards_layout_ = new QVBoxLayout;
    cards_layout_->setSpacing(12);
    body_layout->addLayout(cards_layout_);

    note_ = new QLabel(
        tr("buy는 자동 매매가 산 뒤 1분, sell은 파는 동안과 판 뒤 1분 보입니다. 들고 있는 동안은 wait이고 아래 "
           "가격이나 시각에 팝니다. 다른 신호(4분 예측의 상승 신호 등)는 비용을 빼면 손해라 여기에 넣지 않습니다."));
    note_->setWordWrap(true);
    body_layout->addWidget(note_);

    auto* more = new QHBoxLayout;
    more->setSpacing(8);
    const QList<std::pair<QString, QString>> screens = {{tr("자세히: 자동 매매"), "krx_trader"},
                                                        {tr("4분 예측"), "krx_quant"},
                                                        {tr("지평·비용"), "krx_cost"}};
    for (const auto& [label, id] : screens) {
        auto* button = tool_button(label, tr("그 화면을 엽니다"));
        connect(button, &QPushButton::clicked, this,
                [id] { EventBus::instance().publish("nav.switch_screen", {{"screen_id", id}}); });
        more->addWidget(button);
        details_.push_back(button);
    }
    more->addStretch(1);
    body_layout->addLayout(more);
    body_layout->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, [this] { render(); });

    connect(live_, &services::KrxActionLive::changed, this, [this] { render(); });
    connect(live_, &services::KrxActionLive::board_ready, this, [this] { raise_alerts(); });
    connect(live_, &services::KrxActionLive::engine_changed, this, [this] {
        seen_.clear();
        first_ = true;
    });
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this] { refresh_theme(); });
    refresh_theme();
}

void KrxActionScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    clock_timer_->start();
}

void KrxActionScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    clock_timer_->stop();
}

void KrxActionScreen::refresh_theme() {
    setStyleSheet(QString("#krxActionScreen,#krxActionBody{background:%1;}"
                          "#krxActionScreen QLabel{background:transparent;color:%2;}"
                          "#krxActionScreen QPushButton{background:transparent;color:%2;border:1px solid %3;"
                          "padding:3px 10px;font-weight:700;}"
                          "#krxActionScreen QPushButton:hover{border-color:%4;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                           ui::colors::BORDER_MED()));
    header_.restyle();
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    mode_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    empty_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    note_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_DIM()));
    for (auto* card : cards_)
        card->restyle();
    dot_->setProperty("krxColor", QVariant()); // the dot in the new colors
    render();
}

void KrxActionScreen::render() {
    const auto& board = live_->board();
    const QString failure = live_->failure();
    failure_->setText(board ? tr("읽지 못함: %1 (아래는 %2에 읽은 것)")
                                  .arg(failure, live_->answered_at().toString("HH:mm:ss"))
                            : failure);
    failure_->setVisible(!failure.isEmpty());
    mode_->setText(board ? view::mode_line(*board) : QString());
    const int count = board ? static_cast<int>(board->cards.size()) : 0;
    while (cards_.size() > count) {
        delete cards_.takeLast();
    }
    while (cards_.size() < count) {
        auto* card = new KrxActionCard;
        card->restyle();
        cards_layout_->addWidget(card);
        cards_.push_back(card);
    }
    const QDateTime now = live_->now();
    for (int i = 0; i < count; ++i)
        cards_[i]->set(view::card_text(board->cards[i], now));
    if (!live_->served())
        empty_->setText(tr("이 엔진은 할 일 카드를 주지 않습니다. 엔진이 업데이트되면(매일 06:00) 보입니다."));
    else if (!board)
        empty_->setText(failure.isEmpty() ? tr("읽는 중…") : QString());
    else if (count == 0)
        empty_->setText(tr("자동 매매가 매매하는 종목이 없습니다."));
    else
        empty_->clear();
    empty_->setVisible(!empty_->text().isEmpty());
    show_dot();
}

void KrxActionScreen::show_dot() {
    QString color = ui::colors::TEXT_DIM();
    QString tip = tr("읽는 중");
    if (!live_->failure().isEmpty()) {
        color = ui::colors::NEGATIVE();
        tip = tr("읽지 못함: %1").arg(live_->failure());
    } else if (live_->board() || !live_->served()) {
        color = ui::colors::POSITIVE();
        tip = tr("엔진 응답 %1").arg(live_->answered_at().toString("HH:mm:ss"));
    }
    if (live_->running())
        tip += tr("\n다음 읽기: %1초 뒤").arg(qMax(1, live_->next_ms() / 1000));
    if (dot_->property("krxColor").toString() != color) {
        dot_->setStyleSheet(QString("background:%1;border-radius:4px;").arg(color));
        dot_->setProperty("krxColor", color);
    }
    dot_->setToolTip(tip);
}

void KrxActionScreen::raise_alerts() {
    const auto& board = live_->board();
    if (!board)
        return;
    const QStringList texts = view::alerts(*board, seen_, first_);
    first_ = false;
    for (const auto& text : texts) {
        // Fincept's notifications: an in-app toast and the OS's (DesktopNotifier mirrors every toast).
        ui::ToastService::instance().post(ui::ToastService::Severity::Warning, text, QStringLiteral("krx_quant"));
        if (sound_->isChecked())
            QApplication::beep();
    }
}

} // namespace fincept::screens
