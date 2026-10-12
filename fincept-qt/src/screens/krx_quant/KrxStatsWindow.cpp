#include "screens/krx_quant/KrxStatsWindow.h"

#include "screens/krx_quant/KrxTables.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QCloseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitter>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace {

using krx_tables::cell;
using krx_tables::kNumber;

constexpr int kAxis = 34;     ///< room for an axis's labels, px
constexpr double kDot = 4.0;  ///< a point's radius, px
constexpr double kNear = 9.0; ///< how close the pointer must be to a point to show it, px

enum OverallColumn { kRank, kStats, kSignificant = kStats + krx_tables::kStatsColumns };
enum LiftColumn { kBaseline, kGain, kInterval, kPairs, kP, kQ, kVerdict, kDayInterval, kDays };

/// A breakdown table: what it is split by, then the stats.
QTableWidget* breakdown_table(const QString& split) {
    auto* table = krx_tables::table(QStringList{split} + krx_tables::stats_headers());
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    return table;
}

void fill_breakdown(QTableWidget* table, const QMap<QString, krx_quant::Entry>& entries,
                    QString (*label)(const QString&)) {
    table->setRowCount(0);
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, cell(label ? label(it.key()) : it.key()));
        krx_tables::set_stats(table, row, 1, it.value());
    }
}

/// A table sized to show its rows without a scroll bar.
void fit_rows(QTableWidget* table) {
    int height = table->horizontalHeader()->height() + 2 * table->frameWidth();
    for (int row = 0; row < table->rowCount(); ++row)
        height += table->rowHeight(row);
    table->setFixedHeight(height);
}

} // namespace

// ── The calibration plot ────────────────────────────────────────────────────────────────

KrxCalibrationPlot::KrxCalibrationPlot(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(240, 240);
}

void KrxCalibrationPlot::set_series(const QList<krx_quant::CalibrationSeries>& series) {
    series_ = series;
    hovered_ = {-1, -1};
    update();
}

namespace {

/// A curve's colour: the classes pooled in amber, up and down as prices move, flat in grey.
QColor series_color(const QString& key) {
    if (key == "up")
        return QColor(ui::colors::POSITIVE());
    if (key == "down")
        return QColor(ui::colors::NEGATIVE());
    if (key == "flat")
        return QColor(ui::colors::TEXT_SECONDARY());
    return QColor(ui::colors::AMBER());
}

} // namespace

QRectF KrxCalibrationPlot::plot_rect() const {
    const double side = std::max(0, std::min(width() - kAxis - 8, height() - kAxis - 8));
    return {static_cast<double>(kAxis), 8.0, side, side};
}

QPointF KrxCalibrationPlot::position(double forecast, double observed) const {
    const QRectF r = plot_rect();
    return {r.left() + forecast * r.width(), r.bottom() - observed * r.height()};
}

void KrxCalibrationPlot::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(ui::colors::BG_SURFACE()));
    const QRectF r = plot_rect();
    if (r.width() < 40)
        return;
    const auto x_of = [this](double share) { return position(share, 0).x(); };
    const auto y_of = [this](double share) { return position(0, share).y(); };

    QFont small = font();
    small.setPixelSize(11);
    p.setFont(small);
    for (int k = 0; k <= 4; ++k) {
        const double share = k / 4.0;
        p.setPen(QColor(ui::colors::BORDER_DIM()));
        p.drawLine(QPointF(r.left(), y_of(share)), QPointF(r.right(), y_of(share)));
        p.drawLine(QPointF(x_of(share), r.top()), QPointF(x_of(share), r.bottom()));
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        const QString label = QString::number(k * 25) + "%";
        p.drawText(QRectF(0, y_of(share) - 8, kAxis - 4, 16), Qt::AlignRight | Qt::AlignVCenter, label);
        p.drawText(QRectF(x_of(share) - 20, r.bottom() + 2, 40, 14), Qt::AlignHCenter | Qt::AlignTop, label);
    }
    // Across the whole width: wider than a narrow plot.
    p.drawText(QRectF(0, r.bottom() + 16, width(), 14), Qt::AlignHCenter | Qt::AlignTop,
               tr("가로: 예측한 확률 · 세로: 실제로 일어난 비율"));

    // A perfect forecast: what was forecast at p happens p of the time.
    p.setPen(QPen(QColor(ui::colors::BORDER_MED()), 1, Qt::DashLine));
    p.drawLine(QPointF(x_of(0), y_of(0)), QPointF(x_of(1), y_of(1)));

    if (series_.isEmpty()) // nothing scored: the title's tooltip says so
        return;
    for (int s = 0; s < series_.size(); ++s) {
        const QColor color = series_color(series_[s].key);
        const auto& points = series_[s].points;
        for (int i = 0; i < points.size(); ++i) {
            const auto& point = points[i];
            const double x = x_of(point.forecast);
            p.setPen(QPen(color, 1));
            if (i > 0) // the curve, through its points
                p.drawLine(position(points[i - 1].forecast, points[i - 1].observed),
                           position(point.forecast, point.observed));
            if (point.low && point.high)
                p.drawLine(QPointF(x, y_of(*point.low)), QPointF(x, y_of(*point.high)));
            p.setBrush(hovered_ == QPair<int, int>{s, i} ? QColor(ui::colors::TEXT_PRIMARY()) : color);
            p.drawEllipse(position(point.forecast, point.observed), kDot, kDot);
        }
    }
    // The legend, bottom right over a backing so the curves under it stay apart from it: a
    // swatch and the curve's name each.
    const QRectF legend(r.right() - 66, r.bottom() - 8 - series_.size() * 16, 60, series_.size() * 16 + 4);
    QColor backing(ui::colors::BG_SURFACE());
    backing.setAlpha(220);
    p.fillRect(legend, backing);
    for (int s = 0; s < series_.size(); ++s) {
        const QRectF swatch(legend.left() + 4, legend.top() + 5 + s * 16, 10, 10);
        p.fillRect(swatch, series_color(series_[s].key));
        p.setPen(QColor(ui::colors::TEXT_PRIMARY()));
        p.drawText(QRectF(swatch.right() + 4, swatch.top() - 3, 44, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   series_[s].name);
    }
}

