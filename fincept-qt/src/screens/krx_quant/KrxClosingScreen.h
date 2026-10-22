#pragma once

#include "screens/krx_quant/KrxPanel.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace fincept::services {
class KrxClosingLive;
}

namespace fincept::screens {

class KrxLogos;

/// KRX closing flow (fork addition, codeyoma/krx-quant#305, see custom/README.md): the closing
/// rebalancing demand of the single-stock ±2x ETFs on 005930 and 000660, A × (L² − L) × r, as the
/// engine computes it from the formula. Today's demand card (buy or sell, by structure, the closing
/// auction ratio as an upper bound and its denominator), the product table (each product's leverage,
/// structure, L² − L, previous net assets and demand), the past days (the 15:20 demand and ratio, the
/// closing auction's move, whether it went the demand's way, the next day) and their summary by period
/// and ratio tertile. 009150 is the control: no products, the auction and next-day statistics only. It
/// draws what its KrxClosingLive holds, in Fincept's panels, tables, fonts and theme. The text is Korean.
class KrxClosingScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KrxClosingScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void refresh_theme();
    void render();
    void show_dot();
    void fill_symbols();
    void fill_card();
    void fill_products();
    void fill_days();
    void fill_summary();

    services::KrxClosingLive* live_ = nullptr;
    KrxLogos* logos_ = nullptr;
    QTimer* clock_timer_ = nullptr;
    krx_panel::Header header_;
    QLabel* dot_ = nullptr;
    QComboBox* symbol_box_ = nullptr;
    QLabel* day_line_ = nullptr;
    QLabel* krx_label_ = nullptr;
    QPushButton* reload_ = nullptr;
    QLabel* failure_ = nullptr;
    krx_panel::Header card_header_;
    QLabel* headline_ = nullptr;
    QTableWidget* card_ = nullptr;
    QLabel* notes_ = nullptr;
    krx_panel::Header products_header_;
    QWidget* products_panel_ = nullptr;
    QWidget* products_filler_ = nullptr;
    QTableWidget* products_ = nullptr;
    krx_panel::Header days_header_;
    QLabel* days_line_ = nullptr;
    QTableWidget* days_ = nullptr;
    krx_panel::Header summary_header_;
    QTableWidget* summary_ = nullptr;
};

} // namespace fincept::screens
