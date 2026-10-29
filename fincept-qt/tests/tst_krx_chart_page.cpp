// Tests for the KRX chart's page (src/screens/krx_quant/chart/index.html) as KrxChart drives it.
//
// The first screen shows the latest kFitBars bars however the bars before them come in: the
// screen draws today's bars first and the indicators' history (krx-quant #222, #223) right
// after, sometimes within the chart's frame, sometimes a frame later. Fitted once on today's
// bars, the view kept their zoom, or, as the chart applies a range at its next frame by its
// indexes then, landed on the days read before. A view moved by hand stays where it is.
//
// Minutes without trades (krx-quant #236) take a slot each, drawn as nothing: the first screen fits
// the latest slots, an empty one's hover box says so with the special situation over it, and a bar
// after a gap fills it at once.
//
// Shift and an upright wheel, as Windows sends them (krx-quant #242), move the view sideways.
//
// The wheel zooms on the bar nearest the pointer (krx-quant #253): that bar stays where it is. Out,
// where the latest bars run out on the right the view slides, its gap after the latest bar no wider;
// on the left it stops at the first bar read while more are to be read, and slides once none are. A
// wheel as Qt sends it zooms by its lines, and a sideways one or Shift and the wheel scrolls by them,
// whatever the screen's scale (a scaled Windows screen too). A trackpad's pinch (krx-quant #391), sent to
// a window holding the chart away from its corner, zooms on the bar nearest the pointer too, as much as
// the fingers spread or close, and a smart zoom does not magnify the page itself.
//
// The page keeps no key the screen's shortcuts want: Cmd+D (Ctrl+D) with the keyboard on the chart
// opens the toolbar's drawing menu (krx-quant #241).
//
// 일반 and 로그 at the bottom right (krx-quant #245) choose the price pane's scale: a click turns it
// and tells the screen, and the screen's choice draws it so.
//
// A bar of the 1-minute view clicked (krx-quant #249), an icon on it or not, an empty one too, tells
// the screen its bar close: the prediction panel shows that bar close's. A click past the last bar,
// a drag, and a wider view's bar tell it nothing.
//
// A bar of the 1-minute view double-clicked (krx-quant #255) tells the screen its bar close for its
// window, its first click the panel's as before, and no longer fits the chart. Past the last bar, on a
// drawing (its popup opens), with a drawing tool chosen, while the anchored VWAP waits for its bar,
// and in a wider view, it tells nothing.
//
// The crosshair's line across and its price on the price axis follow the pointer, over a bar too
// (krx-quant #275): the chart's own magnet put them on the bar's close or the volume bar's top. The drawings' magnet
// and another style leave it so.
//
// The price pane legend's first row (krx-quant #285) shows the bar under the crosshair, or the last
// bar, as Toss Securities' chart does: 시가, 고가, 저가 and 종가 in won, each against the close of the
// bar with trades before it, coloured by its sign. The first bar read has no changes, an empty slot
// says 체결 없음, a Heikin-Ashi chart shows the bars as they are, and the bar being made moves it.
//
// The day view draws a daily kind's calls (krx-quant #264): an icon on each day's candle and its box
// without a window; a week view's candle holds many days and has none.
//
// The trader's buys and sells (krx-quant #398) show on the 1-minute view with the consensus's icon on the
// same bar: a buy under the bar, a sell over the icon, the live account's in another colour than paper's;
// one on no bar drawn, and every one in a wider view, is not drawn.
//
// The page runs in Qt WebEngine with TradingView Lightweight Charts, so this suite opens a
// window. It is skipped where the WebEngine runtime is missing or the platform draws nothing
// (offscreen).

#include "screens/krx_quant/KrxChart.h"
#include "screens/krx_quant/KrxChartToolbar.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMenu>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTest>
#include <QTimeZone>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineView>
#include <QWheelEvent>
#include <QWindow>

#include <cmath>

using namespace fincept;
using namespace fincept::krx_quant;

namespace {

/// ``count`` 1-minute bars from ``first`` on, their closes in a wave.
QList<Bar> minutes(const char* first, int count) {
    const QDateTime start = QDateTime::fromString(QString::fromLatin1(first), Qt::ISODate);
    QList<Bar> out;
    for (int i = 0; i < count; ++i) {
        const double open = 1000 + 20 * std::sin(i / 5.0);
        const double close = 1000 + 20 * std::sin((i + 1) / 5.0);
        out << Bar{start.addSecs(60 * i), open, std::max(open, close) + 3, std::min(open, close) - 3, close, 100.0 + i};
    }
    return out;
}

/// As the demo engine's (custom/README.md): an afternoon read before, then today's morning.
const QList<Bar> kBefore = minutes("2026-10-08T14:00:00+09:00", 62);
const QList<Bar> kToday = minutes("2026-10-12T09:00:00+09:00", 18); // fewer than kFitBars
const QList<Bar> kEarlier = minutes("2026-10-07T14:00:00+09:00", 30);

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
}

QList<qint64> times(const QList<Bar>& bars) {
    QList<qint64> out;
    for (const auto& b : chart_bars(bars))
        out << b.time;
    return out;
}

} // namespace

class TstKrxChartPage : public QObject {
    Q_OBJECT

    screens::KrxChart* chart_ = nullptr;
    QWebEngineView* view_ = nullptr;
    std::optional<std::pair<qint64, qint64>> range_; ///< the bars in view, as the page last said

    /// Whether the page shows ``bars`` from ``first`` (an index into them, give or take a bar,
    /// as the page rounds) to the last.
    bool shows(const QList<Bar>& bars, qsizetype first) const {
        if (!range_)
            return false;
        const QList<qint64> all = times(bars);
        const qsizetype from = all.indexOf(range_->first);
        return from >= 0 && std::abs(from - first) <= 1 && range_->second == all.last();
    }

    /// The bars in view, as indexes into ``bars``, for a failure's message.
    QByteArray in_view(const QList<Bar>& bars) const {
        if (!range_)
            return "nothing in view";
        const QList<qint64> all = times(bars);
        return QStringLiteral("in view: bars %1 to %2 of %3")
            .arg(all.indexOf(range_->first))
            .arg(all.indexOf(range_->second))
            .arg(all.size())
            .toUtf8();
    }

    /// ``bars`` drawn as the screen draws them (the 1-minute view, its default indicators), in
    /// ``look``'s style.
    void draw(const QList<Bar>& bars, const ChartLook& look = {}) { chart_->set_data(bars, {}, {}, {}, 1, look); }

    /// Whether the page shows the last kFitBars of ``count`` bricks (krx-quant #225), on their slots.
    bool shows_bricks(qsizetype count) const {
        const qint64 first = brick_slot(std::max<qsizetype>(0, count - kFitBars));
        return range_ && std::abs(range_->first - first) <= 60 && range_->second == brick_slot(count - 1);
    }

