#pragma once

#include "screens/krx_quant/KrxPanel.h"

#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace fincept::services {
class KrxBriefingLive;
}

namespace fincept::screens {

class KrxLogos;
class KrxNightLine;

/// KRX pre-open briefing (fork addition, codeyoma/krx-quant#304, see custom/README.md): what the
/// day's open is weighed with, before 09:00, in one screen. The overnight US semiconductors and the
/// day's regime, the KOSPI200 night session, each symbol's evening, NXT pre-market and KRX expected
/// open with the previous day's net buys, the 08:50 open-to-close forecast and how each input moved
/// it, and the day's calendar. It draws what its KrxBriefingLive holds, in Fincept's panels,
/// tables, fonts and theme. The text is Korean.
class KrxBriefingScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KrxBriefingScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void refresh_theme();
    void render();
    void show_dot();
    void fill_us();
    void fill_symbols();
    void fill_forecasts();
    void fill_inputs();
    void fill_events();
    /// Show ``offset`` days from the day shown (0: the engine's default day).
    void move_day(int offset);
    QTableWidgetItem* symbol_item(QTableWidget* table, const QString& symbol);

    services::KrxBriefingLive* live_ = nullptr;
    KrxLogos* logos_ = nullptr;
    QTimer* clock_timer_ = nullptr; ///< the dot's tooltip (the next read's countdown)
    QString selected_;              ///< the symbol whose inputs are shown; empty: the first

    krx_panel::Header header_;
    QLabel* dot_ = nullptr;
    QLabel* headline_ = nullptr;
    QPushButton* previous_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* today_ = nullptr;
    QPushButton* reload_ = nullptr;
    QLabel* failure_ = nullptr;

    krx_panel::Header us_header_;
    QTableWidget* us_table_ = nullptr;
    QLabel* regime_ = nullptr;
    krx_panel::Header night_header_;
    QLabel* night_text_ = nullptr;
    KrxNightLine* night_line_ = nullptr;
    krx_panel::Header symbols_header_;
    QTableWidget* symbols_table_ = nullptr;
    krx_panel::Header forecast_header_;
    QTableWidget* forecast_table_ = nullptr;
    krx_panel::Header inputs_header_;
    QTableWidget* inputs_table_ = nullptr;
    krx_panel::Header events_header_;
    QLabel* flags_ = nullptr;
    QTableWidget* events_table_ = nullptr;
};

} // namespace fincept::screens
