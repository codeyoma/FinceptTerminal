#pragma once

#include "services/krx_quant/Board.h"

#include <QList>
#include <QMap>
#include <QWidget>

namespace fincept::screens {

/// A symbol's 1-minute bars of the day with the final consensus over them: at each bar
/// close an arrow for the class it predicted (up below the bar, down above it, flat as a
/// dash), and once scored a ✓ or ✗ beside it (? when it could not be scored on the basis
/// shown). Predictions a VI, circuit breaker or sidecar touched are drawn faint: the stats
/// leave them out. Hovering a bar shows its prices and its prediction.
class KrxChart : public QWidget {
    Q_OBJECT
  public:
    explicit KrxChart(QWidget* parent = nullptr);

    void set_data(const QList<krx_quant::Bar>& bars, const QList<krx_quant::Mark>& marks);
    /// Show the last ``minutes`` bars; 0 for the whole day.
    void set_window(int minutes);
    /// What to say when there are no bars (e.g. why they could not be read).
    void set_empty_text(const QString& text);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    struct Frame; ///< where the visible bars go, for painting and for hovering
    Frame frame() const;
    /// The index in bars_ of the first bar shown: the window's start.
    qsizetype first_shown() const;

    QList<krx_quant::Bar> bars_;
    QMap<QDateTime, krx_quant::Mark> marks_; ///< by the start of the bar that closed at their bar close
    int window_ = 60;
    QString empty_text_;
    int hovered_ = -1; ///< index into the visible bars
};

} // namespace fincept::screens
