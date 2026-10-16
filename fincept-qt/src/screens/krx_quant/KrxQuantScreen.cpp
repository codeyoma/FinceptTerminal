#include "screens/krx_quant/KrxQuantScreen.h"

#include "core/config/AppConfig.h"
#include "screens/krx_quant/KrxAlerts.h"
#include "screens/krx_quant/KrxChart.h"
#include "screens/krx_quant/KrxChartToolbar.h"
#include "screens/krx_quant/KrxDrawingPopup.h"
#include "screens/krx_quant/KrxLeaderboard.h"
#include "screens/krx_quant/KrxStatsWindow.h"
#include "screens/krx_quant/KrxStudyPanel.h"
#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/KindView.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace {

using krx_tables::cell;
using krx_tables::kNumber;

// The time to scoring is not a column (krx-quant #243): the latest bar close is every symbol's, so
// it showed the same countdown on every row, back to 4:00 each minute.
enum SymbolColumn { kSymbol, kMid, kFinal };
// The chart's view and style are kept for each symbol (krx-quant #221, krx_quant::prefs_group), and
// so are its indicators and the anchored VWAP's anchor (krx-quant #222), and its lower panes'
// heights (krx-quant #223), and its drawings (krx-quant #224, krx_quant::kPrefsDrawings); the magnet is
// every symbol's (krx_quant::kMagnetKey).
// "krx_quant/chart_view_minutes", the view of every symbol until then (krx-quant #109), and
// "krx_quant/chart_minutes", the range box's until #134, are no longer read.
// The models selected for the chart (krx-quant #159), as "gauss,markov".
constexpr auto kChartSelectionKey = "krx_quant/chart_selection";
// The kind of prediction shown (krx-quant #182): "direction", "range", …
constexpr auto kKindKey = "krx_quant/kind";
// The chart draws again this long after the last prediction of the chosen symbol came:
// a bar close brings about ten at once.
constexpr int kChartSettleMs = 250;
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

