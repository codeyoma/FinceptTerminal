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
// The page runs in Qt WebEngine with TradingView Lightweight Charts, so this suite opens a
// window. It is skipped where the WebEngine runtime is missing or the platform draws nothing
// (offscreen).

#include "screens/krx_quant/KrxChart.h"

#include <QApplication>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>
#include <QWebEngineView>

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
};

QTEST_MAIN(TstKrxChartPage)
#include "tst_krx_chart_page.moc"
