#include "screens/krx_quant/KrxStatsWindow.h"

#include "screens/krx_quant/KrxTables.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
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

enum OverallColumn { kRank, kStats }; ///< then the kind's stats, then the luck check
enum LiftColumn { kBaseline, kGain, kInterval, kPairs, kP, kQ, kVerdict, kDayInterval, kDays };
enum QuantileColumn { kQuantile, kPinball, kPinballInterval, kDayPinball, kPinballDays, kVsClimate, kVsClimateDay };
enum BarrierColumn { kSide, kTrades, kTotal, kPerTrade, kPerTradeInterval };

/// A breakdown table: what it is split by, then the stats.
QTableWidget* breakdown_table(const QString& split) {
    auto* table = krx_tables::table(QStringList{split} + krx_tables::stats_headers());
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    return table;
}

/// ``entries`` a row each, by key; in ``order``'s order when it is given (those it names).
void fill_breakdown(QTableWidget* table, const QMap<QString, krx_quant::Entry>& entries,
                    QString (*label)(const QString&), const std::optional<krx_quant::KindInfo>& kind,
                    const QStringList& order = {}) {
    table->setRowCount(0);
    for (const QString& key : order.isEmpty() ? entries.keys() : order) {
        if (!entries.contains(key))
            continue;
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, cell(label ? label(key) : key));
        krx_tables::set_stats(table, row, 1, entries.value(key), kind);
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

/// A curve's colour: the classes pooled in amber, up and down as prices move, flat in grey;
/// the range's (krx-quant #182) small in grey, medium in cyan, large in blue; the barrier's
/// (krx-quant #185) up first and down first as prices move, timeout in grey.
QColor series_color(const QString& key) {
    if (key == "up" || key == "up_first")
        return QColor(ui::colors::POSITIVE());
    if (key == "down" || key == "down_first")
        return QColor(ui::colors::NEGATIVE());
    if (key == "flat" || key == "small" || key == "timeout")
        return QColor(ui::colors::TEXT_SECONDARY());
    if (key == "medium")
        return QColor(ui::colors::CYAN());
    if (key == "large")
        return QColor(ui::colors::INFO());
    if (key == "all")
        return QColor(ui::colors::AMBER());
    return QColor(ui::colors::TEXT_TERTIARY()); // a class of a kind this screen does not know yet
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

    // What the algorithm is and how it learns and forecasts (krx-quant #163): the engine's own
    // words, read at run time. Above the stats, in a pane of its own the user can resize.
    about_header_ = krx_panel::header(
        tr("학습·예측 방식"),
        tr("엔진이 알려 주는 이 알고리즘의 성격, 예측 로직, 입력, 학습 방법, 지금 모델에 대한 설명이다. 학습 "
           "결과의 손실은 학습 데이터로 잰 값이라 다른 알고리즘이나 검증 손실과 견줄 수 없다."));
    about_ = new QLabel;
    about_->setWordWrap(true);
    about_->setTextFormat(Qt::RichText);
    about_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    about_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    about_->setContentsMargins(12, 6, 12, 6);
    // Below it, what the model learned from and last night's validation (krx-quant #165).
    learning_header_ = krx_panel::header(
        tr("학습과 검증"),
        tr("학습 손실은 학습 데이터로, 검증 손실은 학습에 쓰지 않은 검증일로 잰 값이라 서로 견주지 않는다. 지난밤 "
           "검증은 마지막 밤 학습 리포트 그대로다. 단계는 마지막 판정 때의 것이다."));
    learning_ = new QLabel;
    learning_->setWordWrap(true);
    learning_->setTextFormat(Qt::RichText);
    learning_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    learning_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    learning_->setContentsMargins(12, 6, 12, 6);
    auto* about_body = new QWidget;
    auto* about_column = new QVBoxLayout(about_body);
    about_column->setContentsMargins(0, 0, 0, 0);
    about_column->setSpacing(0);
    about_column->addWidget(about_);
    about_column->addWidget(learning_header_.bar);
    about_column->addWidget(learning_);
    about_column->addStretch(1);
    learning_header_.bar->hide();
    learning_->hide();
    auto* about_scroll = new QScrollArea;
    about_scroll->setWidget(about_body);
    about_scroll->setWidgetResizable(true);
    about_scroll->setFrameShape(QFrame::NoFrame);

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

    // The direction's skill against the class rates (krx-quant #262), one line under the overall's:
    // each column's meaning on its header and cell. Hidden for another kind's predictor and an
    // engine without it.
    skill_header_ = krx_panel::header(tr("기준율 대비 실력"), krx_quant::skill_tip());
    QStringList skill_headers;
    for (const auto& c : krx_quant::skill_cells({}))
        skill_headers << c.header;
    skill_table_ = krx_tables::table(skill_headers);
    skill_table_->horizontalHeader()->setStretchLastSection(false);
    skill_ = new QWidget;
    auto* skill_column = new QVBoxLayout(skill_);
    skill_column->setContentsMargins(0, 0, 0, 0);
    skill_column->setSpacing(0);
    skill_column->addWidget(skill_header_.bar);
    skill_column->addWidget(skill_table_);
    skill_->hide();
    body->addWidget(skill_);

    // A range predictor's quantile stats (krx-quant #182), under the lifts: hidden for another kind's.
    quantiles_header_ = krx_panel::header(
        tr("분위수 통계"),
        tr("pinball loss는 분위수 예측의 손실(틱)이다. 실제 변동폭 r이 분위수 q 이상이면 τ·(r − q), 아니면 "
           "(1 − τ)·(q − r)이고 작을수록 좋다. 거래일 95% 구간은 하루를 값 하나로 본 구간이다. 기후 대비는 같은 "
           "종목·봉 마감의 '변동폭 시간대 기후'와의 pinball loss 차이로, 음수가 낫다. 설명용이고 상태 판정은 "
           "lift로 한다."));
    quantile_summary_ = new QLabel;
    quantile_summary_->setContentsMargins(12, 4, 12, 4);
    quantile_summary_->setWordWrap(true);
    quantile_table_ = krx_tables::table({tr("분위수"), tr("pinball(틱)"), tr("95% 구간"), tr("거래일 95% 구간"),
                                         tr("날"), tr("기후 대비"), tr("기후 대비 거래일 95% 구간")});
    quantile_table_->horizontalHeader()->setStretchLastSection(false);
    quantiles_ = new QWidget;
    auto* quantile_column = new QVBoxLayout(quantiles_);
    quantile_column->setContentsMargins(0, 0, 0, 0);
    quantile_column->setSpacing(0);
    quantile_column->addWidget(quantiles_header_.bar);
    quantile_column->addWidget(quantile_summary_);
    quantile_column->addWidget(quantile_table_, 1);
    quantiles_->hide();

    // A barrier predictor's barrier P&L (krx-quant #185), under the lifts as the range's quantiles.
    barrier_header_ = krx_panel::header(tr("장벽 손익(틱, 연구용)"),
                                        krx_quant::column_tip(QString::fromUtf8(krx_quant::kBarrierPnlHeader)));
    barrier_summary_ = new QLabel;
    barrier_summary_->setContentsMargins(12, 4, 12, 4);
    barrier_summary_->setWordWrap(true);
    barrier_table_ = krx_tables::table({tr("구분"), tr("거래 수"), tr("합계(틱)"), tr("거래당(틱)"), tr("95% 구간")});
    barrier_table_->horizontalHeader()->setStretchLastSection(false);
    barrier_ = new QWidget;
    auto* barrier_column = new QVBoxLayout(barrier_);
    barrier_column->setContentsMargins(0, 0, 0, 0);
    barrier_column->setSpacing(0);
    barrier_column->addWidget(barrier_header_.bar);
    barrier_column->addWidget(barrier_summary_);
    barrier_column->addWidget(barrier_table_, 1);
    barrier_->hide();

    auto* middle = new QSplitter(Qt::Horizontal);
    lift_header_ = krx_panel::header(tr("기준 예측 대비 lift"));
    lifts_ = krx_tables::table({tr("기준"), tr("lift"), tr("95% 구간"), tr("짝"), tr("p"), tr("q"), tr("운 보정"),
                                tr("거래일 95% 구간"), tr("날")});
    // The lifts, then a range predictor's quantile stats or a barrier predictor's P&L below, the
    // divider dragged as the user likes; hidden for another kind, the lifts have it all.
    auto* lift_column = new QSplitter(Qt::Vertical);
    lift_column->addWidget(krx_panel::panel(lift_header_, lifts_));
    lift_column->addWidget(quantiles_);
    lift_column->addWidget(barrier_);
    lift_column->setStretchFactor(0, 1);
    lift_column->setStretchFactor(1, 1);
    lift_column->setStretchFactor(2, 1);
    middle->addWidget(lift_column);
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
    by_volatility_ = breakdown_table(tr("변동성 (봉 마감)"));
    breakdowns_->addTab(by_slot_, tr("30분 시간대별"));
    breakdowns_->addTab(by_model_version_, tr("모델 버전별"));
    breakdowns_->addTab(by_symbol_, tr("종목별"));
    breakdowns_->addTab(by_volatility_, tr("변동성별")); // the direction's only (krx-quant #262)

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(middle);
    split->addWidget(krx_panel::panel(breakdown_header_, breakdowns_));
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    body->addWidget(split, 1);
    auto* outer = new QSplitter(Qt::Vertical);
    outer->addWidget(krx_panel::panel(about_header_, about_scroll));
    outer->addWidget(body_);
    outer->setStretchFactor(0, 2);
    outer->setStretchFactor(1, 3);
    outer->setSizes({300, 460}); // the section's text shows at first; drag the divider for more of either
    layout->addWidget(outer, 1);

    loading_ = new ui::LoadingOverlay(body_);
    body_->installEventFilter(this); // the overlay keeps the body's size
    restyle();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this]() { restyle(); });
}

