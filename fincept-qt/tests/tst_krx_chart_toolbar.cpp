// Tests for src/screens/krx_quant/KrxChartToolbar.h: the drawing menu's keys (krx-quant #241) and
// the drawing button's name (#248).
//
// Cmd+D (Ctrl+D on Windows) with the keyboard in the screen opens the drawing menu as a click on its
// button does, and again closes it. A letter in the open menu picks its tool or turns the magnet and
// closes it; a Hangul key picks by its place. Not with the keyboard outside the screen, nor on a
// style without a time axis, where the drawing button is off. While a tool is chosen the button
// carries its name, and a long name that does not fit hides the labels only until the cursor is back.
//
// The keys go through Qt's shortcuts, which need an active window: the suite opens one and is
// skipped where the platform draws nothing (offscreen) or no window becomes active.

#include "screens/krx_quant/KrxChartToolbar.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

using namespace fincept;
using namespace fincept::krx_quant;

class TstKrxChartToolbar : public QObject {
    Q_OBJECT

    QWidget* window_ = nullptr;
    QWidget* screen_ = nullptr; ///< the KRX screen: the toolbar and a widget that takes the keyboard
    screens::KrxChartToolbar* toolbar_ = nullptr;
    QWidget* inside_ = nullptr;  ///< in the screen, like the chart
    QWidget* outside_ = nullptr; ///< beside the screen, like another panel

    QToolButton* drawing() const { return toolbar_->findChild<QToolButton*>("krxDrawing"); }
    QMenu* menu() const { return drawing()->menu(); }

    static QWidget* focusable(QWidget* parent) {
        auto* w = new QWidget(parent);
        w->setFocusPolicy(Qt::StrongFocus);
        w->setMinimumHeight(40);
        return w;
    }

    /// Cmd+D (Ctrl+D) pressed with the keyboard on ``at``; while the drawing menu is open, ``then``
    /// runs on it (by default nothing: it is closed). Whether it opened.
    bool press_keys(QWidget* at, const std::function<void(QMenu*)>& then = {}) {
        at->setFocus(Qt::OtherFocusReason);
        QMenu* menu = this->menu();
        bool opened = false;
        QTimer guard; // a menu left open would hold the test
        guard.setSingleShot(true);
        connect(&guard, &QTimer::timeout, menu, &QMenu::hide);
        const auto shown = connect(menu, &QMenu::aboutToShow, menu, [&]() {
            opened = true;
            guard.start(5000);
            QTimer::singleShot(50, menu, [menu, then]() {
                if (then)
                    then(menu);
                else
                    menu->hide();
            });
        });
        QTest::keyClick(at, Qt::Key_D, Qt::ControlModifier); // returns once the menu closed
        disconnect(shown);
        return opened;
    }

  private slots:
    void initTestCase() {
        const QString platform = QGuiApplication::platformName();
        if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal"))
            QSKIP("the shortcuts need a real window");
        window_ = new QWidget;
        auto* rows = new QVBoxLayout(window_);
        screen_ = new QWidget;
        auto* in = new QVBoxLayout(screen_);
        toolbar_ = new screens::KrxChartToolbar;
        in->addWidget(toolbar_);
        inside_ = focusable(screen_);
        in->addWidget(inside_);
        rows->addWidget(screen_);
        outside_ = focusable(window_);
        rows->addWidget(outside_);
        toolbar_->take_keys(screen_);
        toolbar_->show_view(1);
        toolbar_->show_style(ChartStyle::Candle);
        window_->resize(900, 200);
        window_->show();
        window_->activateWindow();
        if (!QTest::qWaitForWindowActive(window_))
            QSKIP("no window becomes active here");
    }

    void cleanupTestCase() { delete window_; }

    void init() {
        // Another app may have come to the front meanwhile (ctest runs the suites one after another),
        // and a Mac closes an app's open menu when it leaves the front.
        window_->activateWindow();
        if (!QTest::qWaitForWindowActive(window_))
            QSKIP("no window becomes active here");
    }

    void each_item_shows_its_letter() {
        QStringList texts;
        for (const QAction* action : menu()->actions())
            if (!action->isSeparator())
                texts << action->text();
        QCOMPARE(texts.size(), 13);
        QCOMPARE(texts.first(), QStringLiteral("추세선\tT"));
        QCOMPARE(texts.last(), QStringLiteral("자석\tM"));
    }

    void cmd_d_opens_the_menu_and_again_closes_it() {
        bool open_then = false;
        QVERIFY(press_keys(inside_, [&](QMenu* menu) {
            open_then = menu->isVisible();
            QTest::keyClick(menu, Qt::Key_D, Qt::ControlModifier);
        }));
        QVERIFY(open_then);
        QTRY_VERIFY(!menu()->isVisible());
        // With the keyboard on the toolbar's row too.
        QVERIFY(press_keys(toolbar_));
    }

