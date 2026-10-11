#include "screens/krx_quant/KrxQuantScreen.h"

#include "core/config/AppConfig.h"
#include "screens/krx_quant/KrxAlerts.h"
#include "screens/krx_quant/KrxChart.h"
#include "screens/krx_quant/KrxLeaderboard.h"
#include "screens/krx_quant/KrxStatsWindow.h"
#include "screens/krx_quant/KrxTables.h"
#include "ui/theme/Theme.h"

#include <QGroupBox>
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

constexpr auto kChartMinutesKey = "krx_quant/chart_minutes";

enum SymbolColumn { kSymbol, kMid, kFinal, kScoring };
enum PredictionColumn { kPredictor, kDirection, kUp, kFlat, kDown, kTicks, kModel, kPredictionColumns };

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
    // One style sheet for the whole screen, by object name.
    setStyleSheet(QString("#krxQuantScreen{background:%1;color:%2;}"
                          "#krxQuantScreen QLabel{background:transparent;}"
                          "#krxTitle{color:%3;font-size:16px;font-weight:700;}"
                          "#krxSubtitle{color:%3;font-weight:700;}"
                          "#krxSpecial{color:%3;}"
                          "#krxFooter,#krxNote{color:%4;}"
                          "#krxQuantScreen QLineEdit,#krxQuantScreen QComboBox,#krxQuantScreen QSpinBox"
                          "{background:%5;border:1px solid %6;padding:3px;}"
                          "#krxQuantScreen QPushButton{background:%7;border:1px solid %6;padding:3px 10px;}"
                          "#krxQuantScreen QTableWidget{background:%5;gridline-color:%8;border:1px solid %8;}"
                          "#krxQuantScreen QHeaderView::section{background:%7;color:%9;border:none;padding:4px;}"
                          "#krxQuantScreen QTableWidget::item:selected{background:%7;color:%2;}"
                          "#krxQuantScreen QGroupBox{border:1px solid %8;margin-top:14px;padding-top:6px;}"
                          "#krxQuantScreen QGroupBox::title{subcontrol-origin:margin;left:8px;color:%3;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::AMBER(),
                           ui::colors::TEXT_DIM(), ui::colors::BG_SURFACE(), ui::colors::BORDER_MED(),
                           ui::colors::BG_RAISED(), ui::colors::BORDER_DIM(), ui::colors::TEXT_SECONDARY()));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(6);

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
    macro_label_ = new QLabel;
    macro_label_->setWordWrap(true);
    macro_label_->hide();
    layout->addWidget(macro_label_);
    special_label_ = new QLabel;
    special_label_->setObjectName("krxSpecial");
    special_label_->setWordWrap(true);
    special_label_->hide();
    layout->addWidget(special_label_);

    // Top: the symbols, the chosen one's chart, and its predictions.
    auto* upper = new QSplitter(Qt::Horizontal);
    symbols_table_ = krx_tables::table({tr("종목"), tr("기준가"), tr("최종 합의"), tr("채점까지")});
    symbols_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    upper->addWidget(symbols_table_);

    auto* chart_box = new QWidget;
    auto* chart_layout = new QVBoxLayout(chart_box);
    chart_layout->setContentsMargins(0, 0, 0, 0);
    auto* chart_bar = new QHBoxLayout;
    chart_title_ = new QLabel;
    chart_title_->setObjectName("krxSubtitle");
    chart_bar->addWidget(chart_title_);
    chart_bar->addStretch();
    chart_bar->addWidget(new QLabel(tr("범위")));
    window_combo_ = new QComboBox;
    window_combo_->addItem(tr("30분"), 30);
    window_combo_->addItem(tr("1시간"), 60);
    window_combo_->addItem(tr("2시간"), 120);
    window_combo_->addItem(tr("하루 전체"), 0);
    window_combo_->setCurrentIndex(
        std::max(0, window_combo_->findData(AppConfig::instance().get(kChartMinutesKey, 60).toInt())));
    chart_bar->addWidget(window_combo_);
    chart_layout->addLayout(chart_bar);
    chart_ = new KrxChart;
    chart_->set_window(window_combo_->currentData().toInt());
    chart_layout->addWidget(chart_, 1);
    upper->addWidget(chart_box);

    auto* panel = new QWidget;
    auto* panel_layout = new QVBoxLayout(panel);
    panel_layout->setContentsMargins(0, 0, 0, 0);
    panel_title_ = new QLabel;
    panel_title_->setObjectName("krxSubtitle");
    panel_layout->addWidget(panel_title_);
    predictions_table_ =
        krx_tables::table({tr("예측"), tr("방향"), tr("상승"), tr("보합"), tr("하락"), tr("기대 틱"), tr("모델")});
    panel_layout->addWidget(predictions_table_, 1);
    upper->addWidget(panel);
    upper->setStretchFactor(0, 2);
    upper->setStretchFactor(1, 5);
    upper->setStretchFactor(2, 4);

    // Bottom: the leaderboard and the signal alerts.
    auto* lower = new QSplitter(Qt::Horizontal);
    auto* leaderboard_box = new QGroupBox(tr("순위표"));
    leaderboard_ = new KrxLeaderboard;
    (new QVBoxLayout(leaderboard_box))->addWidget(leaderboard_);
    lower->addWidget(leaderboard_box);
    auto* alerts_box = new QGroupBox(tr("신호 알림"));
    alerts_ = new KrxAlerts;
    (new QVBoxLayout(alerts_box))->addWidget(alerts_);
    lower->addWidget(alerts_box);
    lower->setStretchFactor(0, 4);
    lower->setStretchFactor(1, 1);

    auto* body = new QSplitter(Qt::Vertical);
    body->addWidget(upper);
    body->addWidget(lower);
    body->setStretchFactor(0, 3);
    body->setStretchFactor(1, 2);
    layout->addWidget(body, 1);

    footer_label_ = new QLabel;
    footer_label_->setObjectName("krxFooter");
    layout->addWidget(footer_label_);

    live_ = new services::KrxLive(this);
    connect(live_, &services::KrxLive::status_changed, this, [this]() {
        show_status();
        show_footer();
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
        chart_->set_empty_text(QString());
        fill_chart();
    });
    connect(live_, &services::KrxLive::chart_failed, this, [this](const QString& reason) {
        chart_->set_data({}, {});
        chart_->set_empty_text(tr("1분봉을 읽지 못했습니다: %1").arg(reason));
    });
    connect(live_, &services::KrxLive::macro_changed, this, [this]() { show_macro(); });
    connect(live_, &services::KrxLive::macro_failed, this, [this]() { show_macro(); });
    connect(live_, &services::KrxLive::leaderboard_changed, leaderboard_, &KrxLeaderboard::show_leaderboard);
    connect(live_, &services::KrxLive::leaderboard_failed, leaderboard_, &KrxLeaderboard::show_failure);
    connect(live_, &services::KrxLive::stats_changed, this, [this](const krx_quant::AlgorithmStats& stats) {
        if (stats_window_)
            stats_window_->show_stats(stats);
    });
    connect(live_, &services::KrxLive::stats_failed, this, [this](const QString& reason) {
        if (stats_window_)
            stats_window_->show_failure(reason);
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
    connect(window_combo_, &QComboBox::currentIndexChanged, this, [this]() {
        const int minutes = window_combo_->currentData().toInt();
        AppConfig::instance().set(kChartMinutesKey, minutes);
        chart_->set_window(minutes);
    });
    connect(leaderboard_, &KrxLeaderboard::query_changed, this, [this]() {
        live_->set_leaderboard_query(leaderboard_->query());
        fill_chart(); // the chart's ✓/✗ are on the leaderboard's basis
    });
    connect(leaderboard_, &KrxLeaderboard::detail_requested, this, &KrxQuantScreen::open_detail);
    connect(alerts_, &KrxAlerts::changed, this, [this]() { live_->set_alerts(alerts_->is_on(), alerts_->rule()); });

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, [this]() {
        update_countdowns();
        show_footer();
    });

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

void KrxQuantScreen::apply_base_url() {
    status_label_->setText(tr("엔진을 읽는 중…"));
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

void KrxQuantScreen::show_footer() {
    QString footer = tr("갱신 %1 (엔진 시계 %2)")
                         .arg(krx_quant::kst_text(QDateTime::currentDateTime(), "HH:mm:ss"),
                              krx_quant::kst_text(live_->engine_now(), "HH:mm:ss"));
    if (!live_->read_errors().isEmpty())
        footer += "  ·  " + live_->read_errors().join("; ");
    footer_label_->setText(footer);
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

void KrxQuantScreen::fill_predictions() {
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
    chart_title_->setText(symbol.isEmpty() ? QString() : tr("%1 · 1분봉과 최종 합의").arg(symbol));
    if (symbol.isEmpty()) {
        chart_->set_data({}, {});
        return;
    }
    chart_->set_data(live_->bars(), live_->board().marks(symbol, leaderboard_->query().basis));
}

void KrxQuantScreen::open_detail(const QString& algorithm) {
    if (algorithm.isEmpty())
        return;
    if (!stats_window_) {
        stats_window_ = new KrxStatsWindow(this); // a window of its own, gone with the screen
        connect(stats_window_, &KrxStatsWindow::closed, this, [this]() { live_->set_detail({}); });
    }
    stats_window_->open(algorithm);
    live_->set_detail(algorithm);
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
    if (symbol.isEmpty()) {
        panel_title_->clear();
        return;
    }
    const krx_quant::Latest latest = live_->board().latest(symbol);
    if (!latest.bar_close.isValid()) {
        panel_title_->setText(tr("%1 · 예측 없음").arg(symbol));
        return;
    }
    if (!is_today(latest.bar_close)) {
        panel_title_->setText(
            tr("%1 · %2 봉 마감 (지난 거래일)").arg(symbol, krx_quant::kst_text(latest.bar_close, "MM-dd HH:mm")));
        return;
    }
    QString title = tr("%1 · %2 봉 마감").arg(symbol, krx_quant::kst_text(latest.bar_close, "HH:mm"));
    if (const QDateTime at = krx_quant::scoring_at(latest); at.isValid())
        title += now.secsTo(at) > 0 ? tr(" · 채점까지 %1").arg(krx_countdown(now.secsTo(at)))
                                    : tr(" · %1에 채점").arg(krx_quant::kst_text(at, "HH:mm"));
    panel_title_->setText(title);
}

} // namespace fincept::screens
