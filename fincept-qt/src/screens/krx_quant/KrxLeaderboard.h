#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QWidget>

#include <optional>

namespace fincept::screens {

/// The KRX screen's leaderboard panel: its query (period, scoring basis, symbol, special
/// situations) on the header, and the ranked, unranked and baseline entries with their
/// stats; intervals in tooltips, the ranking rules on the title's. A line double-clicked
/// asks for that predictor's detail.
class KrxLeaderboard : public QWidget {
    Q_OBJECT
  public:
    explicit KrxLeaderboard(QWidget* parent = nullptr);

    services::KrxQuantService::LeaderboardQuery query() const;
    /// The symbols to choose from (besides all of them).
    void set_symbols(const QStringList& symbols);
    void show_leaderboard(const krx_quant::Leaderboard& leaderboard);
    void show_failure(const QString& reason);
    /// Paint in the theme's colors and font now.
    void restyle();

  signals:
    /// The user chose another period, basis, symbol or special situations.
    void query_changed();
    /// The user asked for a predictor's detail (double-clicked its line).
    void detail_requested(const QString& algorithm);

  private:
    krx_panel::Header header_;
    QComboBox* period_ = nullptr;
    QComboBox* basis_ = nullptr;
    QComboBox* symbol_ = nullptr;
    QCheckBox* special_ = nullptr;
    QLabel* failure_ = nullptr; ///< on the header, while the leaderboard cannot be read
    QTableWidget* table_ = nullptr;
    QStringList symbols_;
    std::optional<krx_quant::Leaderboard> shown_; ///< the last one read
};

} // namespace fincept::screens
