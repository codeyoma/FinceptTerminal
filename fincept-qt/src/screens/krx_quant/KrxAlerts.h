#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/Signals.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

#include <optional>

namespace fincept::screens {

/// The KRX screen's signal alert settings (on, the threshold, N, the sound; kept in
/// QSettings `krx_quant/signal_*`), the last signal, and a test notification. A signal goes
/// out through Fincept's notifications (an in-app toast and the OS's) with the OS alert
/// sound if it is on.
///
/// The threshold and N follow the engine's rule (codeyoma/krx-quant#617) unless the user set
/// them here: a value set here is saved and wins, and says so ("직접 설정"); "엔진 기준으로"
/// removes the saved values. Each value is saved only when the user sets it.
class KrxAlerts : public QWidget {
    Q_OBJECT
  public:
    explicit KrxAlerts(QWidget* parent = nullptr);

    bool is_on() const;
    /// The threshold and N saved on the KRX screen; none where they follow the engine's. The
    /// dashboard's signals widget judges by them too (codeyoma/krx-quant#302, #617).
    static krx_quant::SavedRule saved();
    /// The engine's rule (KrxLive::engine_rule): shown where nothing is saved.
    void set_engine_rule(const std::optional<krx_quant::SignalRule>& rule);
    /// Show the signal here and send it out.
    void raise(const krx_quant::Signal& signal);
    /// Paint in the theme's colors and font now.
    void restyle();

  signals:
    /// The user turned the alerts on or off, changed the threshold or N, or went back to the
    /// engine's.
    void changed();

  private:
    /// The rule in force in the spin boxes, and where it comes from.
    void show_rule();
    void notify(const QString& text);

    krx_panel::Header header_; ///< the panel's title; the alert rules on its tooltip

    QCheckBox* on_ = nullptr;
    QSpinBox* probability_ = nullptr;
    QSpinBox* agree_ = nullptr;
    QLabel* source_ = nullptr;         ///< "엔진 기준", "직접 설정(확률) · 엔진 기준 70% · 2개", …
    QPushButton* to_engine_ = nullptr; ///< "엔진 기준으로": shown while a value is saved
    QCheckBox* sound_ = nullptr;
    QLabel* last_ = nullptr;
    std::optional<krx_quant::SignalRule> engine_rule_;
};

} // namespace fincept::screens
