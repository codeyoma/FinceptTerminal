#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/BarView.h"

#include <QDateTime>
#include <QLabel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QWidget>

#include <optional>

namespace fincept::ui {
class LoadingOverlay;
}

namespace fincept::screens {

/// A bar window's four-minute mini chart (krx_quant::MiniChart, krx-quant #255): the bar and the four
/// 1-minute bars after it as candles, the four minutes from its bar close shaded as the chart's hover
/// shades them, the levels across them (the touch targets, the range's highest and lowest mid-points,
/// the barrier's U and L; dashed until their answer is in) with their names and prices on the right,
/// and a dot where each was first reached. Hovering a candle or a dot says what it is.
class KrxBarChart : public QWidget {
    Q_OBJECT
  public:
    explicit KrxBarChart(QWidget* parent = nullptr);
    void set_chart(const krx_quant::MiniChart& chart);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    /// The area the five minutes and the prices span: the labels on the right, the times below.
    QRectF plot_rect() const;
    krx_quant::MiniChart chart_;
};

/// A bar's window (krx-quant #255): a 1-minute bar of the chart double-clicked opens one for that bar,
/// one window per bar as the detail windows are one per predictor, so several bars can be compared
/// side by side; the screen raises the one already open. In Fincept's look as the detail window: its
/// name, background, panel headers, shared tables and loading overlay. Its sections are the bar's
/// prices and special situations, every direction prediction of its bar close, the range's and the
/// barrier's predictions there whatever kind the screen shows, the answer in detail, and the mini
/// chart of its four minutes. The screen's live state reads it again while its bar is not settled.
class KrxBarWindow : public QWidget {
    Q_OBJECT
  public:
    explicit KrxBarWindow(QWidget* parent = nullptr);

    /// Show the bar of ``symbol`` that closed at ``bar_close``, named ``title`` (krx_quant::bar_title):
    /// loading until its read comes the first time; brought to the front.
    void open(const QString& symbol, const QDateTime& bar_close, const QString& title);
    const QString& symbol() const { return symbol_; }
    const QDateTime& bar_close() const { return bar_close_; }
    /// What was read of the bar: ``kinds``, the engine's (GET /v1/kinds); ``selection``, the models
    /// selected for the chart (the selected consensus's row, krx-quant #159); ``now``, the engine clock.
    void show_read(const krx_quant::BarRead& read, const std::optional<QList<krx_quant::KindInfo>>& kinds,
                   const QStringList& selection, const QDateTime& now);
    /// The bar could not be read: what was shown stays, the header says why.
    void show_failure(const QString& reason);
    /// The models selected for the chart changed: the direction's rows again.
    void set_selection(const QStringList& selection);

  signals:
    void closed();

  protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    /// Show the last read again (the selection or the theme changed).
    void render();
    /// Paint in the theme's colors and the user's font size now.
    void restyle();

    QString symbol_;
    QDateTime bar_close_;
    std::optional<krx_quant::BarRead> read_;
    std::optional<QList<krx_quant::KindInfo>> kinds_;
    QStringList selection_;
    QDateTime now_;

    krx_panel::Header header_; ///< the bar; its state and a failure on the right
    QLabel* state_ = nullptr;
    QLabel* failure_ = nullptr;
    QWidget* body_ = nullptr;
    ui::LoadingOverlay* loading_ = nullptr;
    krx_panel::Header info_header_; ///< "봉 정보"
    QLabel* info_ = nullptr;
    krx_panel::Header answer_header_; ///< "정답 상세"
    QLabel* answer_ = nullptr;
    krx_panel::Header chart_header_; ///< "4분 미니 차트"
    KrxBarChart* chart_ = nullptr;
    krx_panel::Header direction_header_; ///< "방향 예측", the answer on the right
    QLabel* direction_note_ = nullptr;
    QTableWidget* direction_ = nullptr;
    /// The range's and the barrier's sections, in krx_quant::bar_kinds' order.
    struct KindPanel {
        krx_panel::Header header;
        QLabel* note = nullptr;
        QTableWidget* table = nullptr;
    };
    QList<KindPanel> kind_panels_;
};

} // namespace fincept::screens
