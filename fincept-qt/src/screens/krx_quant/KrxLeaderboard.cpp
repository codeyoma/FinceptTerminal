#include "screens/krx_quant/KrxLeaderboard.h"

#include "screens/krx_quant/KrxTables.h"
#include "services/krx_quant/ChartModel.h"
#include "ui/theme/Theme.h"

#include <QSignalBlocker>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

using krx_tables::cell;
using krx_tables::kNumber;

enum Column {
    kPick, ///< the box selecting it for the chart (krx-quant #159)
    kRank,
    kName,
    kStatus,
    kModels,
    kStats, ///< the kind's stats columns, then the luck check (significant_column)
};
/// Clicks on the boxes settle for this long before the chart reads anything: a double click
/// ticks and clears.
constexpr int kSettleMs = 300;

} // namespace

KrxLeaderboard::KrxLeaderboard(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    header_ = krx_panel::header(tr("순위표"));
    layout->addWidget(header_.bar);
    auto* controls = header_.controls;
    failure_ = new QLabel(tr("읽지 못함"));
    failure_->hide();
    controls->addWidget(failure_);
    controls->addWidget(new QLabel(tr("기간")));
    period_ = new QComboBox;
    for (const char* period : {"today", "5d", "20d"})
        period_->addItem(krx_quant::period_label(period), period);
    controls->addWidget(period_);
    controls->addWidget(new QLabel(tr("종목")));
    symbol_ = new QComboBox;
    symbol_->addItem(tr("전체"), "all");
    controls->addWidget(symbol_);
    special_ = new QCheckBox(tr("특수 상황 포함"));
    special_->setToolTip(krx_quant::special_tip());
    controls->addWidget(special_);

    table_ = krx_tables::table(QStringList{tr("차트"), tr("순위"), tr("예측"), tr("상태"), tr("모델")} +
                               krx_tables::stats_headers() + QStringList{tr("운 보정")});
    if (auto* pick = table_->horizontalHeaderItem(kPick))
        pick->setToolTip(tr("체크한 모델들의 합의(세 확률의 평균)를 차트에 그린다. 아무것도 체크하지 않으면 "
                            "최종 합의다. 이 머리 칸을 누르면 모두 해제한다."));
    table_->horizontalHeader()->setStretchLastSection(false);
    // The direction's columns as made above; another kind's are set as it is chosen (set_kind).
    table_->horizontalHeader()->setSectionResizeMode(kModels, QHeaderView::Stretch);
    layout->addWidget(table_, 1);
    restyle();

    for (auto* combo : {period_, symbol_})
        connect(combo, &QComboBox::currentIndexChanged, this, &KrxLeaderboard::query_changed);
    connect(special_, &QCheckBox::toggled, this, &KrxLeaderboard::query_changed);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column == kPick)
            return; // two clicks on a box
        if (const auto* name = table_->item(row, kName))
            emit detail_requested(name->data(Qt::UserRole).toString());
    });
    settle_.setSingleShot(true);
    settle_.setInterval(kSettleMs);
    connect(&settle_, &QTimer::timeout, this, [this]() {
        if (selection_ == told_)
            return; // ticked and cleared again
        told_ = selection_;
        emit selection_changed(selection_);
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() != kPick || !(item->flags() & Qt::ItemIsUserCheckable))
            return;
        selection_ =
            krx_quant::toggled(selection_, item->data(Qt::UserRole).toString(), item->checkState() == Qt::Checked);
        settle_.start();
    });
    connect(table_->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section != kPick || selection_.isEmpty())
            return;
        selection_.clear();
        show_selection();
        settle_.start();
    });
}

services::KrxQuantService::LeaderboardQuery KrxLeaderboard::query() const {
    return {symbol_->currentData().toString(), period_->currentData().toString(), special_->isChecked()};
}

void KrxLeaderboard::set_symbols(const QStringList& symbols) {
    if (symbols == symbols_)
        return;
    symbols_ = symbols;
    const QString chosen = symbol_->currentData().toString();
    {
        const QSignalBlocker quiet(symbol_); // the same choice, if it is still there
        symbol_->clear();
        symbol_->addItem(tr("전체"), "all");
        for (const auto& s : symbols)
            symbol_->addItem(s, s);
    }
    const int at = symbol_->findData(chosen);
    symbol_->setCurrentIndex(std::max(0, at));
    if (at < 0)
        emit query_changed(); // the chosen symbol is gone: all of them
}

