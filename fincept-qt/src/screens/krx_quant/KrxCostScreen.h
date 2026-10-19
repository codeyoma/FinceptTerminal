#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/CostView.h"
#include "services/krx_quant/EngineData.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace fincept::services {
class KrxCostLive;
}

namespace fincept::screens {

class KrxLogos;
class KrxStatsWindow;

/// KRX horizons and costs (fork addition, codeyoma/krx-quant#307, see custom/README.md): at which
/// horizon a hit rate pays for its round trip. Each symbol's cost card (the tick and its basis
/// points, the sell tax, commissions and a one-tick spread, the limit and market round trips, k, the
/// inverse 2X round trip, the nearest tick-band edges); a row a kind with a direction (the direction
/// at 4 minutes, the barriers at 4, 15, 30 and 60, open to close) with its width, cost, breakeven
/// hit rate p* and the final consensus', first-ranked algorithm's and strongest baseline's hit rates
/// against it, colored by whether the interval's lower bound clears p*; and the period base rates.
/// The query is the leaderboard's. A double-click opens a predictor's detail window. It draws what
/// its KrxCostLive holds, in Fincept's panels, tables, fonts and theme. The text is Korean.
class KrxCostScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KrxCostScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void refresh_theme();
    void render();
    void show_dot();
    void fill_choices();
    void fill_cards();
    void fill_rows();
    void fill_regimes();
    void choose();
    /// The detail window of the predictor in row ``row``'s ``column`` (a predictor's column, else the
    /// first-ranked, the consensus or the baseline, whichever the row has).
    void open_detail(int row, int column);

    services::KrxCostLive* live_ = nullptr;
    KrxLogos* logos_ = nullptr;
    QTimer* clock_timer_ = nullptr;
    krx_panel::Header header_;
    QLabel* dot_ = nullptr;
    QComboBox* symbol_box_ = nullptr;
    QComboBox* period_box_ = nullptr;
    QCheckBox* special_box_ = nullptr;
    QComboBox* cost_box_ = nullptr;
    QPushButton* reload_ = nullptr;
    QLabel* as_of_ = nullptr;
    QLabel* failure_ = nullptr;
    krx_panel::Header cards_header_;
    QHBoxLayout* cards_row_ = nullptr;
    QList<QTableWidget*> cards_;
    krx_panel::Header rows_header_;
    QLabel* legend_ = nullptr;
    QTableWidget* rows_ = nullptr;
    krx_panel::Header regimes_header_;
    QLabel* regimes_line_ = nullptr;
    QTableWidget* regimes_ = nullptr;
    QMap<QString, KrxStatsWindow*> stats_windows_; ///< by predictor, gone with the screen
};

} // namespace fincept::screens
