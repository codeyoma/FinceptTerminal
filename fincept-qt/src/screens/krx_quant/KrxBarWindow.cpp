#include "screens/krx_quant/KrxBarWindow.h"

#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/ChartSlots.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QTimeZone>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace {

using krx_tables::cell;
using krx_tables::kNumber;

constexpr double kLabelRoom = 150; ///< right of the minutes: the levels' names and prices, px
constexpr double kLeftRoom = 28;   ///< left of them: half the first minute's time, px
constexpr double kTimeRoom = 22;   ///< below them: the minutes' times, px
constexpr double kDot = 4.0;       ///< a reached level's dot, px (as the chart's hover draws it)
constexpr double kNear = 8.0;      ///< how close the pointer must be to a dot to name it, px

enum DirectionColumn {
    kName,
    kStatus,
    kUp,
    kFlat,
    kDown,
    kLeaned,
    kTicks,
    kPooled,
    kModel,
    kResult,
    kDirectionColumns
};

QString percent(const std::optional<double>& p) {
    return p ? QString::number(std::lround(*p * 100.0)) + "%" : QString();
}

/// Labelled lines as a two-column table: the labels dim, the values as text.
QString lines_html(const QList<krx_quant::BarLine>& lines) {
    QString rows;
    for (const auto& line : lines)
        rows += QStringLiteral("<tr><td style=\"color:%1;padding:1px 16px 1px 0;\">%2</td>"
                               "<td style=\"color:%3;padding:1px 0;\">%4</td></tr>")
                    .arg(ui::colors::TEXT_SECONDARY(), line.label.toHtmlEscaped(), ui::colors::TEXT_PRIMARY(),
                         line.value.toHtmlEscaped());
    return QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\">%1</table>").arg(rows);
}

/// A table sized to show its rows without a scroll bar (as the detail window's).
void fit_rows(QTableWidget* table) {
    int height = table->horizontalHeader()->height() + 2 * table->frameWidth();
    for (int row = 0; row < table->rowCount(); ++row)
        height += table->rowHeight(row);
    table->setFixedHeight(height);
}

/// A label for a panel's body: the window's text, a margin as the detail window's.
QLabel* body_label() {
    auto* label = new QLabel;
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    label->setContentsMargins(12, 6, 12, 8);
    return label;
}

/// A time of the chart (chart_time: KST read as UTC) as the clock reads it.
QString clock_of(qint64 time, const char* format) {
    return QDateTime::fromSecsSinceEpoch(time, QTimeZone::UTC).toString(QString::fromLatin1(format));
}

} // namespace

// ── The mini chart ──────────────────────────────────────────────────────────────────────

KrxBarChart::KrxBarChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(460, 300);
}

void KrxBarChart::set_chart(const krx_quant::MiniChart& chart) {
    chart_ = chart;
    update();
}

QRectF KrxBarChart::plot_rect() const {
    return QRectF(kLeftRoom, 10, std::max(40.0, width() - kLeftRoom - kLabelRoom),
                  std::max(40.0, height() - 10 - kTimeRoom));
}

void KrxBarChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(ui::colors::BG_BASE()));
    if (chart_.candles.isEmpty())
        return;
    const QRectF r = plot_rect();
    const auto x_of = [this, &r](qint64 time) { return krx_quant::mini_x(chart_, time, r.left(), r.width()); };
    const auto y_of = [this, &r](double price) { return krx_quant::mini_y(chart_, price, r.top(), r.height()); };
    QFont small = font();
    small.setPixelSize(std::max(10, font().pixelSize() - 2));
    p.setFont(small);

    // A minute a column, its time below; the four minutes from the bar close shaded as the chart's
    // hover shades them (krx-quant #167), their edges dashed while they are not over.
    const QColor amber(ui::colors::AMBER());
    for (qsizetype i = 0; i <= chart_.candles.size(); ++i) {
        const qint64 t = chart_.from + 60 * i;
        const double x = x_of(t);
        p.setPen(QColor(ui::colors::BORDER_DIM()));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(QRectF(x - 30, r.bottom() + 3, 60, kTimeRoom - 4), Qt::AlignHCenter | Qt::AlignTop,
                   clock_of(t, "HH:mm"));
    }
    const double x1 = x_of(chart_.window_from), x2 = x_of(chart_.window_to);
    QColor shade = amber;
    shade.setAlphaF(0.08f);
    p.fillRect(QRectF(x1, r.top(), x2 - x1, r.height()), shade);
    if (chart_.open) {
        QPen edge(amber, 1);
        edge.setDashPattern({4, 3});
        p.setPen(edge);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(x1, r.top(), x2 - x1, r.height()));
    }
    if (chart_.high <= chart_.low) {
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(r, Qt::AlignCenter, tr("그리기에 쓸 봉과 정답이 아직 없다"));
        return;
    }

    // The candles: the bar and the four after it; a minute without trades says so once it is over.
    const QColor up(ui::colors::POSITIVE()), down(ui::colors::NEGATIVE());
    const double slot = r.width() / static_cast<double>(chart_.candles.size());
    for (const auto& c : chart_.candles) {
        const double x = x_of(c.time) + slot / 2;
        if (c.empty) {
            if (c.time + 60 <= chart_.now) {
                p.setPen(QColor(ui::colors::TEXT_DIM()));
                p.drawText(QRectF(x - slot / 2, r.bottom() - 18, slot, 16), Qt::AlignCenter, tr("체결 없음"));
            }
            continue;
        }
        const QColor color = c.rising ? up : down;
        p.setPen(QPen(color, 1));
        p.drawLine(QPointF(x, y_of(c.high)), QPointF(x, y_of(c.low)));
        const double top = y_of(std::max(c.open, c.close)), bottom = y_of(std::min(c.open, c.close));
        const double body = std::max(3.0, slot * 0.42);
        p.fillRect(QRectF(x - body / 2, top, body, std::max(1.0, bottom - top)), color);
    }

    // The levels across the four minutes, dashed until their answer is in; their names and prices on
    // the right, moved apart where they meet, a thin leader to a label moved off its line.
    QList<double> ys;
    for (const auto& level : chart_.levels)
        ys << y_of(level.price);
    const double gap = QFontMetricsF(small).height();
    const QList<double> at = krx_quant::spread_labels(ys, gap, r.top() + gap / 2, r.bottom() - gap / 2);
    for (qsizetype i = 0; i < chart_.levels.size(); ++i) {
        const auto& level = chart_.levels[i];
        const QColor color = level.upper ? up : down;
        QPen pen(color, 1.5);
        if (level.dashed)
            pen.setDashPattern({4, 3});
        p.setPen(pen);
        p.drawLine(QPointF(x1, ys[i]), QPointF(x2, ys[i]));
        p.setPen(QPen(color, 1));
        const double label_x = r.right() + 8;
        if (std::abs(at[i] - ys[i]) > 1)
            p.drawLine(QPointF(x2, ys[i]), QPointF(label_x - 2, at[i]));
        p.drawText(QRectF(label_x, at[i] - gap / 2, kLabelRoom - 10, gap), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1 %2").arg(level.label, krx_quant::price_text(level.price)));
    }
    // Where each was first reached, to the second; ringed in the background's colour, to show on a candle.
    p.setPen(QPen(QColor(ui::colors::BG_BASE()), 1.5));
    for (const auto& point : chart_.points) {
        p.setBrush(point.upper ? up : down);
        p.drawEllipse(QPointF(x_of(point.time), y_of(point.price)), kDot, kDot);
    }
}

