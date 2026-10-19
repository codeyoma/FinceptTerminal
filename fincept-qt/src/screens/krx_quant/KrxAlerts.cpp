#include "screens/krx_quant/KrxAlerts.h"

#include "core/config/AppConfig.h"
#include "ui/notifications/NotificationService.h"

#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

constexpr auto kOnKey = "krx_quant/signal_on";
constexpr auto kProbabilityKey = "krx_quant/signal_probability";
constexpr auto kAgreeKey = "krx_quant/signal_agree";
constexpr auto kSoundKey = "krx_quant/signal_sound";

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
    probability_->setRange(50, 99);
    probability_->setSuffix("%");
    probability_->setValue(qRound(saved_rule().min_probability * 100));
    probability->addWidget(probability_);
    probability->addWidget(new QLabel(tr("이상")));
    probability->addStretch();
    layout->addLayout(probability);

    auto* agree = new QHBoxLayout;
    agree->addWidget(new QLabel(tr("같은 방향 정식 알고리즘")));
    agree_ = new QSpinBox;
    agree_->setRange(1, 20);
    agree_->setSuffix(tr("개"));
    agree_->setValue(saved_rule().min_agree);
    agree->addWidget(agree_);
    agree->addWidget(new QLabel(tr("이상")));
    agree->addStretch();
    layout->addLayout(agree);

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
    connect(on_, &QCheckBox::toggled, this, &KrxAlerts::save);
    connect(probability_, &QSpinBox::valueChanged, this, &KrxAlerts::save);
    connect(agree_, &QSpinBox::valueChanged, this, &KrxAlerts::save);
    connect(sound_, &QCheckBox::toggled, this, &KrxAlerts::save);
}

bool KrxAlerts::is_on() const {
    return on_->isChecked();
}

krx_quant::SignalRule KrxAlerts::rule() const {
    return {probability_->value() / 100.0, agree_->value()};
}

krx_quant::SignalRule KrxAlerts::saved_rule() {
    return {AppConfig::instance().get(kProbabilityKey, 60).toInt() / 100.0,
            AppConfig::instance().get(kAgreeKey, 2).toInt()};
}

void KrxAlerts::raise(const krx_quant::Signal& signal) {
    const QString text = signal.text();
    last_->setText(tr("마지막 신호 (%1): %2").arg(krx_quant::kst_text(QDateTime::currentDateTime(), "HH:mm:ss"), text));
    last_->show();
    notify(tr("KRX 신호 · %1").arg(text));
}

void KrxAlerts::restyle() {
    header_.restyle();
}

void KrxAlerts::save() {
    AppConfig::instance().set(kOnKey, on_->isChecked());
    AppConfig::instance().set(kProbabilityKey, probability_->value());
    AppConfig::instance().set(kAgreeKey, agree_->value());
    AppConfig::instance().set(kSoundKey, sound_->isChecked());
    emit changed();
}

void KrxAlerts::notify(const QString& text) {
    // Fincept's notifications: an in-app toast and the OS's (DesktopNotifier mirrors every toast).
    ui::ToastService::instance().post(ui::ToastService::Severity::Warning, text, QStringLiteral("krx_quant"));
    if (sound_->isChecked())
        QApplication::beep();
}

} // namespace fincept::screens