    void a_letter_picks_its_tool() {
        QSignalSpy chosen(toolbar_, &screens::KrxChartToolbar::tool_chosen);
        for (const DrawingTool tool : kDrawingTools) {
            const auto key = Qt::Key(Qt::Key_A + (tool_letter(tool).unicode() - u'A'));
            QVERIFY(press_keys(inside_, [key](QMenu* menu) { QTest::keyClick(menu, key); }));
            QTRY_VERIFY(!menu()->isVisible());
            QCOMPARE(chosen.size(), 1);
            QCOMPARE(chosen.takeFirst().at(0).value<DrawingTool>(), tool);
        }
        // A letter of nothing leaves the menu open; Esc closes it.
        QVERIFY(press_keys(inside_, [](QMenu* menu) {
            QTest::keyClick(menu, Qt::Key_Z);
            QVERIFY(menu->isVisible());
            QTest::keyClick(menu, Qt::Key_Escape);
        }));
        QCOMPARE(chosen.size(), 0);
    }

    void m_turns_the_magnet() {
        QSignalSpy turned(toolbar_, &screens::KrxChartToolbar::magnet_toggled);
        toolbar_->show_magnet(false);
        QVERIFY(press_keys(inside_, [](QMenu* menu) { QTest::keyClick(menu, Qt::Key_M); }));
        QTRY_COMPARE(turned.size(), 1);
        QCOMPARE(turned.takeFirst().at(0).toBool(), true);
        QVERIFY(press_keys(inside_, [](QMenu* menu) { QTest::keyClick(menu, Qt::Key_M); }));
        QTRY_COMPARE(turned.size(), 1);
        QCOMPARE(turned.takeFirst().at(0).toBool(), false);
    }

    void a_hangul_key_picks_by_its_place() {
#if defined(Q_OS_MACOS)
        constexpr quint32 kT = 0x11; // kVK_ANSI_T
#elif defined(Q_OS_WIN)
        constexpr quint32 kT = 'T';
#else
        QSKIP("the keys' places are read on macOS and Windows");
        constexpr quint32 kT = 0;
#endif
        QSignalSpy chosen(toolbar_, &screens::KrxChartToolbar::tool_chosen);
        QVERIFY(press_keys(inside_, [](QMenu* menu) {
            QKeyEvent press(QEvent::KeyPress, 0x3145, Qt::NoModifier, 0, kT, 0, QStringLiteral("ㅅ"));
            QApplication::sendEvent(menu, &press);
        }));
        QTRY_COMPARE(chosen.size(), 1);
        QCOMPARE(chosen.takeFirst().at(0).value<DrawingTool>(), DrawingTool::TrendLine);
    }

    void not_with_the_keyboard_outside_the_screen() {
        QSignalSpy shown(menu(), &QMenu::aboutToShow);
        QVERIFY(!press_keys(outside_));
        QCOMPARE(shown.size(), 0);
    }

    void not_on_a_style_without_a_time_axis() {
        toolbar_->show_style(ChartStyle::Renko);
        QVERIFY(!press_keys(inside_));
        toolbar_->show_style(ChartStyle::Candle);
        QVERIFY(press_keys(inside_));
    }

    void the_button_is_named_after_the_tool() {
        QToolButton* button = drawing();
        QCOMPARE(button->text(), QStringLiteral("그리기"));
        // As the screen does: the tool picked is the one shown.
        const auto shown = connect(toolbar_, &screens::KrxChartToolbar::tool_chosen, toolbar_,
                                   [this](DrawingTool tool) { toolbar_->show_tool(tool); });
        QVERIFY(press_keys(inside_, [](QMenu* menu) { QTest::keyClick(menu, Qt::Key_R); }));
        disconnect(shown);
        QTRY_COMPARE(button->text(), QStringLiteral("반직선"));
        QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        toolbar_->show_tool(std::nullopt); // drawn, or Esc: the cursor
        QCOMPARE(button->text(), QStringLiteral("그리기"));
    }

    void a_long_name_hides_the_labels_until_the_cursor() {
        // A width where `그리기` fits and the fib fan's name does not.
        QToolButton* button = drawing();
        toolbar_->show_tool(std::nullopt);
        const int short_row = toolbar_->sizeHint().width();
        toolbar_->show_tool(DrawingTool::FibFan);
        QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonTextBesideIcon); // fits the window
        const int long_row = toolbar_->sizeHint().width();
        QVERIFY(long_row > short_row);
        const QSize before = window_->size();
        const int width = (short_row + long_row) / 2;
        window_->resize(before.width() - toolbar_->width() + width, before.height());
        QTRY_COMPARE(toolbar_->width(), width);

        QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
        toolbar_->show_tool(std::nullopt); // measured with the labels while they are hidden
        QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        QCOMPARE(button->text(), QStringLiteral("그리기"));
        toolbar_->show_tool(DrawingTool::FibFan);
        QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
        toolbar_->show_tool(std::nullopt);

        window_->resize(before);
        QTRY_COMPARE(window_->size(), before);
    }
};

QTEST_MAIN(TstKrxChartToolbar)
#include "tst_krx_chart_toolbar.moc"