/// The four minutes a bar close's predictions look at, "10:30–10:34" (krx-quant #243), or the
/// bar close alone, "10:30 봉 마감", when no prediction says when it is scored.
QString krx_window(const QDateTime& bar_close, const QDateTime& scoring) {
    if (!scoring.isValid())
        return QObject::tr("%1 봉 마감").arg(krx_quant::kst_text(bar_close, "HH:mm"));
    return QStringLiteral("%1–%2").arg(krx_quant::kst_text(bar_close, "HH:mm"), krx_quant::kst_text(scoring, "HH:mm"));
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
    // The kind of prediction shown (krx-quant #182): checkable buttons as the chart's views, one
    // kind at a time; a kind the engine does not make (or this screen cannot show yet) greyed.
    kind_bar_ = new QWidget;
    auto* kinds = new QHBoxLayout(kind_bar_);
    kinds->setContentsMargins(12, 0, 0, 0);
    kinds->setSpacing(2);
    kinds->addWidget(new QLabel(tr("예측 종류")));
    for (const auto& choice : krx_quant::kind_choices(std::nullopt)) {
        auto* button = new QPushButton(choice.title);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setProperty("kind", choice.name);
        connect(button, &QPushButton::clicked, this, [this, name = choice.name]() {
            AppConfig::instance().set(kKindKey, name);
            live_->set_kind(name);
            show_kinds(); // checked as chosen, even while it shows another
        });
        kind_buttons_ << button;
        kinds->addWidget(button);
    }
    header_.row->insertWidget(3, kind_bar_);
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
    symbols_table_ = krx_tables::table({tr("종목"), tr("기준가"), tr("최종 합의")});
    upper->addWidget(krx_panel::panel(symbols_header_, symbols_table_));

    // Lightweight Charts' license asks for its attribution and a link where users see it.
    chart_header_ = krx_panel::header(
        tr("차트"), tr("차트: TradingView Lightweight Charts™ · Copyright (c) 2026 TradingView, Inc. · "
                       "https://www.tradingview.com/"));
    chart_failure_ = new QLabel(tr("읽지 못함"));
    chart_failure_->hide();
    chart_header_.controls->addWidget(chart_failure_);
    chart_score_ = new QLabel;
    chart_score_->setToolTip(tr("보이는 구간에서 4분이 다 지나 채점된 봉 중 선택 합의가 맞은 비율, 같은 봉들에서 "
                                "'늘 보합'이 맞았을 비율, ▲·▼라고 한 봉 중 맞은 비율(괄호는 그 수), 봉 수와 거래일 "
                                "수다. 4분 창은 서로 겹쳐 독립 표본이 아니다. 설명용 성적이고, 정식 판정은 순위표의 "
                                "거래일 단위 구간으로 한다. 특수 상황 봉은 세지 않고 따로 센다."));
    chart_score_->hide();
    chart_header_.controls->addWidget(chart_score_);
    auto* fit = new QPushButton(tr("맞춤"));
    fit->setToolTip(tr("최근 60봉 보기로 돌아간다. 차트를 두 번 눌러도 된다."));
    chart_header_.controls->addWidget(fit);
    chart_ = new KrxChart;
    // The row above the chart (krx-quant #220, #221): the minute views' drop-down, the day, week,
    // month and year buttons, the chart style, and the options. Each symbol keeps its view and
    // style; the day separators and the special situations' bands (krx-quant #236) are every symbol's.
    chart_toolbar_ = new KrxChartToolbar;
    look_.day_separator = AppConfig::instance().get(krx_quant::kDaySeparatorKey, true).toBool();
    look_.special = AppConfig::instance().get(krx_quant::kSpecialKey, true).toBool();
    chart_toolbar_->show_view(minutes_);
    chart_toolbar_->show_style(look_.style);
    chart_toolbar_->show_day_separator(look_.day_separator);
    chart_toolbar_->show_special(look_.special);
    connect(chart_toolbar_, &KrxChartToolbar::view_chosen, this, &KrxQuantScreen::set_view);
    connect(chart_toolbar_, &KrxChartToolbar::style_chosen, this, &KrxQuantScreen::set_style);
    // The size of a style without a time axis (krx-quant #225), set in the options menu for the
    // symbol, and the ATR(14) the chart counted for its default.
    connect(chart_toolbar_, &KrxChartToolbar::brick_size_chosen, this, &KrxQuantScreen::set_brick_size);
    connect(chart_, &KrxChart::brick_atr, this, [this](double size) {
        brick_atr_ = size > 0 ? std::optional(size) : std::nullopt;
        show_brick_size();
    });
    connect(chart_toolbar_, &KrxChartToolbar::day_separator_toggled, this, [this](bool on) {
        look_.day_separator = on;
        AppConfig::instance().set(krx_quant::kDaySeparatorKey, on);
        fill_chart();
    });
    connect(chart_toolbar_, &KrxChartToolbar::special_toggled, this, [this](bool on) {
        look_.special = on;
        AppConfig::instance().set(krx_quant::kSpecialKey, on);
        fill_chart();
    });
    // The indicators (krx-quant #222): their panel under the toolbar's button, and the legend's
    // buttons and the anchored VWAP's bar on the chart. Each symbol keeps its own.
    study_panel_ = new KrxStudyPanel(this);
    connect(chart_toolbar_, &KrxChartToolbar::indicators_requested, this, [this]() { open_studies(); });
    connect(study_panel_, &KrxStudyPanel::studies_changed, this, &KrxQuantScreen::set_studies);
    connect(study_panel_, &KrxStudyPanel::anchor_requested, this, [this]() {
        if (!krx_quant::anchor_pickable(look_.studies) || krx_quant::timeless(look_.style))
            return;           // hidden, or bricks: nothing of it waits for a click (the panel's button is off)
        look_.picking = true; // the next click on the chart
        study_panel_->hide();
        fill_chart();
    });
    connect(chart_, &KrxChart::anchor_chosen, this, &KrxQuantScreen::set_anchor);
    connect(chart_, &KrxChart::study_settings_requested, this, [this](krx_quant::Study study) { open_studies(study); });
    connect(chart_, &KrxChart::study_hide_toggled, this, [this](krx_quant::Study study) {
        krx_quant::StudySet set = look_.studies;
        set.of(study).hidden = !set.of(study).hidden;
        set_studies(set);
    });
    connect(chart_, &KrxChart::study_removed, this, [this](krx_quant::Study study) {
        krx_quant::StudySet set = look_.studies;
        set.turn(study, false);
        set_studies(set);
    });
    // A border between panes dragged (krx-quant #223): the lower panes' heights, kept for the
    // symbol (those of the indicators off stay, for when they are turned on again).
    connect(chart_, &KrxChart::pane_heights_changed, this, [this](const krx_quant::PaneHeights& heights) {
        for (auto it = heights.cbegin(); it != heights.cend(); ++it)
            look_.pane_heights.insert(it.key(), it.value());
        if (const QString& symbol = live_->selected(); !symbol.isEmpty())
            AppConfig::instance().set(krx_quant::prefs_group(symbol) + krx_quant::kPrefsPaneHeights,
                                      krx_quant::pane_heights_value(look_.pane_heights));
        fill_chart();
    });
    // The drawings (krx-quant #224): a tool from the toolbar's drawing menu draws with the next clicks
    // on the chart; what the chart placed, dragged, removed or double-clicked is kept for the symbol.
    // The magnet is every symbol's; the trash asks before it removes all of the symbol's.
    drawing_popup_ = new KrxDrawingPopup(this);
    look_.magnet = AppConfig::instance().get(krx_quant::kMagnetKey, false).toBool();
    chart_toolbar_->show_magnet(look_.magnet);
    connect(chart_toolbar_, &KrxChartToolbar::tool_chosen, this,
            [this](krx_quant::DrawingTool tool) { set_tool(tool); });
    connect(chart_, &KrxChart::tool_finished, this, [this]() { chart_toolbar_->show_tool(std::nullopt); });
    connect(chart_toolbar_, &KrxChartToolbar::magnet_toggled, this, [this](bool on) {
        look_.magnet = on;
        AppConfig::instance().set(krx_quant::kMagnetKey, on);
        chart_->set_drawings(look_.drawings, look_.magnet);
    });
    connect(chart_toolbar_, &KrxChartToolbar::trash_requested, this, [this]() {
        const auto count = look_.drawings.size();
        if (count == 0 || live_->selected().isEmpty())
            return;
        if (QMessageBox::question(this, tr("그린 것 지우기"), tr("그린 것 %1개를 지울까요?").arg(count),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        look_.drawings.clear();
        drawing_popup_->hide();
        drawings_changed(-1);
    });
    connect(chart_, &KrxChart::drawing_added, this, [this](krx_quant::Drawing drawing, const QPoint& at) {
        if (live_->selected().isEmpty())
            return;
        drawing.id = krx_quant::next_drawing_id(look_.drawings);
        // A text placed with the tool opens its input, its first text selected to be typed over.
        const bool typing = drawing.tool == krx_quant::DrawingTool::Text && drawing.text.isEmpty();
        if (typing)
            drawing.text = QString::fromUtf8(krx_quant::kDrawingText);
        look_.drawings << drawing;
        drawings_changed(drawing.id);
        if (typing)
            drawing_popup_->open_for(drawing, at);
    });
    connect(chart_, &KrxChart::drawing_changed, this, [this](const krx_quant::Drawing& drawing) {
        for (auto& d : look_.drawings)
            if (d.id == drawing.id)
                d = drawing;
        drawings_changed(drawing.id);
    });
    connect(chart_, &KrxChart::drawing_removed, this, &KrxQuantScreen::remove_drawing);
    connect(chart_, &KrxChart::drawing_edit_requested, this, [this](int id, const QPoint& at) {
        for (const auto& d : std::as_const(look_.drawings))
            if (d.id == id)
                drawing_popup_->open_for(d, at);
    });
    connect(drawing_popup_, &KrxDrawingPopup::drawing_changed, this, [this](const krx_quant::Drawing& drawing) {
        for (auto& d : look_.drawings)
            if (d.id == drawing.id)
                d = drawing;
        drawings_changed(drawing.id);
    });
    connect(drawing_popup_, &KrxDrawingPopup::remove_requested, this, &KrxQuantScreen::remove_drawing);
    connect(drawing_popup_, &KrxDrawingPopup::closed, this, [this](int id) {
        const bool empty =
            std::any_of(look_.drawings.cbegin(), look_.drawings.cend(), [id](const krx_quant::Drawing& d) {
                return d.id == id && d.tool == krx_quant::DrawingTool::Text && d.text.trimmed().isEmpty();
            });
        if (empty) // a text left empty is not kept
            remove_drawing(id);
    });
    auto* chart_body = new QWidget;
    auto* chart_column = new QVBoxLayout(chart_body);
    chart_column->setContentsMargins(0, 0, 0, 0);
    chart_column->setSpacing(0);
    chart_column->addWidget(chart_toolbar_);
    chart_column->addWidget(chart_, 1);
    upper->addWidget(krx_panel::panel(chart_header_, chart_body));

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
    });
    // The chart draws again once the chosen symbol's predictions settle (krx-quant #159).
    chart_soon_ = new QTimer(this);
    chart_soon_->setSingleShot(true);
    chart_soon_->setInterval(kChartSettleMs);
    connect(chart_soon_, &QTimer::timeout, this, &KrxQuantScreen::fill_chart);
    connect(live_, &services::KrxLive::marks_changed, chart_soon_, qOverload<>(&QTimer::start));
    // The special situations' bands (krx-quant #236) with the next drawing.
    connect(live_, &services::KrxLive::special_changed, chart_soon_, qOverload<>(&QTimer::start));
    connect(live_, &services::KrxLive::live_changed, this, [this]() {
        // A candle view's last candle holds today, made of the day's bars (krx-quant #111, #112).
        const bool candles = krx_quant::candle_view(minutes_);
        chart_->update_live(candles ? live_->view_candles(minutes_) : live_->bars(), minutes_);
    });
    connect(live_, &services::KrxLive::selected_changed, this, &KrxQuantScreen::show_symbol_prefs);
    connect(live_, &services::KrxLive::chart_changed, this, [this]() {
        chart_failure_->hide();
        fill_chart();
    });
    connect(live_, &services::KrxLive::chart_failed, this, [this](const QString& reason) {
        chart_->set_data({}, {});
        chart_failure_->setToolTip(tr("1분봉을 읽지 못했습니다: %1").arg(reason));
        chart_failure_->show();
    });
    connect(live_, &services::KrxLive::older_failed, this, [this](const QString& reason) {
        // The bars shown stay; scrolling to the left end again asks again.
        chart_failure_->setToolTip(krx_quant::candle_view(minutes_)
                                       ? tr("일·주·월·년봉을 읽지 못했습니다: %1").arg(reason)
                                       : tr("지난 1분봉을 읽지 못했습니다: %1").arg(reason));
        chart_failure_->show();
    });
    connect(live_, &services::KrxLive::macro_changed, this, [this]() { show_macro(); });
    connect(live_, &services::KrxLive::macro_failed, this, [this]() { show_macro(); });
    connect(live_, &services::KrxLive::leaderboard_changed, leaderboard_, &KrxLeaderboard::show_leaderboard);
    connect(live_, &services::KrxLive::leaderboard_failed, leaderboard_, &KrxLeaderboard::show_failure);
    connect(live_, &services::KrxLive::stats_changed, this, [this](const krx_quant::AlgorithmStats& stats) {
        if (auto* window = stats_windows_.value(stats.algorithm))
            window->show_stats(stats, krx_quant::kind_named(live_->kinds(), stats.kind));
    });
    connect(live_, &services::KrxLive::stats_failed, this, [this](const QString& algorithm, const QString& reason) {
        if (auto* window = stats_windows_.value(algorithm))
            window->show_failure(reason);
    });
    connect(live_, &services::KrxLive::algorithms_changed, this, [this]() {
        for (auto it = stats_windows_.cbegin(); it != stats_windows_.cend(); ++it)
            it.value()->show_about(about_of(it.key()), about_failure(it.key()));
    });
    connect(live_, &services::KrxLive::kinds_changed, this, [this]() {
        show_kinds();
        for (auto it = stats_windows_.cbegin(); it != stats_windows_.cend(); ++it) // a kind's baselines' cards
            it.value()->show_about(about_of(it.key()), about_failure(it.key()));
    });
    connect(live_, &services::KrxLive::kind_changed, this, &KrxQuantScreen::on_kind_changed);
    connect(live_, &services::KrxLive::kind_predictions_changed, this, [this]() {
        if (live_->kind() == QLatin1String(krx_quant::kDirectionKind))
            return; // the direction's panel and boxes follow their own
        fill_predictions();
        chart_soon_->start(); // the hover boxes, once they settle
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
    connect(chart_, &KrxChart::view_moved, this, [this](double from, double to) {
        // Bricks (krx-quant #225) start at a fixed bar: scrolling them back reads nothing before it.
        if (!krx_quant::timeless(look_.style))
            live_->view_from(from, to, minutes_);
    });
    connect(chart_, &KrxChart::view_range, this, [this](qint64 from, qint64 to) {
        view_range_ = {from, to};
        show_score();
    });
    live_->set_view(minutes_); // 1 minute until a symbol is chosen: then its own (show_symbol_prefs)
    connect(latest_, &QPushButton::clicked, this, [this]() { live_->pin({}); });
    connect(live_, &services::KrxLive::pinned_changed, this, [this]() {
        predictions_header_.title->setToolTip({}); // a failed read's reason, now stale
        fill_predictions();
        update_panel_title();
    });
    connect(live_, &services::KrxLive::pinned_failed, this, [this](const QString& reason) {
        predictions_table_->setRowCount(0);
        predictions_header_.title->setToolTip(tr("그 봉 마감의 예측을 읽지 못했습니다: %1").arg(reason));
    });
    connect(leaderboard_, &KrxLeaderboard::query_changed, this,
            [this]() { live_->set_leaderboard_query(leaderboard_->query()); });
    connect(leaderboard_, &KrxLeaderboard::selection_changed, this, [this](const QStringList& selection) {
        AppConfig::instance().set(kChartSelectionKey, krx_quant::selection_value(selection));
        live_->set_selection(selection);
        fill_predictions(); // the pinned bar close's selected consensus
        fill_chart();
    });
    connect(leaderboard_, &KrxLeaderboard::detail_requested, this, &KrxQuantScreen::open_detail);
    connect(alerts_, &KrxAlerts::changed, this, [this]() {
        live_->set_alerts(alerts_->is_on(), alerts_->rule());
        fill_chart(); // the signals the chart marks follow the threshold and N
    });

    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, [this]() {
        update_panel_title();
        show_dot();
        // A slot after the last bar is over while the session is open (krx-quant #238): the chart
        // fills it with an empty bar, so a halt or a quiet stock grows with the clock.
        if (krx_quant::clock_slot(krx_quant::session_clock(live_->status(), live_->engine_now()), minutes_) !=
            clock_slot_drawn_)
            chart_soon_->start();
    });

    refresh_theme();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this]() { refresh_theme(); });

    live_->set_alerts(alerts_->is_on(), alerts_->rule());
    live_->set_kind(AppConfig::instance().get(kKindKey, QString(krx_quant::kDirectionKind)).toString());
    show_kinds();
    live_->set_leaderboard_query(leaderboard_->query());
    const QStringList selection = krx_quant::parse_selection(AppConfig::instance().get(kChartSelectionKey).toString());
    leaderboard_->set_selection(selection);
    live_->set_selection(selection);
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
    chart_score_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));
    chart_toolbar_->restyle();
    study_panel_->restyle();
    drawing_popup_->restyle();
    // The kind selector as the views (krx-quant #182); a kind that cannot be chosen, dim.
    kind_bar_->setStyleSheet(
        QString("QLabel{color:%1;padding-right:4px;}"
                "QPushButton{background:transparent;color:%3;border:1px solid transparent;padding:2px 8px;"
                "font-weight:700;}"
                "QPushButton:checked{color:%4;background:%2;border:1px solid %2;}"
                "QPushButton:hover:!checked:enabled{border-color:%5;}"
                "QPushButton:disabled{color:%6;}")
            .arg(ui::colors::TEXT_TERTIARY(), ui::colors::AMBER(), ui::colors::TEXT_PRIMARY(), ui::colors::BG_BASE(),
                 ui::colors::BORDER_MED(), ui::colors::TEXT_DIM()));
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
        if (quote.symbol == live_->selected())
            symbols_table_->selectRow(row);
    }
    update_panel_title();
}