void KrxStatsWindow::open(const QString& algorithm) {
    if (algorithm != algorithm_ || !isVisible()) {
        algorithm_ = algorithm;
        about_->clear(); // the stats' failure leaves the algorithm's description as read
        learning_->clear();
        learning_header_.bar->hide();
        learning_->hide();
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
    for (auto* table : {overall_, lifts_, by_slot_, by_model_version_, by_symbol_, by_volatility_, quantile_table_,
                        barrier_table_, skill_table_})
        table->setRowCount(0);
    quantile_summary_->clear();
    quantiles_->hide();
    barrier_summary_->clear();
    barrier_->hide();
    skill_->hide();
    lift_header_.title->setToolTip(QString());
    calibration_header_.title->setToolTip(QString());
    calibration_->set_series({});
}

void KrxStatsWindow::show_about(const std::optional<krx_quant::AlgorithmInfo>& info, const QString& failure) {
    QStringList lines;
    for (const auto& [label, text] : krx_quant::about_rows(info, failure))
        lines << QStringLiteral("<b>%1</b>&nbsp;&nbsp;%2")
                     .arg(label.toHtmlEscaped(), text.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
    about_->setText(lines.join(QStringLiteral("<br>")));

    const auto learning = failure.isEmpty() ? krx_quant::learning_rows(info) : QList<QPair<QString, QString>>{};
    QStringList learned;
    for (const auto& [label, text] : learning)
        learned << QStringLiteral("<b>%1</b>&nbsp;&nbsp;%2")
                       .arg(label.toHtmlEscaped(), text.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
    learning_->setText(learned.join(QStringLiteral("<br>")));
    learning_header_.bar->setVisible(!learning.isEmpty());
    learning_->setVisible(!learning.isEmpty());
}

void KrxStatsWindow::set_kind(const std::optional<krx_quant::KindInfo>& kind) {
    const QString name = kind ? kind->name : QStringLiteral("direction");
    if (name == kind_)
        return;
    kind_ = name;
    const auto columns = krx_tables::stats_columns(kind);
    const bool direction = name == QLatin1String("direction");
    krx_tables::set_columns(
        overall_,
        QList<krx_quant::StatsColumn>{{tr("순위"), {}}} + columns +
            QList<krx_quant::StatsColumn>{{tr("운 보정"), direction ? krx_quant::column_tip(QStringLiteral("운 보정"))
                                                                    : krx_quant::luck_tip(*kind)}});
    for (auto* table : {by_slot_, by_model_version_, by_symbol_, by_volatility_}) {
        const QString split = table->horizontalHeaderItem(0) ? table->horizontalHeaderItem(0)->text() : QString();
        krx_tables::set_columns(table, QList<krx_quant::StatsColumn>{{split, {}}} + columns);
    }
}

void KrxStatsWindow::show_stats(const krx_quant::AlgorithmStats& stats,
                                const std::optional<krx_quant::KindInfo>& kind) {
    loading_->hide_loading();
    failure_->hide();
    // The predictor's kind (krx-quant #182): its own, whatever the screen shows.
    std::optional<krx_quant::KindInfo> its =
        kind && kind->name == stats.kind ? kind : std::optional<krx_quant::KindInfo>{};
    if (!its && stats.kind != QLatin1String("direction")) { // its kind not described yet: by name alone
        its = krx_quant::KindInfo{};
        its->name = stats.kind;
        its->title = stats.kind;
    }
    set_kind(stats.kind == QLatin1String("direction") ? std::nullopt : its);
    query_->setText(tr("%1 · %2").arg(krx_quant::query_text(stats), krx_quant::days_text(stats.days)));

    const auto& e = stats.overall;
    overall_->setRowCount(1);
    overall_->setItem(0, kRank, cell(e.rank ? QString::number(*e.rank) : tr("순위 밖"), kNumber));
    krx_tables::set_stats(overall_, 0, kStats, e, its);
    overall_->setItem(0, overall_->columnCount() - 1, cell(e.significant ? tr("통과") : QString()));
    fit_rows(overall_);

    // The range's quantile stats (krx-quant #182).
    quantiles_->setVisible(e.range.has_value());
    quantile_table_->setRowCount(0);
    if (e.range) {
        quantile_summary_->setText(krx_quant::range_summary(*e.range));
        for (const auto& q : krx_quant::quantile_rows(*e.range)) {
            const int row = quantile_table_->rowCount();
            quantile_table_->insertRow(row);
            const QStringList cells{q.quantile, q.pinball,    q.interval,           q.day_interval,
                                    q.days,     q.vs_climate, q.vs_climate_interval};
            for (int c = 0; c < cells.size(); ++c)
                quantile_table_->setItem(row, c,
                                         cell(cells[c], c == kQuantile ? Qt::AlignLeft | Qt::AlignVCenter : kNumber));
        }
    }

    // The barrier's P&L (krx-quant #185): all its trades, then each side.
    barrier_->setVisible(e.barrier.has_value());
    barrier_table_->setRowCount(0);
    if (e.barrier) {
        barrier_summary_->setText(krx_quant::barrier_summary(*e.barrier));
        for (const auto& r : krx_quant::barrier_pnl_rows(*e.barrier)) {
            const int row = barrier_table_->rowCount();
            barrier_table_->insertRow(row);
            const QStringList cells{r.side, r.trades, r.total, r.per_trade, r.interval};
            for (int c = 0; c < cells.size(); ++c)
                barrier_table_->setItem(row, c,
                                        cell(cells[c], c == kSide ? Qt::AlignLeft | Qt::AlignVCenter : kNumber));
        }
        fit_rows(barrier_table_); // its three rows always show: the splitter leaves it that much
    }

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
    lift_header_.title->setToolTip(krx_quant::lift_tip(stats, its));
    excluded_->setText(e.excluded ? krx_quant::excluded_text(*e.excluded, stats.special) : QString());
    calibration_->set_series(krx_quant::calibration_series(stats, its));
    calibration_header_.title->setToolTip(krx_quant::calibration_tip(stats, its));

    fill_breakdown(by_slot_, stats.by_slot, krx_quant::slot_label, its);
    fill_breakdown(by_model_version_, stats.by_model_version, nullptr, its);
    fill_breakdown(by_symbol_, stats.by_symbol, nullptr, its);

    // The direction's skill and volatility terciles (krx-quant #262).
    const bool direction = stats.kind == QLatin1String("direction");
    skill_->setVisible(direction && stats.skill.has_value());
    skill_table_->setRowCount(0);
    if (stats.skill) {
        const auto cells = krx_quant::skill_cells(*stats.skill);
        skill_table_->setRowCount(1);
        for (int c = 0; c < cells.size(); ++c) {
            if (auto* header = skill_table_->horizontalHeaderItem(c))
                header->setToolTip(cells[c].tip);
            auto* value = cell(cells[c].value, kNumber);
            value->setToolTip(cells[c].tip);
            skill_table_->setItem(0, c, value);
        }
        fit_rows(skill_table_);
    }
    const int volatility = breakdowns_->indexOf(by_volatility_);
    breakdowns_->setTabVisible(volatility, direction);
    breakdowns_->setTabToolTip(volatility, direction ? krx_quant::volatility_tip(stats) : QString());
    fill_breakdown(by_volatility_, stats.by_volatility, krx_quant::volatility_label, its,
                   krx_quant::kVolatilityTerciles);
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
    for (const auto* header : {&header_, &about_header_, &learning_header_, &lift_header_, &calibration_header_,
                               &breakdown_header_, &quantiles_header_, &barrier_header_, &skill_header_})
        header->restyle();
    about_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));
    learning_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    for (auto* table : {overall_, lifts_, by_slot_, by_model_version_, by_symbol_, by_volatility_, quantile_table_,
                        barrier_table_, skill_table_})
        krx_tables::restyle(table);
    fit_rows(overall_);
    if (barrier_table_->rowCount() > 0)
        fit_rows(barrier_table_);
    if (skill_table_->rowCount() > 0)
        fit_rows(skill_table_);
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
