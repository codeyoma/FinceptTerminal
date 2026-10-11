#pragma once

#include "services/krx_quant/Board.h"
#include "services/krx_quant/KrxQuantService.h"
#include "services/krx_quant/KrxStream.h"
#include "services/krx_quant/Signals.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

#include <optional>

namespace fincept::screens {

class KrxChart;

/// KRX 4-minute predictions (fork addition, see custom/README.md): the prediction engine's
/// status and macro row, each symbol's latest predictions, the chosen symbol's chart with
/// the final consensus over it, the leaderboard, and signal alerts. It follows the engine's
/// stream and reads the engine's API on connecting and every few seconds. The text is Korean.
class KrxQuantScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KrxQuantScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void build_alerts(QWidget* box);
    void build_leaderboard(QWidget* box);

    void apply_base_url();
    void tick();
    void show_snapshot(const services::KrxQuantService::Snapshot& snapshot);
    void show_failure(const QString& reason);
    void show_status(const krx_quant::EngineStatus& status);
    void show_status_line();
    void show_footer();
    /// The engine clock now, from a status or a stream event: the countdowns follow it.
    void set_engine_clock(const QDateTime& now);
    void show_macro(const krx_quant::Macro& macro);
    void show_chart(const QString& symbol, const QList<krx_quant::Bar>& bars,
                    const QList<krx_quant::Prediction>& finals);
    void show_leaderboard(const krx_quant::Leaderboard& board);
    void on_event(const krx_quant::Event& event);
    void on_stream_connected();
    void on_stream_lost(const QString& reason, int retry_in);

    /// A final consensus seen at ``now`` (the engine clock): raise its signal, if any.
    void consider(const krx_quant::Prediction& final, const QDateTime& now);
    void raise(const krx_quant::Signal& signal);
    /// A desktop notification (and the sound, if on).
    void notify(const QString& text);
    void save_alert_settings();

    void select(const QString& symbol);
    void fill_symbols();
    void fill_predictions();
    void fill_chart();
    void update_countdowns();
    void read_leaderboard();
    services::KrxQuantService::LeaderboardQuery leaderboard_query() const;
    QString basis() const;
    QDateTime engine_now() const;

    services::KrxQuantService* service_ = nullptr;
    services::KrxStream* stream_ = nullptr;
    krx_quant::Board board_;
    krx_quant::Alerts alerts_;
    QString selected_;
    QList<krx_quant::Bar> bars_; ///< the selected symbol's
    std::optional<krx_quant::EngineStatus> status_;
    qint64 clock_offset_ms_ = 0; ///< the engine clock minus this one
    QString stream_text_;
    QString failure_;         ///< why the engine could not be read, until it can again
    QStringList read_errors_; ///< what the last refresh could not read
    int ticks_ = 0;

    QLineEdit* url_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* macro_label_ = nullptr;
    QLabel* special_label_ = nullptr;
    QTableWidget* symbols_table_ = nullptr;
    QLabel* chart_title_ = nullptr;
    QComboBox* window_combo_ = nullptr;
    KrxChart* chart_ = nullptr;
    QLabel* panel_title_ = nullptr;
    QTableWidget* predictions_table_ = nullptr;
    QComboBox* period_combo_ = nullptr;
    QComboBox* basis_combo_ = nullptr;
    QComboBox* board_symbol_combo_ = nullptr;
    QCheckBox* special_check_ = nullptr;
    QTableWidget* board_table_ = nullptr;
    QLabel* board_note_ = nullptr;
    QCheckBox* alert_check_ = nullptr;
    QSpinBox* probability_spin_ = nullptr;
    QSpinBox* agree_spin_ = nullptr;
    QCheckBox* sound_check_ = nullptr;
    QLabel* last_signal_label_ = nullptr;
    QLabel* footer_label_ = nullptr;
    QTimer* timer_ = nullptr;       ///< reads every few seconds
    QTimer* clock_timer_ = nullptr; ///< the countdowns, every second
    QTimer* board_soon_ = nullptr;  ///< the leaderboard, a moment after outcomes come in
};

} // namespace fincept::screens
