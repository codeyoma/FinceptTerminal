#include "screens/krx_quant/KrxQuantScreen.h"

#include "core/config/AppConfig.h"
#include "screens/krx_quant/KrxChart.h"
#include "ui/notifications/NotificationService.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace {

constexpr int kTickMs = 5000;
// With the stream connected the API is read less often: it only catches what the stream missed.
constexpr int kRefreshEvery = 6; ///< ticks between full reads (30 s)
constexpr int kChartEvery = 12;  ///< 60 s
constexpr int kMacroEvery = 60;  ///< 5 min
constexpr int kBoardDelayMs = 3000;

constexpr auto kAlertOnKey = "krx_quant/signal_on";
constexpr auto kAlertProbabilityKey = "krx_quant/signal_probability";
constexpr auto kAlertAgreeKey = "krx_quant/signal_agree";
constexpr auto kAlertSoundKey = "krx_quant/signal_sound";
constexpr auto kChartMinutesKey = "krx_quant/chart_minutes";

enum SymbolColumn { kSymbol, kMid, kFinal, kScoring, kSymbolColumns };
enum PredictionColumn { kPredictor, kDirection, kUp, kFlat, kDown, kTicks, kModel, kPredictionColumns };
enum BoardColumn {
    kRank,
    kName,
    kStatus,
    kModels,
    kN,
    kAccuracy,
    kPrecision,
    kCoverage,
    kBrier,
    kPerTrade,
    kLift,
    kSignificant,
    kBoardColumns
};

const QTimeZone& kst() {
    static const QTimeZone zone("Asia/Seoul");
    return zone;
}

/// Times in KST, the exchange's and the engine's.
QString kst_time(const QDateTime& t, const char* format = "HH:mm:ss") {
    return t.isValid() ? t.toTimeZone(kst()).toString(format) : QStringLiteral("—");
}

QString krx_percent(const std::optional<double>& p) {
    return p ? QString::number(std::lround(*p * 100.0)) + "%" : QString();
}

QString krx_number(double value, int decimals) {
    static const QLocale locale(QLocale::Korean, QLocale::SouthKorea);
    return locale.toString(value, 'f', decimals);
}

QString krx_price(const std::optional<double>& price) {
    if (!price)
        return QStringLiteral("—");
    return krx_number(*price, std::floor(*price) == *price ? 0 : 1);
}

