#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/Signals.h"

#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
#include <QWidget>

namespace fincept::screens {

/// The KRX screen's signal alert settings (on, the threshold, N, the sound; kept in
/// QSettings `krx_quant/signal_*`), the last signal, and a test notification. A signal goes
/// out through Fincept's notifications (an in-app toast and the OS's) with the OS alert
/// sound if it is on.
class KrxAlerts : public QWidget {
    Q_OBJECT
  public:
    explicit KrxAlerts(QWidget* parent = nullptr);

    bool is_on() const;
    krx_quant::SignalRule rule() const;
    /// The rule as last saved on the KRX screen (its threshold and N): the dashboard's signals widget
    /// judges by it too (codeyoma/krx-quant#302).
    static krx_quant::SignalRule saved_rule();
    /// Show the signal here and send it out.
    void raise(const krx_quant::Signal& signal);
    /// Paint in the theme's colors and font now.
    void restyle();

  signals:
    /// The user turned the alerts on or off, or changed the threshold or N.
    void changed();

  private:
    void save();
    void notify(const QString& text);

    krx_panel::Header header_; ///< the panel's title; the alert rules on its tooltip

    QCheckBox* on_ = nullptr;
    QSpinBox* probability_ = nullptr;
    QSpinBox* agree_ = nullptr;
    QCheckBox* sound_ = nullptr;
    QLabel* last_ = nullptr;
};

} // namespace fincept::screens
