#include "screens/krx_quant/KrxStatsWindow.h"

#include "screens/krx_quant/KrxTables.h"
#include "ui/theme/Theme.h"

#include <QCloseEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
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

void KrxCalibrationPlot::set_points(const QList<krx_quant::CalibrationPoint>& points) {
    points_ = points;
    hovered_ = -1;
    update();
}

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
    p.drawText(QRectF(r.left(), r.bottom() + 16, r.width(), 14), Qt::AlignHCenter | Qt::AlignTop,
               tr("가로: 예측한 확률 · 세로: 실제로 일어난 비율"));

    // A perfect forecast: what was forecast at p happens p of the time.
    p.setPen(QPen(QColor(ui::colors::BORDER_MED()), 1, Qt::DashLine));
    p.drawLine(QPointF(x_of(0), y_of(0)), QPointF(x_of(1), y_of(1)));

    if (points_.isEmpty()) {
        p.setPen(QColor(ui::colors::TEXT_DIM()));
        p.drawText(r, Qt::AlignCenter, tr("채점된 예측이 없습니다"));
        return;
    }
    const QColor color(ui::colors::AMBER());
    for (int i = 0; i < points_.size(); ++i) {
        const auto& point = points_[i];
        const double x = x_of(point.forecast);
        p.setPen(QPen(color, 1));
        if (point.low && point.high)
            p.drawLine(QPointF(x, y_of(*point.low)), QPointF(x, y_of(*point.high)));
        p.setBrush(i == hovered_ ? QColor(ui::colors::TEXT_PRIMARY()) : color);
        p.drawEllipse(position(point.forecast, point.observed), kDot, kDot);
    }
}

void KrxCalibrationPlot::mouseMoveEvent(QMouseEvent* event) {
    const QPointF at = event->position();
    int nearest = -1;
    double best = kNear;
    for (int i = 0; i < points_.size(); ++i) {
        const QPointF point = position(points_[i].forecast, points_[i].observed);
        if (const double d = std::hypot(point.x() - at.x(), point.y() - at.y()); d <= best) {
            best = d;
            nearest = i;
        }
    }
    if (nearest != hovered_) {
        hovered_ = nearest;
        update();
    }
    if (nearest < 0)
        QToolTip::hideText();
    else
        QToolTip::showText(event->globalPosition().toPoint(), points_[nearest].tip, this);
}

void KrxCalibrationPlot::leaveEvent(QEvent* event) {
    hovered_ = -1;
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}

// ── The window ──────────────────────────────────────────────────────────────────────────

KrxStatsWindow::KrxStatsWindow(QWidget* parent) : QWidget(parent, Qt::Window) {
    resize(980, 760);
    auto* layout = new QVBoxLayout(this);
    query_ = new QLabel;
    query_->setWordWrap(true);
    layout->addWidget(query_);

    overall_ = krx_tables::table(QStringList{tr("순위")} + krx_tables::stats_headers() + QStringList{tr("운 보정")});
    layout->addWidget(overall_);

    auto* middle = new QHBoxLayout;
    auto* lift_box = new QGroupBox(tr("기준 예측 대비 lift"));
    lifts_ = krx_tables::table({tr("기준"), tr("lift"), tr("95% 구간"), tr("짝"), tr("p"), tr("q"), tr("운 보정"),
                                tr("거래일 95% 구간"), tr("날")});
    (new QVBoxLayout(lift_box))->addWidget(lifts_);
    middle->addWidget(lift_box, 3);
    auto* calibration_box = new QGroupBox(tr("보정도"));
    calibration_ = new KrxCalibrationPlot;
    (new QVBoxLayout(calibration_box))->addWidget(calibration_);
    middle->addWidget(calibration_box, 2);
    layout->addLayout(middle, 2);

    breakdowns_ = new QTabWidget;
    by_slot_ = breakdown_table(tr("시간대 (봉 마감)"));
    by_model_version_ = breakdown_table(tr("모델 버전"));
    by_symbol_ = breakdown_table(tr("종목"));
    breakdowns_->addTab(by_slot_, tr("30분 시간대별"));
    breakdowns_->addTab(by_model_version_, tr("모델 버전별"));
    breakdowns_->addTab(by_symbol_, tr("종목별"));
    layout->addWidget(breakdowns_, 3);

    note_ = new QLabel;
    note_->setObjectName("krxNote");
    note_->setWordWrap(true);
    layout->addWidget(note_);
}

void KrxStatsWindow::open(const QString& algorithm) {
    if (algorithm != algorithm_ || !isVisible()) { // reopened: the query may have changed since
        algorithm_ = algorithm;
        clear();
        note_->setText(tr("읽는 중…"));
    }
    setWindowTitle(tr("%1 상세 통계").arg(krx_quant::predictor_label(algorithm)));
    show();
    raise();
    activateWindow();
}

void KrxStatsWindow::clear() {
    query_->clear();
    for (auto* table : {overall_, lifts_, by_slot_, by_model_version_, by_symbol_})
        table->setRowCount(0);
    calibration_->set_points({});
}

void KrxStatsWindow::show_stats(const krx_quant::AlgorithmStats& stats) {
    query_->setText(tr("%1 · %2").arg(krx_quant::query_text(stats), krx_quant::days_text(stats.days)));

    const auto& e = stats.overall;
    overall_->setRowCount(1);
    overall_->setItem(0, kRank, cell(e.rank ? QString::number(*e.rank) : tr("순위 밖"), kNumber));
    krx_tables::set_stats(overall_, 0, kStats, e);
    overall_->setItem(0, kSignificant, cell(e.significant ? tr("통과") : QString()));
    fit_rows(overall_);

    const auto rows = krx_quant::lift_rows(e);
    lifts_->setRowCount(0);
    for (const auto& l : rows) {
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
    calibration_->set_points(krx_quant::calibration_points(stats.calibration));

    fill_breakdown(by_slot_, stats.by_slot, krx_quant::slot_label);
    fill_breakdown(by_model_version_, stats.by_model_version, nullptr);
    fill_breakdown(by_symbol_, stats.by_symbol, nullptr);

    QString note =
        tr("lift는 같은 종목·봉 마감의 기준 예측과 정답(1/0) 차이의 평균이다. q와 운 보정은 "
           "순위표의 다른 lift와 함께 보정한 것이고, 순위 밖이거나 나눠 본 줄은 함께 검정하지 않아 없다. "
           "95% 구간과 p는 봉 마감마다 따로 센 설명용이다. 같은 날의 4분 정답은 서로 겹쳐 실제보다 확실해 "
           "보인다. 상태(수습·정식·벤치)는 하루를 값 하나로 본 거래일 95% 구간으로 정한다. 2일보다 적으면 없다. "
           "보정도는 모든 클래스 확률을 10% 구간으로 나눠, 예측한 확률과 실제로 일어난 비율을 견준다. "
           "점이 점선에 가까울수록 확률을 믿을 수 있다.");
    if (krx_quant::is_baseline(stats.algorithm))
        note.prepend(tr("기준 예측 자신이라 lift가 없다. "));
    else if (rows.isEmpty())
        note.prepend(tr("기준 예측과 짝지을 채점된 예측이 없어 lift가 없다. "));
    note_->setText(note);
}

void KrxStatsWindow::show_failure(const QString& reason) {
    clear();
    note_->setText(tr("상세 통계를 읽지 못했습니다: %1").arg(reason));
}

void KrxStatsWindow::closeEvent(QCloseEvent* event) {
    emit closed();
    QWidget::closeEvent(event);
}

} // namespace fincept::screens