    /// The page settled on the latest kFitBars bars of ``bars``, and is still there a while later.
    void expect_fitted(const QList<Bar>& bars) {
        const qsizetype first = std::max<qsizetype>(0, bars.size() - kFitBars);
        QTRY_VERIFY2_WITH_TIMEOUT(shows(bars, first), in_view(bars).constData(), 10000);
        QTest::qWait(400);
        QVERIFY2(shows(bars, first), in_view(bars).constData());
    }

    /// ``script``'s value on the page, once it answers (an invalid one if it does not in 5 s).
    QVariant js(const QString& script) {
        QVariant out;
        bool done = false;
        view_->page()->runJavaScript(script, [&out, &done](const QVariant& value) {
            out = value;
            done = true;
        });
        if (!QTest::qWaitFor([&done]() { return done; }, 5000))
            return {};
        return out;
    }

    /// The page's slots: how many, and how many of them empty (krx-quant #236).
    std::pair<int, int> slots_drawn() {
        const QVariantList bars = js("window.krxChart.bars()").toList();
        int empty = 0;
        for (const QVariant& b : bars)
            empty += b.toList().value(1).toInt() == 0 ? 1 : 0;
        return {static_cast<int>(bars.size()), empty};
    }

    /// The pointer resting on the slot holding ``time`` (chart_time), a third of the way down, as
    /// krx-capture's KRX_CAPTURE_HOVER does it: the chart takes a mouseenter before it follows moves.
    void hover_at(qint64 time) {
        js(QStringLiteral("(function(){const x = window.krxChart.x(%1), y = innerHeight / 3;"
                          "const at = document.elementFromPoint(x, y);"
                          "const event = (type, bubbles) => new MouseEvent(type, {clientX: x, clientY: y, "
                          "  pageX: x, pageY: y, bubbles: bubbles, view: window});"
                          "for (let e = at; e; e = e.parentElement) e.dispatchEvent(event('mouseenter', false));"
                          "at.dispatchEvent(event('mousemove', true));})()")
               .arg(time));
    }

    /// The wheel turned upright ``clicks`` times at ``x`` on the chart, a third of the way down (in
    /// for a negative ``delta_y``), a little apart as a hand turns it.
    void wheel_at(double x, int delta_y, int clicks) {
        for (int i = 0; i < clicks; ++i) {
            js(QStringLiteral("document.elementFromPoint(%1, innerHeight / 3).dispatchEvent(new WheelEvent('wheel', "
                              "{deltaY: %2, bubbles: true, cancelable: true, clientX: %1, clientY: innerHeight / 3}))")
                   .arg(x)
                   .arg(delta_y));
            QTest::qWait(50);
        }
        QTest::qWait(300); // the chart's next frames
    }

    /// Where the slot holding ``time`` is on the chart, across.
    double x_of(qint64 time) { return js(QStringLiteral("window.krxChart.x(%1)").arg(time)).toDouble(); }

    /// Where the slot holding ``time`` (chart_time) is in the page's widget, a third of the way down
    /// (a lower point past the last slot when ``past``): as the hand clicks it.
    QPoint spot(qint64 time, int past = 0) {
        const double x = js(QStringLiteral("window.krxChart.x(%1)").arg(time)).toDouble();
        const double y = js("innerHeight / 3").toDouble();
        return QPoint(qRound(x) + past, qRound(y));
    }

    /// Another symbol chosen: the screen empties the chart first (KrxLive::select).
    void empty() {
        draw({});
        QTest::qWait(100);
        range_.reset();
    }

