#include "screens/krx_quant/KrxQuantScreen.h"

#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::screens {

namespace {

constexpr int kRefreshMs = 5000;

enum Column { kSymbol, kMid, kPredictor, kDirection, kUp, kFlat, kDown, kTicks, kModel, kColumns };

QString hhmmss(const QDateTime& t) {
    return t.isValid() ? t.toOffsetFromUtc(9 * 3600).toString("HH:mm:ss") : QStringLiteral("—");
}

QString percent(const std::optional<double>& p) {
    return p ? QString::number(*p * 100.0, 'f', 0) + "%" : QString();
}

QString predictor_label(const QString& name) {
    if (name == "consensus-final")
        return QStringLiteral("합의 (최종)");
    if (name == "consensus-fast")
        return QStringLiteral("합의 (빠른)");
    return name;
}

QTableWidgetItem* cell(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(align);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

} // namespace

KrxQuantScreen::KrxQuantScreen(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;color:%2;").arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* top = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("KRX 4분 예측"));
    title->setStyleSheet(QString("color:%1;font-size:16px;font-weight:700;").arg(ui::colors::AMBER()));
    top->addWidget(title);
    top->addStretch();
    top->addWidget(new QLabel(QStringLiteral("엔진 주소")));
    url_edit_ = new QLineEdit(services::KrxQuantService::instance().base_url());
    url_edit_->setMinimumWidth(260);
    url_edit_->setStyleSheet(QString("background:%1;border:1px solid %2;padding:3px;")
                                 .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_MED()));
    top->addWidget(url_edit_);
    auto* apply = new QPushButton(QStringLiteral("적용"));
    apply->setStyleSheet(QString("background:%1;border:1px solid %2;padding:3px 10px;")
                             .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_MED()));
    top->addWidget(apply);
    layout->addLayout(top);

    status_label_ = new QLabel(QStringLiteral("엔진을 읽는 중…"));
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);
    special_label_ = new QLabel;
    special_label_->setWordWrap(true);
    special_label_->setStyleSheet(QString("color:%1;").arg(ui::colors::AMBER()));
    special_label_->hide();
    layout->addWidget(special_label_);

    table_ = new QTableWidget(0, kColumns);
    table_->setHorizontalHeaderLabels({QStringLiteral("종목"), QStringLiteral("기준가"), QStringLiteral("예측"),
                                       QStringLiteral("방향"), QStringLiteral("상승"), QStringLiteral("보합"),
                                       QStringLiteral("하락"), QStringLiteral("기대 틱"), QStringLiteral("모델")});
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setStyleSheet(QString("QTableWidget{background:%1;gridline-color:%2;border:1px solid %2;}"
                                  "QHeaderView::section{background:%3;color:%4;border:none;padding:4px;}")
                              .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), ui::colors::BG_RAISED(),
                                   ui::colors::TEXT_SECONDARY()));
    layout->addWidget(table_, 1);

    footer_label_ = new QLabel;
    footer_label_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_DIM()));
    layout->addWidget(footer_label_);

    auto& service = services::KrxQuantService::instance();
    connect(&service, &services::KrxQuantService::snapshot_ready, this, &KrxQuantScreen::show_snapshot);
    connect(&service, &services::KrxQuantService::refresh_failed, this, &KrxQuantScreen::show_failure);
    connect(apply, &QPushButton::clicked, this, &KrxQuantScreen::apply_base_url);
    connect(url_edit_, &QLineEdit::returnPressed, this, &KrxQuantScreen::apply_base_url);

    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshMs);
    connect(timer_, &QTimer::timeout, &service, &services::KrxQuantService::refresh);
}

void KrxQuantScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    services::KrxQuantService::instance().refresh();
    timer_->start();
}

void KrxQuantScreen::hideEvent(QHideEvent* event) {
    timer_->stop(); // nothing is read while the screen is not shown
    QWidget::hideEvent(event);
}

void KrxQuantScreen::apply_base_url() {
    auto& service = services::KrxQuantService::instance();
    service.set_base_url(url_edit_->text());
    url_edit_->setText(service.base_url());
    status_label_->setText(QStringLiteral("엔진을 읽는 중…"));
    service.restart();
}