void KrxBarChart::mouseMoveEvent(QMouseEvent* event) {
    const QPointF at = event->position();
    const QRectF r = plot_rect();
    if (chart_.candles.isEmpty() || chart_.high <= chart_.low || !r.contains(at)) {
        QToolTip::hideText();
        return;
    }
    // A dot first: what was reached, when and at what quote.
    QStringList near;
    for (const auto& point : chart_.points) {
        const QPointF there(krx_quant::mini_x(chart_, point.time, r.left(), r.width()),
                            krx_quant::mini_y(chart_, point.price, r.top(), r.height()));
        if (std::hypot(there.x() - at.x(), there.y() - at.y()) <= kNear)
            near << point.label;
    }
    if (!near.isEmpty()) {
        QToolTip::showText(event->globalPosition().toPoint(), near.join('\n'), this);
        return;
    }
    // Else the minute under the pointer: its candle's prices, named by its close as the chart names it.
    const qsizetype i = std::clamp<qsizetype>(
        static_cast<qsizetype>((at.x() - r.left()) / r.width() * chart_.candles.size()), 0, chart_.candles.size() - 1);
    const auto& c = chart_.candles[i];
    QString tip;
    if (!c.empty)
        tip = tr("%1 봉 · 시가 %2 · 고가 %3 · 저가 %4 · 종가 %5 · 거래량 %6")
                  .arg(clock_of(c.time + 60, "HH:mm"), krx_quant::price_text(c.open), krx_quant::price_text(c.high),
                       krx_quant::price_text(c.low), krx_quant::price_text(c.close),
                       QLocale(QLocale::Korean).toString(c.volume, 'f', 0));
    else if (c.time + 60 <= chart_.now)
        tip = krx_quant::empty_line(c.time, 1);
    if (tip.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(event->globalPosition().toPoint(), tip, this);
}

void KrxBarChart::leaveEvent(QEvent* event) {
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

// ── The window ──────────────────────────────────────────────────────────────────────────

KrxBarWindow::KrxBarWindow(QWidget* parent) : QWidget(parent, Qt::Window) {
    setObjectName("krxBarWindow");
    setAttribute(Qt::WA_StyledBackground);
    resize(1040, 860);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = krx_panel::header(QString(), tr("이 봉 마감(봉 시작 + 1분)의 예측과 정답이다. 오늘의 봉이라 채점 전이면 "
                                              "늦게 온 모델 결과와 채점 결과가 오는 대로 다시 읽고, 채점이 끝나면 "
                                              "더 읽지 않는다."));
    failure_ = new QLabel(tr("읽지 못함"));
    failure_->hide();
    header_.controls->addWidget(failure_);
    state_ = new QLabel;
    header_.controls->addWidget(state_);
    layout->addWidget(header_.bar);

    auto* content = new QWidget;
    content->setObjectName("krxBarContent");
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // The bar and its answer on the left, the mini chart of its four minutes on the right.
    info_header_ = krx_panel::header(tr("봉 정보"), tr("체결 수는 엔진이 오늘 만든 봉에만 있다. 지난 날의 봉은 "
                                                       "1분봉 파일에서 읽어 체결 수를 모른다(—)."));
    info_ = body_label();
    answer_header_ = krx_panel::header(
        tr("정답 상세"),
        tr("도달 정답: 4분 안에 매수 1호가가 봉 마감 중간가 + k틱(위 목표)에 닿으면 상승, 아니고 매도 1호가가 − k틱"
           "(아래 목표)에 닿으면 하락, 아니면 보합이다. 채점 전의 중간가·호가단위·k와 목표, 장벽은 예측과 함께 온 "
           "봉 마감의 값이다."));
    answer_ = body_label();
    auto* left = new QWidget;
    auto* left_column = new QVBoxLayout(left);
    left_column->setContentsMargins(0, 0, 0, 0);
    left_column->setSpacing(0);
    left_column->addWidget(krx_panel::panel(info_header_, info_), 0);
    left_column->addWidget(krx_panel::panel(answer_header_, answer_), 1);
    chart_header_ = krx_panel::header(
        tr("4분 미니 차트"),
        tr("그 봉과 봉 마감부터 채점 시각까지의 1분봉 4개다. 칠한 곳이 4분이다. 위 목표·아래 목표(도달 정답), "
           "최고·최저 중간가(변동폭), U·L(장벽)을 가로선으로 긋고, 처음 닿은 곳에 점을 찍는다. 차트의 4분 구간 "
           "호버와 같은 뜻과 색이다(위는 초록, 아래는 빨강). 채점 전에는 선이 점선이다."));
    chart_ = new KrxBarChart;
    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(0);
    top->addWidget(left, 4);
    top->addWidget(krx_panel::panel(chart_header_, chart_), 5);
    column->addLayout(top);

    // Every direction prediction of the bar close, as the pinned panel lists them (krx-quant #107,
    // #159), with each one's expected ticks and model.
    direction_header_ = krx_panel::header(tr("방향 예측"));
    direction_note_ = new QLabel;
    direction_header_.controls->addWidget(direction_note_);
    direction_ = krx_tables::table({tr("예측"), tr("상태"), tr("상승"), tr("보합"), tr("하락"), tr("방향"),
                                    tr("기대 틱"), tr("합의"), tr("모델"), tr("결과")});
    direction_->horizontalHeader()->setStretchLastSection(false);
    if (auto* item = direction_->horizontalHeaderItem(kPooled))
        item->setToolTip(tr("최종 합의가 이 모델의 확률을 넣었는지(포함·제외)"));
    if (auto* item = direction_->horizontalHeaderItem(kStatus))
        item->setToolTip(tr("알고리즘의 지금 수명 상태"));
    column->addWidget(krx_panel::panel(direction_header_, direction_));

    // The range's and the barrier's, whatever kind the screen shows (krx-quant #182, #185).
    for (const auto& kind : krx_quant::bar_kinds()) {
        KindPanel panel;
        panel.header =
            krx_panel::header(kind == QLatin1String(krx_quant::kRangeKind) ? tr("변동폭 예측") : tr("장벽 예측"));
        panel.note = new QLabel;
        panel.header.controls->addWidget(panel.note);
        panel.table = krx_tables::table({});
        panel.table->horizontalHeader()->setStretchLastSection(false);
        column->addWidget(krx_panel::panel(panel.header, panel.table));
        kind_panels_ << panel;
    }
    column->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    body_ = new QWidget;
    auto* body = new QVBoxLayout(body_);
    body->setContentsMargins(0, 0, 0, 0);
    body->addWidget(scroll);
    layout->addWidget(body_, 1);
    loading_ = new ui::LoadingOverlay(body_);
    body_->installEventFilter(this); // the overlay keeps the body's size
    restyle();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this]() {
        restyle();
        render(); // the lines' colours are the theme's when they are written
    });
}