  public:
    static void initMain() { QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts); }

  private slots:
    void initTestCase() {
        const QString platform = QGuiApplication::platformName();
        if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal"))
            QSKIP("the chart's page draws in a real window only");
        chart_ = new screens::KrxChart;
        chart_->resize(900, 500);
        connect(chart_, &screens::KrxChart::view_range, this,
                [this](qint64 from, qint64 to) { range_ = std::pair{from, to}; });
        chart_->show();
        view_ = chart_->findChild<QWebEngineView*>();
        if (!view_)
            QSKIP("no Qt WebEngine runtime here");
        // The page loaded and drawing: another symbol's bars in view.
        draw(minutes("2026-10-06T09:00:00+09:00", 100));
        QTRY_VERIFY_WITH_TIMEOUT(range_.has_value(), 30000);
    }

    void cleanupTestCase() { delete chart_; }

    void the_first_screen_fits_the_bars_read_before_within_a_frame() {
        empty();
        draw(kToday);
        draw(kBefore + kToday); // before the chart's next frame
        expect_fitted(kBefore + kToday);
    }

    void the_first_screen_fits_the_bars_read_before_a_frame_later() {
        empty();
        draw(kToday);
        QTRY_VERIFY_WITH_TIMEOUT(shows(kToday, 0), 10000); // fitted to today's few
        draw(kBefore + kToday);
        expect_fitted(kBefore + kToday);
    }

    void a_view_moved_by_hand_stays_as_bars_come_in_until_fitted() {
        empty();
        draw(kBefore + kToday);
        expect_fitted(kBefore + kToday);
        // Wheeled back to earlier bars, as a hand does.
        view_->page()->runJavaScript(
            "document.elementFromPoint(innerWidth / 2, innerHeight / 3).dispatchEvent(new WheelEvent('wheel', "
            "{deltaX: -400, bubbles: true, cancelable: true, clientX: innerWidth / 2, clientY: innerHeight / 3}));");
        const qint64 last = times(kToday).last();
        QTRY_VERIFY_WITH_TIMEOUT(range_ && range_->second < last, 10000);
        QTest::qWait(400); // the wheel's own move settled
        const auto moved = range_;
        draw(kEarlier + kBefore + kToday); // read before the first: the view stays on its bars
        QTest::qWait(600);
        QCOMPARE(range_, moved);
        chart_->fit(); // the button: the latest bars again
        expect_fitted(kEarlier + kBefore + kToday);
    }

    void shift_and_the_wheel_move_the_view_sideways() {
        // As Windows sends it (krx-quant #242): an upright wheel with Shift held. The view moves to
        // earlier bars and the bars keep their width: it does not zoom.
        empty();
        const QList<Bar> day = minutes("2026-10-12T09:00:00+09:00", 200);
        const QList<qint64> all = times(day);
        draw(day);
        expect_fitted(day);
        const QString spacing =
            QStringLiteral("window.krxChart.x(%2) - window.krxChart.x(%1)").arg(all[150]).arg(all[151]);
        const double width = js(spacing).toDouble();
        QVERIFY(width > 0);
        js("document.elementFromPoint(innerWidth / 2, innerHeight / 3).dispatchEvent(new WheelEvent('wheel', "
           "{deltaY: -400, shiftKey: true, bubbles: true, cancelable: true, clientX: innerWidth / 2, "
           "clientY: innerHeight / 3}))");
        QTRY_VERIFY2_WITH_TIMEOUT(range_->second < all.last(), in_view(day).constData(), 10000);
        QTest::qWait(400); // the wheel's own move settled
        QCOMPARE(js(spacing).toDouble(), width);
    }

    void the_wheel_zooms_on_the_bar_nearest_the_pointer() {
        // krx-quant #253: the pointer a little off a bar's middle, that bar stays where it is, in and
        // out, give or take the few pixels the price axis's labels take or give back.
        empty();
        const QList<Bar> day = minutes("2026-10-12T09:00:00+09:00", 300);
        const QList<qint64> all = times(day);
        draw(day);
        expect_fitted(day); // 240 to 299
        const double at = x_of(all[260]);
        const auto spacing = [&]() { return x_of(all[261]) - x_of(all[260]); };
        const double before = spacing();
        wheel_at(at + 2, -100, 5);
        QVERIFY2(std::abs(x_of(all[260]) - at) <= 3, QByteArray::number(x_of(all[260]) - at));
        QVERIFY(spacing() > before * 1.5);
        wheel_at(at + 2, 100, 2); // out, the latest bars still past the right edge
        QVERIFY2(std::abs(x_of(all[260]) - at) <= 3, QByteArray::number(x_of(all[260]) - at));
        QVERIFY(js("window.krxChart.gap()").toDouble() < 0);
        // Out from the latest bars at the right: the view slides, keeping its gap after the latest.
        // The Ichimoku cloud's leading spans reach past the latest bar: the gap is still the bar's.
        ChartLook cloud;
        cloud.studies.turn(Study::Ichimoku, true);
        draw(day, cloud);
        chart_->fit(); // the latest 60 (the view's times run on into the cloud's)
        QTRY_COMPARE_WITH_TIMEOUT(js("window.krxChart.gap()").toDouble(), kRightOffset, 10000);
        QTest::qWait(400);
        wheel_at(at + 2, 100, 4);
        QVERIFY(spacing() < before * 0.8);
        QVERIFY(std::abs(js("window.krxChart.gap()").toDouble() - kRightOffset) < 0.01);
        QVERIFY(x_of(all[260]) > at + 10);
        draw(day);
    }

    void a_wheel_from_qt_zooms_by_its_lines_whatever_the_screens_scale() {
        // krx-quant #253: Qt WebEngine turns a wheel's notches into pixels itself, 20 to a line of the
        // system's wheel setting, the screen's scale left out. A third of a notch, as a fine wheel
        // turns, zooms by those pixels (a tenth wider for 100) on a scaled Windows screen too, where
        // the chart's own step divided them by the scale.
        empty();
        const QList<Bar> day = minutes("2026-10-12T09:00:00+09:00", 300);
        const QList<qint64> all = times(day);
        draw(day);
        expect_fitted(day);
        QWidget* input = view_->focusProxy();
        QVERIFY(input);
        const auto spacing = [&]() { return x_of(all[261]) - x_of(all[260]); };
        const double before = spacing();
        const QPointF at(x_of(all[260]) + 2, js("innerHeight / 3").toDouble());
        const int notch = QWheelEvent::DefaultDeltasPerStep;
        QWheelEvent wheel(at, input->mapToGlobal(at), {}, {0, notch / 3}, Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(input, &wheel);
        const double pixels = QGuiApplication::styleHints()->wheelScrollLines() * 20 / 3.0;
        const double wanted = 1 + std::min(1.0, pixels / 100) / 10;
        QTRY_VERIFY2(std::abs(spacing() / before - wanted) < 0.001,
                     QByteArray::number(spacing() / before) + " for " + QByteArray::number(wanted));
    }

    void a_sideways_wheel_from_qt_scrolls_by_its_lines_whatever_the_screens_scale() {
        // krx-quant #253: Shift and the wheel (krx-quant #242) and a sideways wheel are the chart's, which
        // scrolls 80 pixels for a wheel's 100 but divides them by the screen's scale in Chromium on
        // Windows. Qt's wheels carry no scale: the page hands them on that much larger, so a notch scrolls
        // by its lines on a scaled Windows screen too.
        empty();
        const QList<Bar> day = minutes("2026-10-12T09:00:00+09:00", 300);
        const QList<qint64> all = times(day);
        draw(day);
        expect_fitted(day);
        QVERIFY(js(QStringLiteral("window.krxChart.show(%1)").arg(all[150])).toBool()); // room on both sides
        QTRY_VERIFY2_WITH_TIMEOUT(range_ && range_->first < all[130] && range_->second > all[170],
                                  in_view(day).constData(), 10000);
        QTest::qWait(400);
        QWidget* input = view_->focusProxy();
        QVERIFY(input);
        const QPointF point(input->width() / 2.0, input->height() / 3.0);
        const int notch = QWheelEvent::DefaultDeltasPerStep;
        const double pixels = QGuiApplication::styleHints()->wheelScrollLines() * 20 * 0.8; // a notch's scroll
        const std::pair<QPoint, Qt::KeyboardModifiers> wheels[] = {{{notch, 0}, Qt::NoModifier},
                                                                   {{0, notch}, Qt::ShiftModifier}};
        for (const auto& [angle, modifiers] : wheels) {
            const double at = x_of(all[150]);
            const double spacing = x_of(all[151]) - x_of(all[150]);
            QWheelEvent wheel(point, input->mapToGlobal(point), {}, angle, Qt::NoButton, modifiers, Qt::NoScrollPhase,
                              false);
            QApplication::sendEvent(input, &wheel);
            QTRY_VERIFY2(std::abs(std::abs(x_of(all[150]) - at) - pixels) < 1,
                         QByteArray::number(x_of(all[150]) - at) + " for " + QByteArray::number(pixels));
            QCOMPARE(x_of(all[151]) - x_of(all[150]), spacing); // no zoom
        }
    }

    void a_zoom_out_waits_at_the_first_bar_read_and_slides_once_none_is_before() {
        // krx-quant #253: the first bar read reached the left end, the zoom out stops with the bar held
        // where it was, as the bars before are read. Once the engine has none before, it slides on.
        empty();
        const QList<Bar> day = minutes("2026-10-12T09:00:00+09:00", 300);
        const QList<qint64> all = times(day);
        draw(day);
        expect_fitted(day);
        QVERIFY(js(QStringLiteral("window.krxChart.show(%1)").arg(all[40])).toBool()); // 7 to 73
        QTRY_VERIFY2_WITH_TIMEOUT(range_ && range_->first > all[0] && range_->first < all[20], in_view(day).constData(),
                                  10000);
        const double at = x_of(all[20]);
        const auto spacing = [&]() { return x_of(all[1]) - x_of(all[0]); };
        const double before = spacing();
        wheel_at(at, 100, 10);
        QVERIFY2(std::abs(x_of(all[20]) - at) <= 3, QByteArray::number(x_of(all[20]) - at));
        QVERIFY(spacing() < before * 0.9);                        // zoomed out until
        QVERIFY(x_of(all[0]) >= -1 && x_of(all[0]) <= spacing()); // the first bar met the left end
        const double stopped = spacing();
        ChartLook look;
        look.first_day_whole = true; // none before
        draw(day, look);
        QTest::qWait(300);
        wheel_at(at, 100, 3);
        QVERIFY(spacing() < stopped * 0.9);
        QVERIFY(x_of(all[0]) >= -1 && x_of(all[0]) <= spacing());
        QVERIFY(x_of(all[20]) < at - 10);
    }

    void a_trackpad_pinch_zooms_on_the_bar_nearest_the_pointer_by_the_fingers() {
        // krx-quant #391: the chart away from the window's corner, as on the screen (the symbols at its
        // left, the toolbar above), a pinch sent to the window as macOS sends it. Qt hands it on with
        // the window's place: the bar under the pointer stays where it is all the same (it slid 12
        // pixels), and the bars grow and shrink as the fingers spread and close (a pinch to 1.48 times
        // zoomed 1.04). A smart zoom does not magnify the page itself.
        auto* outer = new QWidget;
        auto* box = new QVBoxLayout(outer);
        box->setContentsMargins(300, 120, 0, 0);
        box->addWidget(chart_);
        outer->resize(1200, 620);
        outer->show();
        const auto restore = qScopeGuard([&]() {
            chart_->setParent(nullptr);
            chart_->resize(900, 500);
            chart_->show();
            delete outer;
        });
        QVERIFY(QTest::qWaitForWindowExposed(outer));
        empty();
        const QList<Bar> day = minutes("2026-10-12T09:00:00+09:00", 300);
        const QList<qint64> all = times(day);
        draw(day);
        expect_fitted(day); // 240 to 299
        QWidget* input = view_->focusProxy();
        QVERIFY(input);
        static const QPointingDevice pad(QStringLiteral("trackpad"), 391, QInputDevice::DeviceType::TouchPad,
                                         QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 2,
                                         0);
        const auto pinch = [&](const QPointF& local, const QList<double>& steps) {
            const QPointF in_window = input->mapTo(outer, local);
            const auto send = [&](Qt::NativeGestureType type, double value) {
                QNativeGestureEvent gesture(type, &pad, 2, in_window, in_window, outer->mapToGlobal(in_window), value,
                                            {});
                QGuiApplication::sendEvent(outer->windowHandle(), &gesture);
            };
            send(Qt::BeginNativeGesture, 0);
            for (const double step : steps) {
                send(Qt::ZoomNativeGesture, step);
                QTest::qWait(16);
            }
            send(Qt::EndNativeGesture, 0);
        };
        const double y = js("innerHeight / 3").toDouble();
        const double at = x_of(all[260]);
        const auto spacing = [&]() { return x_of(all[261]) - x_of(all[260]); };
        const double before = spacing();
        const auto held = [&]() { return QByteArray::number(x_of(all[260]) - at); };
        pinch({at + 2, y}, QList<double>(10, 0.04)); // spread to 1.04^10, about 1.48 times
        QTRY_VERIFY2(std::abs(spacing() / before - std::pow(1.04, 10)) < 0.01, QByteArray::number(spacing() / before));
        QVERIFY2(std::abs(x_of(all[260]) - at) <= 3, held());
        pinch({at + 2, y}, QList<double>(4, -0.05)); // closed to 0.95^4 of that, the latest still past the right
        QTRY_VERIFY2(std::abs(spacing() / before - std::pow(1.04, 10) * std::pow(0.95, 4)) < 0.01,
                     QByteArray::number(spacing() / before));
        QVERIFY2(std::abs(x_of(all[260]) - at) <= 3, held());
        QCOMPARE(js("visualViewport.scale").toDouble(), 1.0);
        // A smart zoom (two fingers double-tapped) magnified the page around the pointer, 1.2 times.
        const double now = spacing();
        const QPointF in_window = input->mapTo(outer, QPointF(at + 2, y));
        QNativeGestureEvent tap(Qt::SmartZoomNativeGesture, &pad, 2, in_window, in_window,
                                outer->mapToGlobal(in_window), 0, {});
        QGuiApplication::sendEvent(outer->windowHandle(), &tap);
        QTest::qWait(600);
        QCOMPARE(js("visualViewport.scale").toDouble(), 1.0);
        QCOMPARE(spacing(), now);
    }

    void the_price_scale_is_chosen_at_the_bottom_right() {
        // 일반 and 로그 (krx-quant #245): in the time axis's row up to the chart's right edge. A click
        // on 로그 turns the price pane's scale logarithmic and tells the screen; the one on already
        // does nothing. A style changed keeps it, and the screen's choice draws it so.
        empty();
        draw(kBefore + kToday);
        expect_fitted(kBefore + kToday);
        const QVariantList at = js("(function(){const r = document.getElementById('scale').getBoundingClientRect();"
                                   "const a = document.getElementById('chart').getBoundingClientRect();"
                                   "return [a.right - r.right, a.bottom - r.bottom, r.height];})()")
                                    .toList();
        QCOMPARE(at.value(0).toDouble(), 0.0);
        QCOMPARE(at.value(1).toDouble(), 0.0);
        QVERIFY(at.value(2).toDouble() > 10);
        const auto mode = [this]() { return js("window.krxChart.scale()[0]").toInt(); };
        const auto on = [this]() { return js("window.krxChart.scale()[1]").toString(); };
        QCOMPARE(mode(), 0);
        QCOMPARE(on(), QStringLiteral("일반"));
        QSignalSpy chosen(chart_, &screens::KrxChart::log_scale_chosen);
        const char* log = "document.querySelector('#scale [data-log=\"1\"]').click()";
        js(log);
        QTRY_COMPARE(chosen.size(), 1);
        QCOMPARE(chosen.last().at(0).toBool(), true);
        QCOMPARE(mode(), 1);
        QCOMPARE(on(), QStringLiteral("로그"));
        js(log); // on already
        QTest::qWait(100);
        QCOMPARE(chosen.size(), 1);
        ChartLook line;
        line.style = ChartStyle::Line; // another main series in the price pane
        draw(kBefore + kToday, line);
        QTest::qWait(100);
        QCOMPARE(mode(), 1);
        chart_->set_log_scale(false); // as the screen sends the one kept
        QTRY_COMPARE(mode(), 0);
        QCOMPARE(on(), QStringLiteral("일반"));
        QCOMPARE(chosen.size(), 1); // not a click
        chart_->set_log_scale(true);
        QTRY_COMPARE(mode(), 1);
        chart_->set_log_scale(false);
        draw(kBefore + kToday);
    }

    void the_crosshair_follows_the_pointer_over_a_bar() {
        empty();
        draw(kBefore + kToday);
        expect_fitted(kBefore + kToday);
        const auto mode = [this]() { return js("window.krxChart.crosshair()").toInt(); };
        hover_at(times(kToday).last());
        QTRY_VERIFY(js("window.krxChart.hovered()").isValid() && !js("window.krxChart.hovered()").isNull());
        QCOMPARE(mode(), 0);
        chart_->set_drawings({}, true); // the drawings' magnet on
        QTest::qWait(100);
        QCOMPARE(mode(), 0);
        ChartLook line;
        line.style = ChartStyle::Line;
        draw(kBefore + kToday, line);
        QTest::qWait(100);
        QCOMPARE(mode(), 0);
        chart_->set_drawings({}, false);
        draw(kBefore + kToday);
    }

    void the_legend_shows_the_bar_hovered_against_the_bar_before() {
        // 09:00 and 09:01 traded, 09:02 did not, 09:03 did (krx-quant #285).
        empty();
        const QList<Bar> bars = {Bar{kst("2026-10-15T09:00:00+09:00"), 216000, 216500, 215500, 216000, 100},
                                 Bar{kst("2026-10-15T09:01:00+09:00"), 216000, 216500, 216000, 216500, 100},
                                 Bar{kst("2026-10-15T09:03:00+09:00"), 216500, 216500, 215000, 215500, 100}};
        const auto prices = [this]() {
            QStringList out;
            for (const QVariant& field : js("window.krxChart.prices()").toList())
                out << field.toList().value(0).toString() + QLatin1Char('|') + field.toList().value(1).toString();
            return out;
        };
        const QStringList at_0903 = {"시가 216,500원 (0.00%)|flat", "고가 216,500원 (0.00%)|flat",
                                     "저가 215,000원 (-0.69%)|down", "종가 215,500원 (-0.46%)|down"};
        const QStringList at_0901 = {"시가 216,000원 (0.00%)|flat", "고가 216,500원 (+0.23%)|up",
                                     "저가 216,000원 (0.00%)|flat", "종가 216,500원 (+0.23%)|up"};
        // No crosshair: the last bar, against 09:01's close (the empty 09:02 skipped).
        draw(bars);
        QTRY_COMPARE_WITH_TIMEOUT(prices(), at_0903, 10000);
        // The bar being made moves it.
        QList<Bar> live = bars;
        live.last().close = 217000;
        live.last().high = 217000;
        chart_->update_live(live, 1);
        QTRY_COMPARE_WITH_TIMEOUT(prices().value(3), QStringLiteral("종가 217,000원 (+0.23%)|up"), 10000);
        draw(bars);
        hover_at(chart_time(kst("2026-10-15T09:01:00+09:00")));
        QTRY_COMPARE_WITH_TIMEOUT(prices(), at_0901, 10000);
        hover_at(chart_time(kst("2026-10-15T09:00:00+09:00"))); // the first bar read: nothing before it
        QTRY_COMPARE_WITH_TIMEOUT(prices(), (QStringList{"시가 216,000원|", "고가 216,500원|", "저가 215,500원|",
                                                         "종가 216,000원|"}),
                                  10000);
        hover_at(chart_time(kst("2026-10-15T09:02:00+09:00")));
        QTRY_COMPARE_WITH_TIMEOUT(prices(), QStringList{"체결 없음|"}, 10000);
        // A Heikin-Ashi chart: the bars as they are, not the averaged ones.
        ChartLook heikin;
        heikin.style = ChartStyle::HeikinAshi;
        draw(bars, heikin);
        hover_at(chart_time(kst("2026-10-15T09:01:00+09:00")));
        QTRY_COMPARE_WITH_TIMEOUT(prices(), at_0901, 10000);
        // Pointer gone: the last bar again. Emptied: no row.
        js("document.getElementById('chart').dispatchEvent(new MouseEvent('mouseleave'))");
        QTRY_COMPARE_WITH_TIMEOUT(prices(), at_0903, 10000);
        empty();
        QTRY_COMPARE_WITH_TIMEOUT(prices(), QStringList{}, 10000);
    }

    void bricks_fit_the_latest_bricks_once_they_start_and_bars_come_back() {
        // A style without a time axis (krx-quant #225): no bricks while the bars they start with are
        // read, then the last kFitBars bricks, a slot each; back to candles, the latest bars again.
        empty();
        ChartLook look;
        look.style = ChartStyle::Renko;
        look.brick_sizes = {{ChartStyle::Renko, 1}};
        draw(kToday, look); // fewer than kBrickBars and more to read: none yet
        QTest::qWait(300);
        look.first_day_whole = true; // the engine has none before: they start at its first bar
        const QList<Bar> all = kBefore + kToday;
        draw(all, look);
        const auto bricks = make_bricks(ChartStyle::Renko, chart_bars(all), 1);
        QVERIFY(bricks.size() > kFitBars);
        QTRY_VERIFY_WITH_TIMEOUT(shows_bricks(bricks.size()), 10000);
        QTest::qWait(400);
        QVERIFY(shows_bricks(bricks.size()));
        // Kagi, a custom series, on the same slots.
        look.style = ChartStyle::Kagi;
        look.brick_sizes = {{ChartStyle::Kagi, 2}};
        draw(all, look);
        chart_->fit(); // as the screen fits a style changed
        const auto lines = make_bricks(ChartStyle::Kagi, chart_bars(all), 2);
        QVERIFY(lines.size() > 1);
        QTRY_VERIFY_WITH_TIMEOUT(shows_bricks(lines.size()), 10000);
        draw(all); // candles again
        chart_->fit();
        expect_fitted(all);
    }

    void a_redraw_while_the_last_bar_moves_keeps_the_default_brick_size() {
        // The bricks' default size (krx-quant #225, ATR(14)) leaves out the bar being made: drawn again
        // for anything else while it moves (an indicator turned on, a prediction come in), the chart
        // keeps its size and so its bricks; a new bar, which ends that one, counts it.
        empty();
        ChartLook look;
        look.style = ChartStyle::Renko;
        look.first_day_whole = true;
        QList<Bar> bars = kBefore + kToday;
        QSignalSpy sized(chart_, &screens::KrxChart::brick_atr);
        draw(bars, look);
        const auto counted = sized.size();
        bars.last().high += 300; // the bar being made swings far
        bars.last().low -= 300;
        draw(bars, look);
        look.studies.turn(Study::Rsi, true); // drawn again for an indicator
        draw(bars, look);
        QCOMPARE(sized.size(), counted);
        bars << Bar{bars.last().start.addSecs(60), 1000, 1001, 999, 1000, 100}; // a new bar: the wide one ended
        draw(bars, look);
        QCOMPARE(sized.size(), counted + 1);
        QVERIFY(sized.last().at(0).toDouble() > 30); // its true range of 600 in the average
    }

    void a_halt_takes_its_slots_and_the_first_screen_fits_them() {
        // The 2026-10-15 halt (krx-quant #236): trades to 13:09, none for 30 minutes, again from 13:40.
        // The page has a slot for each minute, the 30 of the halt empty, and the first screen shows the
        // latest 60 of them: 13:00 to 13:59.
        empty();
        const QList<Bar> before = minutes("2026-10-15T12:30:00+09:00", 40);
        const QList<Bar> after = minutes("2026-10-15T13:40:00+09:00", 20);
        const QList<SpecialWindow> halt = {SpecialWindow{"circuit_breaker",
                                                         {},
                                                         kst("2026-10-15T13:10:00+09:00"),
                                                         kst("2026-10-15T13:30:00+09:00"),
                                                         QStringLiteral("1단계")}};
        chart_->set_data(before + after, {}, {}, {}, 1, {}, halt);
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn(), (std::pair{90, 30}), 10000);
        const qint64 first = chart_time(kst("2026-10-15T13:00:00+09:00"));
        QTRY_VERIFY_WITH_TIMEOUT(range_ && std::abs(range_->first - first) <= 60 && // give or take a slot
                                     range_->second == chart_time(kst("2026-10-15T13:59:00+09:00")),
                                 10000);
        // An empty bar's box: its time, no trades, and the circuit breaker over it.
        const qint64 empty_slot = chart_time(kst("2026-10-15T13:20:00+09:00"));
        hover_at(empty_slot);
        QTRY_COMPARE_WITH_TIMEOUT(js("window.krxChart.tip()").toString(),
                                  QStringLiteral("13:20–13:21 · 체결 없음\n서킷브레이커 1단계 13:10–13:30"), 10000);
        // A new bar after minutes without trades fills them at once: 13:59, then 14:03.
        QList<Bar> live = before + after;
        live << Bar{kst("2026-10-15T14:03:00+09:00"), 1000, 1002, 999, 1001, 50};
        chart_->update_live(live, 1);
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn(), (std::pair{94, 33}), 10000);
        // The 5-minute view: 13:10 to 13:35 empty.
        chart_->set_data(before + after, {}, {}, {}, 5, {}, halt);
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn(), (std::pair{18, 6}), 10000);
    }

    void a_halt_in_progress_grows_with_the_clock() {
        // The circuit breaker going on (krx-quant #238): no trade since 13:09. Drawn at 13:20:30 and at
        // 13:25:30 of the session (09:00–15:30), the minutes over since are empty bars.
        empty();
        const QList<Bar> before = minutes("2026-10-15T12:30:00+09:00", 40);
        const QList<SpecialWindow> halt = {SpecialWindow{"circuit_breaker",
                                                         {},
                                                         kst("2026-10-15T13:10:00+09:00"),
                                                         kst("2026-10-15T13:30:00+09:00"),
                                                         QStringLiteral("1단계")}};
        const auto clock = [](const char* now) {
            return SessionClock{chart_time(kst(now)), chart_time(kst("2026-10-15T09:00:00+09:00")),
                                chart_time(kst("2026-10-15T15:30:00+09:00"))};
        };
        chart_->set_data(before, {}, {}, {}, 1, {}, halt, clock("2026-10-15T13:20:30+09:00"));
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn(), (std::pair{50, 10}), 10000);
        chart_->set_data(before, {}, {}, {}, 1, {}, halt, clock("2026-10-15T13:25:30+09:00"));
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn(), (std::pair{55, 15}), 10000);
        // A late trade read at 13:15: its empty bar becomes a real one.
        const QList<Bar> late = before + minutes("2026-10-15T13:15:00+09:00", 1);
        chart_->set_data(late, {}, {}, {}, 1, {}, halt, clock("2026-10-15T13:25:30+09:00"));
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn(), (std::pair{55, 14}), 10000);
        // The bar being made on the last empty slot (13:24) takes it in place, and its box names the band.
        chart_->update_live(late + minutes("2026-10-15T13:24:00+09:00", 1), 1);
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn(), (std::pair{55, 13}), 10000);
        hover_at(chart_time(kst("2026-10-15T13:24:00+09:00")));
        QTRY_COMPARE_WITH_TIMEOUT(js("window.krxChart.tip()").toString(),
                                  QStringLiteral("서킷브레이커 1단계 13:10–13:30"), 10000);
        // A trade at 13:27: 13:25 and 13:26 filled before it, at once.
        chart_->update_live(late + minutes("2026-10-15T13:24:00+09:00", 1) + minutes("2026-10-15T13:27:00+09:00", 1),
                            1);
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn(), (std::pair{58, 15}), 10000);
        hover_at(chart_time(kst("2026-10-15T13:27:00+09:00")));
        QTRY_COMPARE_WITH_TIMEOUT(js("window.krxChart.tip()").toString(),
                                  QStringLiteral("서킷브레이커 1단계 13:10–13:30"), 10000);
    }

    void a_bar_clicked_in_the_1_minute_view_tells_its_bar_close() {
        // krx-quant #249: the bar 09:30–09:31 has no icon here; it closed at 09:31. Then a drag, an
        // empty slot (no trades 13:10–13:29), past the last bar, and the 3-minute view.
        empty();
        const QList<Bar> morning = minutes("2026-10-15T09:00:00+09:00", 80);
        draw(morning);
        expect_fitted(morning);
        QWidget* input = view_->focusProxy();
        QVERIFY(input);
        QSignalSpy clicked(chart_, &screens::KrxChart::close_clicked);
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(chart_time(kst("2026-10-15T09:30:00+09:00"))));
        QTRY_COMPARE_WITH_TIMEOUT(clicked.size(), 1, 5000);
        const QDateTime close = clicked[0][0].toDateTime();
        QCOMPARE(close, kst("2026-10-15T09:31:00+09:00"));
        QCOMPARE(close.toString(Qt::ISODate), QStringLiteral("2026-10-15T09:31:00+09:00")); // as the engine is asked

        // A drag scrolls the chart back to earlier bars: no bar close. The pointer comes over the
        // chart first, as hover_at's, and the button held over 120 pixels to the right.
        clicked.clear();
        js(QStringLiteral("(function(){const x = window.krxChart.x(%1), y = innerHeight / 3;"
                          "const at = document.elementFromPoint(x, y);"
                          "const event = (type, dx, buttons, bubbles) => new MouseEvent(type, {clientX: x + dx, "
                          "  clientY: y, pageX: x + dx, pageY: y, button: 0, buttons: buttons, bubbles: bubbles, "
                          "  cancelable: true, view: window});"
                          "for (let e = at; e; e = e.parentElement) e.dispatchEvent(event('mouseenter', 0, 0, false));"
                          "at.dispatchEvent(event('mousemove', 0, 0, true));"
                          "at.dispatchEvent(event('mousedown', 0, 1, true));"
                          "for (let dx = 12; dx <= 120; dx += 12) at.dispatchEvent(event('mousemove', dx, 1, true));"
                          "at.dispatchEvent(event('mouseup', 120, 0, true));})()")
               .arg(chart_time(kst("2026-10-15T09:50:00+09:00"))));
        QTRY_VERIFY_WITH_TIMEOUT(range_ && range_->second < times(morning).last(), 5000); // it moved
        QTest::qWait(400);
        QCOMPARE(clicked.size(), 0);

        const QList<Bar> bars = minutes("2026-10-15T12:50:00+09:00", 20) + minutes("2026-10-15T13:30:00+09:00", 20);
        empty();
        draw(bars);
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn(), (std::pair{60, 20}), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(range_.has_value(), 10000);
        QTest::qWait(400); // fitted
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(chart_time(kst("2026-10-15T13:20:00+09:00"))));
        QTRY_COMPARE_WITH_TIMEOUT(clicked.size(), 1, 5000);
        QCOMPARE(clicked[0][0].toDateTime(), kst("2026-10-15T13:21:00+09:00"));

        // Past the last bar (13:49), in the room kept right of it: nothing.
        clicked.clear();
        const qint64 last = chart_time(kst("2026-10-15T13:49:00+09:00"));
        const int width = qRound(js(QStringLiteral("window.krxChart.x(%1) - window.krxChart.x(%2)")
                                        .arg(last)
                                        .arg(chart_time(kst("2026-10-15T13:48:00+09:00"))))
                                     .toDouble());
        QVERIFY(width > 0);
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(last, 3 * width));
        QTest::qWait(400);
        QCOMPARE(clicked.size(), 0);

        // The 3-minute view: a bar holds three bar closes, none of them told.
        chart_->set_data(bars, {}, {}, {}, 3, {});
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn().first, 21, 10000); // 12:48 to 13:48
        QTest::qWait(400);
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(chart_time(kst("2026-10-15T13:00:00+09:00"))));
        QTest::qWait(400);
        QCOMPARE(clicked.size(), 0);
    }

    void a_daily_kind_s_calls_sit_on_the_day_view_s_candles_with_their_boxes() {
        // krx-quant #264: the day view draws a daily kind's icons on their days' candles and their boxes
        // without a window (a day is no bar close); a week view's candle holds many days: none there.
        empty();
        const QTimeZone seoul = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
        QList<Bar> days;
        for (int i = 0; i < 30; ++i)
            days << Bar{QDateTime(QDate(2026, 9, 1).addDays(i), QTime(0, 0), seoul), 1000, 1010, 990, 1005, 1e6};
        ChartIcon up;
        up.time = chart_time(days[28].start);
        up.bar_close = QDateTime(days[28].start.date(), QTime(8, 50), seoul);
        up.direction = Direction::Up;
        up.arrow = QStringLiteral("▲");
        up.result = QStringLiteral("✓");
        ChartIcon flat = up;
        flat.time = chart_time(days[29].start);
        flat.direction = Direction::Flat;
        flat.result = QStringLiteral("?");
        BarHover box;
        box.time = up.time;
        box.lines = {QStringLiteral("09-29 시가→종가 (08:50 예측) · 정답 상승 +0.50% (1,000 → 1,005)"),
                     QStringLiteral("cox · 상승 · 상승 40% · 보합 30% · 하락 30% · ✓")};
        chart_->set_data(days, {}, {box}, {}, kDayView, {}, {}, std::nullopt, {up, flat});
        QTRY_COMPARE_WITH_TIMEOUT(js("window.krxChart.icons().length").toInt(), 2, 10000);
        const QVariantList first = js("window.krxChart.icons()[0]").toList();
        QCOMPARE(first.value(0).toLongLong(), up.time);
        QCOMPARE(first.value(1).toString(), QStringLiteral("arrowUp"));
        QCOMPARE(first.value(2).toString(), QStringLiteral("✓"));
        QTest::qWait(400); // fitted
        hover_at(up.time);
        QTRY_COMPARE_WITH_TIMEOUT(js("window.krxChart.tip()").toString(), box.lines.join('\n'), 10000);
        // The week view: no icon, no box.
        chart_->set_data(days, {}, {box}, {}, kWeekView, {}, {}, std::nullopt, {up, flat});
        QTRY_COMPARE_WITH_TIMEOUT(js("window.krxChart.icons().length").toInt(), 0, 10000);
    }

    void the_trader_s_buys_and_sells_show_with_the_consensus_s_icon_on_their_bar() {
        empty();
        const QList<Bar> morning = minutes("2026-10-15T09:00:00+09:00", 30);
        const qint64 ten = chart_time(kst("2026-10-15T09:10:00+09:00")); // the bar 09:10–09:11
        Mark icon; // the consensus's of the 09:11 bar close, on the bar that closed then
        icon.bar_close = kst("2026-10-15T09:11:00+09:00");
        icon.predicted = Direction::Down;
        icon.prediction.algorithm = QStringLiteral("consensus-final");
        const QList<TraderIcon> trades{
            {ten, true, false, QStringLiteral("t1b")},                                         // paper
            {ten, false, true, QStringLiteral("t2s")},                                         // live
            {chart_time(kst("2026-10-15T09:20:00+09:00")), true, true, QStringLiteral("t3b")}, // live
            {chart_time(kst("2026-10-15T10:30:00+09:00")), true, false, QStringLiteral("t4b")}, // no bar there
        };
        chart_->set_data(morning, {icon}, {}, {}, 1, {}, {}, std::nullopt, {}, trades);
        QTRY_COMPARE_WITH_TIMEOUT(js("window.krxChart.markers().length").toInt(), 4, 10000);
        QList<QVariantList> at_ten; // [time, position, shape, text, colour], as drawn
        QString live, paper;
        for (const QVariant& m : js("window.krxChart.markers()").toList()) {
            const QVariantList marker = m.toList();
            if (marker.value(0).toLongLong() == ten)
                at_ten << marker;
            if (marker.value(3).toString() == QStringLiteral("매수") && marker.value(0).toLongLong() != ten)
                live = marker.value(4).toString();
        }
        QCOMPARE(at_ten.size(), 3); // the icon and both of the trader's: none hides another
        QCOMPARE(at_ten[0].value(1).toString(), QStringLiteral("aboveBar")); // the icon, nearest its bar
        QCOMPARE(at_ten[0].value(3).toString(), QStringLiteral("?"));
        QCOMPARE(at_ten[1].mid(1, 3), (QVariantList{"belowBar", "arrowUp", QStringLiteral("매수")}));
        QCOMPARE(at_ten[2].mid(1, 3), (QVariantList{"aboveBar", "arrowDown", QStringLiteral("매도")})); // over it
        paper = at_ten[1].value(4).toString();
        QCOMPARE(at_ten[2].value(4).toString(), live); // both live
        QVERIFY2(paper != live, qPrintable(paper));
        QVERIFY(paper != at_ten[0].value(4).toString() && live != at_ten[0].value(4).toString());
        // A wider view has neither the icons nor the trader's.
        chart_->set_data(morning, {icon}, {}, {}, 5, {}, {}, std::nullopt, {}, trades);
        QTRY_COMPARE_WITH_TIMEOUT(js("window.krxChart.markers().length").toInt(), 0, 10000);
    }

    void a_bar_double_clicked_in_the_1_minute_view_opens_its_window() {
        // krx-quant #255: the bar 09:40–09:41 closed at 09:41. The view moved by hand first: a
        // double-click that fitted the chart would show.
        empty();
        const QList<Bar> morning = minutes("2026-10-15T09:00:00+09:00", 120);
        draw(morning);
        expect_fitted(morning);
        const qint64 bar = chart_time(kst("2026-10-15T09:40:00+09:00"));
        QVERIFY(js(QStringLiteral("window.krxChart.show(%1)").arg(bar)).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(range_ && range_->second < times(morning).last(), 5000);
        QTest::qWait(400);
        const auto moved = *range_;
        QWidget* input = view_->focusProxy();
        QVERIFY(input);
        QSignalSpy clicked(chart_, &screens::KrxChart::close_clicked);
        QSignalSpy opened(chart_, &screens::KrxChart::bar_window_requested);
        const QPoint at = spot(bar);
        QTest::mouseClick(input, Qt::LeftButton, {}, at);
        QTest::mouseClick(input, Qt::LeftButton, {}, at);
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
        QCOMPARE(opened[0][0].toDateTime(), kst("2026-10-15T09:41:00+09:00"));
        QCOMPARE(opened[0][0].toDateTime().toString(Qt::ISODate), QStringLiteral("2026-10-15T09:41:00+09:00"));
        QCOMPARE(clicked.size(), 1); // the first click pinned the panel at once; the second is no click
        QCOMPARE(clicked[0][0].toDateTime(), kst("2026-10-15T09:41:00+09:00"));
        QTest::qWait(600);
        QCOMPARE(*range_, moved); // not fitted: the 맞춤 button alone fits

        // Past the last bar, in the room kept right of it: nothing.
        chart_->fit();
        expect_fitted(morning);
        opened.clear();
        clicked.clear();
        const qint64 last = times(morning).last();
        const int width = qRound(x_of(last) - x_of(last - 60));
        QVERIFY(width > 0);
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(last, 3 * width));
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(last, 3 * width));
        QTest::qWait(700);
        QCOMPARE(opened.size(), 0);
        QCOMPARE(clicked.size(), 0);

        // On a drawing (a horizontal line through the bar's close): its popup, not the window. As the
        // hand does it, in the page (its double-click comes with Chromium's own click count).
        const qint64 drawn = times(morning).at(100);
        const double price = morning.at(100).close;
        krx_quant::Drawing line = krx_quant::new_drawing(krx_quant::DrawingTool::Horizontal, {{drawn + 30, price}});
        line.id = 1;
        chart_->set_drawings({line}, false);
        QTest::qWait(300);
        QSignalSpy edited(chart_, &screens::KrxChart::drawing_edit_requested);
        js(QStringLiteral("(function(){const x = window.krxChart.x(%1), y = window.krxChart.y(%2);"
                          "const at = document.elementFromPoint(x, y);"
                          "const event = (type, buttons, detail) => new MouseEvent(type, {clientX: x, clientY: y, "
                          "  pageX: x, pageY: y, button: 0, buttons: buttons, detail: detail, bubbles: true, "
                          "  cancelable: true, view: window});"
                          "for (const n of [1, 2]) {"
                          "  at.dispatchEvent(event('mousedown', 1, n)); at.dispatchEvent(event('mouseup', 0, n));"
                          "  at.dispatchEvent(event('click', 0, n)); }"
                          "at.dispatchEvent(event('dblclick', 0, 2));})()")
               .arg(drawn)
               .arg(price));
        QTRY_COMPARE_WITH_TIMEOUT(edited.size(), 1, 5000);
        QCOMPARE(edited[0][0].toInt(), 1);
        QTest::qWait(700);
        QCOMPARE(opened.size(), 0);
        QCOMPARE(clicked.size(), 0);
        chart_->set_drawings({}, false);

        // A drawing tool chosen (a trend line): both clicks are its points.
        QSignalSpy added(chart_, &screens::KrxChart::drawing_added);
        chart_->set_tool(krx_quant::DrawingTool::TrendLine);
        QTest::qWait(300);
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(times(morning).at(90)));
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(times(morning).at(90)));
        QTRY_COMPARE_WITH_TIMEOUT(added.size(), 1, 5000);
        QTest::qWait(700);
        QCOMPARE(opened.size(), 0);
        QCOMPARE(clicked.size(), 0);
        chart_->set_tool(std::nullopt);

        // The anchored VWAP waiting for its bar: the first click chooses it, the second pins (as a
        // click after it always did), and no window opens.
        ChartLook picking;
        picking.studies.turn(Study::AnchoredVwap, true);
        picking.picking = true;
        draw(morning, picking);
        QTest::qWait(500);
        QSignalSpy anchored(chart_, &screens::KrxChart::anchor_chosen);
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(times(morning).at(95)));
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(times(morning).at(95)));
        QTRY_COMPARE_WITH_TIMEOUT(anchored.size(), 1, 5000);
        QTest::qWait(700);
        QCOMPARE(opened.size(), 0);
        draw(morning);
        QTest::qWait(300);

        // The 3-minute view: a bar holds three bar closes, none of them opened.
        chart_->set_data(morning, {}, {}, {}, 3, {});
        QTRY_COMPARE_WITH_TIMEOUT(slots_drawn().first, 40, 10000);
        QTest::qWait(400);
        clicked.clear();
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(chart_time(kst("2026-10-15T10:30:00+09:00"))));
        QTest::mouseClick(input, Qt::LeftButton, {}, spot(chart_time(kst("2026-10-15T10:30:00+09:00"))));
        QTest::qWait(700);
        QCOMPARE(opened.size(), 0);
        QCOMPARE(clicked.size(), 0);
    }

    void cmd_d_on_the_chart_opens_the_drawing_menu() {
        screens::KrxChartToolbar toolbar;
        toolbar.show_style(ChartStyle::Candle);
        toolbar.take_keys(chart_);
        toolbar.show();
        chart_->activateWindow();
        if (!QTest::qWaitForWindowActive(chart_))
            QSKIP("no window becomes active here");
        view_->setFocus(Qt::MouseFocusReason); // as a click on the chart
        QTRY_VERIFY(QApplication::focusWidget() && chart_->isAncestorOf(QApplication::focusWidget()));
        QMenu* menu = toolbar.findChild<QToolButton*>("krxDrawing")->menu();
        QSignalSpy shown(menu, &QMenu::aboutToShow);
        connect(menu, &QMenu::aboutToShow, menu, [menu]() { QTimer::singleShot(200, menu, &QMenu::hide); });
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_D, Qt::ControlModifier); // returns once it closed
        QCOMPARE(shown.size(), 1);
    }
};

QTEST_MAIN(TstKrxChartPage)
#include "tst_krx_chart_page.moc"
