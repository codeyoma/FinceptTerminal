#include "screens/krx_quant/KrxLeaderboard.h"

#include "screens/krx_quant/KrxTables.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QLocale>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

using krx_tables::cell;
using krx_tables::kNumber;

enum Column {
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
    kColumns
};

QString krx_number(double value, int decimals) {
    static const QLocale locale(QLocale::Korean, QLocale::SouthKorea);
    return locale.toString(value, 'f', decimals);
}

/// A rate as a percent, its interval in the tooltip.
QTableWidgetItem* rate_cell(const std::optional<krx_quant::Interval>& rate) {
    if (!rate)
        return cell(QStringLiteral("—"), kNumber);
    auto* item = cell(krx_number(rate->value * 100, 1) + "%", kNumber);
    if (rate->low && rate->high)
        item->setToolTip(QStringLiteral("95% 구간 %1–%2% (n %3)")
                             .arg(krx_number(*rate->low * 100, 1), krx_number(*rate->high * 100, 1))
                             .arg(rate->n));
    return item;
}

/// A mean (Brier, ticks a trade), its interval in the tooltip.
QTableWidgetItem* mean_cell(const std::optional<krx_quant::Interval>& mean, int decimals) {
    if (!mean)
        return cell(QStringLiteral("—"), kNumber);
    auto* item = cell(krx_number(mean->value, decimals), kNumber);
    if (mean->low && mean->high)
        item->setToolTip(QStringLiteral("95% 구간 %1 – %2 (n %3)")
                             .arg(krx_number(*mean->low, decimals), krx_number(*mean->high, decimals))
                             .arg(mean->n));
    return item;
}

} // namespace

KrxLeaderboard::KrxLeaderboard(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("기간")));
    period_ = new QComboBox;
    period_->addItem(tr("오늘"), "today");
    period_->addItem(tr("5일"), "5d");
    period_->addItem(tr("20일"), "20d");
    controls->addWidget(period_);
    controls->addWidget(new QLabel(tr("채점 기준")));
    basis_ = new QComboBox;
    basis_->addItem(tr("표시 시점 (B)"), "b");
    basis_->addItem(tr("봉 마감 (A)"), "a");
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

    table_ = krx_tables::table({tr("순위"), tr("예측"), tr("상태"), tr("모델"), tr("표본"), tr("정답률"),
                                tr("방향 정밀도"), tr("커버리지"), tr("Brier"), tr("거래당 손익(틱)"),
                                tr("가장 약한 lift 하한"), tr("운 보정")});
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
        table_->setItem(row, kName, cell(krx_quant::predictor_label(e.algorithm)));
        table_->setItem(row, kStatus, cell(krx_quant::standing_label(e.status)));
        auto* models = cell(e.model_versions.join(", "));
        models->setToolTip(e.model_versions.join("\n"));
        table_->setItem(row, kModels, models);
        table_->setItem(row, kN, cell(krx_number(e.n, 0), kNumber));
        table_->setItem(row, kAccuracy, rate_cell(e.accuracy));
        table_->setItem(row, kPrecision, rate_cell(e.precision));
        table_->setItem(row, kCoverage, rate_cell(e.coverage));
        table_->setItem(row, kBrier, mean_cell(e.brier, 3));
        auto* pnl = mean_cell(e.per_trade, 2);
        const QString trades = tr("거래 %1건, 합계 %2틱").arg(e.trades).arg(krx_number(e.total_ticks, 1));
        pnl->setToolTip(pnl->toolTip().isEmpty() ? trades : trades + "\n" + pnl->toolTip());
        table_->setItem(row, kPerTrade, pnl);
        const QString lift = e.lift_low
                                 ? QStringLiteral("%1%2").arg(*e.lift_low > 0 ? "+" : "", krx_number(*e.lift_low, 3))
                                 : QStringLiteral("—");
        table_->setItem(row, kLift, cell(lift, kNumber));
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
    QString days = tr("예측이 있는 날 없음");
    if (leaderboard.days.size() == 1)
        days = leaderboard.days.first();
    else if (leaderboard.days.size() > 1)
        days = tr("%1 ~ %2 (%3일)").arg(leaderboard.days.first(), leaderboard.days.last()).arg(leaderboard.days.size());
    note_->setText(tr("%1 · 순위에는 채점 %2건 이상, 기준 예측과의 짝도 그만큼 있어야 오른다. "
                      "순위는 가장 약한 lift의 하한 순이고, 운 보정 통과는 기준 예측 셋을 모두 이긴 것이 "
                      "운으로 보기 어렵다는 뜻이다(q ≤ 0.05).")
                       .arg(days)
                       .arg(leaderboard.min_sample));
}

void KrxLeaderboard::show_failure(const QString& reason) {
    table_->setRowCount(0);
    note_->setText(tr("순위표를 읽지 못했습니다: %1").arg(reason));
}

} // namespace fincept::screens
