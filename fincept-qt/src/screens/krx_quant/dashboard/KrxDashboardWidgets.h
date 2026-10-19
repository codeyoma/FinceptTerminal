#pragma once

#include "screens/krx_quant/dashboard/KrxEngineTile.h"
#include "services/krx_quant/EngineData.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QStringList>

#include <optional>

class QLabel;
class QTableWidget;

namespace fincept::screens {
class KrxLogos;
}

/// The dashboard's KRX widgets (codeyoma/krx-quant#302): what the KRX 4-minute prediction screen
/// shows at length, as tiles beside the other markets. Each reads the engine on its own while it is
/// shown; what they show is worked out in krx_quant::dashboard (DashboardView.h).
namespace fincept::screens::krx_dashboard {

/// Refresh intervals (#302): a signal holds four minutes from a bar close a minute, the leaderboard
/// changes as predictions are scored, the status by the second, open-to-close twice a day.
inline constexpr int kSignalsEveryMs = 5'000;
inline constexpr int kLeaderboardEveryMs = 60'000;
inline constexpr int kStatusEveryMs = 30'000;
inline constexpr int kOpenToCloseEveryMs = 300'000;

/// Each symbol's latest final consensus, whether it is a signal by the KRX screen's rule, and
/// whether the latest scored one was right. It raises no alert: the KRX screen alone does.
class KrxSignalsWidget : public KrxEngineTile {
    Q_OBJECT
  public:
    explicit KrxSignalsWidget(QWidget* parent = nullptr);

  protected:
    void read() override;
    void render() override;

  private:
    /// Read the next symbol's recent final consensus (the latest scored one), one at a time.
    void read_next_recent();

    QTableWidget* table_ = nullptr;
    KrxLogos* logos_ = nullptr;
    std::optional<services::KrxQuantService::Snapshot> snapshot_;
    QMap<QString, QList<krx_quant::Prediction>> recent_; ///< by symbol: its final consensus over the latest bar closes
    QMap<QString, QDateTime> recent_for_;                ///< the latest bar close each was read after
    QMap<QString, QDateTime> queued_for_;                ///< the symbols to read, and after which bar close
    QString recent_reading_;                             ///< the symbol being read; empty when none
};

/// The leaderboard's top predictors, the hit rate first, and the baseline to beat. The query
/// (symbol, period, kind) is the tile's config.
class KrxLeaderboardWidget : public KrxEngineTile {
    Q_OBJECT
  public:
    explicit KrxLeaderboardWidget(const QJsonObject& config, QWidget* parent = nullptr);

    QJsonObject config() const override;
    void apply_config(const QJsonObject& config) override;

  protected:
    QDialog* make_config_dialog(QWidget* parent) override;
    void read() override;
    void render() override;

  private:
    /// The query as the title says it: "방향 · 오늘 · 전체".
    QString query_title() const;

    services::KrxQuantService::LeaderboardQuery query_;
    std::optional<krx_quant::Leaderboard> board_;
    QStringList symbols_;                                 ///< the engine's, for the config's choices
    std::optional<QList<krx_quant::KindInfo>> kinds_;     ///< likewise
    QTableWidget* table_ = nullptr;
};

/// What the engine is doing: the market phase, KIS, today's special situations, last night's
/// training, the CPU budget; the first trouble on top.
class KrxStatusWidget : public KrxEngineTile {
    Q_OBJECT
  public:
    explicit KrxStatusWidget(QWidget* parent = nullptr);

  protected:
    void read() override;
    void render() override;

  private:
    std::optional<krx_quant::EngineStatus> status_;
    QLabel* headline_ = nullptr;
    QTableWidget* table_ = nullptr;
};

/// Today's open-to-close call (krx-quant #264): each symbol's algorithms' mean at 08:50 and the
/// day's answer after the close.
class KrxOpenToCloseWidget : public KrxEngineTile {
    Q_OBJECT
  public:
    explicit KrxOpenToCloseWidget(QWidget* parent = nullptr);

  protected:
    void read() override;
    void render() override;

  private:
    void read_next_latest();
    /// The engine makes no open_to_close (one before krx-quant #293, or before #179 without kinds).
    void show_missing();

    QList<krx_quant::SymbolQuote> symbols_;
    QDate today_;
    std::optional<krx_quant::KindInfo> kind_;
    bool missing_ = false;
    QMap<QString, krx_quant::KindLatest> latest_;
    QStringList queue_;    ///< the symbols still to read this round
    QStringList problems_; ///< what this round could not read
    QLabel* missing_label_ = nullptr;
    QTableWidget* table_ = nullptr;
    KrxLogos* logos_ = nullptr;
};

} // namespace fincept::screens::krx_dashboard