void KrxQuantScreen::fill_pinned() {
    // A bar close's judgements (krx-quant #107): the consensuses, the models with their status,
    // whether the final consensus pooled them and how they did, then the baselines, dimmed.
    predictions_table_->setColumnCount(kPinnedColumns);
    predictions_table_->setHorizontalHeaderLabels(
        {tr("예측"), tr("상태"), tr("방향"), tr("상승"), tr("보합"), tr("하락"), tr("합의"), tr("결과")});
    predictions_table_->setRowCount(0);
    for (const auto& r : krx_quant::close_rows(live_->pinned_predictions(), live_->statuses(), live_->selection())) {
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

void KrxQuantScreen::show_kinds() {
    const QString saved = AppConfig::instance().get(kKindKey, QString(krx_quant::kDirectionKind)).toString();
    const auto choices = krx_quant::kind_choices(live_->kinds());
    for (auto* button : kind_buttons_) {
        const QString name = button->property("kind").toString();
        for (const auto& c : choices) {
            if (c.name != name)
                continue;
            button->setText(c.title);
            button->setEnabled(c.enabled);
            button->setToolTip(c.tip);
        }
        // The kind shown; the one chosen stays kept while it cannot be shown (an older engine).
        button->setChecked(name == live_->kind());
        if (name == saved && name != live_->kind())
            button->setToolTip(button->toolTip() + tr("\n고른 종류지만 지금 엔진에서는 방향을 보인다."));
    }
}

void KrxQuantScreen::on_kind_changed() {
    show_kinds();
    const bool direction = live_->kind() == QLatin1String(krx_quant::kDirectionKind);
    leaderboard_->set_kind(direction ? std::nullopt : live_->kind_info());
    // The panel's header items go with their columns: the kind's tooltips do not stay on the direction's.
    predictions_table_->setColumnCount(0);
    fill_predictions();
    fill_chart();
}

void KrxQuantScreen::fill_kind() {
    const auto kind = live_->kind_info();
    const bool pinned = live_->pinned().isValid();
    const QString& symbol = live_->selected();
    // The kind's columns (krx-quant #182, #185), what each is on its header: the range's widths in
    // ticks, the barrier's w, U and L on the panel's title.
    const auto columns = krx_quant::kind_panel_columns(kind, pinned);
    QStringList headers;
    for (const auto& c : columns)
        headers << c.header;
    predictions_table_->setColumnCount(static_cast<int>(headers.size()));
    predictions_table_->setHorizontalHeaderLabels(headers);
    for (int c = 0; c < columns.size(); ++c)
        if (auto* item = predictions_table_->horizontalHeaderItem(c); item && !columns[c].tip.isEmpty())
            item->setToolTip(columns[c].tip);
    const bool range = live_->kind() == QLatin1String(krx_quant::kRangeKind);
    predictions_table_->setRowCount(0);
    if (!kind || symbol.isEmpty())
        return kind_title();
    const QList<krx_quant::KindPrediction> at_close = pinned ? live_->kind_board().closes(symbol).value(live_->pinned())
                                                             : live_->kind_board().latest(symbol).predictions;
    const auto quantile = [](const std::optional<double>& q) { return q ? krx_quant::ticks_text(*q) : QString(); };
    for (const auto& r : krx_quant::kind_rows(at_close, *kind)) {
        const int row = predictions_table_->rowCount();
        predictions_table_->insertRow(row);
        QList<QPair<QString, Qt::Alignment>> cells{{r.name, Qt::AlignLeft | Qt::AlignVCenter},
                                                   {r.top, Qt::AlignLeft | Qt::AlignVCenter}};
        for (const auto& p : r.probabilities)
            cells.append({krx_percent(p), kNumber});
        for (qsizetype i = r.probabilities.size(); i < kind->classes.size(); ++i)
            cells.append({QString(), kNumber}); // no forecast
        if (range)
            cells << QPair<QString, Qt::Alignment>{quantile(r.q10), kNumber}
                  << QPair<QString, Qt::Alignment>{quantile(r.q50), kNumber}
                  << QPair<QString, Qt::Alignment>{quantile(r.q90), kNumber};
        if (pinned)
            cells.append({r.result, Qt::AlignLeft | Qt::AlignVCenter});
        cells.append({r.model_version, Qt::AlignLeft | Qt::AlignVCenter});
        for (int c = 0; c < cells.size(); ++c) {
            auto* item = cell(cells[c].first, cells[c].second);
            if (r.baseline)
                item->setForeground(QColor(ui::colors::TEXT_DIM()));
            predictions_table_->setItem(row, c, item);
        }
    }
    kind_title();
}

void KrxQuantScreen::kind_title() {
    QLabel* panel_title = predictions_header_.title;
    const QString& symbol = live_->selected();
    const auto kind = live_->kind_info();
    const QString name = kind ? kind->title : live_->kind();
    if (symbol.isEmpty()) {
        panel_title->setText(tr("예측 · %1").arg(name));
        return;
    }
    // k now, from the symbols' answer: shown only when the bar close's own is not known (an
    // engine before krx-quant #200, whose predictions do not carry it, before the answer).
    std::optional<int> k_now;
    for (const auto& q : live_->board().symbols())
        if (q.symbol == symbol)
            k_now = q.flat_ticks;
    const bool range = live_->kind() == QLatin1String(krx_quant::kRangeKind);
    const bool barrier = live_->kind() == QLatin1String(krx_quant::kBarrierKind);
    if (live_->pinned().isValid()) {
        const auto at_close = live_->kind_board().closes(symbol).value(live_->pinned());
        QStringList parts{
            tr("%1 · %2 · %3 봉 마감의 판단").arg(symbol, name, krx_quant::kst_text(live_->pinned(), "MM-dd HH:mm"))};
        if (kind) {
            std::optional<krx_quant::KindAnswer> answer;
            for (const auto& p : at_close)
                if (p.answer)
                    answer = p.answer;
            parts << krx_quant::kind_answer_text(answer, *kind);
        }
        if (range)
            if (const QString widths = krx_quant::widths_text(krx_quant::bar_close_k(at_close, std::nullopt));
                !widths.isEmpty())
                parts << widths;
        if (barrier) // its w, U and L (krx-quant #185)
            if (const QString barriers = krx_quant::barrier_text(at_close); !barriers.isEmpty())
                parts << barriers;
        panel_title->setText(parts.join(QStringLiteral(" · ")));
        return;
    }
    const krx_quant::KindLatest latest = live_->kind_board().latest(symbol);
    if (!latest.bar_close.isValid()) {
        panel_title->setText(tr("%1 · %2 · 예측 없음").arg(symbol, name));
        return;
    }
    QStringList parts;
    if (!is_today(latest.bar_close)) {
        parts << tr("%1 · %2 · %3 봉 마감 (지난 거래일)")
                     .arg(symbol, name, krx_quant::kst_text(latest.bar_close, "MM-dd HH:mm"));
    } else {
        QDateTime at;
        for (const auto& p : latest.predictions)
            if (p.target_at.isValid())
                at = p.target_at;
        parts << tr("%1 · %2 · %3").arg(symbol, name, krx_window(latest.bar_close, at));
    }
    if (range)
        if (const QString widths = krx_quant::widths_text(krx_quant::bar_close_k(latest.predictions, k_now));
            !widths.isEmpty())
            parts << widths;
    if (barrier) // its w, U and L (krx-quant #185), before its answer too (#210)
        if (const QString barriers = krx_quant::barrier_text(latest.predictions); !barriers.isEmpty())
            parts << barriers;
    panel_title->setText(parts.join(QStringLiteral(" · ")));
}

void KrxQuantScreen::fill_predictions() {
    latest_->setVisible(live_->pinned().isValid());
    if (live_->kind() != QLatin1String(krx_quant::kDirectionKind))
        return fill_kind(); // another kind's panel (krx-quant #182)
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
    update_panel_title();
}

void KrxQuantScreen::set_view(int minutes) {
    minutes_ = minutes;
    if (const QString& symbol = live_->selected(); !symbol.isEmpty())
        AppConfig::instance().set(krx_quant::prefs_group(symbol) + krx_quant::kPrefsView, minutes);
    chart_toolbar_->show_view(minutes);
    live_->set_view(minutes);
    want_history(); // the indicators' bars in this view (krx-quant #222)
    study_panel_->show_studies(look_.studies, minutes,
                               krx_quant::timeless(look_.style)); // the VWAP is the minute views'
    fill_chart();
    chart_->fit(); // the last 60 bars of the new view
}

void KrxQuantScreen::set_style(krx_quant::ChartStyle style) {
    const bool was_timeless = krx_quant::timeless(look_.style);
    look_.style = style;
    if (const QString& symbol = live_->selected(); !symbol.isEmpty())
        AppConfig::instance().set(krx_quant::prefs_group(symbol) + krx_quant::kPrefsStyle, krx_quant::style_key(style));
    chart_toolbar_->show_style(style);
    show_brick_size();
    const bool timeless = krx_quant::timeless(style);
    if (timeless) { // no drawings there (krx-quant #220, #224, #225)
        set_tool(std::nullopt);
        drawing_popup_->hide();
    }
    study_panel_->show_studies(look_.studies, minutes_, timeless); // neither VWAP on bricks (krx-quant #225)
    if (timeless != was_timeless)
        want_history(); // the bars the bricks start with, or the indicators' again
    fill_chart();
    if (timeless || was_timeless)
        chart_->fit(); // bricks and bars are not on the same slots: the last 60 of them
}

void KrxQuantScreen::show_brick_size() {
    chart_toolbar_->show_brick_size(
        look_.brick_sizes.contains(look_.style) ? std::optional(look_.brick_sizes.value(look_.style)) : std::nullopt,
        brick_atr_);
}

void KrxQuantScreen::set_brick_size(krx_quant::ChartStyle style, double size) {
    if (size > 0 && krx_quant::valid_brick_size(style, size))
        look_.brick_sizes.insert(style, size);
    else
        look_.brick_sizes.remove(style); // its default again
    if (const QString& symbol = live_->selected(); !symbol.isEmpty())
        AppConfig::instance().set(krx_quant::prefs_group(symbol) + krx_quant::kPrefsBrickSizes,
                                  krx_quant::brick_sizes_value(look_.brick_sizes));
    show_brick_size();
    fill_chart();
}

void KrxQuantScreen::show_symbol_prefs() {
    const QString group = krx_quant::prefs_group(live_->selected());
    const auto prefs = krx_quant::parse_prefs(AppConfig::instance().get(group + krx_quant::kPrefsView),
                                              AppConfig::instance().get(group + krx_quant::kPrefsStyle));
    look_.style = prefs.style;
    // The sizes it set for the styles without a time axis (krx-quant #225); the others their defaults.
    look_.brick_sizes = krx_quant::parse_brick_sizes(AppConfig::instance().get(group + krx_quant::kPrefsBrickSizes));
    chart_toolbar_->show_style(prefs.style);
    show_brick_size();
    chart_toolbar_->show_view(prefs.view);
    if (prefs.view != minutes_) {
        minutes_ = prefs.view;
        live_->set_view(minutes_); // its bars are read next, for this view
    }
    // Its indicators (krx-quant #222): opened for the first time, the moving averages 5, 20, 60
    // and 120 of the close, and the volume.
    look_.studies = krx_quant::parse_studies(AppConfig::instance().get(group + krx_quant::kPrefsStudies));
    const QDateTime anchor =
        QDateTime::fromString(AppConfig::instance().get(group + krx_quant::kPrefsAnchor).toString(), Qt::ISODate);
    look_.anchor = anchor.isValid() ? std::optional(krx_quant::chart_time(anchor)) : std::nullopt;
    look_.picking = krx_quant::anchor_pickable(look_.studies) && !look_.anchor;
    // Its lower panes' heights (krx-quant #223): those dragged; the others the default.
    look_.pane_heights = krx_quant::parse_pane_heights(AppConfig::instance().get(group + krx_quant::kPrefsPaneHeights));
    study_panel_->show_studies(look_.studies, minutes_, krx_quant::timeless(look_.style));
    want_history();
    // Its drawings (krx-quant #224), none selected; a tool chosen for another symbol is put down.
    look_.drawings = krx_quant::parse_drawings(AppConfig::instance().get(group + krx_quant::kPrefsDrawings));
    drawing_popup_->hide();
    set_tool(std::nullopt);
    chart_toolbar_->show_drawings(static_cast<int>(look_.drawings.size()));
    chart_->set_drawings(look_.drawings, look_.magnet, -1);
}

void KrxQuantScreen::drawings_changed(int select) {
    if (const QString& symbol = live_->selected(); !symbol.isEmpty())
        AppConfig::instance().set(krx_quant::prefs_group(symbol) + krx_quant::kPrefsDrawings,
                                  krx_quant::drawings_value(look_.drawings));
    chart_toolbar_->show_drawings(static_cast<int>(look_.drawings.size()));
    chart_->set_drawings(look_.drawings, look_.magnet, select);
}

void KrxQuantScreen::remove_drawing(int id) {
    const auto before = look_.drawings.size();
    look_.drawings.removeIf([id](const krx_quant::Drawing& d) { return d.id == id; });
    if (look_.drawings.size() != before)
        drawings_changed(-1);
}

void KrxQuantScreen::set_tool(std::optional<krx_quant::DrawingTool> tool) {
    chart_toolbar_->show_tool(tool);
    chart_->set_tool(tool);
}

void KrxQuantScreen::set_studies(const krx_quant::StudySet& set) {
    using krx_quant::Study;
    const bool anchoring = set.on(Study::AnchoredVwap) && !look_.studies.on(Study::AnchoredVwap);
    look_.studies = set;
    if (!krx_quant::anchor_pickable(set))
        look_.picking = false;
    else if (anchoring && !look_.anchor) {
        look_.picking = true; // turned on without an anchor: the next click on the chart
        study_panel_->hide();
    }
    if (const QString& symbol = live_->selected(); !symbol.isEmpty())
        AppConfig::instance().set(krx_quant::prefs_group(symbol) + krx_quant::kPrefsStudies,
                                  krx_quant::studies_value(set));
    study_panel_->show_studies(set, minutes_, krx_quant::timeless(look_.style)); // as the legend changed it
    want_history();
    fill_chart();
}

void KrxQuantScreen::open_studies(std::optional<krx_quant::Study> study) {
    study_panel_->show_studies(look_.studies, minutes_, krx_quant::timeless(look_.style));
    if (study)
        study_panel_->select(*study);
    study_panel_->open_at(chart_toolbar_->indicators_corner());
}

void KrxQuantScreen::set_anchor(qint64 time) {
    look_.anchor = time;
    look_.picking = false;
    if (const QString& symbol = live_->selected(); !symbol.isEmpty()) {
        // As a moment in KST (chart_time is KST read as UTC): the bar holding it in any view.
        const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
        AppConfig::instance().set(krx_quant::prefs_group(symbol) + krx_quant::kPrefsAnchor,
                                  QDateTime::fromSecsSinceEpoch(time - 9 * 3600, kst).toString(Qt::ISODate));
    }
    want_history();
    fill_chart();
}

void KrxQuantScreen::want_history() {
    // Bricks (krx-quant #225) start from at least kBrickBars of the view's bars, read first; the
    // indicators on them count on the bricks, not on the bars before.
    if (krx_quant::timeless(look_.style))
        return live_->set_history(krx_quant::kBrickBars);
    QDateTime back_to;
    if (look_.studies.on(krx_quant::Study::AnchoredVwap) && look_.anchor)
        back_to = QDateTime::fromSecsSinceEpoch(*look_.anchor - 9 * 3600, QTimeZone::fromSecondsAheadOfUtc(9 * 3600));
    // The VWAP's days on the first screen whole (krx-quant #222): a minute view's only.
    const bool whole_days = look_.studies.on(krx_quant::Study::Vwap) && !krx_quant::candle_view(minutes_);
    // The AD line's and the OBV's 0 held, and the bars from it on (krx-quant #223): any view.
    const bool cumulative = look_.studies.on(krx_quant::Study::AdLine) || look_.studies.on(krx_quant::Study::Obv);
    live_->set_history(krx_quant::history_bars(look_.studies, minutes_), back_to, whole_days, cumulative);
}

void KrxQuantScreen::fill_chart() {
    if (chart_soon_)
        chart_soon_->stop(); // drawn now
    const QString& symbol = live_->selected();
    const bool candles = krx_quant::candle_view(minutes_);
    const bool chosen = !live_->selection().isEmpty();
    const QString candle_title = minutes_ == krx_quant::kWeekView    ? tr("%1 · 주봉")
                                 : minutes_ == krx_quant::kMonthView ? tr("%1 · 월봉")
                                 : minutes_ == krx_quant::kYearView  ? tr("%1 · 년봉")
                                                                     : tr("%1 · 일봉");
    const bool bricks = krx_quant::timeless(look_.style); // no icons on them (krx-quant #225)
    chart_header_.title->setText(
        symbol.isEmpty() ? tr("차트")
        : bricks ? tr("%1 · %2").arg(candles ? candle_title.arg(symbol) : tr("%1 · %2분봉").arg(symbol).arg(minutes_),
                                     krx_quant::style_title(look_.style))
        : candles       ? candle_title.arg(symbol)
        : minutes_ == 1 ? (chosen ? tr("%1 · 1분봉") : tr("%1 · 1분봉과 최종 합의")).arg(symbol)
                        : tr("%1 · %2분봉").arg(symbol).arg(minutes_));
    if (symbol.isEmpty()) {
        chart_->set_data({}, {});
        show_score();
        return;
    }
    const krx_quant::SignalRule rule = alerts_->rule();
    const QList<krx_quant::Mark> marks = live_->board().marks(symbol, live_->selection());
    // Each 1-minute bar's box (krx-quant #159): the 1-minute view's only. While another kind is
    // shown, its own (krx-quant #182): its answer and predictions, and the range's highest and
    // lowest mid-points or the barrier's U and L (krx-quant #185); the icons stay the direction's.
    // The chart adds the empty bars' boxes and the special situations' lines in every minute view
    // (krx-quant #236).
    const auto kind = live_->kind() != QLatin1String(krx_quant::kDirectionKind) ? live_->kind_info() : std::nullopt;
    const QList<krx_quant::BarHover> hovers =
        minutes_ != 1 ? QList<krx_quant::BarHover>{}
        : kind
            ? krx_quant::kind_hovers(live_->bars(), live_->kind_board().closes(symbol), *kind, live_->kind_standings())
            : krx_quant::bar_hovers(live_->bars(), live_->board().closes(symbol), live_->selection(),
                                    live_->standings(), rule);
    // Nothing before the first bar held in this view: its first day's VWAP is drawn, and the AD
    // line's and the OBV's stretch from their 0 is whole (krx-quant #223).
    look_.first_day_whole = live_->reached_start(minutes_);
    // Today's slots after the last bar fill up to the engine's clock while the session is open (krx-quant #238).
    const auto clock = krx_quant::session_clock(live_->status(), live_->engine_now());
    clock_slot_drawn_ = krx_quant::clock_slot(clock, minutes_);
    chart_->set_data(candles ? live_->view_candles(minutes_) : live_->bars(), marks, hovers, rule, minutes_, look_,
                     live_->special(), clock);
    show_score();
}

void KrxQuantScreen::show_score() {
    const QString& symbol = live_->selected();
    const QStringList& selection = live_->selection();
    const QString head = krx_quant::selection_head(selection); // empty: several baselines alone, nothing drawn
    if (symbol.isEmpty() || head.isEmpty() || minutes_ != 1 || view_range_.second <= 0 ||
        krx_quant::timeless(look_.style)) { // bricks have no icons (krx-quant #225)
        chart_score_->hide();
        return;
    }
    const auto score = krx_quant::score_of(live_->board().marks(symbol, selection), view_range_.first,
                                           view_range_.second, live_->engine_now());
    chart_score_->setText(krx_quant::score_text(head, score));
    chart_score_->show();
}

QString KrxQuantScreen::about_failure(const QString& algorithm) const {
    // Another kind's baseline is described by GET /v1/kinds: a failed /v1/algorithms is not its.
    if (!live_->algorithms().contains(algorithm) && krx_quant::kind_baseline_info(live_->kinds(), algorithm))
        return {};
    return live_->algorithms_failure();
}

std::optional<krx_quant::AlgorithmInfo> KrxQuantScreen::about_of(const QString& algorithm) const {
    const auto& algorithms = live_->algorithms();
    if (const auto it = algorithms.constFind(algorithm); it != algorithms.cend())
        return *it;
    // Another kind's baseline (krx-quant #182): its card is in GET /v1/kinds.
    return krx_quant::kind_baseline_info(live_->kinds(), algorithm);
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
    window->show_about(about_of(algorithm), about_failure(algorithm)); // as last read; read again with its stats
    live_->set_details(stats_windows_.keys());
}

void KrxQuantScreen::update_panel_title() {
    const QString& symbol = live_->selected();
    QLabel* panel_title = predictions_header_.title;
    if (live_->kind() != QLatin1String(krx_quant::kDirectionKind))
        return kind_title();                              // another kind's panel (krx-quant #182)
    if (live_->pinned().isValid() && !symbol.isEmpty()) { // pinned: its own time
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
    panel_title->setText(tr("%1 · %2").arg(symbol, krx_window(latest.bar_close, krx_quant::scoring_at(latest))));
}

} // namespace fincept::screens
