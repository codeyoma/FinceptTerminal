#pragma once

#include "services/krx_quant/Board.h"
#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/Signals.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QWidget>

class QLabel;
class QWebEngineView;

namespace fincept::screens {

/// What the chart's page and the screen say to each other over the web channel: the page
/// asks for what to draw once it listens and says where its view is; the screen sends the
/// theme, the data, the bars' hover boxes (those changed) and fits.
class KrxChartBridge : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;

    /// The page listens: it is sent the theme and the data now.
    Q_INVOKABLE void ready();
    /// An icon was clicked: its bar close (ISO 8601).
    Q_INVOKABLE void clicked(const QString& bar_close);
    /// The view moved: it starts ``from`` bars (a logical index) from the left end.
    Q_INVOKABLE void moved(double from);
    /// The bars in view start from ``from`` to ``to`` (the chart's times, chart_time).
    Q_INVOKABLE void range(double from, double to);

  signals:
    void page_ready();
    void icon_clicked(const QString& bar_close);
    void view_moved(double from);
    void range_moved(double from, double to);
    void theme_changed(const QString& json);
    void data_changed(const QString& json);
    /// Hover boxes to set and drop: {"reset", "set": [...], "drop": [times]} (krx-quant #159).
    void hovers_changed(const QString& json);
    void live_changed(const QString& json);
    void fit_requested();
};

/// A symbol's 1-minute bars of the day, drawn by TradingView Lightweight Charts in a web
/// view (src/screens/krx_quant/chart, bundled as Qt resources): wheel to zoom, drag to
/// scroll, double-click (or fit()) for the last 60 bars, volumes in a pane below and the
/// last price on the price axis. Over the bars, the final consensus's icons
/// (ChartModel: ▲▼– and ✓/✗/?, faint for special predictions). Without Qt WebEngine, at
/// build or at run time, it says the chart cannot open.
class KrxChart : public QWidget {
    Q_OBJECT
  public:
    explicit KrxChart(QWidget* parent = nullptr);

    /// The bars, the icons of ``marks`` (the final consensus's, or the selected models',
    /// krx-quant #159) judged as signals under ``rule`` (the alerts'), and each 1-minute bar's
    /// hover box (``hovers``): hovering a bar shows its box and links it to the bar it aims at.
    /// ``minutes`` is the view (krx-quant #109): the 1-minute bars are drawn as bars of that
    /// many minutes, and only the 1-minute view has icons and boxes. In a candle view
    /// (krx_quant::candle_view: day, week, month, year; krx-quant #111, #112) ``bars`` are
    /// its candles, drawn as they are, with dates only on the axis. Only the boxes that
    /// changed are sent again; one shown stays shown.
    void set_data(const QList<krx_quant::Bar>& bars, const QList<krx_quant::Mark>& marks,
                  const QList<krx_quant::BarHover>& hovers = {}, const krx_quant::SignalRule& rule = {},
                  int minutes = 1);
    /// Only the last bar of ``bars`` moved (the bar being made, krx-quant #110): it alone is
    /// drawn again, in the view of ``minutes``. A candle view's ``bars`` are its candles.
    void update_live(const QList<krx_quant::Bar>& bars, int minutes);
    /// Show the last 60 bars again.
    void fit();
    /// Paint in the theme's colors and the user's font now.
    void restyle();

  signals:
    /// A final consensus icon was clicked: the table shows that bar close's judgements
    /// (krx-quant #107).
    void icon_clicked(const QDateTime& bar_close);
    /// The view was scrolled or zoomed: it starts ``from`` bars (a logical index, negative
    /// past the first bar) from the left end; near it, the bars before are read (krx-quant #108).
    void view_moved(double from);
    /// The bars in view now start from ``from`` to ``to`` (chart_time): the score over them
    /// (krx-quant #159).
    void view_range(qint64 from, qint64 to);

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    /// The web view, made when first shown: only then is the WebEngine runtime started.
    void ensure_view();

    KrxChartBridge* bridge_ = nullptr;
    QWebEngineView* view_ = nullptr;
    QLabel* unavailable_ = nullptr;
    QString theme_; ///< the last sent, sent again when the page is ready
    QString data_;
    QHash<qint64, QString> hovers_; ///< the hover boxes sent, by bar time
};

} // namespace fincept::screens
