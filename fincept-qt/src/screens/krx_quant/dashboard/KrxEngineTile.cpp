#include "screens/krx_quant/dashboard/KrxEngineTile.h"

#include "core/events/EventBus.h"
#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/KrxQuantService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <algorithm>

#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>

namespace fincept::screens::krx_dashboard {

namespace {

/// Open the KRX 4-minute prediction screen.
void open_krx_screen() {
    EventBus::instance().publish("nav.switch_screen", QVariantMap{{"screen_id", QStringLiteral("krx_quant")}});
}

} // namespace

KrxEngineTile::KrxEngineTile(const QString& title, int every_ms, QWidget* parent)
    : BaseWidget(title, parent), head_(title) {
    service_ = new services::KrxQuantService(this);

    failure_ = new QLabel(this);
    failure_->setWordWrap(true);
    failure_->setContentsMargins(6, 2, 6, 2);
    failure_->hide();
    content_layout()->addWidget(failure_);

    auto* holder = new QWidget(this);
    body_ = new QVBoxLayout(holder);
    body_->setContentsMargins(0, 0, 0, 0);
    body_->setSpacing(0);
    content_layout()->addWidget(holder, 1);

    timer_ = new QTimer(this);
    timer_->setInterval(every_ms);
    connect(timer_, &QTimer::timeout, this, &KrxEngineTile::tick);
    connect(this, &BaseWidget::refresh_requested, this, &KrxEngineTile::tick);

    on_theme_changed();
}

void KrxEngineTile::showEvent(QShowEvent* event) {
    BaseWidget::showEvent(event);
    if (!timer_->isActive()) {
        timer_->start();
        tick();
    }
}

void KrxEngineTile::hideEvent(QHideEvent* event) {
    BaseWidget::hideEvent(event);
    timer_->stop(); // nothing is asked of the engine while the tile cannot be seen
}

void KrxEngineTile::tick() {
    const QString address = services::KrxQuantService::base_url();
    if (address.isEmpty()) {
        set_loading(false);
        set_error(tr("KRX 엔진 주소가 없습니다. KRX 4분 예측 화면에서 엔진 주소를 넣으세요."));
        return;
    }
    if (address != service_->url()) {
        // A new address (set on the KRX screen): every read under way is dropped with the old one.
        service_->restart();
        reading_ = false;
    }
    if (reading_)
        return;
    reading_ = true;
    // The first read shows the loading overlay; a failure's overlay stays up while it is read again.
    if (!shown_data_ && !is_showing_error())
        set_loading(true);
    read();
}

void KrxEngineTile::read_done(const QString& problem) {
    reading_ = false;
    shown_data_ = true;
    set_loading(false);
    failure_->setText(problem.isEmpty() ? QString() : tr("일부 읽지 못함: %1").arg(problem));
    failure_->setVisible(!problem.isEmpty());
    answered_at_ = QDateTime::currentDateTime().toString("HH:mm:ss");
    update_title();
}

void KrxEngineTile::read_failed(const QString& reason) {
    reading_ = false;
    if (!shown_data_) {
        set_loading(false);
        set_error(reason);
        return;
    }
    failure_->setText(tr("읽지 못함: %1").arg(reason));
    failure_->show();
}

void KrxEngineTile::set_head(const QString& head) {
    head_ = head;
    update_title();
}

void KrxEngineTile::update_title() {
    set_title(answered_at_.isEmpty() ? head_ : QStringLiteral("%1 · %2").arg(head_, answered_at_));
}

QTableWidget* KrxEngineTile::make_table(const QStringList& headers) {
    auto* table = krx_tables::table(headers);
    table->setParent(this);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setToolTip(tr("더블클릭하면 KRX 4분 예측 화면을 엽니다"));
    connect(table, &QTableWidget::cellDoubleClicked, this, [](int, int) { open_krx_screen(); });
    tables_ << table;
    return table;
}

void KrxEngineTile::on_theme_changed() {
    // A size below the user's font, as the KRX screen's panel headers (KrxPanel.h).
    const int size = std::max(9, ui::ThemeManager::instance().current_font().pixelSize() - 3);
    failure_->setStyleSheet(QStringLiteral("color:%1;font-size:%2px;background:transparent;")
                                .arg(ui::colors::WARNING())
                                .arg(size));
    for (auto* table : tables_)
        krx_tables::restyle(table);
    if (shown_data_)
        render();
}

} // namespace fincept::screens::krx_dashboard
