#include "screens/krx_quant/dashboard/KrxEngineTile.h"

#include "core/events/EventBus.h"
#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/EnginePoller.h"
#include "services/krx_quant/KrxQuantService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>

#include <algorithm>

namespace fincept::screens::krx_dashboard {

namespace {

/// Open the KRX 4-minute prediction screen.
void open_krx_screen() {
    EventBus::instance().publish("nav.switch_screen", QVariantMap{{"screen_id", QStringLiteral("krx_quant")}});
}

} // namespace

KrxEngineTile::KrxEngineTile(const QString& title, int every_ms, QWidget* parent)
    : BaseWidget(title, parent), head_(title) {
    poller_ = new services::EnginePoller(services::EnginePoller::Symbols::None, this);
    poller_->set_read([this] { tick(); });
    poller_->set_poll([every_ms] { return every_ms; });
    // Nothing is asked of the engine while the tile cannot be seen.
    poller_->run_while_shown(this);
    // Without the address: where to set it, and nothing read.
    connect(poller_, &services::EnginePoller::changed, this, [this] {
        if (!service()->url().isEmpty())
            return;
        set_loading(false);
        set_error(tr("KRX %1").arg(QString::fromUtf8(services::EnginePoller::kNoAddress)));
    });
    // A new address (set on the KRX screen): every read under way was dropped with the old one.
    connect(poller_, &services::EnginePoller::engine_changed, this, [this] { reading_ = false; });

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

    connect(this, &BaseWidget::refresh_requested, poller_, &services::EnginePoller::read_now);

    on_theme_changed();
}

services::KrxQuantService* KrxEngineTile::service() const {
    return poller_->service();
}

void KrxEngineTile::tick() {
    if (reading_)
        return;
    reading_ = true;
    // The first read shows the loading overlay; a failure's overlay stays up while it is read again.
    if (!shown_data_ && !is_showing_error())
        set_loading(true);
    read();
}

void KrxEngineTile::read_again() {
    reading_ = false;
    poller_->read_now();
}

void KrxEngineTile::read_done(const QString& problem) {
    reading_ = false;
    shown_data_ = true;
    set_loading(false);
    failure_->setText(problem.isEmpty() ? QString() : tr("일부 읽지 못함: %1").arg(problem));
    failure_->setVisible(!problem.isEmpty());
    poller_->answered(); // the time shown, and the next read
    update_title();
}

void KrxEngineTile::read_failed(const QString& reason) {
    reading_ = false;
    poller_->failed(reason); // the next read
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
    const QDateTime& at = poller_->answered_at();
    set_title(!at.isValid() ? head_ : QStringLiteral("%1 · %2").arg(head_, at.toString("HH:mm:ss")));
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
