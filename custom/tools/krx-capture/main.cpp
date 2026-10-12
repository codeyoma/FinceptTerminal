// Fork addition (custom/README.md, "화면 캡처"): shows the KRX screen against
// an engine, waits for it to read the engine, and saves the screen as a PNG.
//
//   krx_capture <engine url> <out.png> [seconds=8] [width=1600] [height=1000]
//
// Run it with QT_QPA_PLATFORM=offscreen so no window opens. The theme and font
// are the app's defaults (Obsidian, Consolas 14px).
#include "screens/krx_quant/KrxQuantScreen.h"
#include "services/krx_quant/KrxQuantService.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QTextStream>
#include <QTimer>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        QTextStream(stderr) << "usage: krx_capture <engine url> <out.png> "
                               "[seconds] [width] [height]\n";
        return 2;
    }
    QApplication app(argc, argv);
    const int seconds = argc > 3 ? QString(argv[3]).toInt() : 8;
    const int width = argc > 4 ? QString(argv[4]).toInt() : 1600;
    const int height = argc > 5 ? QString(argv[5]).toInt() : 1000;

    auto& theme = fincept::ui::ThemeManager::instance();
    theme.apply_typography_and_density("Consolas", 14, "Default");
    theme.apply_theme("Obsidian");
    fincept::services::KrxQuantService::set_base_url(QString::fromLocal8Bit(argv[1]));

    fincept::screens::KrxQuantScreen screen;
    screen.resize(width, height);
    screen.show();
    const QString out = QString::fromLocal8Bit(argv[2]);
    int status = 0;
    QTimer::singleShot(seconds * 1000, &app, [&]() {
        if (!screen.grab().save(out)) {
            QTextStream(stderr) << "could not save " << out << "\n";
            status = 1;
        }
        app.quit();
    });
    app.exec();
    return status;
}
