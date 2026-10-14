#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

#include <optional>

namespace fincept::screens {

/// The KRX screen's leaderboard panel: its query (period, symbol, special
/// situations) on the header, and the ranked, unranked and baseline entries with their
/// stats; intervals in tooltips, the ranking rules on the title's. A line double-clicked
/// asks for that predictor's detail. The first column's boxes select models for the chart
/// (krx-quant #159): an algorithm's or a baseline's line, not a consensus's; clicking that
/// column's header clears them. Of another kind than the direction (krx-quant #182), its
/// predictors with its stats columns, and no boxes: the chart's consensus is the direction's.
class KrxLeaderboard : public QWidget {
    Q_OBJECT
  public:
    explicit KrxLeaderboard(QWidget* parent = nullptr);

    services::KrxQuantService::LeaderboardQuery query() const;
    /// The symbols to choose from (besides all of them).
    void set_symbols(const QStringList& symbols);
    void show_leaderboard(const krx_quant::Leaderboard& leaderboard);
    void show_failure(const QString& reason);
    /// The models selected for the chart: their boxes ticked.
    void set_selection(const QStringList& selection);
    /// The kind ranked (krx-quant #182): its columns; none or the direction's, the direction's as
    /// before. The lines shown go until its leaderboard comes.
    void set_kind(const std::optional<krx_quant::KindInfo>& kind);
    const QStringList& selection() const { return selection_; }
    /// Paint in the theme's colors and font now.
    void restyle();

  signals:
    /// The user chose another period, symbol or special situations.
    void query_changed();
    /// The user asked for a predictor's detail (double-clicked its line).
    void detail_requested(const QString& algorithm);
    /// The user ticked or cleared models for the chart, once their clicks settled.
    void selection_changed(const QStringList& selection);

  private:
    krx_panel::Header header_;
    QComboBox* period_ = nullptr;
    QComboBox* symbol_ = nullptr;
    QCheckBox* special_ = nullptr;
    QLabel* failure_ = nullptr; ///< on the header, while the leaderboard cannot be read
    QTableWidget* table_ = nullptr;
    QStringList symbols_;
    std::optional<krx_quant::Leaderboard> shown_; ///< the last one read
    QStringList selection_;                       ///< the models selected for the chart
    QStringList told_;                            ///< the selection last said (selection_changed)
    QTimer settle_;                               ///< a moment after the last click
    std::optional<krx_quant::KindInfo> kind_;     ///< the kind ranked; none: the direction
    /// Tick the boxes as selection_ says, without taking it for the user's clicks.
    void show_selection();
    /// Whether the kind ranked is the direction.
    bool direction() const;
    /// The column of the luck check, the last: after the kind's stats.
    int significant_column() const;
    /// The table's columns for the kind ranked.
    void set_columns();
};

} // namespace fincept::screens
