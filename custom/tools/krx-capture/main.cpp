// Fork addition (custom/README.md, "화면 캡처"): shows the KRX screen against an engine,
// waits for it to read the engine, and saves the screen as a PNG. Given predictors, it
// also opens their detail windows, as a double-click on the leaderboard does, and saves
// each next to the screen's: <out>-<predictor>.png. KRX_CAPTURE_BAR opens bar windows, as a
// double-click on a 1-minute bar does (krx-quant #255): <out>-bar<n>.png.
// KRX_CAPTURE_TRADER=1 saves the auto trader screen (codeyoma/krx-quant#397); KRX_CAPTURE_CONFIRM=entries or
// =sell then presses that pause button and saves its confirm window, <out>-confirm.png, and cancels it:
// nothing is sent. KRX_CAPTURE_POLICY=<version> chooses that policy's trades over the trades table, and
// KRX_CAPTURE_SCROLL=trades|policies scrolls the screen to that panel before it is saved
// (codeyoma/krx-quant#401). KRX_CAPTURE_HOVER_AT=<ISO 8601> rests the pointer on that moment's bar of its
// 1-minute chart (codeyoma/krx-quant#537), as on the KRX screen's.
// KRX_CAPTURE_VERDICT=1 saves the verdict screen (종합 판단, codeyoma/krx-quant#678): KRX_CAPTURE_SYMBOL the
// symbol whose members are shown (default the one kept, else the first verdict's).
//
//   krx_capture <engine url> <out.png> [seconds=8] [width=1600] [height=1000] [predictor,...]
//
// The window shows for those seconds: the chart (Qt WebEngine) draws in a real window only.
// With QT_QPA_PLATFORM=offscreen no window opens, and the chart's panel stays empty. The
// theme and font are the app's defaults (Obsidian, Consolas 14px).
#include "core/config/AppConfig.h"
#include "screens/krx_quant/KrxVerdictScreen.h"
#include "screens/krx_quant/KrxTraderScreen.h"
#include "screens/krx_quant/KrxBarWindow.h"
#include "screens/krx_quant/KrxChart.h"
#include "screens/krx_quant/KrxChartToolbar.h"
#include "screens/krx_quant/KrxLeaderboard.h"
#include "screens/krx_quant/KrxQuantScreen.h"
#include "screens/krx_quant/KrxStatsWindow.h"
#include "screens/krx_quant/KrxStudyPanel.h"
#include "services/krx_quant/KrxVerdictLive.h"
#include "services/krx_quant/KrxLive.h"
#include "services/krx_quant/KrxQuantService.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <optional>
#ifdef HAS_QT_WEBENGINE
#    include <QWebEngineView>
#endif

