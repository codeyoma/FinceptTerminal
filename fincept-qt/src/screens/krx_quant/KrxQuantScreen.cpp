#include "screens/krx_quant/KrxQuantScreen.h"

#include "screens/krx_quant/KrxAlerts.h"
#include "screens/krx_quant/KrxChart.h"
#include "screens/krx_quant/KrxLeaderboard.h"
#include "screens/krx_quant/KrxStatsWindow.h"
#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/ChartModel.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace {

using krx_tables::cell;
using krx_tables::kNumber;

enum SymbolColumn { kSymbol, kMid, kFinal, kScoring };
enum PredictionColumn { kPredictor, kDirection, kUp, kFlat, kDown, kTicks, kModel, kPredictionColumns };
// The table while a bar close is pinned (krx-quant #107).
enum PinnedColumn {
    kPinnedName,
    kPinnedStatus,
    kPinnedDirection,
    kPinnedUp,
    kPinnedFlat,
    kPinnedDown,
    kPinnedIn,
    kPinnedResult,
    kPinnedColumns
};

QString krx_percent(const std::optional<double>& p) {
    return p ? QString::number(std::lround(*p * 100.0)) + "%" : QString();
}

/// "3:12" to go, from seconds.
QString krx_countdown(qint64 seconds) {
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QColor direction_color(krx_quant::Direction d) {
    if (d == krx_quant::Direction::Up)
        return QColor(ui::colors::POSITIVE());
    if (d == krx_quant::Direction::Down)
        return QColor(ui::colors::NEGATIVE());
    return QColor(ui::colors::TEXT_PRIMARY());
}

/// A prediction's direction cell: its direction (leaning neither way shows none), or why
/// it was not made.
QTableWidgetItem* direction_cell(const krx_quant::Prediction& p) {
    const auto dir = krx_quant::shown_direction(p);
    auto* item = cell(p.status == "ok" ? krx_quant::direction_label(dir) : krx_quant::status_label(p.status));
    item->setForeground(direction_color(dir));
    return item;
}

const krx_quant::Prediction* final_in(const krx_quant::Latest& latest) {
    for (const auto& p : latest.predictions)
        if (p.algorithm == "consensus-final")
            return &p;
    return nullptr;
}

} // namespace