/// "3:12" to go, from seconds.
QString krx_countdown(qint64 seconds) {
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QTableWidgetItem* krx_cell(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(align);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

QTableWidget* krx_table(const QStringList& headers) {
    auto* table = new QTableWidget(0, static_cast<int>(headers.size()));
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
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
    const auto dir = krx_quant::direction(p);
    auto* cell = krx_cell(p.status == "ok" ? krx_quant::direction_label(dir) : krx_quant::status_label(p.status));
    cell->setForeground(direction_color(dir));
    return cell;
}

const krx_quant::Prediction* final_of(const krx_quant::Latest& latest) {
    for (const auto& p : latest.predictions)
        if (p.algorithm == "consensus-final")
            return &p;
    return nullptr;
}

/// A rate as a percent, its interval in the tooltip.
QTableWidgetItem* rate_cell(const std::optional<krx_quant::Interval>& rate) {
    const auto numbers = Qt::AlignRight | Qt::AlignVCenter;
    if (!rate)
        return krx_cell(QStringLiteral("—"), numbers);
    auto* cell = krx_cell(krx_number(rate->value * 100, 1) + "%", numbers);
    if (rate->low && rate->high)
        cell->setToolTip(QStringLiteral("95%% 구간 %1–%2%% (n %3)")
                             .arg(krx_number(*rate->low * 100, 1), krx_number(*rate->high * 100, 1))
                             .arg(rate->n));
    return cell;
}

/// A mean (Brier, ticks a trade), its interval in the tooltip.
QTableWidgetItem* mean_cell(const std::optional<krx_quant::Interval>& mean, int decimals) {
    const auto numbers = Qt::AlignRight | Qt::AlignVCenter;
    if (!mean)
        return krx_cell(QStringLiteral("—"), numbers);
    auto* cell = krx_cell(krx_number(mean->value, decimals), numbers);
    if (mean->low && mean->high)
        cell->setToolTip(QStringLiteral("95%% 구간 %1 – %2 (n %3)")
                             .arg(krx_number(*mean->low, decimals), krx_number(*mean->high, decimals))
                             .arg(mean->n));
    return cell;
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
    symbols_table_ = krx_table({tr("종목"), tr("기준가"), tr("최종 합의"), tr("채점까지")});
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
        krx_table({tr("예측"), tr("방향"), tr("상승"), tr("보합"), tr("하락"), tr("기대 틱"), tr("모델")});
    panel_layout->addWidget(predictions_table_, 1);
    upper->addWidget(panel);
    upper->setStretchFactor(0, 2);
    upper->setStretchFactor(1, 5);
    upper->setStretchFactor(2, 4);

    // Bottom: the leaderboard and the signal alerts.
    auto* lower = new QSplitter(Qt::Horizontal);
    auto* board_box = new QGroupBox(tr("순위표"));
    build_leaderboard(board_box);
    lower->addWidget(board_box);
    auto* alerts_box = new QGroupBox(tr("신호 알림"));
    build_alerts(alerts_box);
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

    service_ = new services::KrxQuantService(this);
    connect(service_, &services::KrxQuantService::snapshot_ready, this, &KrxQuantScreen::show_snapshot);
    connect(service_, &services::KrxQuantService::refresh_failed, this, &KrxQuantScreen::show_failure);
    connect(service_, &services::KrxQuantService::status_ready, this, &KrxQuantScreen::show_status);
    connect(service_, &services::KrxQuantService::macro_ready, this, &KrxQuantScreen::show_macro);
    connect(service_, &services::KrxQuantService::macro_failed, this, [this](const QString& reason) {
        macro_label_->setText(tr("매크로: %1").arg(reason));
        macro_label_->show();
    });
    connect(service_, &services::KrxQuantService::chart_ready, this, &KrxQuantScreen::show_chart);
    connect(service_, &services::KrxQuantService::chart_failed, this,
            [this](const QString& symbol, const QString& why) {
                if (symbol == selected_)
                    chart_->set_empty_text(tr("1분봉을 읽지 못했습니다: %1").arg(why));
            });
    connect(service_, &services::KrxQuantService::leaderboard_ready, this, &KrxQuantScreen::show_leaderboard);
    connect(service_, &services::KrxQuantService::leaderboard_failed, this, [this](const QString& reason) {
        board_table_->setRowCount(0);
        board_note_->setText(tr("순위표를 읽지 못했습니다: %1").arg(reason));
    });
    connect(service_, &services::KrxQuantService::base_url_changed, this, [this](const QString& url) {
        if (!url_edit_->hasFocus()) // not while the address is being typed here
            url_edit_->setText(url);
        stream_->open(url); // another KRX panel applied an address
    });

    stream_ = new services::KrxStream(this);
    connect(stream_, &services::KrxStream::connected, this, &KrxQuantScreen::on_stream_connected);
    connect(stream_, &services::KrxStream::disconnected, this, &KrxQuantScreen::on_stream_lost);
    connect(stream_, &services::KrxStream::event_received, this, &KrxQuantScreen::on_event);

    connect(apply, &QPushButton::clicked, this, &KrxQuantScreen::apply_base_url);
    connect(url_edit_, &QLineEdit::returnPressed, this, &KrxQuantScreen::apply_base_url);
    connect(symbols_table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto rows = symbols_table_->selectionModel()->selectedRows();
        if (!rows.isEmpty())
            select(symbols_table_->item(rows.first().row(), kSymbol)->text());
    });
    connect(window_combo_, &QComboBox::currentIndexChanged, this, [this]() {
        const int minutes = window_combo_->currentData().toInt();
        AppConfig::instance().set(kChartMinutesKey, minutes);
        chart_->set_window(minutes);
    });

    timer_ = new QTimer(this);
    timer_->setInterval(kTickMs);
    connect(timer_, &QTimer::timeout, this, &KrxQuantScreen::tick);
    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, &KrxQuantScreen::update_countdowns);
    board_soon_ = new QTimer(this);
    board_soon_->setSingleShot(true);
    board_soon_->setInterval(kBoardDelayMs);
    connect(board_soon_, &QTimer::timeout, this, &KrxQuantScreen::read_leaderboard);
}

