#pragma once

#include "screens/krx_quant/KrxPanel.h"

#include <QCheckBox>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace fincept::services {
class KrxActionLive;
}

namespace fincept::screens {

class KrxActionCard;

/// KRX 할 일 (fork addition, codeyoma/krx-quant#674, spec #671, see custom/README.md): what to do now,
/// one card a stock, in three words: wait, buy, sell. The card follows the auto trader's champion book
/// (the one rule checked net of costs), with the trade's take-profit and stop prices and the time it sells
/// by, and how that signal did on the days before, in plain Korean. A card turning buy or sell raises a
/// notification and the alert sound once. "자세히" opens the screens with the rest. It draws what its
/// KrxActionLive holds, in Fincept's panels, fonts and theme.
class KrxActionScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KrxActionScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void refresh_theme();
    void render();
    void show_dot();
    void raise_alerts();

    services::KrxActionLive* live_ = nullptr;
    QTimer* clock_timer_ = nullptr; ///< the countdowns and the dot's tooltip
    QMap<QString, QString> seen_;   ///< each stock's last alert key (action::alerts)
    bool first_ = true;             ///< no answer yet since the screen opened or the engine changed

    krx_panel::Header header_;
    QLabel* dot_ = nullptr;
    QLabel* mode_ = nullptr;
    QCheckBox* sound_ = nullptr;
    QPushButton* reload_ = nullptr;
    QLabel* failure_ = nullptr;
    QLabel* empty_ = nullptr; ///< no card: reading, an engine without them, or none
    QVBoxLayout* cards_layout_ = nullptr;
    QList<KrxActionCard*> cards_;
    QLabel* note_ = nullptr;
    QList<QPushButton*> details_;
};

} // namespace fincept::screens
