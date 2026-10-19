#include "screens/krx_quant/dashboard/KrxDashboardWidgets.h"

#include "screens/krx_quant/KrxAlerts.h"
#include "screens/krx_quant/KrxSymbolCell.h"
#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/DashboardView.h"
#include "services/krx_quant/KindView.h"
#include "services/krx_quant/Signals.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>

#include <functional>

namespace fincept::screens::krx_dashboard {

namespace {

using krx_quant::Direction;
using krx_tables::cell;
using krx_tables::kNumber;

/// The final consensus's bar closes the signals widget reads back for the latest scored one: it is
/// scored four minutes on, so the fifth bar close back at the latest.
constexpr int kRecentCloses = 6;

QString direction_color(Direction d) {
    if (d == Direction::Up)
        return ui::colors::POSITIVE();
    if (d == Direction::Down)
        return ui::colors::NEGATIVE();
    return ui::colors::TEXT_PRIMARY();
}

void paint(QTableWidgetItem* item, const QString& color, bool bold = false) {
    item->setForeground(QColor(color));
    if (bold) {
        QFont font = item->font();
        font.setBold(true);
        item->setFont(font);
    }
}

/// The symbol as the KRX screen's symbols panel shows it (krx-quant #250): the round logo, the name
/// and the code after it, dimmed (``table`` draws its first column with KrxSymbolDelegate).
QTableWidgetItem* symbol_cell(QTableWidget* table, KrxLogos* logos, const QString& symbol, const QString& name,
                              const QString& logo_url) {
    auto* item = cell(name.isEmpty() ? symbol : name);
    if (!name.isEmpty())
        item->setData(KrxSymbolDelegate::kCodeRole, symbol);
    if (!logo_url.isEmpty() || !name.isEmpty()) { // an older engine: the code alone
        const int size = table->fontMetrics().height(); // as high as the name
        table->setIconSize(QSize(size, size));
        item->setIcon(logos->logo(logo_url, name, size, table->devicePixelRatioF()));
    }
    return item;
}

/// Draw the first column as the KRX screen's symbols panel, the logos read by ``logos``.
void show_symbols(QTableWidget* table, KrxLogos* logos, KrxEngineTile* tile, std::function<void()> render) {
    table->setItemDelegateForColumn(0, new KrxSymbolDelegate(table));
    QObject::connect(logos, &KrxLogos::arrived, tile, std::move(render));
}

} // namespace

// ---- Signals --------------------------------------------------------------------------------------

KrxSignalsWidget::KrxSignalsWidget(QWidget* parent) : KrxEngineTile(tr("KRX 지금 신호"), kSignalsEveryMs, parent) {
    table_ = make_table({tr("종목"), tr("기준가"), tr("봉 마감"), tr("상승"), tr("보합"), tr("하락"), tr("정식"),
                         tr("신호"), tr("직전 채점")});
    body()->addWidget(table_);
    logos_ = new KrxLogos(this);
    show_symbols(table_, logos_, this, [this]() { render(); });

    connect(service(), &services::KrxQuantService::snapshot_ready, this,
            [this](const services::KrxQuantService::Snapshot& snapshot) {
                snapshot_ = snapshot;
                render();
                read_done(snapshot.errors.join(QStringLiteral(" · ")));
                // A new bar close: the one scored since is read back for its ✓/✗.
                for (auto it = snapshot.latest.cbegin(); it != snapshot.latest.cend(); ++it)
                    if (recent_for_.value(it.key()) != it->bar_close && it.key() != recent_reading_)
                        queued_for_.insert(it.key(), it->bar_close);
                read_next_recent();
            });
    connect(service(), &services::KrxQuantService::refresh_failed, this,
            [this](const QString& reason) { read_failed(reason); });
    connect(service(), &services::KrxQuantService::predictions_ready, this,
            [this](const QString& symbol, const QDateTime&, const QList<krx_quant::Prediction>& predictions, bool) {
                if (symbol != recent_reading_)
                    return;
                recent_[symbol] = predictions;
                recent_reading_.clear();
                render();
                read_next_recent();
            });
    connect(service(), &services::KrxQuantService::predictions_failed, this,
            [this](const QString& symbol, const QString&) {
                if (symbol != recent_reading_)
                    return;
                recent_for_.remove(symbol); // read again after the next bar close
                recent_reading_.clear();
                read_next_recent();
            });
}

void KrxSignalsWidget::read() {
    service()->refresh();
}

void KrxSignalsWidget::read_next_recent() {
    if (!isVisible()) {
        queued_for_.clear(); // read after a later bar close, once shown again
        return;
    }
    while (recent_reading_.isEmpty() && !queued_for_.isEmpty()) {
        const QString symbol = queued_for_.firstKey();
        const QDateTime after = queued_for_.take(symbol);
        // The final consensus's and "늘 보합"'s of the latest bar closes (not every predictor's).
        if (service()->read_predictions(symbol, QDateTime(), kRecentCloses, false)) {
            recent_reading_ = symbol;
            recent_for_[symbol] = after;
        }
    }
}

void KrxSignalsWidget::render() {
    if (!snapshot_)
        return;
    const auto rule = KrxAlerts::saved_rule();
    const auto rows =
        krx_quant::dashboard::signal_rows(snapshot_->symbols, snapshot_->latest, recent_, rule, snapshot_->status.now);
    table_->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        table_->setItem(r, 0, symbol_cell(table_, logos_, row.symbol, row.name, row.logo_url));
        table_->setItem(r, 1, cell(row.price, kNumber));
        table_->setItem(r, 2, cell(row.window));
        const QList<std::pair<QString, Direction>> classes = {
            {row.up, Direction::Up}, {row.flat, Direction::Flat}, {row.down, Direction::Down}};
        for (int c = 0; c < classes.size(); ++c) {
            auto* item = cell(classes[c].first, kNumber);
            if (row.call == classes[c].second)
                paint(item, direction_color(row.call), true); // the likeliest class
            table_->setItem(r, 3 + c, item);
        }
        table_->setItem(r, 6, cell(row.members));

        auto* signal = cell(row.signal);
        if (row.signal_direction != Direction::None) {
            paint(signal, row.live ? direction_color(row.signal_direction) : ui::colors::TEXT_TERTIARY(), row.live);
            if (!row.live)
                signal->setToolTip(tr("목표 시각이 지났습니다"));
        } else if (!row.signal.isEmpty()) {
            paint(signal, ui::colors::WARNING());
        }
        table_->setItem(r, 7, signal);

        auto* last = cell(row.last);
        if (row.last_hit)
            paint(last, *row.last_hit ? ui::colors::POSITIVE() : ui::colors::NEGATIVE());
        table_->setItem(r, 8, last);
    }
    if (auto* header = table_->horizontalHeaderItem(7))
        header->setToolTip(tr("KRX 화면의 신호 규칙: 최종 합의의 확률 %1% 이상, 정식 알고리즘 %2개 이상 같은 방향. "
                              "이 위젯은 알림을 내지 않습니다.")
                               .arg(qRound(rule.min_probability * 100))
                               .arg(rule.min_agree));
}

