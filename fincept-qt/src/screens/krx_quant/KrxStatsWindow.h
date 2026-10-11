#pragma once

#include "services/krx_quant/StatsView.h"

#include <QLabel>
#include <QList>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// The calibration curve: how often a class happened against how likely it was forecast,
/// one point per bin with its 95% interval, over the diagonal of a perfect forecast.
/// Hovering a point shows its bin.
class KrxCalibrationPlot : public QWidget {
    Q_OBJECT
  public:
    explicit KrxCalibrationPlot(QWidget* parent = nullptr);
    void set_points(const QList<krx_quant::CalibrationPoint>& points);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    /// The square the probabilities 0–1 span on both axes.
    QRectF plot_rect() const;
    /// Where a forecast and how often it happened go in it.
    QPointF position(double forecast, double observed) const;
    QList<krx_quant::CalibrationPoint> points_;
    int hovered_ = -1;
};

/// A predictor's detail window, on the leaderboard's query: the lift over each baseline
/// with its test, the calibration curve, and the stats by 30-minute slot, model version and
/// symbol. The screen keeps one, not modal; the screen's live state reads the stats with
/// the leaderboard while it is open.
class KrxStatsWindow : public QWidget {
    Q_OBJECT
  public:
    explicit KrxStatsWindow(QWidget* parent = nullptr);

    /// Show ``algorithm``'s detail, emptied until its stats come.
    void open(const QString& algorithm);
    void show_stats(const krx_quant::AlgorithmStats& stats);
    void show_failure(const QString& reason);

  signals:
    void closed();

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void clear();

    QString algorithm_;
    QLabel* query_ = nullptr;
    QTableWidget* overall_ = nullptr; ///< one line: the leaderboard's
    QTableWidget* lifts_ = nullptr;
    KrxCalibrationPlot* calibration_ = nullptr;
    QTabWidget* breakdowns_ = nullptr;
    QTableWidget* by_slot_ = nullptr;
    QTableWidget* by_model_version_ = nullptr;
    QTableWidget* by_symbol_ = nullptr;
    QLabel* note_ = nullptr;
};

} // namespace fincept::screens
