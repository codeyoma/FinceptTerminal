#pragma once

#include "services/krx_quant/KrxQuantService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// The KRX screen's leaderboard: its query (period, scoring basis, symbol, special
/// situations) and the ranked, unranked and baseline entries with their stats; intervals
/// in tooltips. A line double-clicked asks for that predictor's detail.
class KrxLeaderboard : public QWidget {
    Q_OBJECT
  public:
    explicit KrxLeaderboard(QWidget* parent = nullptr);

    services::KrxQuantService::LeaderboardQuery query() const;
    /// The symbols to choose from (besides all of them).
    void set_symbols(const QStringList& symbols);
    void show_leaderboard(const krx_quant::Leaderboard& leaderboard);
    void show_failure(const QString& reason);

  signals:
    /// The user chose another period, basis, symbol or special situations.
    void query_changed();
    /// The user asked for a predictor's detail (double-clicked its line).
    void detail_requested(const QString& algorithm);

  private:
    QComboBox* period_ = nullptr;
    QComboBox* basis_ = nullptr;
    QComboBox* symbol_ = nullptr;
    QCheckBox* special_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* note_ = nullptr;
    QStringList symbols_;
};

} // namespace fincept::screens