void KrxCalibrationPlot::mouseMoveEvent(QMouseEvent* event) {
    const QPointF at = event->position();
    QPair<int, int> nearest{-1, -1};
    double best = kNear;
    for (int s = 0; s < series_.size(); ++s) {
        const auto& points = series_[s].points;
        for (int i = 0; i < points.size(); ++i) {
            const QPointF point = position(points[i].forecast, points[i].observed);
            if (const double d = std::hypot(point.x() - at.x(), point.y() - at.y()); d <= best) {
                best = d;
                nearest = {s, i};
            }
        }
    }
    if (nearest != hovered_) {
        hovered_ = nearest;
        update();
    }
    if (nearest.first < 0) {
        QToolTip::hideText();
        return;
    }
    // Points drawn on one another (the pooled curve and a class can meet) show together.
    QStringList tips;
    for (const auto& curve : series_) {
        for (const auto& point : curve.points) {
            const QPointF there = position(point.forecast, point.observed);
            if (std::hypot(there.x() - at.x(), there.y() - at.y()) <= best + 1)
                tips << point.tip;
        }
    }
    QToolTip::showText(event->globalPosition().toPoint(), tips.join("\n\n"), this);
}

void KrxCalibrationPlot::leaveEvent(QEvent* event) {
    hovered_ = {-1, -1};
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}

// ── The window ──────────────────────────────────────────────────────────────────────────

KrxStatsWindow::KrxStatsWindow(QWidget* parent) : QWidget(parent, Qt::Window) {
    setObjectName("krxStatsWindow");
    setAttribute(Qt::WA_StyledBackground);
    resize(980, 760);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = krx_panel::header(QString());
    failure_ = new QLabel(tr("읽지 못함"));
    failure_->hide();
    header_.controls->addWidget(failure_);
    query_ = new QLabel;
    header_.controls->addWidget(query_);
    layout->addWidget(header_.bar);

    body_ = new QWidget;
    auto* body = new QVBoxLayout(body_);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    overall_ = krx_tables::table(QStringList{tr("순위")} + krx_tables::stats_headers() + QStringList{tr("운 보정")});
    body->addWidget(overall_);
    excluded_ = new QLabel;
    excluded_->setContentsMargins(12, 4, 12, 4);
    excluded_->setWordWrap(true);
    body->addWidget(excluded_);

    auto* middle = new QSplitter(Qt::Horizontal);
    lift_header_ = krx_panel::header(tr("기준 예측 대비 lift"));
    lifts_ = krx_tables::table({tr("기준"), tr("lift"), tr("95% 구간"), tr("짝"), tr("p"), tr("q"), tr("운 보정"),
                                tr("거래일 95% 구간"), tr("날")});
    middle->addWidget(krx_panel::panel(lift_header_, lifts_));
    calibration_header_ = krx_panel::header(tr("보정도"));
    calibration_ = new KrxCalibrationPlot;
    middle->addWidget(krx_panel::panel(calibration_header_, calibration_));
    middle->setStretchFactor(0, 3);
    middle->setStretchFactor(1, 2);

    breakdown_header_ =
        krx_panel::header(tr("나눠 본 통계"), tr("이 줄들의 lift는 함께 검정하지 않아 q와 운 보정이 없다."));
    breakdowns_ = new QTabWidget;
    by_slot_ = breakdown_table(tr("시간대 (봉 마감)"));
    by_model_version_ = breakdown_table(tr("모델 버전"));
    by_symbol_ = breakdown_table(tr("종목"));
    breakdowns_->addTab(by_slot_, tr("30분 시간대별"));
    breakdowns_->addTab(by_model_version_, tr("모델 버전별"));
    breakdowns_->addTab(by_symbol_, tr("종목별"));

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(middle);
    split->addWidget(krx_panel::panel(breakdown_header_, breakdowns_));
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    body->addWidget(split, 1);
    layout->addWidget(body_, 1);

    loading_ = new ui::LoadingOverlay(body_);
    body_->installEventFilter(this); // the overlay keeps the body's size
    restyle();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this]() { restyle(); });
}