void KrxBarWindow::open(const QString& symbol, const QDateTime& bar_close, const QString& title) {
    symbol_ = symbol;
    bar_close_ = bar_close;
    header_.title->setText(title);
    // As Fincept names its windows: the app, then what is shown.
    setWindowTitle(QStringLiteral("Fincept Terminal — %1 — %2").arg(tr("KRX 4분 예측"), title));
    if (!read_)
        loading_->show_loading(tr("읽는 중…"));
    show();
    raise();
    activateWindow();
}

void KrxBarWindow::show_read(const krx_quant::BarRead& read, const std::optional<QList<krx_quant::KindInfo>>& kinds,
                             const QStringList& selection, const QDateTime& now) {
    read_ = read;
    kinds_ = kinds;
    selection_ = selection;
    now_ = now;
    loading_->hide_loading();
    failure_->hide();
    render();
}

void KrxBarWindow::show_failure(const QString& reason) {
    loading_->hide_loading();
    failure_->setToolTip(tr("이 봉을 읽지 못했습니다: %1").arg(reason));
    failure_->show();
}

void KrxBarWindow::set_selection(const QStringList& selection) {
    if (selection == selection_)
        return;
    selection_ = selection;
    render();
}

void KrxBarWindow::render() {
    if (!read_)
        return;
    const krx_quant::BarRead& read = *read_;
    state_->setText(krx_quant::bar_state_text(read, now_));
    info_->setText(lines_html(krx_quant::bar_info_lines(read, now_)));
    answer_->setText(lines_html(krx_quant::answer_lines(read, kinds_)));
    chart_->set_chart(krx_quant::mini_chart(read, now_));

    // The direction: the selected consensus, the consensuses, the models, the baselines (dimmed).
    direction_note_->setText(krx_quant::truth_text(krx_quant::truth_of(read.predictions, read.bar_close)));
    direction_->setRowCount(0);
    const auto rows = krx_quant::close_rows(read.predictions, read.statuses, selection_);
    for (const auto& r : rows) {
        const int row = direction_->rowCount();
        direction_->insertRow(row);
        const QStringList cells{r.name,
                                r.status,
                                percent(r.p_up),
                                percent(r.p_flat),
                                percent(r.p_down),
                                r.direction,
                                r.expected_ticks ? QString::number(*r.expected_ticks, 'f', 1) : QString(),
                                r.in_consensus,
                                r.model_version,
                                r.result};
        for (int c = 0; c < kDirectionColumns; ++c) {
            const bool number = (c >= kUp && c <= kDown) || c == kTicks;
            auto* item = cell(cells[c], number ? kNumber : Qt::AlignLeft | Qt::AlignVCenter);
            if (r.baseline)
                item->setForeground(QColor(ui::colors::TEXT_DIM()));
            if (r.consensus) {
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
            direction_->setItem(row, c, item);
        }
    }
    direction_->setVisible(!rows.isEmpty());
    if (rows.isEmpty())
        direction_note_->setText(tr("그 봉 마감의 예측 없음"));
    fit_rows(direction_);

    // The range and the barrier, as their panels with the results (krx-quant #182, #185).
    const auto sections = krx_quant::kind_sections(read, kinds_);
    for (qsizetype i = 0; i < sections.size() && i < kind_panels_.size(); ++i) {
        const auto& section = sections[i];
        KindPanel& panel = kind_panels_[i];
        panel.header.title->setText(section.title);
        panel.note->setText(section.note);
        QTableWidget* table = panel.table;
        table->setRowCount(0);
        krx_tables::set_columns(table, [&section]() {
            QList<krx_quant::StatsColumn> columns;
            for (const auto& c : section.columns)
                columns.push_back({c.header, c.tip});
            return columns;
        }());
        const bool range = section.kind == QLatin1String(krx_quant::kRangeKind);
        const auto quantile = [](const std::optional<double>& q) { return q ? krx_quant::ticks_text(*q) : QString(); };
        for (const auto& r : section.rows) {
            const int row = table->rowCount();
            table->insertRow(row);
            QList<QPair<QString, Qt::Alignment>> cells{{r.name, Qt::AlignLeft | Qt::AlignVCenter},
                                                       {r.top, Qt::AlignLeft | Qt::AlignVCenter}};
            for (const auto& p : r.probabilities)
                cells.append({percent(p), kNumber});
            const qsizetype classes = section.info ? section.info->classes.size() : 0;
            for (qsizetype k = r.probabilities.size(); k < classes; ++k)
                cells.append({QString(), kNumber}); // no forecast
            if (range)
                cells << QPair<QString, Qt::Alignment>{quantile(r.q10), kNumber}
                      << QPair<QString, Qt::Alignment>{quantile(r.q50), kNumber}
                      << QPair<QString, Qt::Alignment>{quantile(r.q90), kNumber};
            cells.append({r.result, Qt::AlignLeft | Qt::AlignVCenter});
            cells.append({r.model_version, Qt::AlignLeft | Qt::AlignVCenter});
            for (int c = 0; c < cells.size() && c < table->columnCount(); ++c) {
                auto* item = cell(cells[c].first, cells[c].second);
                if (r.baseline)
                    item->setForeground(QColor(ui::colors::TEXT_DIM()));
                table->setItem(row, c, item);
            }
        }
        table->setVisible(!section.rows.isEmpty());
        fit_rows(table);
    }
}

void KrxBarWindow::restyle() {
    setStyleSheet(QString("#krxBarWindow{background:%1;}#krxBarWindow QLabel{background:transparent;}"
                          "#krxBarContent{background:%1;}")
                      .arg(ui::colors::BG_BASE()));
    for (const auto* header : {&header_, &info_header_, &answer_header_, &chart_header_, &direction_header_})
        header->restyle();
    for (const auto& panel : std::as_const(kind_panels_)) {
        panel.header.restyle();
        panel.note->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));
        krx_tables::restyle(panel.table);
        fit_rows(panel.table);
    }
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    state_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));
    direction_note_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));
    krx_tables::restyle(direction_);
    fit_rows(direction_);
    chart_->update();
}

bool KrxBarWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == body_ && event->type() == QEvent::Resize)
        loading_->setGeometry(body_->rect());
    return QWidget::eventFilter(watched, event);
}

void KrxBarWindow::closeEvent(QCloseEvent* event) {
    emit closed();
    QWidget::closeEvent(event);
}

} // namespace fincept::screens