void KrxQuantScreen::build_leaderboard(QWidget* box) {
    auto* layout = new QVBoxLayout(box);
    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("기간")));
    period_combo_ = new QComboBox;
    period_combo_->addItem(tr("오늘"), "today");
    period_combo_->addItem(tr("5일"), "5d");
    period_combo_->addItem(tr("20일"), "20d");
    controls->addWidget(period_combo_);
    controls->addWidget(new QLabel(tr("채점 기준")));
    basis_combo_ = new QComboBox;
    basis_combo_->addItem(tr("표시 시점 (B)"), "b");
    basis_combo_->addItem(tr("봉 마감 (A)"), "a");
    basis_combo_->setToolTip(
        tr("B: 예측이 화면에 뜬 시각의 중간가부터, A: 봉 마감의 중간가부터 4분. 차트의 ✓/✗도 이 기준이다."));
    controls->addWidget(basis_combo_);
    controls->addWidget(new QLabel(tr("종목")));
    board_symbol_combo_ = new QComboBox;
    board_symbol_combo_->addItem(tr("전체"), "all");
    controls->addWidget(board_symbol_combo_);
    special_check_ = new QCheckBox(tr("특수 상황 포함"));
    special_check_->setToolTip(tr("VI·서킷브레이커·사이드카가 4분 구간에 낀 예측도 센다(기본은 뺀다)."));
    controls->addWidget(special_check_);
    controls->addStretch();
    layout->addLayout(controls);
    board_table_ =
        krx_table({tr("순위"), tr("예측"), tr("상태"), tr("모델"), tr("표본"), tr("정답률"), tr("방향 정밀도"),
                   tr("커버리지"), tr("Brier"), tr("거래당 손익(틱)"), tr("가장 약한 lift 하한"), tr("운 보정")});
    board_table_->horizontalHeader()->setStretchLastSection(false);
    board_table_->horizontalHeader()->setSectionResizeMode(kModels, QHeaderView::Stretch);
    layout->addWidget(board_table_, 1);
    board_note_ = new QLabel;
    board_note_->setObjectName("krxNote");
    board_note_->setWordWrap(true);
    layout->addWidget(board_note_);

    for (auto* combo : {period_combo_, board_symbol_combo_})
        connect(combo, &QComboBox::currentIndexChanged, this, &KrxQuantScreen::read_leaderboard);
    connect(basis_combo_, &QComboBox::currentIndexChanged, this, [this]() {
        fill_chart(); // the chart's ✓/✗ are on the same basis
        read_leaderboard();
    });
    connect(special_check_, &QCheckBox::toggled, this, &KrxQuantScreen::read_leaderboard);
}

void KrxQuantScreen::build_alerts(QWidget* box) {
    auto* layout = new QVBoxLayout(box);
    alert_check_ = new QCheckBox(tr("신호 알림 켜기"));
    alert_check_->setChecked(AppConfig::instance().get(kAlertOnKey, true).toBool());
    layout->addWidget(alert_check_);
    auto* probability = new QHBoxLayout;
    probability->addWidget(new QLabel(tr("최종 합의 확률")));
    probability_spin_ = new QSpinBox;
    probability_spin_->setRange(50, 99);
    probability_spin_->setSuffix("%");
    probability_spin_->setValue(AppConfig::instance().get(kAlertProbabilityKey, 60).toInt());
    probability->addWidget(probability_spin_);
    probability->addWidget(new QLabel(tr("이상")));
    probability->addStretch();
    layout->addLayout(probability);
    auto* agree = new QHBoxLayout;
    agree->addWidget(new QLabel(tr("같은 방향 정식 알고리즘")));
    agree_spin_ = new QSpinBox;
    agree_spin_->setRange(1, 20);
    agree_spin_->setSuffix(tr("개"));
    agree_spin_->setValue(AppConfig::instance().get(kAlertAgreeKey, 2).toInt());
    agree->addWidget(agree_spin_);
    agree->addWidget(new QLabel(tr("이상")));
    agree->addStretch();
    layout->addLayout(agree);
    sound_check_ = new QCheckBox(tr("소리"));
    sound_check_->setChecked(AppConfig::instance().get(kAlertSoundKey, true).toBool());
    layout->addWidget(sound_check_);
    auto* rule = new QLabel(tr("같은 종목·같은 방향은 4분(채점 시각)까지 다시 알리지 않는다. "
                               "알림은 Fincept 알림(앱 안 알림과 OS 알림)으로 나간다."));
    rule->setObjectName("krxNote");
    rule->setWordWrap(true);
    layout->addWidget(rule);
    last_signal_label_ = new QLabel(tr("아직 신호가 없습니다"));
    last_signal_label_->setWordWrap(true);
    layout->addWidget(last_signal_label_);
    auto* test = new QPushButton(tr("시험 알림"));
    test->setToolTip(tr("OS 알림 권한과 소리를 확인한다. 신호는 이렇게 온다."));
    connect(test, &QPushButton::clicked, this,
            [this]() { notify(tr("KRX 신호 시험 · 이 알림과 소리가 나면 신호도 이렇게 온다")); });
    layout->addWidget(test, 0, Qt::AlignLeft);
    layout->addStretch();

    alerts_.set_rule({probability_spin_->value() / 100.0, agree_spin_->value()});
    connect(alert_check_, &QCheckBox::toggled, this, &KrxQuantScreen::save_alert_settings);
    connect(probability_spin_, &QSpinBox::valueChanged, this, &KrxQuantScreen::save_alert_settings);
    connect(agree_spin_, &QSpinBox::valueChanged, this, &KrxQuantScreen::save_alert_settings);
    connect(sound_check_, &QCheckBox::toggled, this, &KrxQuantScreen::save_alert_settings);
}

void KrxQuantScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    service_->refresh();
    service_->read_macro();
    read_leaderboard();
    if (!selected_.isEmpty())
        service_->read_chart(selected_);
    if (!stream_->is_connected())
        stream_->open(services::KrxQuantService::base_url());
    fill_symbols();
    fill_predictions();
    timer_->start();
    clock_timer_->start();
}

void KrxQuantScreen::hideEvent(QHideEvent* event) {
    // The stream stays open and the timer keeps going: signal alerts come while another
    // screen is shown. Only what the views need is not read meanwhile (see tick).
    clock_timer_->stop();
    QWidget::hideEvent(event);
}

void KrxQuantScreen::apply_base_url() {
    services::KrxQuantService::set_base_url(url_edit_->text());
    url_edit_->setText(services::KrxQuantService::base_url());
    status_label_->setText(tr("엔진을 읽는 중…"));
    board_ = {};
    bars_.clear();
    selected_.clear();
    service_->restart();
    stream_->open(services::KrxQuantService::base_url());
    service_->read_macro();
    read_leaderboard();
}

void KrxQuantScreen::tick() {
    ++ticks_;
    const bool live = stream_->is_connected();
    if (!isVisible()) {
        // Only for the signal alerts: without the stream, the predictions are read here.
        if (alert_check_->isChecked() && !live)
            service_->refresh();
        return;
    }
    if (!live || ticks_ % kRefreshEvery == 0)
        service_->refresh();
    else
        service_->read_status();
    if (ticks_ % kChartEvery == 0) {
        if (!selected_.isEmpty())
            service_->read_chart(selected_);
        read_leaderboard();
    }
    if (ticks_ % kMacroEvery == 0)
        service_->read_macro();
}

void KrxQuantScreen::show_snapshot(const services::KrxQuantService::Snapshot& snapshot) {
    const QStringList before = [this]() {
        QStringList symbols;
        for (const auto& q : board_.symbols())
            symbols << q.symbol;
        return symbols;
    }();
    board_.set_symbols(snapshot.symbols);
    if (snapshot.members)
        board_.set_members(*snapshot.members);
    const QDateTime shown = board_.latest(selected_).bar_close;
    for (const auto& latest : snapshot.latest)
        board_.set_latest(latest);
    for (const auto& latest : snapshot.latest)
        if (const auto* final = final_of(board_.latest(latest.symbol)))
            consider(*final, snapshot.status.now);

    QStringList symbols;
    for (const auto& q : snapshot.symbols)
        symbols << q.symbol;
    if (symbols != before) { // the leaderboard's symbol choices follow the engine's symbols
        const QString chosen = board_symbol_combo_->currentData().toString();
        const QSignalBlocker quiet(board_symbol_combo_);
        board_symbol_combo_->clear();
        board_symbol_combo_->addItem(tr("전체"), "all");
        for (const auto& s : symbols)
            board_symbol_combo_->addItem(s, s);
        board_symbol_combo_->setCurrentIndex(std::max(0, board_symbol_combo_->findData(chosen)));
    }
    if (!symbols.contains(selected_) && !symbols.isEmpty())
        select(symbols.first());
    else if (board_.latest(selected_).bar_close != shown)
        service_->read_chart(selected_); // a new bar close: a new bar

    show_status(snapshot.status);
    fill_symbols();
    fill_predictions();
    fill_chart();
    read_errors_ = snapshot.errors;
    show_footer();
}

void KrxQuantScreen::set_engine_clock(const QDateTime& now) {
    if (!now.isValid())
        return;
    clock_offset_ms_ = now.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch();
    show_footer();
}

void KrxQuantScreen::show_footer() {
    QString footer = tr("갱신 %1 (엔진 시계 %2)").arg(kst_time(QDateTime::currentDateTime()), kst_time(engine_now()));
    if (!read_errors_.isEmpty())
        footer += "  ·  " + read_errors_.join("; ");
    footer_label_->setText(footer);
}