int main(int argc, char* argv[]) {
    if (argc < 3) {
        QTextStream(stderr) << "usage: krx_capture <engine url> <out.png> [seconds] [width] [height] "
                               "[predictor,...]\n";
        return 2;
    }
    QApplication app(argc, argv);
    const int seconds = argc > 3 ? QString(argv[3]).toInt() : 8;
    const int width = argc > 4 ? QString(argv[4]).toInt() : 1600;
    const int height = argc > 5 ? QString(argv[5]).toInt() : 1000;
    const QStringList details = argc > 6 ? QString(argv[6]).split(',', Qt::SkipEmptyParts) : QStringList{};

    auto& theme = fincept::ui::ThemeManager::instance();
    theme.apply_typography_and_density("Consolas", 14, "Default");
    theme.apply_theme("Obsidian");
    fincept::services::KrxQuantService::set_base_url(QString::fromLocal8Bit(argv[1]));
    // KRX_CAPTURE_SELECTION=<predictor,...>: the models ticked on the leaderboard for the
    // chart (krx-quant #159); empty clears them. Unset, the scratch settings' last one stays.
    if (qEnvironmentVariableIsSet("KRX_CAPTURE_SELECTION"))
        fincept::AppConfig::instance().set("krx_quant/chart_selection", qEnvironmentVariable("KRX_CAPTURE_SELECTION"));
    // KRX_CAPTURE_KIND=<kind>: the prediction kind chosen at the top (krx-quant #182), e.g. "range";
    // "direction" goes back. Unset, the scratch settings' last one stays. Shown once the engine says it makes it.
    if (qEnvironmentVariableIsSet("KRX_CAPTURE_KIND"))
        fincept::AppConfig::instance().set("krx_quant/kind", qEnvironmentVariable("KRX_CAPTURE_KIND"));
    // KRX_CAPTURE_SEPARATOR=<0|1>: the options menu's day separators (krx-quant #221).
    if (qEnvironmentVariableIsSet("KRX_CAPTURE_SEPARATOR"))
        fincept::AppConfig::instance().set("krx_quant/chart_day_separator",
                                           qEnvironmentVariableIntValue("KRX_CAPTURE_SEPARATOR") != 0);
    // KRX_CAPTURE_SPECIAL=<0|1>: the options menu's special situations' bands (krx-quant #236).
    if (qEnvironmentVariableIsSet("KRX_CAPTURE_SPECIAL"))
        fincept::AppConfig::instance().set(fincept::krx_quant::kSpecialKey,
                                           qEnvironmentVariableIntValue("KRX_CAPTURE_SPECIAL") != 0);
    // KRX_CAPTURE_SMT=<0|1>: the toolbar's SMT divergences (codeyoma/krx-quant#522).
    if (qEnvironmentVariableIsSet("KRX_CAPTURE_SMT"))
        fincept::AppConfig::instance().set(fincept::krx_quant::kSmtKey,
                                           qEnvironmentVariableIntValue("KRX_CAPTURE_SMT") != 0);
    // KRX_CAPTURE_LOG=<0|1>: 일반 or 로그 at the chart's bottom right (krx-quant #245).
    if (qEnvironmentVariableIsSet("KRX_CAPTURE_LOG"))
        fincept::AppConfig::instance().set(fincept::krx_quant::kLogScaleKey,
                                           qEnvironmentVariableIntValue("KRX_CAPTURE_LOG") != 0);

    // KRX_CAPTURE_VERDICT=1: the verdict screen (codeyoma/krx-quant#678), saved after the wait;
    // KRX_CAPTURE_SYMBOL the symbol whose members are shown.
    if (qEnvironmentVariableIntValue("KRX_CAPTURE_VERDICT") != 0) {
        if (const QString symbol = qEnvironmentVariable("KRX_CAPTURE_SYMBOL"); !symbol.isEmpty())
            fincept::AppConfig::instance().set(fincept::services::KrxVerdictLive::kSymbolKey, symbol);
        fincept::screens::KrxVerdictScreen verdict;
        verdict.resize(width, height);
        verdict.show();
        const QString out = QString::fromLocal8Bit(argv[2]);
        int status = 0;
        QTimer::singleShot(seconds * 1000, &app, [&]() {
            if (!verdict.grab().save(out)) {
                QTextStream(stderr) << "could not save " << out << "\n";
                status = 1;
            }
            app.quit();
        });
        app.exec();
        return status;
    }

    // KRX_CAPTURE_TRADER=1: the auto trader screen (codeyoma/krx-quant#397), saved after the wait;
    // KRX_CAPTURE_CONFIRM=entries|sell: then a pause button's confirm window too, cancelled.
    // KRX_CAPTURE_POLICY=<version>: that policy's trades, chosen at half the wait (codeyoma/krx-quant#401);
    // KRX_CAPTURE_SCROLL=trades|policies: the screen scrolled to that panel before it is saved.
    // KRX_CAPTURE_HOVER_AT=<ISO 8601>: as the pointer resting on that moment's bar of the 1-minute chart
    // (codeyoma/krx-quant#537), from 0.8 of the wait: its hover box shows (a buy's or sell's lines).
    if (qEnvironmentVariableIntValue("KRX_CAPTURE_TRADER") != 0) {
        fincept::screens::KrxTraderScreen trader;
        trader.resize(width, height);
        trader.show();
        const QDateTime trader_hover = QDateTime::fromString(qEnvironmentVariable("KRX_CAPTURE_HOVER_AT"), Qt::ISODate);
        if (trader_hover.isValid())
            QTimer::singleShot(seconds * 800, &app, [&trader, trader_hover]() {
#ifdef HAS_QT_WEBENGINE
                auto* chart = trader.findChild<fincept::screens::KrxChart*>();
                if (auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr)
                    view->page()->runJavaScript(
                        QStringLiteral(
                            "(function(){const x = window.krxChart.x(%1), y = innerHeight / 3; if (x === null) return;"
                            "const at = document.elementFromPoint(x, y);"
                            "const event = (type, bubbles) => new MouseEvent(type, {clientX: x, clientY: y, "
                            "  pageX: x, pageY: y, bubbles: bubbles, view: window});"
                            "for (let e = at; e; e = e.parentElement) e.dispatchEvent(event('mouseenter', false));"
                            "at.dispatchEvent(event('mousemove', true));"
                            "})();")
                            .arg(fincept::krx_quant::chart_time(trader_hover)));
#else
                Q_UNUSED(trader);
                Q_UNUSED(trader_hover);
#endif
            });
        const QString out = QString::fromLocal8Bit(argv[2]);
        const QString confirm = qEnvironmentVariable("KRX_CAPTURE_CONFIRM");
        int status = 0;
        if (const QString policy = qEnvironmentVariable("KRX_CAPTURE_POLICY"); !policy.isEmpty())
            QTimer::singleShot(seconds * 500, &app, [&trader, &status, policy]() {
                auto* box = trader.findChild<QComboBox*>(
                    QString::fromLatin1(fincept::screens::KrxTraderScreen::kTradesPolicyName));
                const int index = box != nullptr ? box->findData(QVariant(policy.toLongLong())) : -1;
                if (index < 0) {
                    QTextStream(stderr) << "no policy " << policy << " to choose\n";
                    status = 1;
                    return;
                }
                box->setCurrentIndex(index);
                emit box->activated(index); // as a click on it
            });
        QTimer::singleShot(seconds * 1000, &app, [&]() {
            if (const QString to = qEnvironmentVariable("KRX_CAPTURE_SCROLL"); !to.isEmpty()) {
                const char* name = to == QLatin1String("trades") ? fincept::screens::KrxTraderScreen::kTradesName
                                                                 : fincept::screens::KrxTraderScreen::kPoliciesName;
                auto* area = trader.findChild<QScrollArea*>();
                QWidget* panel = trader.findChild<QWidget*>(QString::fromLatin1(name));
                // The table's panel, its header at the top of the view: the ancestor right under the body.
                while (area != nullptr && panel != nullptr && panel->parentWidget() != area->widget())
                    panel = panel->parentWidget();
                if (area != nullptr && panel != nullptr) {
                    area->verticalScrollBar()->setValue(panel->mapTo(area->widget(), QPoint(0, 0)).y());
                    QApplication::processEvents();
                } else {
                    QTextStream(stderr) << "no " << to << " to scroll to\n";
                    status = 1;
                }
            }
            if (!trader.grab().save(out)) {
                QTextStream(stderr) << "could not save " << out << "\n";
                status = 1;
            }
            if (confirm.isEmpty())
                return app.quit();
            const char* name = confirm == QLatin1String("sell") ? fincept::screens::KrxTraderScreen::kPauseSellName
                                                                : fincept::screens::KrxTraderScreen::kPauseEntriesName;
            auto* button = trader.findChild<QPushButton*>(QString::fromLatin1(name));
            if (button == nullptr || !button->isEnabled()) {
                QTextStream(stderr) << "the " << confirm << " pause button cannot be pressed\n";
                status = 1;
                return app.quit();
            }
            // The window is modal: it is saved and cancelled from within its own loop.
            QTimer::singleShot(800, &app, [&]() {
                QWidget* window = QApplication::activeModalWidget();
                const QFileInfo info(out);
                const QString path = info.dir().filePath(info.completeBaseName() + "-confirm.png");
                if (window == nullptr || !window->grab().save(path)) {
                    QTextStream(stderr) << "could not save " << path << "\n";
                    status = 1;
                }
                if (window != nullptr)
                    window->close(); // the cancel: nothing is sent
                QTimer::singleShot(0, &app, [&app]() { app.quit(); });
            });
            button->click();
        });
        app.exec();
        return status;
    }

    fincept::screens::KrxQuantScreen screen;
    screen.resize(width, height);
    // KRX_CAPTURE_SCREEN=<index>: open on that display (QGuiApplication::screens()), e.g. a wide
    // one when the primary is too narrow for the width asked (the window is kept within its display).
    if (const QString index = qEnvironmentVariable("KRX_CAPTURE_SCREEN"); !index.isEmpty()) {
        const auto screens = QGuiApplication::screens();
        if (const int i = index.toInt(); i >= 0 && i < screens.size())
            screen.move(screens[i]->availableGeometry().topLeft());
    }
    screen.show();
    // Keys go through Qt's shortcuts, which need the window active (KRX_CAPTURE_KEY): asked for at
    // once, as a Mac only lets an app take the front while it starts.
    if (qEnvironmentVariableIsSet("KRX_CAPTURE_KEY"))
        screen.activateWindow();
    if (auto* leaderboard = screen.findChild<fincept::screens::KrxLeaderboard*>()) {
        for (const auto& predictor : details) // as a double-click on its line asks
            QMetaObject::invokeMethod(leaderboard, "detail_requested", Q_ARG(QString, predictor));
    }

    // KRX_CAPTURE_SYMBOL=<symbol>: as picking that symbol in the list (krx-quant #236), at 0.2 of the
    // wait, once the engine is read: the views and styles below are then its own.
    if (const QString symbol = qEnvironmentVariable("KRX_CAPTURE_SYMBOL"); !symbol.isEmpty()) {
        QTimer::singleShot(seconds * 200, &app, [&screen, symbol]() {
            if (auto* live = screen.findChild<fincept::services::KrxLive*>())
                live->select(symbol);
        });
    }

    // KRX_CAPTURE_VIEW=<minutes>: as picking that view above the chart (krx-quant #109, #221); 1440
    // is the day view's ("일", krx-quant #111). KRX_CAPTURE_STYLE=<style>: as picking that chart
    // style (krx-quant #221), e.g. "heikin_ashi", or one without a time axis (krx-quant #225),
    // "renko". Both once the engine is read and a symbol is chosen, as each symbol keeps its own:
    // they stay in the scratch settings for that symbol.
    //
    // KRX_CAPTURE_INDICATORS=<indicator,...>: those indicators on (krx-quant #222), in that order,
    // at their defaults, the others off but the volume, e.g. "ma,bollinger,ichimoku,volume_profile"
    // (the names of custom/README.md); "none" turns all but the volume off. Naming a lower one
    // (krx-quant #223) turns the lower ones off too, the volume among them, so that only those
    // named stack below, in that order: "ma,volume,rsi,macd". KRX_CAPTURE_ANCHOR=<ISO 8601>: the
    // anchored VWAP's bar, as a click on the chart at that moment. KRX_CAPTURE_PANES=<indicator:
    // height,...>: the lower panes' heights in CSS pixels, as dragged, e.g. "rsi:180". All at the
    // same time, kept for that symbol too.
    QTimer::singleShot(seconds * 300, &app, [&screen]() {
        auto* toolbar = screen.findChild<fincept::screens::KrxChartToolbar*>();
        if (!toolbar)
            return;
        if (const int minutes = qEnvironmentVariableIntValue("KRX_CAPTURE_VIEW"); minutes > 0)
            emit toolbar->view_chosen(minutes);
        if (const auto style = fincept::krx_quant::parse_style(qEnvironmentVariable("KRX_CAPTURE_STYLE")))
            emit toolbar->style_chosen(*style);
        // KRX_CAPTURE_BRICK=<size>: the size of the style shown when it has no time axis (krx-quant #225),
        // as set in the options menu: won, or the line break's lines; 0 back to its default (ATR(14), 3
        // lines). Kept for that symbol too.
        if (const QString size = qEnvironmentVariable("KRX_CAPTURE_BRICK"); !size.isEmpty()) {
            auto* live = screen.findChild<fincept::services::KrxLive*>();
            const QString group = fincept::krx_quant::prefs_group(live ? live->selected() : QString());
            const auto prefs = fincept::krx_quant::parse_prefs(
                fincept::AppConfig::instance().get(group + fincept::krx_quant::kPrefsView),
                fincept::AppConfig::instance().get(group + fincept::krx_quant::kPrefsStyle));
            emit toolbar->brick_size_chosen(prefs.style, size.toDouble());
        }
        auto* panel = screen.findChild<fincept::screens::KrxStudyPanel*>();
        if (const QString on = qEnvironmentVariable("KRX_CAPTURE_INDICATORS"); panel && !on.isEmpty()) {
            fincept::krx_quant::StudySet set = fincept::krx_quant::default_studies();
            for (const auto study : fincept::krx_quant::kUpperStudies)
                set.turn(study, false);
            const QStringList keys = on.split(',', Qt::SkipEmptyParts);
            const bool lower = std::any_of(keys.begin(), keys.end(), [](const QString& key) {
                const auto study = fincept::krx_quant::parse_study(key.trimmed());
                return study && !fincept::krx_quant::study_spec(*study).upper;
            });
            if (lower) // only those named below, in that order
                for (const auto study : fincept::krx_quant::kLowerStudies)
                    set.turn(study, false);
            for (const QString& key : keys)
                if (const auto study = fincept::krx_quant::parse_study(key.trimmed()))
                    set.turn(*study, true);
            emit panel->studies_changed(set);
        }
        if (const QString panes = qEnvironmentVariable("KRX_CAPTURE_PANES"); !panes.isEmpty()) {
            fincept::krx_quant::PaneHeights heights;
            for (const QString& pane : panes.split(',', Qt::SkipEmptyParts)) {
                const QStringList parts = pane.split(':');
                if (const auto study = fincept::krx_quant::parse_study(parts.value(0).trimmed()); study)
                    heights.insert(*study, parts.value(1).toInt());
            }
            if (auto* chart = screen.findChild<fincept::screens::KrxChart*>(); chart && !heights.isEmpty())
                emit chart->pane_heights_changed(heights);
        }
        const QDateTime anchor = QDateTime::fromString(qEnvironmentVariable("KRX_CAPTURE_ANCHOR"), Qt::ISODate);
        if (auto* chart = screen.findChild<fincept::screens::KrxChart*>(); chart && anchor.isValid())
            emit chart->anchor_chosen(fincept::krx_quant::chart_time(anchor));
    });
    // KRX_CAPTURE_MENU=<minutes|style|options|indicators>: that menu of the row above the chart opened
    // at 0.9 of the wait, and saved as <out>-menu.png (a menu is a window of its own). "indicators" is
    // the indicators panel (krx-quant #222), KRX_CAPTURE_INDICATOR=<indicator> picked in it.
    QWidget* menu = nullptr;
    if (const QString which = qEnvironmentVariable("KRX_CAPTURE_MENU"); !which.isEmpty()) {
        QTimer::singleShot(seconds * 900, &app, [&screen, &menu, which]() {
            if (which == "indicators") {
                auto* toolbar = screen.findChild<fincept::screens::KrxChartToolbar*>();
                auto* panel = screen.findChild<fincept::screens::KrxStudyPanel*>();
                if (!toolbar || !panel)
                    return;
                emit toolbar->indicators_requested();
                if (const auto study = fincept::krx_quant::parse_study(qEnvironmentVariable("KRX_CAPTURE_INDICATOR")))
                    panel->select(*study);
                menu = panel;
                return;
            }
            const QString name = which == "style"     ? "krxStyle"
                                 : which == "options" ? "krxOptions"
                                                      : "krxMinutes";
            if (auto* button = screen.findChild<QToolButton*>(name); button && button->menu()) {
                button->menu()->popup(button->mapToGlobal(QPoint(0, button->height())));
                menu = button->menu();
            }
        });
    }

    // KRX_CAPTURE_PERIOD=<today|5d|20d>: the leaderboard's period, chosen once the screen has read the
    // engine (krx-quant #264: a daily kind's today is empty until its answers come at night).
    if (const QString period = qEnvironmentVariable("KRX_CAPTURE_PERIOD"); !period.isEmpty()) {
        QTimer::singleShot(seconds * 300, &app, [&screen, period]() {
            auto* board = screen.findChild<fincept::screens::KrxLeaderboard*>();
            for (auto* combo : board ? board->findChildren<QComboBox*>() : QList<QComboBox*>{})
                if (const int at = combo->findData(period); at >= 0)
                    combo->setCurrentIndex(at);
        });
    }

    // KRX_CAPTURE_PIN=<bar close, ISO 8601>: as a click on that bar close's chart icon or bar,
    // once the screen has read the engine (krx-quant #107, #249).
    if (const QDateTime pin = QDateTime::fromString(qEnvironmentVariable("KRX_CAPTURE_PIN"), Qt::ISODate);
        pin.isValid()) {
        QTimer::singleShot(seconds * 500, &app, [&screen, pin]() {
            if (auto* chart = screen.findChild<fincept::screens::KrxChart*>())
                emit chart->close_clicked(pin);
        });
    }

    // KRX_CAPTURE_BAR=<bar close, ISO 8601>[,…]: as a double-click on that 1-minute bar (krx-quant #255),
    // once the screen has read the engine: its window opens, saved as <out>-bar<n>.png in that order.
    for (const QString& text : qEnvironmentVariable("KRX_CAPTURE_BAR").split(',', Qt::SkipEmptyParts)) {
        if (const QDateTime close = QDateTime::fromString(text.trimmed(), Qt::ISODate); close.isValid())
            QTimer::singleShot(seconds * 500, &app, [&screen, close]() {
                if (auto* chart = screen.findChild<fincept::screens::KrxChart*>())
                    emit chart->bar_window_requested(close);
            });
    }

#ifdef HAS_QT_WEBENGINE
    // KRX_CAPTURE_BACK=<wheel turns>: as wheeling the chart back that many times, a turn every
    // half second from a quarter of the wait, so earlier days are read and shown (krx-quant #108).
    if (const int turns = qEnvironmentVariableIntValue("KRX_CAPTURE_BACK"); turns > 0) {
        for (int turn = 0; turn < turns; ++turn) {
            QTimer::singleShot(seconds * 250 + turn * 500, &app, [&screen]() {
                auto* chart = screen.findChild<fincept::screens::KrxChart*>();
                auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr;
                if (view)
                    view->page()->runJavaScript(
                        "document.elementFromPoint(innerWidth / 2, innerHeight / 3).dispatchEvent(new WheelEvent("
                        "'wheel', {deltaX: -400, bubbles: true, cancelable: true, clientX: innerWidth / 2, "
                        "clientY: innerHeight / 3}));");
            });
        }
    }
    // KRX_CAPTURE_HOVER=<0–1>[,<0–1>]: as the pointer resting that far across the chart, a third of
    // the way down (or as far as the second, e.g. in a lower pane, krx-quant #223), from 0.8 of the
    // wait: the hovered bar's box shows (krx-quant #159).
    if (const QString at = qEnvironmentVariable("KRX_CAPTURE_HOVER"); !at.isEmpty()) {
        const double down = at.section(',', 1, 1).isEmpty() ? 1.0 / 3 : at.section(',', 1, 1).toDouble();
        QTimer::singleShot(seconds * 800, &app, [&screen, across = at.section(',', 0, 0).toDouble(), down]() {
            auto* chart = screen.findChild<fincept::screens::KrxChart*>();
            auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr;
            if (view)
                view->page()->runJavaScript(
                    QStringLiteral(
                        "(function(){const x = innerWidth * %1, y = innerHeight * %2;"
                        "const at = document.elementFromPoint(x, y);"
                        "const event = (type, bubbles) => new MouseEvent(type, {clientX: x, clientY: y, "
                        "  pageX: x, pageY: y, bubbles: bubbles, view: window});"
                        "for (let e = at; e; e = e.parentElement) e.dispatchEvent(event('mouseenter', false));"
                        "at.dispatchEvent(event('mousemove', true));"
                        "})();")
                        .arg(across)
                        .arg(down));
        });
    }
    // KRX_CAPTURE_AT=<ISO 8601>: the chart's view moved, at 0.7 of the wait, so that the slot holding
    // that moment is in its middle, as many bars wide as it was (krx-quant #236), e.g. a halt's start.
    // KRX_CAPTURE_HOVER_AT=<ISO 8601>: as the pointer resting on that moment's slot, a third of the
    // way down, from 0.8 of the wait: its hover box shows (an empty bar's, a band's).
    const QDateTime show_at = QDateTime::fromString(qEnvironmentVariable("KRX_CAPTURE_AT"), Qt::ISODate);
    if (show_at.isValid()) {
        QTimer::singleShot(seconds * 700, &app, [&screen, show_at]() {
            auto* chart = screen.findChild<fincept::screens::KrxChart*>();
            if (auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr)
                view->page()->runJavaScript(QStringLiteral("window.krxChart && window.krxChart.show(%1);")
                                                .arg(fincept::krx_quant::chart_time(show_at)));
        });
    }
    const QDateTime hover_at = QDateTime::fromString(qEnvironmentVariable("KRX_CAPTURE_HOVER_AT"), Qt::ISODate);
    if (hover_at.isValid()) {
        QTimer::singleShot(seconds * 800, &app, [&screen, hover_at]() {
            auto* chart = screen.findChild<fincept::screens::KrxChart*>();
            if (auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr)
                view->page()->runJavaScript(
                    QStringLiteral(
                        "(function(){const x = window.krxChart.x(%1), y = innerHeight / 3; if (x === null) return;"
                        "const at = document.elementFromPoint(x, y);"
                        "const event = (type, bubbles) => new MouseEvent(type, {clientX: x, clientY: y, "
                        "  pageX: x, pageY: y, bubbles: bubbles, view: window});"
                        "for (let e = at; e; e = e.parentElement) e.dispatchEvent(event('mouseenter', false));"
                        "at.dispatchEvent(event('mousemove', true));"
                        "})();")
                        .arg(fincept::krx_quant::chart_time(hover_at)));
        });
    }
    // KRX_CAPTURE_DRAG=<pane>:<pixels>: as dragging the border above that lower pane (1 the top one,
    // krx-quant #223) that many pixels down (up when negative), at 0.6 of the wait: the panes'
    // heights change and are kept for the symbol, as a drag with the mouse does.
    if (const QString drag = qEnvironmentVariable("KRX_CAPTURE_DRAG"); !drag.isEmpty()) {
        QTimer::singleShot(seconds * 600, &app, [&screen, drag]() {
            auto* chart = screen.findChild<fincept::screens::KrxChart*>();
            auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr;
            if (view)
                view->page()->runJavaScript(
                    QStringLiteral(
                        "(function(){const handles = [...document.querySelectorAll('div')].filter("
                        "  (d) => d.style.cursor === 'row-resize' && d.style.zIndex === '50');"
                        "const h = handles[%1 - 1]; if (!h) return;"
                        "const r = h.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2;"
                        "const event = (type, dy) => new MouseEvent(type, {clientX: x, clientY: y + dy, screenX: x,"
                        "  screenY: y + dy, button: 0, buttons: type === 'mouseup' ? 0 : 1, bubbles: true,"
                        "  cancelable: true, view: window});"
                        "h.dispatchEvent(event('mousedown', 0));"
                        "const root = document.documentElement;"
                        "for (let k = 1; k <= 10; ++k) root.dispatchEvent(event('mousemove', %2 * k / 10));"
                        "root.dispatchEvent(event('mouseup', %2));"
                        "})();")
                        .arg(drag.section(':', 0, 0).toInt())
                        .arg(drag.section(':', 1, 1).toInt()));
        });
    }
    // KRX_CAPTURE_JS=<script>: that script run on the chart's page at 0.7 of the wait, e.g. synthetic mouse
    // and key events.
    if (const QString script = qEnvironmentVariable("KRX_CAPTURE_JS"); !script.isEmpty()) {
        QTimer::singleShot(seconds * 700, &app, [&screen, script]() {
            auto* chart = screen.findChild<fincept::screens::KrxChart*>();
            if (auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr)
                view->page()->runJavaScript(script);
        });
    }
    // KRX_CAPTURE_KEY=<key>[,<key>…]: each key (Escape, a letter, …) pressed 0.3 s apart from 0.75 of the
    // wait, through Qt's shortcuts as the keyboard's are: to the open menu, else the widget with the
    // keyboard. The chart takes the keyboard first, as a click on it does. Each key's widget is named on
    // stderr.
    if (const QStringList keys = qEnvironmentVariable("KRX_CAPTURE_KEY").split(',', Qt::SkipEmptyParts);
        !keys.isEmpty()) {
        QTimer::singleShot(seconds * 750 - 100, &app, [&screen]() {
            auto* chart = screen.findChild<fincept::screens::KrxChart*>();
            if (auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr)
                view->setFocus(Qt::MouseFocusReason);
        });
        for (qsizetype i = 0; i < keys.size(); ++i) {
            QTimer::singleShot(seconds * 750 + static_cast<int>(i) * 300, &app, [key = keys[i].trimmed()]() {
                const QKeySequence sequence(key);
                QWidget* target = QApplication::activePopupWidget();
                if (!target)
                    target = QApplication::focusWidget();
                QTextStream(stderr) << "key " << key << " to "
                                    << (target ? target->metaObject()->className() : "nothing")
                                    << (QApplication::activeWindow() ? "" : " (no active window)") << "\n";
                if (target && !sequence.isEmpty())
                    QTest::keyClick(target, sequence[0].key(), sequence[0].keyboardModifiers());
            });
        }
    }
    // KRX_CAPTURE_LEGEND=<row>: as the pointer resting on that row of the price pane's legend
    // (krx-quant #222, 0 the first), from 0.85 of the wait: its hide, settings and delete buttons show.
    if (const QString row = qEnvironmentVariable("KRX_CAPTURE_LEGEND"); !row.isEmpty()) {
        QTimer::singleShot(seconds * 850, &app, [&screen, row]() {
            auto* chart = screen.findChild<fincept::screens::KrxChart*>();
            auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr;
            if (view)
                view->page()->runJavaScript(
                    QStringLiteral("(function(){const r = document.querySelectorAll('#legend0 .row')[%1];"
                                   "if (r) r.dispatchEvent(new MouseEvent('mouseenter'));})();")
                        .arg(row.toInt()));
        });
    }
#endif

    const QString out = QString::fromLocal8Bit(argv[2]);
    int status = 0;
    const auto save = [&status](QWidget* widget, const QString& path) {
        if (!widget->grab().save(path)) {
            QTextStream(stderr) << "could not save " << path << "\n";
            status = 1;
        }
    };
    QTimer::singleShot(seconds * 1000, &app, [&]() {
        save(&screen, out);
        const QFileInfo file(out);
        // The windows, in the order they were opened: the predictors'.
        const auto windows = screen.findChildren<fincept::screens::KrxStatsWindow*>(Qt::FindDirectChildrenOnly);
        // KRX_CAPTURE_STATS_TAB=<tab>: each detail window's tab of that name chosen first, e.g. 변동성별
        // (krx-quant #262).
        if (const QString tab = qEnvironmentVariable("KRX_CAPTURE_STATS_TAB"); !tab.isEmpty()) {
            for (auto* window : windows)
                for (auto* tabs : window->findChildren<QTabWidget*>())
                    for (int t = 0; t < tabs->count(); ++t)
                        if (tabs->tabText(t) == tab)
                            tabs->setCurrentIndex(t);
            QApplication::processEvents();
        }
        // KRX_CAPTURE_STATS_SIZE=<width>x<height>: each detail window resized first, e.g. 1100x1100 so the lifts
        // show above a P&L section (krx-quant #264).
        if (const QStringList size = qEnvironmentVariable("KRX_CAPTURE_STATS_SIZE").split('x'); size.size() == 2) {
            for (auto* window : windows)
                window->resize(size[0].toInt(), size[1].toInt());
            QApplication::processEvents();
        }
        for (qsizetype i = 0; i < windows.size() && i < details.size(); ++i)
            save(windows[i], file.dir().filePath(
                                 QStringLiteral("%1-%2.%3").arg(file.completeBaseName(), details[i], file.suffix())));
        // The bar windows (KRX_CAPTURE_BAR, krx-quant #255), each as tall as its sections, in the order opened.
        const auto bars = screen.findChildren<fincept::screens::KrxBarWindow*>(Qt::FindDirectChildrenOnly);
        for (qsizetype i = 0; i < bars.size(); ++i) {
            if (auto* scroll = bars[i]->findChild<QScrollArea*>(); scroll && scroll->widget())
                bars[i]->resize(bars[i]->width(),
                                bars[i]->height() - scroll->height() + scroll->widget()->sizeHint().height() + 2);
            QApplication::processEvents();
            const QString name =
                QStringLiteral("%1-bar%2.%3").arg(file.completeBaseName()).arg(i + 1).arg(file.suffix());
            save(bars[i], file.dir().filePath(name));
        }
        // A menu the keys opened (KRX_CAPTURE_KEY) is saved as a menu.
        if (!menu)
            menu = QApplication::activePopupWidget();
        if (menu)
            save(menu, file.dir().filePath(QStringLiteral("%1-menu.%2").arg(file.completeBaseName(), file.suffix())));
        app.quit();
    });
    app.exec();
    return status;
}
