#pragma once

#include "services/krx_quant/Board.h"
#include "services/krx_quant/ChartBricks.h"
#include "services/krx_quant/ChartLook.h"
#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/Signals.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <optional>

class QJsonArray;
class QJsonObject;
class QJsonValue;
class QLabel;
class QWebEngineView;

namespace fincept::screens {

/// What the chart's page and the screen say to each other over the web channel: the page
/// asks for what to draw once it listens and says where its view is; the screen sends the
/// theme, the data, the bars' hover boxes (those changed) and fits. The drawings (krx-quant #224)
/// go both ways: the screen sends them and the tool chosen, the page what was drawn, moved,
/// removed or double-clicked, in bar positions ([logical, price]) on the bars it has, which start
/// at ``first`` (chart_time).
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
    /// The anchored VWAP's bar was chosen by a click, or its marker dragged (krx-quant #222): its
    /// time (chart_time).
    Q_INVOKABLE void anchored(double time);
    /// A legend row's button (krx-quant #222): ``action`` "hide" (the eye), "settings" or
    /// "remove" on the indicator ``key`` (krx_quant::study_key).
    Q_INVOKABLE void legend(const QString& key, const QString& action);
    /// A border between panes was dragged (krx-quant #223): the lower panes' heights now, a JSON
    /// object by indicator ({"rsi": 140}, CSS pixels).
    Q_INVOKABLE void panes(const QString& heights);
    /// A drawing was placed with the tool (krx-quant #224): {"tool", "points": [[logical, price]…],
    /// "first", "x", "y"}, its last point at (x, y) in the page.
    Q_INVOKABLE void drawn(const QString& json);
    /// A drawing was dragged: {"id", "grip" (the point dragged; -1: the whole drawing), "points",
    /// "first"}.
    Q_INVOKABLE void reshaped(const QString& json);
    /// The selected drawing was removed (Delete or Backspace).
    Q_INVOKABLE void erased(int id);
    /// A drawing was double-clicked at (``x``, ``y``) in the page.
    Q_INVOKABLE void edit(int id, double x, double y);
    /// The tool placed its drawing or was cancelled (Esc): the page is back at the cursor.
    Q_INVOKABLE void tool_ended();

  signals:
    void page_ready();
    void icon_clicked(const QString& bar_close);
    void view_moved(double from);
    void range_moved(double from, double to);
    void anchor_moved(double time);
    void legend_clicked(const QString& key, const QString& action);
    void panes_resized(const QString& heights);
    void drawing_drawn(const QString& json);
    void drawing_reshaped(const QString& json);
    void drawing_erased(int id);
    void drawing_edit(int id, double x, double y);
    void tool_done();
    void theme_changed(const QString& json);
    void data_changed(const QString& json);
    /// Hover boxes to set and drop: {"reset", "set": [...], "drop": [times]} (krx-quant #159).
    void hovers_changed(const QString& json);
    void live_changed(const QString& json);
    /// The baseline style's base price moved with the view (krx-quant #221): {"base"}.
    void base_changed(const QString& json);
    /// The volume profile of the bars now in view (krx-quant #222): {"color", "rows": [{"low",
    /// "high", "volume"}]}, or {} when it is not drawn.
    void profile_changed(const QString& json);
    /// The drawings (krx-quant #224): {"items": [{"id", "tool", "points": [[logical, price]…],
    /// "times", "color", "width", "style", "text"}], "magnet", "hidden", "select"}.
    void drawings_changed(const QString& json);
    /// The drawing tool chosen (krx-quant #224): {"tool", "points", "color", "width", "style"}; {}
    /// for the cursor.
    void tool_changed(const QString& json);
    void fit_requested();
};