void KrxQuantScreen::show_failure(const QString& reason) {
    status_ = std::nullopt;
    failure_ = reason;
    show_status_line();
    special_label_->hide();
    // Old predictions must not pass for current ones.
    board_ = {};
    bars_.clear();
    symbols_table_->setRowCount(0);
    predictions_table_->setRowCount(0);
    panel_title_->clear();
    chart_title_->clear();
    chart_->set_data({}, {});
    chart_->set_empty_text(QString());
    footer_label_->setText(
        tr("실패 %1 · %2초 뒤 다시 읽습니다").arg(kst_time(QDateTime::currentDateTime())).arg(kTickMs / 1000));
}

void KrxQuantScreen::show_status(const krx_quant::EngineStatus& status) {
    status_ = status;
    if (!failure_.isEmpty()) {
        // Back after a failure: what was not read meanwhile is read again.
        failure_.clear();
        service_->read_macro();
        read_leaderboard();
        if (!selected_.isEmpty())
            service_->read_chart(selected_);
    }
    set_engine_clock(status.now);
    show_status_line();

    QStringList windows;
    for (const auto& w : status.special) {
        const QString who = w.symbol.isEmpty() ? tr("시장 전체") : w.symbol;
        windows
            << QString("%1 %2 %3–%4").arg(who, krx_quant::special_label(w.kind), kst_time(w.start), kst_time(w.end));
    }
    special_label_->setText(tr("오늘 특수 상황: %1").arg(windows.join(", ")));
    special_label_->setVisible(!windows.isEmpty());
}

void KrxQuantScreen::show_status_line() {
    QStringList parts;
    if (!failure_.isEmpty())
        parts << failure_;
    if (status_) {
        const auto& s = *status_;
        if (s.mode == "demo")
            parts << tr("데모 데이터");
        parts << krx_quant::phase_label(s.phase);
        parts << tr("마지막 봉 마감 %1").arg(kst_time(s.last_bar_close));
        parts << tr("채점 대기 %1건").arg(s.pending_outcomes);
        if (s.kis_connected) {
            QString kis = *s.kis_connected ? tr("KIS 연결됨") : tr("KIS 끊김");
            double delay = -1;
            QStringList gaps;
            for (const auto& f : s.feeds) {
                if (!f.gap.isEmpty())
                    gaps << QString("%1 %2 (%3)").arg(f.feed, f.key, f.gap);
                else if (f.seconds_since)
                    delay = std::max(delay, *f.seconds_since);
            }
            QStringList detail;
            if (delay >= 0)
                detail << tr("지연 최대 %1초").arg(krx_number(delay, 0));
            if (!gaps.isEmpty())
                detail << tr("결측 피드 %1개").arg(gaps.size());
            if (!detail.isEmpty())
                kis += QStringLiteral(" (%1)").arg(detail.join(", "));
            parts << kis;
            status_label_->setToolTip(gaps.isEmpty() ? QString() : tr("결측 피드: %1").arg(gaps.join(", ")));
        }
        if (s.training) {
            const QStringList promoted = s.training->promoted.keys();
            const QString what = promoted.isEmpty() ? tr("승격 없음") : tr("%1 승격").arg(promoted.join(", "));
            parts << tr("지난밤 학습 %1: %2, %3")
                         .arg(s.training->day, what, s.training->applied ? tr("적용됨") : tr("미적용"));
        }
    }
    if (!stream_text_.isEmpty())
        parts << stream_text_;
    status_label_->setText(parts.join("  ·  "));
}

void KrxQuantScreen::show_macro(const krx_quant::Macro& macro) {
    if (macro.day.isEmpty()) {
        macro_label_->setText(tr("매크로: 아직 정한 국면이 없습니다"));
        macro_label_->show();
        return;
    }
    QStringList parts;
    QString regime = tr("매크로 %1 %2").arg(macro.day, krx_quant::regime_label(macro.regime));
    if (status_ && status_->now.isValid() && macro.day != status_->now.toTimeZone(kst()).toString("yyyy-MM-dd"))
        regime += tr(" (오늘 것이 아님)");
    parts << regime;
    if (!macro.reasons.isEmpty())
        parts << macro.reasons.join(", ");
    QStringList all;
    for (const auto& r : macro.readings) {
        const QString value = r.value ? krx_number(*r.value, std::abs(*r.value) >= 100 ? 1 : 2) : tr("모름");
        const QString change =
            r.change_pct
                ? QStringLiteral(" (%1%2%)").arg(*r.change_pct >= 0 ? "+" : "").arg(krx_number(*r.change_pct, 2))
                : QString();
        const QString line = QStringLiteral("%1 %2%3").arg(r.label, value, change);
        all << QStringLiteral("%1 · %2").arg(line, r.as_of.isEmpty() ? tr("날짜 모름") : r.as_of);
        if (r.indicator == "usdf")
            parts << line; // the USD futures, on the row itself
    }
    for (auto it = macro.failed.begin(); it != macro.failed.end(); ++it)
        all << tr("%1 읽기 실패: %2").arg(it.key(), it.value());
    if (!macro.skipped.isEmpty())
        all << tr("키 없어 건너뜀: %1").arg(macro.skipped.join(", "));
    macro_label_->setText(parts.join("  ·  "));
    macro_label_->setToolTip(tr("국면을 정한 %1의 지표\n%2").arg(kst_time(macro.decided_at, "HH:mm"), all.join("\n")));
    macro_label_->show();
}