// ---- Leaderboard ----------------------------------------------------------------------------------

KrxLeaderboardWidget::KrxLeaderboardWidget(const QJsonObject& config, QWidget* parent)
    : KrxEngineTile(tr("KRX 순위표"), kLeaderboardEveryMs, parent) {
    // The hit rate first: what the user reads a predictor by (codeyoma/krx-quant#302).
    table_ = make_table({tr("순위"), tr("예측"), tr("정답률 (95% 구간)"), tr("거래일 lift 하한"), tr("상태"), tr("표본"),
                         tr("손익"), tr("인버스 손익")});
    if (auto* header = table_->horizontalHeaderItem(3))
        header->setToolTip(tr("모든 기준 예측과의 lift 중 가장 약한 것의 거래일 단위 95% 하한. 상태 판정이 이것으로 "
                              "한다"));
    body()->addWidget(table_);
    set_configurable(true);

    connect(service(), &services::KrxQuantService::leaderboard_ready, this,
            [this](const krx_quant::Leaderboard& board) {
                if (!query_.matches(board))
                    return; // a query changed since
                board_ = board;
                render();
                read_done();
            });
    connect(service(), &services::KrxQuantService::leaderboard_failed, this,
            [this](const QString& reason) { read_failed(reason); });
    // The engine's symbols and kinds, for the config's choices.
    connect(service(), &services::KrxQuantService::snapshot_ready, this,
            [this](const services::KrxQuantService::Snapshot& snapshot) {
                symbols_.clear();
                for (const auto& quote : snapshot.symbols)
                    symbols_ << quote.symbol;
            });
    connect(service(), &services::KrxQuantService::kinds_ready, this,
            [this](const QList<krx_quant::KindInfo>& kinds) {
                kinds_ = kinds;
                set_head(QStringLiteral("%1 · %2").arg(tr("KRX 순위표"), query_title()));
            });

    apply_config(config);
}