void KrxLeaderboard::show_leaderboard(const krx_quant::Leaderboard& leaderboard) {
    shown_ = leaderboard;
    failure_->hide();
    header_.title->setToolTip(krx_quant::ranking_rules(leaderboard.days, leaderboard.min_sample, kind_));
    table_->setRowCount(0);
    const QSignalBlocker quiet(table_); // the boxes as selected, not the user's clicks
    const auto add = [this](const krx_quant::Entry& e, bool baseline) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* pick = cell(QString());
        pick->setData(Qt::UserRole, e.algorithm);
        if (direction() && krx_quant::selectable(e.algorithm)) { // another kind's are not the chart's
            pick->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            pick->setCheckState(selection_.contains(e.algorithm) ? Qt::Checked : Qt::Unchecked);
        }
        table_->setItem(row, kPick, pick);
        table_->setItem(row, kRank, cell(e.rank ? QString::number(*e.rank) : QStringLiteral("—"), kNumber));
        auto* name = cell(krx_quant::predictor_label(e.algorithm));
        name->setData(Qt::UserRole, e.algorithm);
        table_->setItem(row, kName, name);
        table_->setItem(row, kStatus, cell(krx_quant::standing_label(e.status)));
        auto* models = cell(e.model_versions.join(", "));
        models->setToolTip(e.model_versions.join("\n"));
        table_->setItem(row, kModels, models);
        krx_tables::set_stats(table_, row, kStats, e, kind_);
        table_->setItem(row, significant_column(), cell(e.significant ? tr("통과") : QString()));
        if (baseline || krx_quant::is_consensus(e.algorithm)) {
            const QColor color(baseline ? ui::colors::TEXT_DIM() : ui::colors::AMBER());
            for (int c = 0; c < table_->columnCount(); ++c)
                if (auto* item = table_->item(row, c))
                    item->setForeground(color);
        }
    };
    for (const auto& e : leaderboard.ranked)
        add(e, false);
    for (const auto& e : leaderboard.unranked)
        add(e, false);
    for (const auto& e : leaderboard.baselines)
        add(e, true);
}

bool KrxLeaderboard::direction() const {
    return !kind_ || kind_->name == QLatin1String("direction");
}

int KrxLeaderboard::significant_column() const {
    return kStats + static_cast<int>(krx_tables::stats_columns(kind_).size());
}

void KrxLeaderboard::set_kind(const std::optional<krx_quant::KindInfo>& kind) {
    const QString before = kind_ ? kind_->name : QStringLiteral("direction");
    kind_ = kind;
    if ((kind_ ? kind_->name : QStringLiteral("direction")) == before)
        return;
    set_columns();
    special_->setToolTip(krx_quant::special_tip(kind_)); // a daily kind's day (krx-quant #264)
    shown_.reset(); // the other kind's: its own comes next
    header_.title->setToolTip(QString());
    table_->setRowCount(0);
}

void KrxLeaderboard::set_columns() {
    // The first five as they are; the kind's stats, then the luck check.
    QList<krx_quant::StatsColumn> columns;
    for (int c = 0; c < kStats; ++c)
        if (const auto* item = table_->horizontalHeaderItem(c))
            columns.push_back({item->text(), item->toolTip()});
    columns += krx_tables::stats_columns(kind_);
    columns.push_back(
        {tr("운 보정"), direction() ? krx_quant::column_tip(QStringLiteral("운 보정")) : krx_quant::luck_tip(*kind_)});
    krx_tables::set_columns(table_, columns);
    // The chart's boxes are the direction's (krx-quant #159): no column for another kind.
    table_->setColumnHidden(kPick, !direction());
}

void KrxLeaderboard::set_selection(const QStringList& selection) {
    selection_ = selection;
    told_ = selection;
    show_selection();
}

void KrxLeaderboard::show_selection() {
    const QSignalBlocker quiet(table_);
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* pick = table_->item(row, kPick);
        if (pick && (pick->flags() & Qt::ItemIsUserCheckable))
            pick->setCheckState(selection_.contains(pick->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
    }
}

void KrxLeaderboard::show_failure(const QString& reason) {
    shown_.reset();
    header_.title->setToolTip(QString()); // the rules were the last leaderboard's
    table_->setRowCount(0);
    failure_->setToolTip(tr("순위표를 읽지 못했습니다: %1").arg(reason));
    failure_->show();
}

void KrxLeaderboard::restyle() {
    header_.restyle();
    failure_->setStyleSheet(QString("color:%1;font-weight:700;").arg(ui::colors::NEGATIVE()));
    krx_tables::restyle(table_);
    if (shown_) // its highlighted lines in the theme's colors
        show_leaderboard(*shown_);
}

} // namespace fincept::screens