void KrxQuantScreen::show_chart(const QString& symbol, const QList<krx_quant::Bar>& bars,
                                const QList<krx_quant::Prediction>& finals) {
    if (symbol != selected_) { // chosen meanwhile: read the one shown now
        if (!selected_.isEmpty())
            service_->read_chart(selected_);
        return;
    }
    bars_ = bars;
    board_.set_history(symbol, finals);
    chart_->set_empty_text(QString());
    fill_chart();
}

void KrxQuantScreen::show_leaderboard(const krx_quant::Leaderboard& board) {
    const auto query = leaderboard_query();
    if (board.symbol != query.symbol || board.period != query.period || board.basis != query.basis ||
        board.special != (query.special ? "include" : "exclude")) {
        read_leaderboard(); // changed meanwhile
        return;
    }
    const auto numbers = Qt::AlignRight | Qt::AlignVCenter;
    board_table_->setRowCount(0);
    const auto add = [&](const krx_quant::Entry& e, bool baseline) {
        const int row = board_table_->rowCount();
        board_table_->insertRow(row);
        board_table_->setItem(row, kRank, krx_cell(e.rank ? QString::number(*e.rank) : QStringLiteral("—"), numbers));
        board_table_->setItem(row, kName, krx_cell(krx_quant::predictor_label(e.algorithm)));
        board_table_->setItem(row, kStatus, krx_cell(krx_quant::standing_label(e.status)));
        auto* models = krx_cell(e.model_versions.join(", "));
        models->setToolTip(e.model_versions.join("\n"));
        board_table_->setItem(row, kModels, models);
        board_table_->setItem(row, kN, krx_cell(krx_number(e.n, 0), numbers));
        board_table_->setItem(row, kAccuracy, rate_cell(e.accuracy));
        board_table_->setItem(row, kPrecision, rate_cell(e.precision));
        board_table_->setItem(row, kCoverage, rate_cell(e.coverage));
        board_table_->setItem(row, kBrier, mean_cell(e.brier, 3));
        auto* pnl = mean_cell(e.per_trade, 2);
        pnl->setToolTip(tr("거래 %1건, 합계 %2틱").arg(e.trades).arg(krx_number(e.total_ticks, 1)) +
                        (pnl->toolTip().isEmpty() ? QString() : "\n" + pnl->toolTip()));
        board_table_->setItem(row, kPerTrade, pnl);
        board_table_->setItem(
            row, kLift,
            krx_cell(e.lift_low ? QStringLiteral("%1%2").arg(*e.lift_low > 0 ? "+" : "").arg(krx_number(*e.lift_low, 3))
                                : QStringLiteral("—"),
                     numbers));
        board_table_->setItem(row, kSignificant, krx_cell(e.significant ? tr("통과") : QString()));
        if (baseline || krx_quant::is_consensus(e.algorithm))
            for (int c = 0; c < kBoardColumns; ++c)
                board_table_->item(row, c)->setForeground(
                    QColor(baseline ? ui::colors::TEXT_DIM() : ui::colors::AMBER()));
    };
    for (const auto& e : board.ranked)
        add(e, false);
    for (const auto& e : board.unranked)
        add(e, false);
    for (const auto& e : board.baselines)
        add(e, true);
    const QString days =
        board.days.isEmpty() ? tr("예측이 있는 날 없음")
        : board.days.size() == 1
            ? board.days.first()
            : QStringLiteral("%1 ~ %2 (%3일)").arg(board.days.first(), board.days.last()).arg(board.days.size());
    board_note_->setText(tr("%1 · 순위에는 채점 %2건 이상, 기준 예측과의 짝도 그만큼 있어야 오른다. "
                            "순위는 가장 약한 lift의 하한 순이고, 운 보정 통과는 기준 예측 셋을 모두 이긴 것이 "
                            "운으로 보기 어렵다는 뜻이다(q ≤ 0.05).")
                             .arg(days)
                             .arg(board.min_sample));
}

