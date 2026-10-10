#include "screens/krx_quant/KrxQuantScreen.h"

#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QPushButton>
#include <QTimeZone>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::screens {

namespace {

constexpr int kRefreshMs = 5000;

enum Column { kSymbol, kMid, kPredictor, kDirection, kUp, kFlat, kDown, kTicks, kModel, kColumns };

/// Times in KST, the exchange's and the engine's.
QString kst_time(const QDateTime& t) {
    static const QTimeZone kst("Asia/Seoul");
    return t.isValid() ? t.toTimeZone(kst).toString("HH:mm:ss") : QStringLiteral("—");
}

QString krx_percent(const std::optional<double>& p) {
    return p ? QString::number(std::lround(*p * 100.0)) + "%" : QString();
}

QTableWidgetItem* krx_cell(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(align);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

} // namespace

KrxQuantScreen::KrxQuantScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("krxQuantScreen");
    // One style sheet for the whole screen, by object name.
    setStyleSheet(QString("#krxQuantScreen{background:%1;color:%2;}"
                          "#krxQuantScreen QLabel{background:transparent;}"
                          "#krxTitle{color:%3;font-size:16px;font-weight:700;}"
                          "#krxSpecial{color:%3;}"
                          "#krxFooter{color:%4;}"
                          "#krxQuantScreen QLineEdit{background:%5;border:1px solid %6;padding:3px;}"
                          "#krxQuantScreen QPushButton{background:%7;border:1px solid %6;padding:3px 10px;}"
                          "#krxQuantScreen QTableWidget{background:%5;gridline-color:%8;border:1px solid %8;}"
                          "#krxQuantScreen QHeaderView::section{background:%7;color:%9;border:none;padding:4px;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::AMBER(),
                           ui::colors::TEXT_DIM(), ui::colors::BG_SURFACE(), ui::colors::BORDER_MED(),
                           ui::colors::BG_RAISED(), ui::colors::BORDER_DIM(), ui::colors::TEXT_SECONDARY()));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* top = new QHBoxLayout;
    auto* title = new QLabel(tr("KRX 4분 예측"));
    title->setObjectName("krxTitle");
    top->addWidget(title);
    top->addStretch();
    top->addWidget(new QLabel(tr("엔진 주소")));
    url_edit_ = new QLineEdit(services::KrxQuantService::base_url());
    url_edit_->setPlaceholderText(tr("예: http://엔진-주소:8080"));
    url_edit_->setMinimumWidth(260);
    top->addWidget(url_edit_);
    auto* apply = new QPushButton(tr("적용"));
    top->addWidget(apply);
    layout->addLayout(top);

    status_label_ = new QLabel(tr("엔진을 읽는 중…"));
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);
    special_label_ = new QLabel;
    special_label_->setObjectName("krxSpecial");
    special_label_->setWordWrap(true);
    special_label_->hide();
    layout->addWidget(special_label_);

    table_ = new QTableWidget(0, kColumns);
    table_->setHorizontalHeaderLabels({tr("종목"), tr("기준가"), tr("예측"), tr("방향"), tr("상승"), tr("보합"),
                                       tr("하락"), tr("기대 틱"), tr("모델")});
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);

    footer_label_ = new QLabel;
    footer_label_->setObjectName("krxFooter");
    layout->addWidget(footer_label_);

    service_ = new services::KrxQuantService(this);
    connect(service_, &services::KrxQuantService::snapshot_ready, this, &KrxQuantScreen::show_snapshot);
    connect(service_, &services::KrxQuantService::refresh_failed, this, &KrxQuantScreen::show_failure);
    connect(service_, &services::KrxQuantService::base_url_changed, this, [this](const QString& url) {
        if (!url_edit_->hasFocus()) // not while the address is being typed here
            url_edit_->setText(url);
    });
    connect(apply, &QPushButton::clicked, this, &KrxQuantScreen::apply_base_url);
    connect(url_edit_, &QLineEdit::returnPressed, this, &KrxQuantScreen::apply_base_url);

    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshMs);
    connect(timer_, &QTimer::timeout, service_, &services::KrxQuantService::refresh);
}

void KrxQuantScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    service_->refresh();
    timer_->start();
}

void KrxQuantScreen::hideEvent(QHideEvent* event) {
    timer_->stop(); // nothing is read while the screen is not shown
    QWidget::hideEvent(event);
}

void KrxQuantScreen::apply_base_url() {
    services::KrxQuantService::set_base_url(url_edit_->text());
    url_edit_->setText(services::KrxQuantService::base_url());
    status_label_->setText(tr("엔진을 읽는 중…"));
    service_->restart();
}

