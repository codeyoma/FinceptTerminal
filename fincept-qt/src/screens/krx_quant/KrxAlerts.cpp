#include "screens/krx_quant/KrxAlerts.h"

#include "core/config/AppConfig.h"
#include "ui/notifications/NotificationService.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::screens {

namespace {

constexpr auto kOnKey = "krx_quant/signal_on";
constexpr auto kProbabilityKey = "krx_quant/signal_probability";
constexpr auto kAgreeKey = "krx_quant/signal_agree";
constexpr auto kSoundKey = "krx_quant/signal_sound";
// The spin boxes' ranges: the engine's threshold is at most 1 (codeyoma/krx-quant#617).
constexpr int kMinPercent = 50;
constexpr int kMaxPercent = 100;
constexpr int kMaxAgree = 20;

} // namespace

KrxAlerts::KrxAlerts(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    header_ = krx_panel::header(tr("신호 알림"),
                                tr("같은 종목·같은 방향은 4분(채점 시각)까지 다시 알리지 않는다.\n"
                                   "엔진이 막은 최종 합의(특수 상황, 기준가 없음, 목표 시각 지남)로는 알리지 않는다.\n"
                                   "알림은 Fincept 알림(앱 안 알림과 OS 알림)으로 나간다.\n"
                                   "이 화면을 연 뒤부터 다른 화면을 보는 동안에도 알리고, 탭을 닫으면 멈춘다."));
    outer->addWidget(header_.bar);
    auto* body = new QWidget;
    outer->addWidget(body, 1);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(12, 8, 12, 8);
    on_ = new QCheckBox(tr("신호 알림 켜기"));
    on_->setChecked(AppConfig::instance().get(kOnKey, true).toBool());
    layout->addWidget(on_);

    auto* probability = new QHBoxLayout;
    probability->addWidget(new QLabel(tr("최종 합의 확률")));
    probability_ = new QSpinBox;
    probability_->setRange(kMinPercent, kMaxPercent);
    probability_->setSuffix("%");
    probability->addWidget(probability_);
    probability->addWidget(new QLabel(tr("이상")));
    probability->addStretch();
    layout->addLayout(probability);

    auto* agree = new QHBoxLayout;
    agree->addWidget(new QLabel(tr("같은 방향 정식 알고리즘")));
    agree_ = new QSpinBox;
    agree_->setRange(1, kMaxAgree);
    agree_->setSuffix(tr("개"));
    agree->addWidget(agree_);
    agree->addWidget(new QLabel(tr("이상")));
    agree->addStretch();
    layout->addLayout(agree);

    // Where the two values come from (codeyoma/krx-quant#617), and the way back to the engine's.
    auto* source = new QHBoxLayout;
    source_ = new QLabel;
    source_->setWordWrap(true);
    source_->setToolTip(tr("기준값과 N은 엔진의 신호 규칙(텔레그램 신호와 같은 값)을 따른다.\n"
                           "여기서 바꾼 값은 저장되어 엔진 기준보다 우선한다(직접 설정).\n"
                           "기준을 주지 않는 옛 엔진에서는 기본값 60% · 2개다."));
    source->addWidget(source_, 1);
    to_engine_ = new QPushButton(tr("엔진 기준으로"));
    to_engine_->setToolTip(tr("여기서 저장한 기준값과 N을 지우고 엔진의 신호 규칙을 따른다."));
    source->addWidget(to_engine_, 0, Qt::AlignTop);
    layout->addLayout(source);
    show_rule();

    sound_ = new QCheckBox(tr("소리"));
    sound_->setChecked(AppConfig::instance().get(kSoundKey, true).toBool());
    layout->addWidget(sound_);

    last_ = new QLabel; // shown from the first signal on
    last_->setWordWrap(true);
    last_->hide();
    layout->addWidget(last_);

    auto* test = new QPushButton(tr("시험 알림"));
    test->setToolTip(tr("OS 알림 권한과 소리를 확인한다. 신호는 이렇게 온다."));
    layout->addWidget(test, 0, Qt::AlignLeft);
    layout->addStretch();

    connect(test, &QPushButton::clicked, this,
            [this]() { notify(tr("KRX 신호 시험 · 이 알림과 소리가 나면 신호도 이렇게 온다")); });
    // Each setting is saved when the user changes it, and only it: a value never set here keeps
    // following the engine's (codeyoma/krx-quant#617).
    connect(on_, &QCheckBox::toggled, this, [this](bool on) {
        AppConfig::instance().set(kOnKey, on);
        emit changed();
    });
    connect(probability_, &QSpinBox::valueChanged, this, [this](int percent) {
        AppConfig::instance().set(kProbabilityKey, percent);
        show_rule();
        emit changed();
    });
    connect(agree_, &QSpinBox::valueChanged, this, [this](int agree) {
        AppConfig::instance().set(kAgreeKey, agree);
        show_rule();
        emit changed();
    });
    connect(to_engine_, &QPushButton::clicked, this, [this]() {
        AppConfig::instance().remove(kProbabilityKey);
        AppConfig::instance().remove(kAgreeKey);
        show_rule();
        emit changed();
    });
    connect(sound_, &QCheckBox::toggled, this, [](bool on) { AppConfig::instance().set(kSoundKey, on); });
}

bool KrxAlerts::is_on() const {
    return on_->isChecked();
}

krx_quant::SavedRule KrxAlerts::saved() {
    krx_quant::SavedRule saved;
    // As the spin boxes show them, so the rule and what is shown agree.
    if (const QVariant percent = AppConfig::instance().get(kProbabilityKey); percent.isValid())
        saved.min_probability = std::clamp(percent.toInt(), kMinPercent, kMaxPercent) / 100.0;
    if (const QVariant agree = AppConfig::instance().get(kAgreeKey); agree.isValid())
        saved.min_agree = std::clamp(agree.toInt(), 1, kMaxAgree);
    return saved;
}

void KrxAlerts::set_engine_rule(const std::optional<krx_quant::SignalRule>& rule) {
    engine_rule_ = rule;
    show_rule();
}

void KrxAlerts::show_rule() {
    const krx_quant::SavedRule kept = saved();
    const krx_quant::SignalRule rule = krx_quant::rule_in_force(kept, engine_rule_);
    {
        // Shown, not set by the user: nothing is saved.
        const QSignalBlocker quiet_probability(probability_);
        const QSignalBlocker quiet_agree(agree_);
        probability_->setValue(qRound(rule.min_probability * 100));
        agree_->setValue(rule.min_agree);
    }
    source_->setText(krx_quant::rule_source(kept, engine_rule_));
    to_engine_->setVisible(kept.any());
}

void KrxAlerts::raise(const krx_quant::Signal& signal) {
    const QString text = signal.text();
    last_->setText(tr("마지막 신호 (%1): %2").arg(krx_quant::kst_text(QDateTime::currentDateTime(), "HH:mm:ss"), text));
    last_->show();
    notify(tr("KRX 신호 · %1").arg(text));
}

void KrxAlerts::restyle() {
    header_.restyle();
    source_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
}

void KrxAlerts::notify(const QString& text) {
    // Fincept's notifications: an in-app toast and the OS's (DesktopNotifier mirrors every toast).
    ui::ToastService::instance().post(ui::ToastService::Severity::Warning, text, QStringLiteral("krx_quant"));
    if (sound_->isChecked())
        QApplication::beep();
}

} // namespace fincept::screens
