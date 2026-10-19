#pragma once

#include "screens/dashboard/widgets/BaseWidget.h"

#include <QList>
#include <QString>

class QLabel;
class QTableWidget;
class QTimer;

namespace fincept::services {
class KrxQuantService;
}

namespace fincept::screens::krx_dashboard {

/// A dashboard tile that reads the KRX prediction engine (codeyoma/krx-quant#302) through its own
/// KrxQuantService: at once and then every ``every_ms`` while it is shown, never while it is hidden.
///
/// Without the engine's address (set on the KRX screen) it says where to set it and reads nothing.
/// A failed read keeps the last values on screen with why above them; with nothing shown yet, the
/// tile's error overlay says it. The title shows when the engine last answered.
class KrxEngineTile : public widgets::BaseWidget {
    Q_OBJECT
  public:
    KrxEngineTile(const QString& title, int every_ms, QWidget* parent = nullptr);

  protected:
    services::KrxQuantService* service() const { return service_; }
    /// Where the subclass puts its content, below the failure line.
    QVBoxLayout* body() const { return body_; }

    /// Ask the engine; the subclass ends it with read_done() or read_failed().
    virtual void read() = 0;
    /// Draw again from what was last read (a theme change).
    virtual void render() = 0;

    /// The read ended with something to show; ``problem``: what part of it could not be read.
    void read_done(const QString& problem = {});
    void read_failed(const QString& reason);
    /// Change the title's head (the part before the time of the last answer).
    void set_head(const QString& head);

    /// A table as the KRX screen draws them (krx_tables::table), its last column stretched and
    /// painted again on a theme change. Double-clicking a row opens the KRX screen.
    QTableWidget* make_table(const QStringList& headers);

    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void on_theme_changed() override;

  private:
    void tick();
    void update_title();

    QString head_;
    QString answered_at_; ///< "HH:mm:ss" of the last answer; empty before any
    services::KrxQuantService* service_ = nullptr;
    QTimer* timer_ = nullptr;
    QLabel* failure_ = nullptr;
    QVBoxLayout* body_ = nullptr;
    bool shown_data_ = false; ///< something read is on screen
    bool reading_ = false;    ///< a read is under way: the next tick waits for it
    QList<QTableWidget*> tables_;
};

/// Open the KRX 4-minute prediction screen.
void open_krx_screen();

} // namespace fincept::screens::krx_dashboard
