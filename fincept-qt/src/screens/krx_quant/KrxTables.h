#pragma once

#include "services/krx_quant/StatsView.h"
#include "ui/tables/DataTable.h"
#include "ui/theme/Theme.h"

#include <QHeaderView>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <iterator>

/// The KRX screen's tables: Fincept's DataTable (read-only, whole rows selected, alternating
/// rows), columns sized to their contents, and painted again on a theme change (restyle).
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
inline constexpr const char* kStatsHeaders[] = {
    "표본", "정답률", "방향 정밀도", "커버리지", "Brier", "닿음 손익(틱, 연구용)", "가장 약한 lift 하한"};
inline constexpr int kStatsColumns = static_cast<int>(std::size(kStatsHeaders));

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
    table->setItem(row, column + 5, shown_cell(krx_quant::pnl_shown(e)));
    table->setItem(row, column + 6, cell(krx_quant::weakest_lift_text(e), kNumber));
}

/// The stats columns of ``kind`` (krx-quant #182): the direction's (kStatsHeaders) without one.
inline QList<krx_quant::StatsColumn> stats_columns(const std::optional<krx_quant::KindInfo>& kind) {
    if (kind && kind->name != QLatin1String("direction"))
        return krx_quant::kind_stats_columns(*kind);
    QList<krx_quant::StatsColumn> columns;
    for (const auto& header : stats_headers())
        columns.push_back({header, krx_quant::column_tip(header)});
    return columns;
}

/// An entry's stats in ``row`` from ``column`` on, under stats_columns(``kind``).
inline void set_stats(QTableWidget* table, int row, int column, const krx_quant::Entry& e,
                      const std::optional<krx_quant::KindInfo>& kind) {
    if (!kind || kind->name == QLatin1String("direction"))
        return set_stats(table, row, column, e);
    const auto cells = krx_quant::kind_stats_cells(e, *kind);
    for (int i = 0; i < cells.size(); ++i)
        table->setItem(row, column + i, shown_cell(cells[i]));
}

/// ``table``'s columns: ``columns``' headers, each with its tooltip.
inline void set_columns(QTableWidget* table, const QList<krx_quant::StatsColumn>& columns) {
    table->setColumnCount(static_cast<int>(columns.size()));
    for (int c = 0; c < columns.size(); ++c) {
        auto* item = new QTableWidgetItem(columns[c].header);
        item->setToolTip(columns[c].tip);
        table->setHorizontalHeaderItem(c, item);
    }
}

/// DataTable's colors (ui/tables/DataTable.cpp), the theme's now: DataTable paints once,
/// when it is made. Its fixed cell padding is left out, so the global style's, which
/// follows the user's density, applies.
inline void restyle(QTableWidget* table) {
    // A row's box (the leaderboard's, krx-quant #159) as Fincept's check boxes: the global
    // style draws QCheckBox's, not a table item's, which would be black on black.
    table->setStyleSheet(
        QString("QTableWidget { background: %1; alternate-background-color: %2; "
                "gridline-color: %3; border: none; }"
                "QTableWidget::item:selected { background: %4; }"
                "QTableWidget::indicator { width: 12px; height: 12px; border: 1px solid %5; background: %6; }"
                "QTableWidget::indicator:checked { background: %7; border-color: %7; }")
            .arg(ui::colors::DARK(), ui::colors::ROW_ALT(), ui::colors::BORDER(), ui::colors::BG_RAISED(),
                 ui::colors::BORDER_MED(), ui::colors::BG_SURFACE(), ui::colors::AMBER()));
}

inline QTableWidget* table(const QStringList& headers) {
    auto* table = new ui::DataTable;
    table->set_headers(headers);
    for (int column = 0; column < headers.size(); ++column) {
        auto* item = table->horizontalHeaderItem(column);
        if (const QString tip = krx_quant::column_tip(headers[column]); item && !tip.isEmpty())
            item->setToolTip(tip);
    }
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // Rows as high as the user's font and density make them, not DataTable's fixed 26px:
    // these tables have a few dozen rows at most.
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->setWordWrap(false); // one line a row: a narrow column elides, as DataTable's fixed rows did
    restyle(table);
    return table;
}

} // namespace fincept::screens::krx_tables
