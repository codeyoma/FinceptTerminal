// Fork addition (custom/README.md, "화면 캡처"): shows the KRX screen against an engine,
// waits for it to read the engine, and saves the screen as a PNG. Given predictors, it
// also opens their detail windows, as a double-click on the leaderboard does, and saves
// each next to the screen's: <out>-<predictor>.png.
//
//   krx_capture <engine url> <out.png> [seconds=8] [width=1600] [height=1000] [predictor,...]
//
// Run it with QT_QPA_PLATFORM=offscreen so no window opens. The theme and font are the
// app's defaults (Obsidian, Consolas 14px).
#include "screens/krx_quant/KrxLeaderboard.h"
#include "screens/krx_quant/KrxQuantScreen.h"
#include "screens/krx_quant/KrxStatsWindow.h"
#include "services/krx_quant/KrxQuantService.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

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

    fincept::screens::KrxQuantScreen screen;
    screen.resize(width, height);
    screen.show();
    if (auto* leaderboard = screen.findChild<fincept::screens::KrxLeaderboard*>()) {
        for (const auto& predictor : details) // as a double-click on its line asks
            QMetaObject::invokeMethod(leaderboard, "detail_requested", Q_ARG(QString, predictor));
    }

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