void KrxQuantScreen::show_snapshot(const services::KrxQuantService::Snapshot& snapshot) {
    const auto& s = snapshot.status;
    QStringList parts;
    if (s.mode == "demo")
        parts << tr("데모 데이터");
    parts << krx_quant::phase_label(s.phase);
    parts << tr("마지막 봉 마감 %1").arg(kst_time(s.last_bar_close));
    parts << tr("채점 대기 %1건").arg(s.pending_outcomes);
    if (s.kis_connected)
        parts << (*s.kis_connected ? tr("KIS 연결됨") : tr("KIS 끊김"));
    if (s.training) {
        const QStringList promoted = s.training->promoted.keys();
        const QString what = promoted.isEmpty() ? tr("승격 없음") : tr("%1 승격").arg(promoted.join(", "));
        parts << tr("지난밤 학습 %1: %2, %3")
                     .arg(s.training->day, what, s.training->applied ? tr("적용됨") : tr("미적용"));
    }
    status_label_->setText(parts.join("  ·  "));

    QStringList windows;
    for (const auto& w : s.special) {
        const QString who = w.symbol.isEmpty() ? tr("시장 전체") : w.symbol;
        windows
            << QString("%1 %2 %3–%4").arg(who, krx_quant::special_label(w.kind), kst_time(w.start), kst_time(w.end));
    }
    special_label_->setText(tr("오늘 특수 상황: %1").arg(windows.join(", ")));
    special_label_->setVisible(!windows.isEmpty());

    fill_table(snapshot);
    QString footer = tr("갱신 %1 (엔진 시계 %2)").arg(kst_time(QDateTime::currentDateTime()), kst_time(s.now));
    if (!snapshot.errors.isEmpty())
        footer += "  ·  " + snapshot.errors.join("; ");
    footer_label_->setText(footer);
}

void KrxQuantScreen::show_failure(const QString& reason) {
    status_label_->setText(reason);
    special_label_->hide();
    table_->setRowCount(0); // old predictions must not pass for current ones
    footer_label_->setText(
        tr("실패 %1 · %2초 뒤 다시 읽습니다").arg(kst_time(QDateTime::currentDateTime())).arg(kRefreshMs / 1000));
}

void KrxQuantScreen::fill_table(const services::KrxQuantService::Snapshot& snapshot) {
    const auto numbers = Qt::AlignRight | Qt::AlignVCenter;
    const QLocale locale(QLocale::Korean, QLocale::SouthKorea);
    table_->setRowCount(0);
    for (const auto& quote : snapshot.symbols) {
        const auto latest = snapshot.latest.constFind(quote.symbol);
        const int first = table_->rowCount();
        const QString mid = quote.mid ? locale.toString(*quote.mid, 'f', std::floor(*quote.mid) == *quote.mid ? 0 : 1)
                                      : QStringLiteral("—");
        if (latest == snapshot.latest.cend() || latest->predictions.isEmpty()) {
            table_->insertRow(first);
            table_->setItem(first, kSymbol, krx_cell(quote.symbol));
            table_->setItem(first, kMid, krx_cell(mid, numbers));
            table_->setItem(first, kPredictor, krx_cell(tr("예측 없음")));
            continue;
        }
        for (const auto& p : latest->predictions) {
            const int row = table_->rowCount();
            table_->insertRow(row);
            table_->setItem(row, kSymbol, krx_cell(row == first ? quote.symbol : QString()));
            table_->setItem(row, kMid, krx_cell(row == first ? mid : QString(), numbers));
            table_->setItem(row, kPredictor, krx_cell(krx_quant::predictor_label(p.algorithm)));
            const auto dir = krx_quant::direction(p);
            auto* dir_cell =
                krx_cell(p.status == "ok" ? krx_quant::direction_label(dir) : krx_quant::status_label(p.status));
            if (dir == krx_quant::Direction::Up)
                dir_cell->setForeground(QColor(ui::colors::POSITIVE()));
            else if (dir == krx_quant::Direction::Down)
                dir_cell->setForeground(QColor(ui::colors::NEGATIVE()));
            table_->setItem(row, kDirection, dir_cell);
            table_->setItem(row, kUp, krx_cell(krx_percent(p.p_up), numbers));
            table_->setItem(row, kFlat, krx_cell(krx_percent(p.p_flat), numbers));
            table_->setItem(row, kDown, krx_cell(krx_percent(p.p_down), numbers));
            const QString ticks = p.expected_ticks ? QString::number(*p.expected_ticks, 'f', 1) : QString();
            table_->setItem(row, kTicks, krx_cell(ticks, numbers));
            table_->setItem(row, kModel, krx_cell(p.model_version));
            if (krx_quant::is_consensus(p.algorithm)) {
                for (int c = 0; c < kColumns; ++c) {
                    QFont font = table_->item(row, c)->font();
                    font.setBold(true);
                    table_->item(row, c)->setFont(font);
                }
            }
        }
        table_->item(first, kSymbol)->setToolTip(tr("봉 마감 %1").arg(kst_time(latest->bar_close)));
    }
}

} // namespace fincept::screens
