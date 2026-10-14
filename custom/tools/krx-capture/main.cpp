// Fork addition (custom/README.md, "화면 캡처"): shows the KRX screen against an engine,
// waits for it to read the engine, and saves the screen as a PNG. Given predictors, it
// also opens their detail windows, as a double-click on the leaderboard does, and saves
// each next to the screen's: <out>-<predictor>.png.
//
//   krx_capture <engine url> <out.png> [seconds=8] [width=1600] [height=1000] [predictor,...]
//
// The window shows for those seconds: the chart (Qt WebEngine) draws in a real window only.
// With QT_QPA_PLATFORM=offscreen no window opens, and the chart's panel stays empty. The
// theme and font are the app's defaults (Obsidian, Consolas 14px).
#include "core/config/AppConfig.h"
#include "screens/krx_quant/KrxChart.h"
#include "screens/krx_quant/KrxLeaderboard.h"
#include "screens/krx_quant/KrxQuantScreen.h"
#include "screens/krx_quant/KrxStatsWindow.h"
#include "services/krx_quant/KrxQuantService.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>
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

    fincept::screens::KrxQuantScreen screen;
    screen.resize(width, height);
    screen.show();
    if (auto* leaderboard = screen.findChild<fincept::screens::KrxLeaderboard*>()) {
        for (const auto& predictor : details) // as a double-click on its line asks
            QMetaObject::invokeMethod(leaderboard, "detail_requested", Q_ARG(QString, predictor));
    }

    // KRX_CAPTURE_VIEW=<minutes>: as a click on the chart's view button (krx-quant #109); 1440
    // is the day view's ("일", krx-quant #111). The choice stays in the scratch settings, so
    // the next capture opens on it.
    if (const int minutes = qEnvironmentVariableIntValue("KRX_CAPTURE_VIEW"); minutes > 0) {
        for (auto* button : screen.findChildren<QPushButton*>())
            if (button->isCheckable() && button->property("view").toInt() == minutes)
                button->click();
    }

    // KRX_CAPTURE_PIN=<bar close, ISO 8601>: as a click on that bar close's chart icon, once
    // the screen has read the engine (krx-quant #107).
    if (const QDateTime pin = QDateTime::fromString(qEnvironmentVariable("KRX_CAPTURE_PIN"), Qt::ISODate);
        pin.isValid()) {
        QTimer::singleShot(seconds * 500, &app, [&screen, pin]() {
            if (auto* chart = screen.findChild<fincept::screens::KrxChart*>())
                emit chart->icon_clicked(pin);
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
    // KRX_CAPTURE_HOVER=<0–1>: as the pointer resting that far across the chart, a third of
    // the way down, from 0.8 of the wait: the hovered bar's box shows (krx-quant #159).
    if (const QString at = qEnvironmentVariable("KRX_CAPTURE_HOVER"); !at.isEmpty()) {
        QTimer::singleShot(seconds * 800, &app, [&screen, across = at.toDouble()]() {
            auto* chart = screen.findChild<fincept::screens::KrxChart*>();
            auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr;
            if (view)
                view->page()->runJavaScript(
                    QStringLiteral(
                        "(function(){const x = innerWidth * %1, y = innerHeight / 3;"
                        "const at = document.elementFromPoint(x, y);"
                        "const event = (type, bubbles) => new MouseEvent(type, {clientX: x, clientY: y, "
                        "  pageX: x, pageY: y, bubbles: bubbles, view: window});"
                        "for (let e = at; e; e = e.parentElement) e.dispatchEvent(event('mouseenter', false));"
                        "at.dispatchEvent(event('mousemove', true));"
                        "})();")
                        .arg(across));
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
        for (qsizetype i = 0; i < windows.size() && i < details.size(); ++i)
            save(windows[i], file.dir().filePath(
                                 QStringLiteral("%1-%2.%3").arg(file.completeBaseName(), details[i], file.suffix())));
        app.quit();
    });
    app.exec();
    return status;
}