QJsonObject KrxLeaderboardWidget::config() const {
    return {{"symbol", query_.symbol}, {"period", query_.period}, {"kind", query_.kind}};
}

void KrxLeaderboardWidget::apply_config(const QJsonObject& config) {
    services::KrxQuantService::LeaderboardQuery query;
    query.symbol = config.value("symbol").toString("all");
    query.period = config.value("period").toString("today");
    if (!QStringList{"today", "5d", "20d"}.contains(query.period))
        query.period = "today";
    query.kind = config.value("kind").toString("direction");
    if (query == query_ && board_)
        return;
    query_ = query;
    board_.reset();
    set_head(QStringLiteral("%1 · %2").arg(tr("KRX 순위표"), query_title()));
    if (isVisible())
        request_refresh();
}

QString KrxLeaderboardWidget::query_title() const {
    const QString period = krx_quant::period_label(query_.period);
    const QString symbol = query_.symbol == "all" ? tr("전체") : query_.symbol;
    QString kind = query_.kind == "direction" ? tr("방향") : query_.kind;
    if (const auto info = krx_quant::kind_named(kinds_, query_.kind))
        kind = info->title;
    return QStringLiteral("%1 · %2 · %3").arg(kind, period, symbol);
}

void KrxLeaderboardWidget::read() {
    if (!kinds_)
        service()->read_kinds(); // once, for the title and the config's choices
    service()->read_leaderboard(query_);
}

void KrxLeaderboardWidget::render() {
    if (!board_)
        return;
    const QString unit = krx_quant::dashboard::pnl_unit(board_->kind);
    table_->setColumnHidden(6, unit.isEmpty());
    table_->setColumnHidden(7, unit.isEmpty());
    if (!unit.isEmpty()) {
        table_->setHorizontalHeaderItem(6, cell(tr("손익 %1/거래 (연구용)").arg(unit)));
        table_->setHorizontalHeaderItem(7, cell(tr("인버스 손익 %1/거래 (연구용)").arg(unit)));
    }
    const auto rows = krx_quant::dashboard::leaderboard_rows(*board_);
    table_->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        const QList<QTableWidgetItem*> items = {cell(row.rank, kNumber),
                                                cell(row.name),
                                                cell(row.accuracy.text, kNumber),
                                                cell(row.day_low, kNumber),
                                                cell(row.status),
                                                cell(row.n, kNumber),
                                                cell(row.pnl, kNumber),
                                                cell(row.pnl_inverse, kNumber)};
        for (int c = 0; c < items.size(); ++c) {
            if (row.baseline)
                paint(items[c], ui::colors::TEXT_TERTIARY());
            table_->setItem(r, c, items[c]);
        }
        if (row.baseline)
            items[1]->setToolTip(tr("이겨야 하는 기준 예측"));
    }
    if (board_->ranked.isEmpty())
        table_->setToolTip(tr("아직 순위에 오른 알고리즘이 없습니다(채점 %1개 이상 필요)").arg(board_->min_sample));
    else
        table_->setToolTip(tr("더블클릭하면 KRX 4분 예측 화면을 엽니다"));
}

