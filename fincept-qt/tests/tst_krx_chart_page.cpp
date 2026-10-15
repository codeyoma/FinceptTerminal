// Tests for the KRX chart's page (src/screens/krx_quant/chart/index.html) as KrxChart drives it.
//
// The first screen shows the latest kFitBars bars however the bars before them come in: the
// screen draws today's bars first and the indicators' history (krx-quant #222, #223) right
// after, sometimes within the chart's frame, sometimes a frame later. Fitted once on today's
// bars, the view kept their zoom, or, as the chart applies a range at its next frame by its
// indexes then, landed on the days read before. A view moved by hand stays where it is.
//
// The page runs in Qt WebEngine with TradingView Lightweight Charts, so this suite opens a
// window. It is skipped where the WebEngine runtime is missing or the platform draws nothing
// (offscreen).

#include "screens/krx_quant/KrxChart.h"

#include <QApplication>
#include <QGuiApplication>
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

    /// ``bars`` drawn as the screen draws them (the 1-minute view, its default indicators).
    void draw(const QList<Bar>& bars) { chart_->set_data(bars, {}); }

    /// The page settled on the latest kFitBars bars of ``bars``, and is still there a while later.
    void expect_fitted(const QList<Bar>& bars) {
        const qsizetype first = std::max<qsizetype>(0, bars.size() - kFitBars);
        QTRY_VERIFY2_WITH_TIMEOUT(shows(bars, first), in_view(bars).constData(), 10000);
        QTest::qWait(400);
        QVERIFY2(shows(bars, first), in_view(bars).constData());
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
};

QTEST_MAIN(TstKrxChartPage)
#include "tst_krx_chart_page.moc"
