// Tests for src/screens/krx_quant/KrxChartToolbar.h: the SMT button and the indicators button's label.
//
// The SMT button (codeyoma/krx-quant#522) turns the SMT divergences on and off, filled while on, in the
// 1-minute view with a time axis alone: off and unfilled in the others, which draw none. In a panel too
// narrow for the row the indicators button keeps its icon only, and its label again once it fits.
//
// The suite opens a window and is skipped where the platform draws nothing (offscreen) or no window
// becomes active.

#include "screens/krx_quant/KrxChartToolbar.h"

#include <QApplication>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>

using namespace fincept;
using namespace fincept::krx_quant;

class TstKrxChartToolbar : public QObject {
    Q_OBJECT

    QWidget* window_ = nullptr;
    screens::KrxChartToolbar* toolbar_ = nullptr;

  private slots:
    void initTestCase() {
        const QString platform = QGuiApplication::platformName();
        if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal"))
            QSKIP("the toolbar needs a real window");
        window_ = new QWidget;
        auto* rows = new QVBoxLayout(window_);
        toolbar_ = new screens::KrxChartToolbar;
        rows->addWidget(toolbar_);
        rows->addStretch(1);
        toolbar_->show_view(1);
        toolbar_->show_style(ChartStyle::Candle);
        window_->resize(900, 200);
        window_->show();
        window_->activateWindow();
        if (!QTest::qWaitForWindowActive(window_))
            QSKIP("no window becomes active here");
    }

    void cleanupTestCase() { delete window_; }

    void the_smt_button_is_the_1_minute_view_s() {
        auto* smt = toolbar_->findChild<QToolButton*>("krxSmt");
        QVERIFY(smt);
        QSignalSpy toggled(toolbar_, &screens::KrxChartToolbar::smt_toggled);
        toolbar_->show_smt(true); // shown, not toggled
        QVERIFY(smt->isChecked() && smt->property("active").toBool());
        QCOMPARE(toggled.count(), 0);
        QTest::mouseClick(smt, Qt::LeftButton);
        QCOMPARE(toggled.count(), 1);
        QCOMPARE(toggled.first().first().toBool(), false);
        QVERIFY(!smt->property("active").toBool());
        toolbar_->show_smt(true);
        toolbar_->show_view(5);
        QVERIFY(!smt->isEnabled());
        QVERIFY(!smt->property("active").toBool()); // not filled where none are drawn
        toolbar_->show_view(1);
        QVERIFY(smt->isEnabled() && smt->property("active").toBool());
        toolbar_->show_style(ChartStyle::Renko);
        QVERIFY(!smt->isEnabled());
        toolbar_->show_style(ChartStyle::Candle);
        QVERIFY(smt->isEnabled());
        toolbar_->show_smt(false);
        QCOMPARE(toggled.count(), 1);
    }

    void a_narrow_panel_hides_the_indicators_label() {
        auto* indicators = toolbar_->findChild<QToolButton*>("krxIndicators");
        QVERIFY(indicators);
        QCOMPARE(indicators->toolButtonStyle(), Qt::ToolButtonTextBesideIcon); // fits the window
        const int row = toolbar_->sizeHint().width();
        const QSize before = window_->size();
        const int width = row - 40;
        window_->resize(before.width() - toolbar_->width() + width, before.height());
        QTRY_COMPARE(toolbar_->width(), width);
        QCOMPARE(indicators->toolButtonStyle(), Qt::ToolButtonIconOnly);

        window_->resize(before);
        QTRY_COMPARE(window_->size(), before);
        QTRY_COMPARE(indicators->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    }
};

QTEST_MAIN(TstKrxChartToolbar)
#include "tst_krx_chart_toolbar.moc"