QDialog* KrxLeaderboardWidget::make_config_dialog(QWidget* parent) {
    auto* dialog = new QDialog(parent);
    dialog->setWindowTitle(tr("KRX 순위표 설정"));
    auto* form = new QFormLayout(dialog);

    auto* kind = new QComboBox(dialog);
    for (const auto& choice : krx_quant::kind_choices(kinds_))
        if (choice.enabled)
            kind->addItem(choice.title, choice.name);
    if (kind->findData(query_.kind) < 0)
        kind->addItem(query_.kind, query_.kind);
    kind->setCurrentIndex(kind->findData(query_.kind));
    form->addRow(tr("종류"), kind);

    auto* period = new QComboBox(dialog);
    for (const char* p : {"today", "5d", "20d"})
        period->addItem(krx_quant::period_label(p), QString::fromLatin1(p));
    period->setCurrentIndex(period->findData(query_.period));
    form->addRow(tr("기간"), period);

    auto* symbol = new QComboBox(dialog);
    symbol->addItem(tr("전체"), QStringLiteral("all"));
    for (const auto& s : symbols_)
        symbol->addItem(s, s);
    if (symbol->findData(query_.symbol) < 0)
        symbol->addItem(query_.symbol, query_.symbol);
    symbol->setCurrentIndex(symbol->findData(query_.symbol));
    form->addRow(tr("종목"), symbol);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, kind, period, symbol]() {
        apply_config({{"kind", kind->currentData().toString()},
                      {"period", period->currentData().toString()},
                      {"symbol", symbol->currentData().toString()}});
        emit config_changed(config());
        dialog->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return dialog;
}

// ---- Status ---------------------------------------------------------------------------------------

KrxStatusWidget::KrxStatusWidget(QWidget* parent) : KrxEngineTile(tr("KRX 엔진 상태"), kStatusEveryMs, parent) {
    headline_ = new QLabel(this);
    headline_->setContentsMargins(6, 4, 6, 4);
    headline_->setWordWrap(true);
    headline_->hide();
    body()->addWidget(headline_);
    table_ = make_table({tr("항목"), tr("값")});
    table_->horizontalHeader()->hide();
    body()->addWidget(table_);

    connect(service(), &services::KrxQuantService::status_ready, this, [this](const krx_quant::EngineStatus& status) {
        status_ = status;
        render();
        read_done();
    });
    connect(service(), &services::KrxQuantService::refresh_failed, this,
            [this](const QString& reason) { read_failed(reason); });
}

void KrxStatusWidget::read() {
    service()->read_status();
}

void KrxStatusWidget::render() {
    if (!status_)
        return;
    const auto view = krx_quant::dashboard::status_view(*status_);
    headline_->setText(QStringLiteral("⚠ %1").arg(view.headline));
    headline_->setVisible(!view.headline.isEmpty());
    headline_->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;background:transparent;")
                                 .arg(ui::colors::WARNING()));
    table_->setRowCount(static_cast<int>(view.lines.size()));
    for (int r = 0; r < view.lines.size(); ++r) {
        auto* label = cell(view.lines[r].label);
        paint(label, ui::colors::TEXT_SECONDARY());
        auto* value = cell(view.lines[r].value);
        value->setToolTip(view.lines[r].value);
        if (view.lines[r].trouble)
            paint(value, ui::colors::WARNING(), true);
        table_->setItem(r, 0, label);
        table_->setItem(r, 1, value);
    }
}

// ---- Open to close --------------------------------------------------------------------------------