/// A symbol's 1-minute bars of the day, drawn by TradingView Lightweight Charts in a web
/// view (src/screens/krx_quant/chart, bundled as Qt resources): wheel to zoom, drag to
/// scroll, double-click (or fit()) for the last 60 bars, the lower indicators (the volume among
/// them) each in a pane below, their borders dragged to resize them (krx-quant #223), and the
/// last price on the price axis. Over the bars, the final consensus's icons
/// (ChartModel: ▲▼– and ✓/✗/?, faint for special predictions). Over the prices, the symbol's
/// drawings (krx-quant #224): placed with the tool chosen, picked, dragged, removed and
/// double-clicked on the page, kept as moments and prices by the screen. Without Qt WebEngine, at
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
    /// changed are sent again; one shown stays shown. ``look`` is the chart's style and whether a
    /// minute view fills its days' first bars (krx-quant #221): a Heikin-Ashi chart draws its
    /// bars as Heikin-Ashi bars and its price line at the last bar's own close; a baseline chart
    /// colors the price above and below its base (krx_quant::baseline_base, again as a candle
    /// view scrolls). A style without a time axis (krx-quant #225) draws the bricks of the bars
    /// (krx_quant::make_bricks) in order, a slot each, its axis and crosshair at the time each ended,
    /// its volumes and indicators those of the bricks: from a start fixed once for the view's bars
    /// (krx_quant::brick_start: none drawn until it is), at the size set or ATR(14) of the bars as
    /// they are now. It has no icons, hover boxes, four-minute windows, day separators or drawings.
    void set_data(const QList<krx_quant::Bar>& bars, const QList<krx_quant::Mark>& marks,
                  const QList<krx_quant::BarHover>& hovers = {}, const krx_quant::SignalRule& rule = {},
                  int minutes = 1, const krx_quant::ChartLook& look = {});
    /// Only the last bar of ``bars`` moved (the bar being made, krx-quant #110): it alone is
    /// drawn again, in the view of ``minutes`` and the style last set. A candle view's ``bars``
    /// are its candles. The indicators' values on it move with it (krx-quant #222): the point
    /// each line takes from it, the fractals and the volume profile. A Heikin-Ashi bar comes with its
    /// own prices, which the magnet picks from; a new bar sends the drawings again, as the places
    /// past the old last bar move onto it (krx-quant #224). Bricks (krx-quant #225) are made again at
    /// the start and size last drawn: all of them sent again when they changed, else the price line.
    void update_live(const QList<krx_quant::Bar>& bars, int minutes);
    /// The drawings to draw (krx-quant #224) and whether the magnet is on, sent at once apart from
    /// the data; ``select``: the drawing to select (0: as it is; -1: none).
    void set_drawings(const QList<krx_quant::Drawing>& drawings, bool magnet, int select = 0);
    /// The tool the next clicks on the chart's prices draw with (krx-quant #224); none: the cursor.
    /// The chart takes the keyboard, so Esc cancels it.
    void set_tool(std::optional<krx_quant::DrawingTool> tool);
    /// Select the drawing ``id``, as a click on it does (krx-capture).
    void select_drawing(int id);
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
    /// The anchored VWAP's bar was clicked or dragged to (krx-quant #222): its time (chart_time).
    void anchor_chosen(qint64 time);
    /// A legend row's eye, settings or delete button (krx-quant #222).
    void study_hide_toggled(krx_quant::Study study);
    void study_settings_requested(krx_quant::Study study);
    void study_removed(krx_quant::Study study);
    /// A border between panes was dragged (krx-quant #223): the lower panes' heights now.
    void pane_heights_changed(const krx_quant::PaneHeights& heights);
    /// A drawing placed with the tool (krx-quant #224), its id 0, its points as moments (on the
    /// magnet's when it is on); ``at`` is where its last point went (global): a text's input opens
    /// there.
    void drawing_added(const krx_quant::Drawing& drawing, const QPoint& at);
    /// A drawing dragged: moved whole, or one of its points (on the magnet's when it is on).
    void drawing_changed(const krx_quant::Drawing& drawing);
    void drawing_removed(int id);
    /// A drawing double-clicked at ``at`` (global): its colour, width and line style (a text's text).
    void drawing_edit_requested(int id, const QPoint& at);
    /// The tool placed its drawing or was cancelled: back to the cursor.
    void tool_finished();
    /// The default size of the styles without a time axis changed (krx-quant #225): ATR(14) of the
    /// view's bars as last drawn, to the won (krx_quant::atr_size); 0 without one.
    void brick_atr(double size);

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    /// The web view, made when first shown: only then is the WebEngine runtime started.
    void ensure_view();
    /// The baseline's base again for the bars in view from ``first_in_view``: sent when it moved.
    void rebase(std::optional<qint64> first_in_view);
    /// The volume profile of the bars in view, as the page takes it ({} when not drawn).
    QString profile_json() const;
    /// What the page draws of ``real`` (the view's bars as they are) in the look last set, the icons
    /// ``icons`` over them: the bars, or the bricks last made (krx-quant #225); shown_ and drawing_ follow.
    QJsonObject data_json(const QList<krx_quant::ChartBar>& real, const QJsonArray& icons);
    /// The drawings as the page takes them, on the bars last sent (krx-quant #224); none on bricks.
    QJsonObject drawings_json(int select) const;
    /// The page's points ([[logical, price]…] on its bars from ``first``) as moments on the bars
    /// last sent; with the magnet on, those of ``snap`` (-1: none, -2: all, else that one). None when
    /// they cannot be read or placed.
    std::optional<QList<krx_quant::DrawingPoint>> page_points(const QJsonValue& points, const QJsonValue& first,
                                                              int snap) const;
    /// A point (x, y) of the page, global.
    QPoint global_at(double x, double y) const;

    KrxChartBridge* bridge_ = nullptr;
    QWebEngineView* view_ = nullptr;
    QLabel* unavailable_ = nullptr;
    QString theme_; ///< the last sent, sent again when the page is ready
    QString data_;
    QHash<qint64, QString> hovers_; ///< the hover boxes sent, by bar time
    QList<krx_quant::ChartBar> shown_; ///< the bars last sent, as they are (not Heikin-Ashi)
    int minutes_ = 1;
    krx_quant::ChartLook look_;
    std::optional<double> base_;          ///< the baseline's base last sent
    std::optional<qint64> first_in_view_; ///< the first bar in view, as the page last said
    std::optional<qint64> last_in_view_;  ///< and the last
    krx_quant::StudyDrawing drawing_;     ///< the indicators as last sent (krx-quant #222)
    QString tool_ = QStringLiteral("{}"); ///< the drawing tool as last sent (krx-quant #224)
    // The styles without a time axis (krx-quant #225).
    bool timeless_ = false;            ///< the style last set draws bricks
    std::optional<qint64> brick_from_; ///< the view's bar the bricks start from, fixed once
    std::optional<double> brick_size_; ///< the box, reversal or lines last drawn with
    std::optional<double> atr_;        ///< ATR(14) of the view's bars as last drawn
    QList<krx_quant::Brick> bricks_;   ///< as last sent
};

} // namespace fincept::screens