KrxQuantScreen::KrxQuantScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("krxQuantScreen");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    header_ = krx_panel::header(tr("KRX 4분 예측"));
    dot_ = new QLabel;
    dot_->setFixedSize(8, 8);
    header_.row->insertWidget(2, dot_); // right after the title
    header_.controls->addWidget(new QLabel(tr("엔진 주소")));
    url_edit_ = new QLineEdit(services::KrxQuantService::base_url());
    url_edit_->setPlaceholderText(tr("예: http://엔진-주소:8080"));
    url_edit_->setMinimumWidth(260);
    header_.controls->addWidget(url_edit_);
    auto* apply = new QPushButton(tr("적용"));
    header_.controls->addWidget(apply);
    layout->addWidget(header_.bar);

    auto* rows = new QWidget;
    auto* rows_layout = new QVBoxLayout(rows);
    rows_layout->setContentsMargins(12, 4, 12, 4);
    rows_layout->setSpacing(2);
    status_label_ = new QLabel;
    status_label_->setWordWrap(true);
    rows_layout->addWidget(status_label_);
    macro_label_ = new QLabel;
    macro_label_->setWordWrap(true);
    macro_label_->hide();
    rows_layout->addWidget(macro_label_);
    special_label_ = new QLabel;
    special_label_->setObjectName("krxSpecial");
    special_label_->setWordWrap(true);
    special_label_->hide();
    rows_layout->addWidget(special_label_);
    layout->addWidget(rows);

    // Top: the symbols, the chosen one's chart, and its predictions.
    auto* upper = new QSplitter(Qt::Horizontal);
    symbols_header_ = krx_panel::header(tr("종목"));
    symbols_table_ = krx_tables::table({tr("종목"), tr("기준가"), tr("최종 합의"), tr("채점까지")});
    upper->addWidget(krx_panel::panel(symbols_header_, symbols_table_));

    // Lightweight Charts' license asks for its attribution and a link where users see it.
    chart_header_ = krx_panel::header(
        tr("차트"), tr("차트: TradingView Lightweight Charts™ · Copyright (c) 2026 TradingView, Inc. · "
                       "https://www.tradingview.com/"));
    chart_failure_ = new QLabel(tr("읽지 못함"));
    chart_failure_->hide();
    chart_header_.controls->addWidget(chart_failure_);
    auto* fit = new QPushButton(tr("맞춤"));
    fit->setToolTip(tr("최근 60봉 보기로 돌아간다. 차트를 두 번 눌러도 된다."));
    chart_header_.controls->addWidget(fit);
    chart_ = new KrxChart;
    upper->addWidget(krx_panel::panel(chart_header_, chart_));

    predictions_header_ = krx_panel::header(tr("예측"));
    latest_ = new QPushButton(tr("최신으로"));
    latest_->setToolTip(tr("차트에서 고른 봉 마감 대신 최신 봉 마감의 예측을 따라간다."));
    latest_->hide();
    predictions_header_.controls->addWidget(latest_);
    predictions_table_ =
        krx_tables::table({tr("예측"), tr("방향"), tr("상승"), tr("보합"), tr("하락"), tr("기대 틱"), tr("모델")});
    upper->addWidget(krx_panel::panel(predictions_header_, predictions_table_));
    upper->setStretchFactor(0, 3);
    upper->setStretchFactor(1, 5);
    upper->setStretchFactor(2, 4);

    // Bottom: the leaderboard and the signal alerts, each with its own header.
    auto* lower = new QSplitter(Qt::Horizontal);
    leaderboard_ = new KrxLeaderboard;
    lower->addWidget(leaderboard_);
    alerts_ = new KrxAlerts;
    lower->addWidget(alerts_);
    lower->setStretchFactor(0, 4);
    lower->setStretchFactor(1, 1);

    auto* body = new QSplitter(Qt::Vertical);
    body->addWidget(upper);
    body->addWidget(lower);
    body->setStretchFactor(0, 3);
    body->setStretchFactor(1, 2);
    body_ = new QWidget; // the overlay's parent: a splitter would take it for a pane
    auto* body_layout = new QVBoxLayout(body_);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->addWidget(body);
    layout->addWidget(body_, 1);
    loading_ = new ui::LoadingOverlay(body_);
    body_->installEventFilter(this); // the overlay keeps the body's size
    loading_->show_loading(tr("엔진을 읽는 중…"));

    live_ = new services::KrxLive(this);
    connect(live_, &services::KrxLive::status_changed, this, [this]() {
        if (live_->status() || !live_->failure().isEmpty())
            loading_->hide_loading(); // answered, or failed: the dot says which
        show_status();
        show_dot();
        show_macro(); // the USD futures move with each status
    });
    connect(live_, &services::KrxLive::predictions_changed, this, [this]() {
        QStringList symbols;
        for (const auto& q : live_->board().symbols())
            symbols << q.symbol;
        leaderboard_->set_symbols(symbols);
        fill_symbols();
        fill_predictions();
        fill_chart();
    });
    connect(live_, &services::KrxLive::chart_changed, this, [this]() {
        chart_failure_->hide();
        fill_chart();
    });
    connect(live_, &services::KrxLive::chart_failed, this, [this](const QString& reason) {
        chart_->set_data({}, {});
        chart_failure_->setToolTip(tr("1분봉을 읽지 못했습니다: %1").arg(reason));
        chart_failure_->show();
    });
    connect(live_, &services::KrxLive::macro_changed, this, [this]() { show_macro(); });
    connect(live_, &services::KrxLive::macro_failed, this, [this]() { show_macro(); });
    connect(live_, &services::KrxLive::leaderboard_changed, leaderboard_, &KrxLeaderboard::show_leaderboard);
    connect(live_, &services::KrxLive::leaderboard_failed, leaderboard_, &KrxLeaderboard::show_failure);
    connect(live_, &services::KrxLive::stats_changed, this, [this](const krx_quant::AlgorithmStats& stats) {
        if (auto* window = stats_windows_.value(stats.algorithm))
            window->show_stats(stats);
    });
    connect(live_, &services::KrxLive::stats_failed, this, [this](const QString& algorithm, const QString& reason) {
        if (auto* window = stats_windows_.value(algorithm))
            window->show_failure(reason);
    });
    connect(live_, &services::KrxLive::signal_raised, alerts_, &KrxAlerts::raise);
    connect(live_, &services::KrxLive::base_url_changed, this, [this](const QString& url) {
        if (!url_edit_->hasFocus()) // not while the address is being typed here
            url_edit_->setText(url);
    });

    connect(apply, &QPushButton::clicked, this, &KrxQuantScreen::apply_base_url);
    connect(url_edit_, &QLineEdit::returnPressed, this, &KrxQuantScreen::apply_base_url);
    connect(symbols_table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto rows = symbols_table_->selectionModel()->selectedRows();
        if (!rows.isEmpty())
            live_->select(symbols_table_->item(rows.first().row(), kSymbol)->text());
    });
    connect(fit, &QPushButton::clicked, chart_, &KrxChart::fit);
    connect(chart_, &KrxChart::icon_clicked, this, [this](const QDateTime& bar_close) { live_->pin(bar_close); });
    connect(chart_, &KrxChart::view_moved, live_, &services::KrxLive::view_from);
    connect(latest_, &QPushButton::clicked, this, [this]() { live_->pin({}); });
    connect(live_, &services::KrxLive::pinned_changed, this, [this]() {
        predictions_header_.title->setToolTip({}); // a failed read's reason, now stale
        fill_predictions();
        update_countdowns();
    });
    connect(live_, &services::KrxLive::pinned_failed, this, [this](const QString& reason) {
        predictions_table_->setRowCount(0);
        predictions_header_.title->setToolTip(tr("그 봉 마감의 예측을 읽지 못했습니다: %1").arg(reason));
    });
    connect(leaderboard_, &KrxLeaderboard::query_changed, this, [this]() {
        live_->set_leaderboard_query(leaderboard_->query());
        fill_chart(); // the chart's ✓/✗ are on the leaderboard's basis
    });
    connect(leaderboard_, &KrxLeaderboard::detail_requested, this, &KrxQuantScreen::open_detail);
    connect(alerts_, &KrxAlerts::changed, this, [this]() {
        live_->set_alerts(alerts_->is_on(), alerts_->rule());
        fill_chart(); // the signals the chart marks follow the threshold and N
    });

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, [this]() {
        update_countdowns();
        show_dot();
    });

    refresh_theme();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this]() { refresh_theme(); });

    live_->set_alerts(alerts_->is_on(), alerts_->rule());
    live_->set_leaderboard_query(leaderboard_->query());
    live_->start(); // from now on, even before the screen is first shown: the signal alerts
}

void KrxQuantScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    live_->set_shown(true);
    show_status();
    fill_symbols();
    fill_predictions();
    fill_chart();
    clock_timer_->start();
}

void KrxQuantScreen::hideEvent(QHideEvent* event) {
    // The live state keeps following the engine: signal alerts come while another screen is
    // shown. Only what the views need is not read meanwhile.
    live_->set_shown(false);
    clock_timer_->stop();
    QWidget::hideEvent(event);
}

bool KrxQuantScreen::eventFilter(QObject* watched, QEvent* event) {
    if (watched == body_ && event->type() == QEvent::Resize)
        loading_->setGeometry(body_->rect());
    return QWidget::eventFilter(watched, event);
}

void KrxQuantScreen::refresh_theme() {
    setStyleSheet(QString("#krxQuantScreen{background:%1;}"
                          "#krxQuantScreen QLabel{background:transparent;}"
                          "#krxSpecial{color:%2;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::AMBER()));
    for (const auto* header : {&header_, &symbols_header_, &chart_header_, &predictions_header_})
        header->restyle();
    krx_tables::restyle(symbols_table_);
    krx_tables::restyle(predictions_table_);
    leaderboard_->restyle();
    alerts_->restyle();
    chart_->restyle();
    chart_failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    fill_chart();                              // the volumes' and icons' colors too
    dot_->setProperty("krxColor", QVariant()); // the dot in the new colors
    show_dot();
    fill_symbols(); // the cells' colors are the theme's when they are filled
    fill_predictions();
}

void KrxQuantScreen::apply_base_url() {
    loading_->show_loading(tr("엔진을 읽는 중…"));
    live_->apply_base_url(url_edit_->text());
    url_edit_->setText(services::KrxQuantService::base_url());
}

bool KrxQuantScreen::is_today(const QDateTime& bar_close) const {
    return krx_quant::kst_text(bar_close, "yyyy-MM-dd") == krx_quant::kst_text(live_->engine_now(), "yyyy-MM-dd");
}

void KrxQuantScreen::show_status() {
    QStringList parts;
    if (!live_->failure().isEmpty())
        parts << live_->failure();
    status_label_->setToolTip(QString());
    if (const auto& status = live_->status()) {
        const auto& s = *status;
        if (s.mode == "demo")
            parts << tr("데모 데이터");
        parts << krx_quant::engine_build_parts(s);
        parts << krx_quant::phase_label(s.phase);
        parts << tr("마지막 봉 마감 %1")
                     .arg(krx_quant::kst_text(s.last_bar_close,
                                              is_today(s.last_bar_close) ? "HH:mm:ss" : "MM-dd HH:mm"));
        parts << tr("채점 대기 %1건").arg(s.pending_outcomes);
        if (s.kis_connected) {
            // The freshest feed shows whether data flows at all; the engine judges each feed's
            // trouble itself (a gap), quiet feeds and the hours outside trading included.
            std::optional<double> freshest;
            QStringList gaps;
            for (const auto& f : s.feeds) {
                if (!f.gap.isEmpty())
                    gaps << QString("%1 %2 (%3)").arg(f.feed, f.key, f.gap);
                if (f.seconds_since && (!freshest || *f.seconds_since < *freshest))
                    freshest = f.seconds_since;
            }
            QStringList detail;
            if (freshest)
                detail << tr("마지막 수신 %1초 전").arg(QLocale(QLocale::Korean).toString(*freshest, 'f', 0));
            if (!gaps.isEmpty())
                detail << tr("결측 피드 %1개").arg(gaps.size());
            QString kis = *s.kis_connected ? tr("KIS 연결됨") : tr("KIS 끊김");
            if (!detail.isEmpty())
                kis += QStringLiteral(" (%1)").arg(detail.join(", "));
            parts << kis;
            if (!gaps.isEmpty())
                status_label_->setToolTip(tr("결측 피드: %1").arg(gaps.join(", ")));
        }
        if (s.training) {
            const QStringList promoted = s.training->promoted.keys();
            const QString what = promoted.isEmpty() ? tr("승격 없음") : tr("%1 승격").arg(promoted.join(", "));
            const QString running = s.training->applied
                                        ? tr("적용됨")
                                        : tr("미적용(%1)").arg(krx_quant::training_reason_label(s.training->reason));
            parts << tr("지난밤 학습 %1: %2, %3").arg(s.training->day, what, running);
        }

        QStringList windows;
        for (const auto& w : s.special) {
            const QString who = w.symbol.isEmpty() ? tr("시장 전체") : w.symbol;
            windows << QString("%1 %2 %3–%4")
                           .arg(who, krx_quant::special_label(w.kind), krx_quant::kst_text(w.start, "HH:mm:ss"),
                                krx_quant::kst_text(w.end, "HH:mm:ss"));
        }
        special_label_->setText(tr("오늘 특수 상황: %1").arg(windows.join(", ")));
        special_label_->setVisible(!windows.isEmpty());
    } else {
        special_label_->hide();
    }
    if (!live_->stream_state().isEmpty())
        parts << live_->stream_state();
    status_label_->setText(parts.join("  ·  "));
}

void KrxQuantScreen::show_macro() {
    const auto& status = live_->status();
    const std::optional<krx_quant::UsdFutures> live_usd = status ? status->usd_futures : std::nullopt;
    if (!live_->macro()) {
        // Not read yet, or the read failed: the USD futures still show once they trade.
        QStringList parts;
        if (!live_->macro_failure().isEmpty())
            parts << tr("매크로: %1").arg(live_->macro_failure());
        if (const QString usd = krx_quant::usd_futures_text(live_usd, {}); !usd.isEmpty())
            parts << usd;
        macro_label_->setVisible(!parts.isEmpty());
        macro_label_->setText(parts.join("  ·  "));
        macro_label_->setToolTip(QString());
        return;
    }
    const krx_quant::Macro& macro = *live_->macro();
    macro_label_->show();
    QStringList parts;
    if (macro.day.isEmpty()) {
        parts << tr("매크로: 아직 정한 국면이 없습니다");
    } else {
        QString regime = tr("매크로 %1 %2").arg(macro.day, krx_quant::regime_label(macro.regime));
        if (macro.day != krx_quant::kst_text(live_->engine_now(), "yyyy-MM-dd"))
            regime += tr(" (오늘 것이 아님)");
        parts << regime;
        if (!macro.reasons.isEmpty())
            parts << macro.reasons.join(", ");
    }
    if (const QString usd = krx_quant::usd_futures_text(live_usd, macro.readings); !usd.isEmpty())
        parts << usd; // the USD futures, on the row itself
    QStringList all;
    for (const auto& r : macro.readings)
        all << QStringLiteral("%1 · %2").arg(krx_quant::reading_text(r), r.as_of.isEmpty() ? tr("날짜 모름") : r.as_of);
    for (auto it = macro.failed.begin(); it != macro.failed.end(); ++it)
        all << tr("%1 읽기 실패: %2").arg(it.key(), it.value());
    if (!macro.skipped.isEmpty())
        all << tr("키 없어 건너뜀: %1").arg(macro.skipped.join(", "));
    macro_label_->setText(parts.join("  ·  "));
    macro_label_->setToolTip(
        macro.day.isEmpty()
            ? QString()
            : tr("국면을 정한 %1의 지표\n%2").arg(krx_quant::kst_text(macro.decided_at, "HH:mm"), all.join("\n")));
}

void KrxQuantScreen::show_dot() {
    const auto dot = krx_quant::engine_dot(live_->status().has_value(), live_->failure(), live_->read_errors(),
                                           QDateTime::currentDateTime(), live_->engine_now());
    using State = krx_quant::EngineDot::State;
    const QString color = dot.state == State::Ok       ? ui::colors::POSITIVE()
                          : dot.state == State::Failed ? ui::colors::NEGATIVE()
                                                       : ui::colors::TEXT_DIM();
    if (dot_->property("krxColor").toString() != color) { // not every second
        dot_->setStyleSheet(QString("background:%1;border-radius:4px;").arg(color));
        dot_->setProperty("krxColor", color);
    }
    dot_->setToolTip(dot.tip);
}

void KrxQuantScreen::fill_symbols() {
    const QSignalBlocker quiet(symbols_table_); // a refill is not a choice
    symbols_table_->setRowCount(0);
    for (const auto& quote : live_->board().symbols()) {
        const int row = symbols_table_->rowCount();
        symbols_table_->insertRow(row);
        symbols_table_->setItem(row, kSymbol, cell(quote.symbol));
        symbols_table_->setItem(row, kMid,
                                cell(quote.mid ? krx_quant::price_text(*quote.mid) : QStringLiteral("—"), kNumber));
        const krx_quant::Latest latest = live_->board().latest(quote.symbol);
        const krx_quant::Prediction* final = final_in(latest);
        if (!final) {
            symbols_table_->setItem(row, kFinal, cell(latest.predictions.isEmpty() ? tr("예측 없음") : QString()));
        } else {
            auto* item = direction_cell(*final);
            if (final->status == "ok") {
                // Leaning neither way: up and down show as the same percent.
                const QString which = item->text().isEmpty() ? tr("상승=하락") : item->text();
                item->setText(QStringLiteral("%1 %2").arg(
                    which, krx_percent(std::max({final->p_up, final->p_flat, final->p_down}))));
            }
            const bool today = is_today(latest.bar_close);
            item->setToolTip(tr("%1 봉 마감%2")
                                 .arg(krx_quant::kst_text(latest.bar_close, today ? "HH:mm" : "MM-dd HH:mm"),
                                      today ? QString() : tr(" (지난 거래일)")));
            if (!today)
                item->setForeground(QColor(ui::colors::TEXT_DIM()));
            symbols_table_->setItem(row, kFinal, item);
        }
        symbols_table_->setItem(row, kScoring, cell(QString(), kNumber));
        if (quote.symbol == live_->selected())
            symbols_table_->selectRow(row);
    }
    update_countdowns();
}

void KrxQuantScreen::fill_pinned() {
    // A bar close's judgements (krx-quant #107): the consensuses, the models with their status,
    // whether the final consensus pooled them and how they did, then the baselines, dimmed.
    predictions_table_->setColumnCount(kPinnedColumns);
    predictions_table_->setHorizontalHeaderLabels(
        {tr("예측"), tr("상태"), tr("방향"), tr("상승"), tr("보합"), tr("하락"), tr("합의"), tr("결과")});
    predictions_table_->setRowCount(0);
    for (const auto& r :
         krx_quant::close_rows(live_->pinned_predictions(), live_->statuses(), leaderboard_->query().basis)) {
        const int row = predictions_table_->rowCount();
        predictions_table_->insertRow(row);
        const QStringList cells{
            r.name,         r.status, r.direction, krx_percent(r.p_up), krx_percent(r.p_flat), krx_percent(r.p_down),
            r.in_consensus, r.result};
        for (int c = 0; c < kPinnedColumns; ++c) {
            auto* item =
                cell(cells[c], c >= kPinnedUp && c <= kPinnedDown ? kNumber : Qt::AlignLeft | Qt::AlignVCenter);
            if (r.baseline)
                item->setForeground(QColor(ui::colors::TEXT_DIM()));
            if (r.consensus) {
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
            predictions_table_->setItem(row, c, item);
        }
    }
}

void KrxQuantScreen::fill_predictions() {
    latest_->setVisible(live_->pinned().isValid());
    if (live_->pinned().isValid())
        return fill_pinned();
    predictions_table_->setColumnCount(kPredictionColumns);
    predictions_table_->setHorizontalHeaderLabels(
        {tr("예측"), tr("방향"), tr("상승"), tr("보합"), tr("하락"), tr("기대 틱"), tr("모델")});
    predictions_table_->setRowCount(0);
    const krx_quant::Latest latest = live_->board().latest(live_->selected());
    for (const auto& p : latest.predictions) {
        const int row = predictions_table_->rowCount();
        predictions_table_->insertRow(row);
        auto* name = cell(krx_quant::predictor_label(p.algorithm));
        if (live_->board().members().contains(p.algorithm))
            name->setToolTip(tr("정식: 합의에 들어간다"));
        predictions_table_->setItem(row, kPredictor, name);
        predictions_table_->setItem(row, kDirection, direction_cell(p));
        predictions_table_->setItem(row, kUp, cell(krx_percent(p.p_up), kNumber));
        predictions_table_->setItem(row, kFlat, cell(krx_percent(p.p_flat), kNumber));
        predictions_table_->setItem(row, kDown, cell(krx_percent(p.p_down), kNumber));
        const QString ticks = p.expected_ticks ? QString::number(*p.expected_ticks, 'f', 1) : QString();
        predictions_table_->setItem(row, kTicks, cell(ticks, kNumber));
        predictions_table_->setItem(row, kModel, cell(p.model_version));
        if (krx_quant::is_consensus(p.algorithm)) {
            for (int c = 0; c < kPredictionColumns; ++c) {
                QFont font = predictions_table_->item(row, c)->font();
                font.setBold(true);
                predictions_table_->item(row, c)->setFont(font);
            }
        }
    }
    update_countdowns();
}

void KrxQuantScreen::fill_chart() {
    const QString& symbol = live_->selected();
    chart_header_.title->setText(symbol.isEmpty() ? tr("차트") : tr("%1 · 1분봉과 최종 합의").arg(symbol));
    if (symbol.isEmpty()) {
        chart_->set_data({}, {});
        return;
    }
    const QString basis = leaderboard_->query().basis;
    chart_->set_data(live_->bars(), live_->board().marks(symbol, basis), basis, alerts_->rule());
}

void KrxQuantScreen::open_detail(const QString& algorithm) {
    if (algorithm.isEmpty())
        return;
    KrxStatsWindow* window = stats_windows_.value(algorithm);
    if (!window) { // a window of its own for each predictor, gone with the screen
        window = new KrxStatsWindow(this);
        stats_windows_.insert(algorithm, window);
        connect(window, &KrxStatsWindow::closed, this, [this, algorithm]() {
            if (auto* closed = stats_windows_.take(algorithm))
                closed->deleteLater();
            live_->set_details(stats_windows_.keys());
        });
    }
    window->open(algorithm);
    live_->set_details(stats_windows_.keys());
}

void KrxQuantScreen::update_countdowns() {
    const QDateTime now = live_->engine_now();
    for (int row = 0; row < symbols_table_->rowCount(); ++row) {
        auto* item = symbols_table_->item(row, kScoring);
        if (!item)
            continue;
        const krx_quant::Latest latest = live_->board().latest(symbols_table_->item(row, kSymbol)->text());
        const QDateTime at = krx_quant::scoring_at(latest);
        if (!at.isValid())
            item->setText(QString());
        else if (!is_today(latest.bar_close))
            item->setText(tr("지난 거래일"));
        else
            item->setText(now.secsTo(at) > 0 ? krx_countdown(now.secsTo(at)) : tr("채점됨"));
    }
    const QString& symbol = live_->selected();
    QLabel* panel_title = predictions_header_.title;
    if (live_->pinned().isValid() && !symbol.isEmpty()) { // pinned: no countdown, its own time
        panel_title->setText(
            tr("%1 · %2 봉 마감의 판단").arg(symbol, krx_quant::kst_text(live_->pinned(), "MM-dd HH:mm")));
        return;
    }
    if (symbol.isEmpty()) {
        panel_title->setText(tr("예측"));
        return;
    }
    const krx_quant::Latest latest = live_->board().latest(symbol);
    if (!latest.bar_close.isValid()) {
        panel_title->setText(tr("%1 · 예측 없음").arg(symbol));
        return;
    }
    if (!is_today(latest.bar_close)) {
        panel_title->setText(
            tr("%1 · %2 봉 마감 (지난 거래일)").arg(symbol, krx_quant::kst_text(latest.bar_close, "MM-dd HH:mm")));
        return;
    }
    QString title = tr("%1 · %2 봉 마감").arg(symbol, krx_quant::kst_text(latest.bar_close, "HH:mm"));
    if (const QDateTime at = krx_quant::scoring_at(latest); at.isValid())
        title += now.secsTo(at) > 0 ? tr(" · 채점까지 %1").arg(krx_countdown(now.secsTo(at)))
                                    : tr(" · %1에 채점").arg(krx_quant::kst_text(at, "HH:mm"));
    panel_title->setText(title);
}

} // namespace fincept::screens
