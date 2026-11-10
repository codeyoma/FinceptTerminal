#pragma once

#include "screens/krx_quant/KrxPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace fincept::services {
class KrxVerdictLive;
}

namespace fincept::screens {

class KrxVerdictCard;

/// KRX 종합 판단 (fork addition, codeyoma/krx-quant#678, see custom/README.md): one call per symbol
/// from every barrier algorithm that took part, first in the KRX menu. A card per symbol shows, large,
/// 사세요 / 파세요 / 기다리세요 with its strength, a buy's maximum holding time with its target and
/// stop, and the expected net of a share bought now per horizon (4/15/30/60 minutes). Clicking a card
/// shows that symbol's members below (name, kind and horizon, family, status, evidence, probabilities,
/// lean, weight) and the direction and range forecasts beside them. It reads GET /v1/verdict through its
/// KrxVerdictLive and only draws what that holds, in Fincept's panels, tables, fonts and theme. The
/// text is Korean. It places no order.
class KrxVerdictScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KrxVerdictScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void refresh_theme();
    void render();
    void show_dot();
    void fill_cards();
    void fill_members();
    void fill_side();

    services::KrxVerdictLive* live_ = nullptr;
    QTimer* clock_timer_ = nullptr;
    krx_panel::Header header_;
    QLabel* dot_ = nullptr;
    QPushButton* reload_ = nullptr;
    QLabel* failure_ = nullptr;
    QLabel* as_of_ = nullptr;
    krx_panel::Header cards_header_;
    QHBoxLayout* cards_row_ = nullptr;
    QLabel* empty_ = nullptr;
    QList<KrxVerdictCard*> cards_;
    krx_panel::Header members_header_;
    QLabel* members_empty_ = nullptr;
    QTableWidget* members_ = nullptr;
    krx_panel::Header side_header_;
    QTableWidget* side_ = nullptr;
};

} // namespace fincept::screens