void KrxQuantScreen::on_event(const krx_quant::Event& event) {
    switch (event.type) {
        case krx_quant::Event::Type::Health:
            if (status_) {
                status_->kis_connected = event.kis_connected;
                status_->feeds = event.feeds;
                show_status_line();
            }
            return;
        case krx_quant::Event::Type::Lifecycle:
            board_.apply(event);
            board_soon_->start(); // its status in the leaderboard
            return;
        case krx_quant::Event::Type::Other:
            return;
        case krx_quant::Event::Type::Prediction:
        case krx_quant::Event::Type::Consensus:
        case krx_quant::Event::Type::Outcome:
            break;
    }
    set_engine_clock(event.at);
    const QDateTime shown = board_.latest(selected_).bar_close;
    for (const auto& final : board_.apply(event))
        consider(final, event.at);
    if (event.type == krx_quant::Event::Type::Outcome)
        board_soon_->start(); // outcomes come a bar close at a time: one read after them
    if (!isVisible())
        return;
    if (!selected_.isEmpty() && board_.latest(selected_).bar_close != shown)
        service_->read_chart(selected_); // a new bar close: a new bar
    fill_symbols();
    fill_predictions();
    fill_chart();
}

void KrxQuantScreen::on_stream_connected() {
    stream_text_ = tr("스트림 연결됨");
    show_status_line();
    // The stream sends what happens from now on: what happened before is read again.
    service_->refresh();
    if (!selected_.isEmpty())
        service_->read_chart(selected_);
    read_leaderboard();
}

void KrxQuantScreen::on_stream_lost(const QString& reason, int retry_in) {
    stream_text_ = retry_in < 0 ? reason : tr("스트림 끊김: %1 · %2초 뒤 다시 연결").arg(reason).arg(retry_in);
    show_status_line();
}

void KrxQuantScreen::consider(const krx_quant::Prediction& final, const QDateTime& now) {
    if (!alert_check_->isChecked())
        return;
    if (const auto signal = alerts_.consider(final, board_.pooled(final.symbol, final.bar_close), now))
        raise(*signal);
}

void KrxQuantScreen::raise(const krx_quant::Signal& signal) {
    const QString text = signal.text();
    last_signal_label_->setText(tr("마지막 신호 (%1): %2").arg(kst_time(QDateTime::currentDateTime()), text));
    notify(tr("KRX 신호 · %1").arg(text));
}

void KrxQuantScreen::notify(const QString& text) {
    // Fincept's notifications: an in-app toast and the OS's (DesktopNotifier mirrors every toast).
    ui::ToastService::instance().post(ui::ToastService::Severity::Warning, text, QStringLiteral("krx_quant"));
    if (sound_check_->isChecked())
        QApplication::beep();
}

void KrxQuantScreen::save_alert_settings() {
    AppConfig::instance().set(kAlertOnKey, alert_check_->isChecked());
    AppConfig::instance().set(kAlertProbabilityKey, probability_spin_->value());
    AppConfig::instance().set(kAlertAgreeKey, agree_spin_->value());
    AppConfig::instance().set(kAlertSoundKey, sound_check_->isChecked());
    alerts_.set_rule({probability_spin_->value() / 100.0, agree_spin_->value()});
}

void KrxQuantScreen::select(const QString& symbol) {
    if (symbol == selected_)
        return;
    selected_ = symbol;
    bars_.clear();
    chart_->set_data({}, {});
    chart_->set_empty_text(tr("1분봉을 읽는 중…"));
    service_->read_chart(symbol);
    fill_predictions();
    fill_symbols();
}

void KrxQuantScreen::fill_symbols() {
    const auto numbers = Qt::AlignRight | Qt::AlignVCenter;
    const QSignalBlocker quiet(symbols_table_); // a refill is not a choice
    symbols_table_->setRowCount(0);
    for (const auto& quote : board_.symbols()) {
        const int row = symbols_table_->rowCount();
        symbols_table_->insertRow(row);
        symbols_table_->setItem(row, kSymbol, krx_cell(quote.symbol));
        symbols_table_->setItem(row, kMid, krx_cell(krx_price(quote.mid), numbers));
        const krx_quant::Latest latest = board_.latest(quote.symbol);
        const krx_quant::Prediction* final = final_of(latest);
        if (!final) {
            symbols_table_->setItem(row, kFinal, krx_cell(latest.predictions.isEmpty() ? tr("예측 없음") : QString()));
        } else {
            auto* cell = direction_cell(*final);
            if (final->status == "ok") {
                // Leaning neither way: up and down show as the same percent.
                const QString which = cell->text().isEmpty() ? tr("상승=하락") : cell->text();
                cell->setText(QStringLiteral("%1 %2").arg(
                    which, krx_percent(std::max({final->p_up, final->p_flat, final->p_down}))));
            }
            cell->setToolTip(tr("%1 봉 마감").arg(kst_time(latest.bar_close, "HH:mm")));
            symbols_table_->setItem(row, kFinal, cell);
        }
        symbols_table_->setItem(row, kScoring, krx_cell(QString(), numbers));
        if (quote.symbol == selected_)
            symbols_table_->selectRow(row);
    }
    update_countdowns();
}