void KrxStatsWindow::open(const QString& algorithm) {
    if (algorithm != algorithm_ || !isVisible()) {
        algorithm_ = algorithm;
        clear();
        loading_->show_loading(tr("읽는 중…"));
    }
    const QString title = tr("%1 상세 통계").arg(krx_quant::predictor_label(algorithm));
    header_.title->setText(title);
    // As Fincept names its windows: the app, then what is shown.
    setWindowTitle(QStringLiteral("Fincept Terminal — %1 — %2").arg(tr("KRX 4분 예측"), title));
    show();
    raise();
    activateWindow();
}

void KrxStatsWindow::clear() {
    query_->clear();
    excluded_->clear();
    failure_->hide();
    for (auto* table : {overall_, lifts_, by_slot_, by_model_version_, by_symbol_})
        table->setRowCount(0);
    lift_header_.title->setToolTip(QString());
    calibration_header_.title->setToolTip(QString());
    calibration_->set_series({});
}

void KrxStatsWindow::show_stats(const krx_quant::AlgorithmStats& stats) {
    loading_->hide_loading();
    failure_->hide();
    query_->setText(tr("%1 · %2").arg(krx_quant::query_text(stats), krx_quant::days_text(stats.days)));

    const auto& e = stats.overall;
    overall_->setRowCount(1);
    overall_->setItem(0, kRank, cell(e.rank ? QString::number(*e.rank) : tr("순위 밖"), kNumber));
    krx_tables::set_stats(overall_, 0, kStats, e);
    overall_->setItem(0, kSignificant, cell(e.significant ? tr("통과") : QString()));
    fit_rows(overall_);

    lifts_->setRowCount(0);
    for (const auto& l : krx_quant::lift_rows(e)) {
        const int row = lifts_->rowCount();
        lifts_->insertRow(row);
        lifts_->setItem(row, kBaseline, cell(l.baseline));
        lifts_->setItem(row, kGain, cell(l.gain, kNumber));
        lifts_->setItem(row, kInterval, cell(l.interval, kNumber));
        lifts_->setItem(row, kPairs, cell(l.pairs, kNumber));
        lifts_->setItem(row, kP, cell(l.p, kNumber));
        lifts_->setItem(row, kQ, cell(l.q, kNumber));
        lifts_->setItem(row, kVerdict, cell(l.verdict));
        lifts_->setItem(row, kDayInterval, cell(l.day_interval, kNumber));
        lifts_->setItem(row, kDays, cell(l.days, kNumber));
    }
    lift_header_.title->setToolTip(krx_quant::lift_tip(stats));
    excluded_->setText(e.excluded ? krx_quant::excluded_text(*e.excluded, stats.special) : QString());
    calibration_->set_series(krx_quant::calibration_series(stats));
    calibration_header_.title->setToolTip(krx_quant::calibration_tip(stats));

    fill_breakdown(by_slot_, stats.by_slot, krx_quant::slot_label);
    fill_breakdown(by_model_version_, stats.by_model_version, nullptr);
    fill_breakdown(by_symbol_, stats.by_symbol, nullptr);
}

void KrxStatsWindow::show_failure(const QString& reason) {
    clear();
    loading_->hide_loading();
    failure_->setToolTip(tr("상세 통계를 읽지 못했습니다: %1").arg(reason));
    failure_->show();
}

void KrxStatsWindow::restyle() {
    setStyleSheet(QString("#krxStatsWindow{background:%1;}#krxStatsWindow QLabel{background:transparent;}")
                      .arg(ui::colors::BG_BASE()));
    for (const auto* header : {&header_, &lift_header_, &calibration_header_, &breakdown_header_})
        header->restyle();
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    for (auto* table : {overall_, lifts_, by_slot_, by_model_version_, by_symbol_})
        krx_tables::restyle(table);
    fit_rows(overall_);
    calibration_->update();
}

bool KrxStatsWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == body_ && event->type() == QEvent::Resize)
        loading_->setGeometry(body_->rect());
    return QWidget::eventFilter(watched, event);
}

void KrxStatsWindow::closeEvent(QCloseEvent* event) {
    emit closed();
    QWidget::closeEvent(event);
}

} // namespace fincept::screens
