#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/StatsView.h"

#include <QLabel>
#include <QList>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>

#include <optional>

namespace fincept::ui {
class LoadingOverlay;
}

namespace fincept::screens {

/// The calibration curves: how often a class happened against how likely it was forecast,
/// one point per bin with its 95% interval, over the diagonal of a perfect forecast; the
/// classes pooled and each class in its own colour. Hovering a point shows its bin.
class KrxCalibrationPlot : public QWidget {
    Q_OBJECT
  public:
    explicit KrxCalibrationPlot(QWidget* parent = nullptr);
    void set_series(const QList<krx_quant::CalibrationSeries>& series);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    /// The square the probabilities 0–1 span on both axes.
    QRectF plot_rect() const;
    /// Where a forecast and how often it happened go in it.
    QPointF position(double forecast, double observed) const;
    QList<krx_quant::CalibrationSeries> series_;
    QPair<int, int> hovered_{-1, -1}; ///< (curve, point)
};

/// A predictor's detail window, on the leaderboard's query: the lift over each baseline
/// with its test, the calibration curve, and the stats by 30-minute slot, model version and
/// symbol, in Fincept's panels and tables. A predictor of another kind than the direction
/// (krx-quant #182) shows its kind's stats columns and classes, and a range predictor its
/// quantile stats too. The screen opens one per predictor, not modal,
/// so several can be compared side by side; the screen's live state reads each one's stats
/// with the leaderboard while it is open.
class KrxStatsWindow : public QWidget {
    Q_OBJECT
  public:
    explicit KrxStatsWindow(QWidget* parent = nullptr);

    /// Show ``algorithm``'s detail, emptied and loading until its stats come.
    void open(const QString& algorithm);
    /// ``kind``: the predictor's kind as the engine describes it (none: the direction).
    void show_stats(const krx_quant::AlgorithmStats& stats, const std::optional<krx_quant::KindInfo>& kind = {});
    void show_failure(const QString& reason);
    /// What the engine says about the algorithm (krx-quant #163): the "학습·예측 방식" section;
    /// none for a baseline or a consensus, or before the engine answers; ``failure``: why the
    /// engine's descriptions could not be read (none shown then, not a stale one).
    void show_about(const std::optional<krx_quant::AlgorithmInfo>& info, const QString& failure = {});

  signals:
    void closed();

  protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void clear();
    /// Paint in the theme's colors and the user's font size now.
    void restyle();
    /// The stats columns of ``kind`` (none: the direction's) on the overall line and the breakdowns.
    void set_kind(const std::optional<krx_quant::KindInfo>& kind);

    QString algorithm_;
    krx_panel::Header header_; ///< the predictor; the query and a failure on the right
    QLabel* query_ = nullptr;
    QLabel* failure_ = nullptr;
    QWidget* body_ = nullptr;
    ui::LoadingOverlay* loading_ = nullptr;
    krx_panel::Header about_header_;    ///< "학습·예측 방식": the training loss's meaning on its tooltip
    QLabel* about_ = nullptr;           ///< the section's labelled lines
    krx_panel::Header learning_header_; ///< "학습과 검증" (krx-quant #165): the losses' meaning on its tooltip
    QLabel* learning_ = nullptr;        ///< its labelled lines; hidden for a baseline or the consensus
    QLabel* excluded_ = nullptr;        ///< what the stats left out
    QTableWidget* overall_ = nullptr;   ///< one line: the leaderboard's
    krx_panel::Header lift_header_;     ///< what lift is on its tooltip
    QTableWidget* lifts_ = nullptr;
    krx_panel::Header calibration_header_; ///< what the plot compares on its tooltip
    KrxCalibrationPlot* calibration_ = nullptr;
    krx_panel::Header breakdown_header_;
    QTabWidget* breakdowns_ = nullptr;
    QTableWidget* by_slot_ = nullptr;
    QTableWidget* by_model_version_ = nullptr;
    QTableWidget* by_symbol_ = nullptr;
    QString kind_ = QStringLiteral("direction"); ///< the kind whose columns are shown
    QWidget* quantiles_ = nullptr;               ///< a range predictor's quantile stats (krx-quant #182)
    krx_panel::Header quantiles_header_;
    QLabel* quantile_summary_ = nullptr;
    QTableWidget* quantile_table_ = nullptr;
};

} // namespace fincept::screens