void KrxQuantScreen::show_snapshot(const services::KrxQuantService::Snapshot& snapshot) {
    const auto& s = snapshot.status;
    QStringList parts;
    if (s.mode == "demo")
        parts << QStringLiteral("데모 데이터");
    parts << krx_quant::phase_label(s.phase);
    parts << QStringLiteral("마지막 봉 마감 %1").arg(hhmmss(s.last_bar_close));
    parts << QStringLiteral("채점 대기 %1건").arg(s.pending_outcomes);
    if (s.kis_connected)
        parts << (*s.kis_connected ? QStringLiteral("KIS 연결됨") : QStringLiteral("KIS 끊김"));
    if (s.training) {
        QStringList promoted;
        for (auto it = s.training->promoted.begin(); it != s.training->promoted.end(); ++it)
            promoted << it.key();
        const QString what =
            promoted.isEmpty() ? QStringLiteral("승격 없음") : QStringLiteral("%1 승격").arg(promoted.join(", "));
        parts << QStringLiteral("지난밤 학습 %1: %2, %3")
                     .arg(s.training->day, what,
                          s.training->applied ? QStringLiteral("적용됨") : QStringLiteral("미적용"));
    }
    status_label_->setText(parts.join("  ·  "));

    QStringList windows;
    for (const auto& w : s.special) {
        const QString who = w.symbol.isEmpty() ? QStringLiteral("시장 전체") : w.symbol;
        const QString kind = w.kind == "vi"                ? QStringLiteral("VI")
                             : w.kind == "sidecar"         ? QStringLiteral("사이드카")
                             : w.kind == "circuit_breaker" ? QStringLiteral("서킷브레이커")
                                                           : w.kind;
        windows << QStringLiteral("%1 %2 %3–%4").arg(who, kind, hhmmss(w.start), hhmmss(w.end));
    }
    special_label_->setText(QStringLiteral("오늘 특수 상황: ") + windows.join(", "));
    special_label_->setVisible(!windows.isEmpty());

    fill_table(snapshot);
    QString footer =
        QStringLiteral("갱신 %1 (엔진 시계 %2)").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), hhmmss(s.now));
    if (!snapshot.errors.isEmpty())
        footer += QStringLiteral("  ·  ") + snapshot.errors.join("; ");
    footer_label_->setText(footer);
}

void KrxQuantScreen::show_failure(const QString& reason) {
    status_label_->setText(reason);
    special_label_->hide();
    table_->setRowCount(0); // old predictions must not pass for current ones
    footer_label_->setText(QStringLiteral("실패 %1 · %2초 뒤 다시 읽습니다")
                               .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                               .arg(kRefreshMs / 1000));
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
            table_->setItem(first, kSymbol, cell(quote.symbol));
            table_->setItem(first, kMid, cell(mid, numbers));
            table_->setItem(first, kPredictor, cell(QStringLiteral("예측 없음")));
            continue;
        }
        for (const auto& p : latest->predictions) {
            const int row = table_->rowCount();
            table_->insertRow(row);
            const bool consensus = p.algorithm.startsWith("consensus-");
            table_->setItem(row, kSymbol, cell(row == first ? quote.symbol : QString()));
            table_->setItem(row, kMid, cell(row == first ? mid : QString(), numbers));
            table_->setItem(row, kPredictor, cell(predictor_label(p.algorithm)));
            const QString dir = p.status == "ok" ? krx_quant::direction(p) : krx_quant::status_label(p.status);
            auto* dir_cell = cell(dir);
            if (dir == QStringLiteral("상승"))
                dir_cell->setForeground(QColor(ui::colors::POSITIVE()));
            else if (dir == QStringLiteral("하락"))
                dir_cell->setForeground(QColor(ui::colors::NEGATIVE()));
            table_->setItem(row, kDirection, dir_cell);
            table_->setItem(row, kUp, cell(percent(p.p_up), numbers));
            table_->setItem(row, kFlat, cell(percent(p.p_flat), numbers));
            table_->setItem(row, kDown, cell(percent(p.p_down), numbers));
            table_->setItem(row, kTicks,
                            cell(p.expected_ticks ? QString::number(*p.expected_ticks, 'f', 1) : QString(), numbers));
            table_->setItem(row, kModel, cell(p.model_version));
            if (consensus) {
                for (int c = 0; c < kColumns; ++c) {
                    QFont font = table_->item(row, c)->font();
                    font.setBold(true);
                    table_->item(row, c)->setFont(font);
                }
            }
        }
        table_->item(first, kSymbol)->setToolTip(QStringLiteral("봉 마감 %1").arg(hhmmss(latest->bar_close)));
    }
}

} // namespace fincept::screens
