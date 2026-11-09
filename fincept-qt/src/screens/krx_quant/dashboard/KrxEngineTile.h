#pragma once

#include "screens/dashboard/widgets/BaseWidget.h"

#include <QList>
#include <QString>

class QLabel;
class QTableWidget;

namespace fincept::services {
class EnginePoller;
class KrxQuantService;
} // namespace fincept::services

namespace fincept::screens::krx_dashboard {

/// A dashboard tile that reads the KRX prediction engine (codeyoma/krx-quant#302) through its own
/// EnginePoller (codeyoma/krx-quant#649): at once and then ``every_ms`` after each answer while it is
/// shown, never while it is hidden.
///
/// Without the engine's address (set on the KRX screen) it says where to set it and reads nothing.
/// A failed read keeps the last values on screen with why above them; with nothing shown yet, the
/// tile's error overlay says it. The title shows when the engine last answered.
class KrxEngineTile : public widgets::BaseWidget {
    Q_OBJECT
  public:
    KrxEngineTile(const QString& title, int every_ms, QWidget* parent = nullptr);

  protected:
    services::KrxQuantService* service() const;
    /// Where the subclass puts its content, below the failure line.
    QVBoxLayout* body() const { return body_; }

    /// Ask the engine; the subclass ends it with read_done(), read_failed() or read_again(). A new
    /// address drops what is under way (the service's base_url_changed: forget the old engine's).
    virtual void read() = 0;
    /// Draw again from what was last read (a theme change).
    virtual void render() = 0;

    /// The read ended with something to show; ``problem``: what part of it could not be read.
    void read_done(const QString& problem = {});
    void read_failed(const QString& reason);
    /// The answer that came is not the one wanted now (the query changed while it was read): end
    /// this read without showing it and read again at once.
    void read_again();
    /// Change the title's head (the part before the time of the last answer).
    void set_head(const QString& head);

    /// A table as the KRX screen draws them (krx_tables::table), its last column stretched and
    /// painted again on a theme change. Double-clicking a row opens the KRX screen.
    QTableWidget* make_table(const QStringList& headers);

    void on_theme_changed() override;

  private:
    void tick();
    void update_title();

    QString head_;
    services::EnginePoller* poller_ = nullptr;
    QLabel* failure_ = nullptr;
    QVBoxLayout* body_ = nullptr;
    bool shown_data_ = false; ///< something read is on screen
    bool reading_ = false;    ///< a read is under way: the next tick waits for it
    QList<QTableWidget*> tables_;
};

} // namespace fincept::screens::krx_dashboard