KrxOpenToCloseWidget::KrxOpenToCloseWidget(QWidget* parent)
    : KrxEngineTile(tr("KRX 오늘 시가→종가"), kOpenToCloseEveryMs, parent) {
    missing_label_ = new QLabel(tr("이 엔진에는 시가→종가 예측이 없습니다."), this);
    missing_label_->setContentsMargins(6, 6, 6, 6);
    missing_label_->setWordWrap(true);
    missing_label_->hide();
    body()->addWidget(missing_label_);
    table_ = make_table({tr("종목"), tr("예측 시각"), tr("상승"), tr("보합"), tr("하락"), tr("보합 폭"), tr("결과")});
    body()->addWidget(table_);
    logos_ = new KrxLogos(this);
    show_symbols(table_, logos_, this, [this]() { render(); });

    connect(service(), &services::KrxQuantService::snapshot_ready, this,
            [this](const services::KrxQuantService::Snapshot& snapshot) {
                symbols_ = snapshot.symbols;
                today_ = krx_quant::daily_day(snapshot.status.now);
                if (kind_) {
                    queue_.clear();
                    for (const auto& quote : symbols_)
                        queue_ << quote.symbol;
                    problems_.clear();
                    read_next_latest();
                } else {
                    service()->read_kinds();
                }
            });
    connect(service(), &services::KrxQuantService::refresh_failed, this,
            [this](const QString& reason) { read_failed(reason); });
    connect(service(), &services::KrxQuantService::kinds_ready, this,
            [this](const QList<krx_quant::KindInfo>& kinds) {
                kind_ = krx_quant::kind_named(kinds, QStringLiteral("open_to_close"));
                if (!kind_)
                    return show_missing();
                missing_ = false;
                queue_.clear();
                for (const auto& quote : symbols_)
                    queue_ << quote.symbol;
                problems_.clear();
                read_next_latest();
            });
    connect(service(), &services::KrxQuantService::kinds_failed, this, [this](const QString& reason) {
        if (krx_quant::is_not_found(reason)) // an engine before krx-quant #179
            return show_missing();
        read_failed(reason);
    });
    connect(service(), &services::KrxQuantService::kind_latest_ready, this,
            [this](const krx_quant::KindLatest& latest) {
                if (latest.kind != QLatin1String("open_to_close"))
                    return;
                latest_[latest.symbol] = latest;
                read_next_latest();
            });
    connect(service(), &services::KrxQuantService::kind_latest_failed, this,
            [this](const QString& symbol, const QString& kind, const QString& reason) {
                if (kind != QLatin1String("open_to_close"))
                    return;
                problems_ << QStringLiteral("%1: %2").arg(symbol, reason);
                read_next_latest();
            });
}

void KrxOpenToCloseWidget::read() {
    service()->refresh();
}

void KrxOpenToCloseWidget::read_next_latest() {
    if (!isVisible())
        queue_.clear(); // the round ends with what was read; the next starts once shown again
    if (queue_.isEmpty()) {
        render();
        read_done(problems_.join(QStringLiteral(" · ")));
        return;
    }
    service()->read_kind_latest(queue_.takeFirst(), QStringLiteral("open_to_close"));
}

void KrxOpenToCloseWidget::show_missing() {
    missing_ = true;
    render();
    read_done();
}

void KrxOpenToCloseWidget::render() {
    missing_label_->setVisible(missing_);
    table_->setVisible(!missing_);
    if (missing_ || !kind_)
        return;
    const auto rows = krx_quant::dashboard::open_to_close_rows(symbols_, latest_, *kind_, today_);
    table_->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        table_->setItem(r, 0, symbol_cell(table_, logos_, row.symbol, row.name, row.logo_url));
        auto* day = cell(row.predicted_at);
        day->setToolTip(row.basis);
        table_->setItem(r, 1, day);
        const QList<std::pair<QString, Direction>> classes = {
            {row.up, Direction::Up}, {row.flat, Direction::Flat}, {row.down, Direction::Down}};
        for (int c = 0; c < classes.size(); ++c) {
            auto* item = cell(classes[c].first, kNumber);
            if (row.call == classes[c].second)
                paint(item, direction_color(row.call), true);
            table_->setItem(r, 2 + c, item);
        }
        table_->setItem(r, 5, cell(row.band));
        auto* answer = cell(row.result);
        answer->setToolTip(row.answer);
        if (row.hit)
            paint(answer, *row.hit ? ui::colors::POSITIVE() : ui::colors::NEGATIVE());
        table_->setItem(r, 6, answer);
    }
    table_->setToolTip(krx_quant::daily_tip(*kind_));
}

} // namespace fincept::screens::krx_dashboard