void KrxQuantScreen::fill_predictions() {
    const auto numbers = Qt::AlignRight | Qt::AlignVCenter;
    predictions_table_->setRowCount(0);
    const krx_quant::Latest latest = board_.latest(selected_);
    for (const auto& p : latest.predictions) {
        const int row = predictions_table_->rowCount();
        predictions_table_->insertRow(row);
        predictions_table_->setItem(row, kPredictor, krx_cell(krx_quant::predictor_label(p.algorithm)));
        predictions_table_->setItem(row, kDirection, direction_cell(p));
        predictions_table_->setItem(row, kUp, krx_cell(krx_percent(p.p_up), numbers));
        predictions_table_->setItem(row, kFlat, krx_cell(krx_percent(p.p_flat), numbers));
        predictions_table_->setItem(row, kDown, krx_cell(krx_percent(p.p_down), numbers));
        const QString ticks = p.expected_ticks ? QString::number(*p.expected_ticks, 'f', 1) : QString();
        predictions_table_->setItem(row, kTicks, krx_cell(ticks, numbers));
        predictions_table_->setItem(row, kModel, krx_cell(p.model_version));
        const bool member = board_.members().contains(p.algorithm);
        if (krx_quant::is_consensus(p.algorithm) || member) {
            for (int c = 0; c < kPredictionColumns; ++c) {
                QFont font = predictions_table_->item(row, c)->font();
                font.setBold(krx_quant::is_consensus(p.algorithm));
                predictions_table_->item(row, c)->setFont(font);
            }
            if (member)
                predictions_table_->item(row, kPredictor)->setToolTip(tr("정식: 합의에 들어간다"));
        }
    }
    update_countdowns();
}

void KrxQuantScreen::fill_chart() {
    if (selected_.isEmpty()) {
        chart_title_->clear();
        chart_->set_data({}, {});
        return;
    }
    chart_title_->setText(tr("%1 · 1분봉과 최종 합의").arg(selected_));
    chart_->set_data(bars_, board_.marks(selected_, basis()));
}

void KrxQuantScreen::update_countdowns() {
    const QDateTime now = engine_now();
    // Seconds to the scoring of a symbol's latest bar close; none if nothing was predicted.
    const auto to_go = [&now](const krx_quant::Latest& latest) -> std::optional<qint64> {
        const QDateTime at = krx_quant::scoring_at(latest);
        if (!at.isValid())
            return std::nullopt;
        return std::max<qint64>(0, now.secsTo(at));
    };
    for (int row = 0; row < symbols_table_->rowCount(); ++row) {
        auto* item = symbols_table_->item(row, kScoring);
        if (!item)
            continue;
        const auto left = to_go(board_.latest(symbols_table_->item(row, kSymbol)->text()));
        item->setText(!left ? QString() : *left > 0 ? krx_countdown(*left) : tr("채점됨"));
    }
    if (selected_.isEmpty()) {
        panel_title_->clear();
        return;
    }
    const krx_quant::Latest latest = board_.latest(selected_);
    if (!latest.bar_close.isValid()) {
        panel_title_->setText(tr("%1 · 예측 없음").arg(selected_));
        return;
    }
    QString title = tr("%1 · %2 봉 마감").arg(selected_, kst_time(latest.bar_close, "HH:mm"));
    if (const auto left = to_go(latest))
        title += *left > 0 ? tr(" · 채점까지 %1").arg(krx_countdown(*left))
                           : tr(" · %1에 채점").arg(kst_time(krx_quant::scoring_at(latest), "HH:mm"));
    panel_title_->setText(title);
}

void KrxQuantScreen::read_leaderboard() {
    service_->read_leaderboard(leaderboard_query());
}

services::KrxQuantService::LeaderboardQuery KrxQuantScreen::leaderboard_query() const {
    return {board_symbol_combo_->currentData().toString(), period_combo_->currentData().toString(), basis(),
            special_check_->isChecked()};
}

QString KrxQuantScreen::basis() const {
    return basis_combo_->currentData().toString();
}

QDateTime KrxQuantScreen::engine_now() const {
    return QDateTime::currentDateTime().addMSecs(clock_offset_ms_);
}

} // namespace fincept::screens
