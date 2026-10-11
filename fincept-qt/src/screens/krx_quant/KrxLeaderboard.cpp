#include "screens/krx_quant/KrxLeaderboard.h"

#include "screens/krx_quant/KrxTables.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
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
    auto* controls = new QHBoxLayout;
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
    controls->addStretch();
    layout->addLayout(controls);

    table_ = krx_tables::table(QStringList{tr("순위"), tr("예측"), tr("상태"), tr("모델")} +
                               krx_tables::stats_headers() + QStringList{tr("운 보정")});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(kModels, QHeaderView::Stretch);
    layout->addWidget(table_, 1);
    note_ = new QLabel;
    note_->setObjectName("krxNote");
    note_->setWordWrap(true);
    layout->addWidget(note_);

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
    note_->setText(tr("%1 · 순위에는 채점 %2건 이상, 기준 예측과의 짝도 그만큼 있어야 오른다. "
                      "순위는 가장 약한 lift의 하한 순이고, 운 보정 통과는 기준 예측 셋을 모두 이긴 것이 "
                      "운으로 보기 어렵다는 뜻이다(q ≤ 0.05). 줄을 두 번 누르면 그 예측의 상세 통계가 열린다.")
                       .arg(krx_quant::days_text(leaderboard.days))
                       .arg(leaderboard.min_sample));
}

void KrxLeaderboard::show_failure(const QString& reason) {
    table_->setRowCount(0);
    note_->setText(tr("순위표를 읽지 못했습니다: %1").arg(reason));
}

} // namespace fincept::screens
