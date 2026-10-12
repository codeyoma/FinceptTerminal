#pragma once

#include "services/krx_quant/StatsView.h"

#include <QHeaderView>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <iterator>

/// The KRX screen's tables: read-only, whole rows selected, columns sized to their contents.
/// Also the stats columns the leaderboard and a detail window's breakdowns share.
namespace fincept::screens::krx_tables {

inline constexpr Qt::Alignment kNumber = Qt::AlignRight | Qt::AlignVCenter;

inline QTableWidgetItem* cell(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(align);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

/// A value, its interval in the tooltip.
inline QTableWidgetItem* shown_cell(const krx_quant::Shown& shown) {
    auto* item = cell(shown.text, kNumber);
    item->setToolTip(shown.tip);
    return item;
}

/// The stats columns set_stats fills, in order.
inline constexpr const char* kStatsHeaders[] = {"표본",  "정답률",          "방향 정밀도",        "커버리지",
                                                "Brier", "중간가 손익(틱)", "가장 약한 lift 하한"};
inline constexpr int kStatsColumns = static_cast<int>(std::size(kStatsHeaders));

/// What a column's name alone would let be misread, shown on its header.
inline QString header_tip(const QString& header) {
    if (header == QObject::tr("중간가 손익(틱)"))
        return QObject::tr("거래당 평균, 연구용 값이다. 방향을 낸 모든 예측을 거래로 치고 시작 중간가에서 목표 "
                           "중간가까지 잰다. 스프레드와 체결은 반영하지 않아 따라 했을 때의 손익이 아니다.");
    return {};
}

inline QStringList stats_headers() {
    QStringList headers;
    for (const char* header : kStatsHeaders)
        headers << QObject::tr(header);
    return headers;
}

/// An entry's stats in ``row``, from ``column`` on: the columns of kStatsHeaders.
inline void set_stats(QTableWidget* table, int row, int column, const krx_quant::Entry& e) {
    table->setItem(row, column, cell(krx_quant::number_text(e.n, 0), kNumber));
    table->setItem(row, column + 1, shown_cell(krx_quant::rate_shown(e.accuracy)));
    table->setItem(row, column + 2, shown_cell(krx_quant::rate_shown(e.precision)));
    table->setItem(row, column + 3, shown_cell(krx_quant::rate_shown(e.coverage)));
    table->setItem(row, column + 4, shown_cell(krx_quant::mean_shown(e.brier, 3)));
    auto* pnl = shown_cell(krx_quant::mean_shown(e.per_trade, 2));
    const QString trades =
        QObject::tr("거래 %1건, 합계 %2틱").arg(e.trades).arg(krx_quant::number_text(e.total_ticks, 1));
    pnl->setToolTip(pnl->toolTip().isEmpty() ? trades : trades + "\n" + pnl->toolTip());
    table->setItem(row, column + 5, pnl);
    table->setItem(row, column + 6, cell(krx_quant::weakest_lift_text(e), kNumber));
}

inline QTableWidget* table(const QStringList& headers) {
    auto* table = new QTableWidget(0, static_cast<int>(headers.size()));
    table->setHorizontalHeaderLabels(headers);
    for (int column = 0; column < headers.size(); ++column) {
        if (const QString tip = header_tip(headers[column]); !tip.isEmpty())
            table->horizontalHeaderItem(column)->setToolTip(tip);
    }
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

} // namespace fincept::screens::krx_tables
