#pragma once

#include <QHeaderView>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

/// The KRX screen's tables: read-only, whole rows selected, columns sized to their contents.
namespace fincept::screens::krx_tables {

inline constexpr Qt::Alignment kNumber = Qt::AlignRight | Qt::AlignVCenter;

inline QTableWidgetItem* cell(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(align);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

inline QTableWidget* table(const QStringList& headers) {
    auto* table = new QTableWidget(0, static_cast<int>(headers.size()));
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

} // namespace fincept::screens::krx_tables
