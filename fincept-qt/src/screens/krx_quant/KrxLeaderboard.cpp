#include "screens/krx_quant/KrxLeaderboard.h"

#include "screens/krx_quant/KrxTables.h"
#include "ui/theme/Theme.h"

#include <QSignalBlocker>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

using krx_tables::cell;
using krx_tables::kNumber;

enum Column { kRank, kName, kStatus, kModels, kStats, kSignificant = kStats + krx_tables::kStatsColumns, kColumns };

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
    controls->addWidget(new QLabel(tr("채점 기준")));
    basis_ = new QComboBox;
    for (const char* basis : {"b", "a"})
        basis_->addItem(krx_quant::basis_label(basis), basis);
    basis_->setToolTip(tr("B: 예측이 화면에 뜬 시각의 중간가부터, A: 봉 마감의 중간가부터 4분. "
                          "차트의 ✓/✗도 이 기준이다."));
    controls->addWidget(basis_);
    controls->addWidget(new QLabel(tr("종목")));
    symbol_ = new QComboBox;
    symbol_->addItem(tr("전체"), "all");
    controls->addWidget(symbol_);
    special_ = new QCheckBox(tr("특수 상황 포함"));
    special_->setToolTip(tr("VI·서킷브레이커·사이드카가 4분 구간에 낀 예측도 센다(기본은 뺀다)."));
    controls->addWidget(special_);

    table_ = krx_tables::table(QStringList{tr("순위"), tr("예측"), tr("상태"), tr("모델")} +
                               krx_tables::stats_headers() + QStringList{tr("운 보정")});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(kModels, QHeaderView::Stretch);
    layout->addWidget(table_, 1);
    restyle();

    for (auto* combo : {period_, basis_, symbol_})
        connect(combo, &QComboBox::currentIndexChanged, this, &KrxLeaderboard::query_changed);
    connect(special_, &QCheckBox::toggled, this, &KrxLeaderboard::query_changed);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (const auto* name = table_->item(row, kName))
            emit detail_requested(name->data(Qt::UserRole).toString());
    });
}

services::KrxQuantService::LeaderboardQuery KrxLeaderboard::query() const {
    return {symbol_->currentData().toString(), period_->currentData().toString(), basis_->currentData().toString(),
            special_->isChecked()};
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
    header_.title->setToolTip(krx_quant::ranking_rules(leaderboard.days, leaderboard.min_sample));
    table_->setRowCount(0);
    const auto add = [this](const krx_quant::Entry& e, bool baseline) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, kRank, cell(e.rank ? QString::number(*e.rank) : QStringLiteral("—"), kNumber));
        auto* name = cell(krx_quant::predictor_label(e.algorithm));
        name->setData(Qt::UserRole, e.algorithm);
        table_->setItem(row, kName, name);
        table_->setItem(row, kStatus, cell(krx_quant::standing_label(e.status)));
        auto* models = cell(e.model_versions.join(", "));
        models->setToolTip(e.model_versions.join("\n"));
        table_->setItem(row, kModels, models);
        krx_tables::set_stats(table_, row, kStats, e);
        table_->setItem(row, kSignificant, cell(e.significant ? tr("통과") : QString()));
        if (baseline || krx_quant::is_consensus(e.algorithm)) {
            const QColor color(baseline ? ui::colors::TEXT_DIM() : ui::colors::AMBER());
            for (int c = 0; c < kColumns; ++c)
                table_->item(row, c)->setForeground(color);
        }
    };
    for (const auto& e : leaderboard.ranked)
        add(e, false);
    for (const auto& e : leaderboard.unranked)
        add(e, false);
    for (const auto& e : leaderboard.baselines)
        add(e, true);
}

void KrxLeaderboard::show_failure(const QString& reason) {
    shown_.reset();
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
