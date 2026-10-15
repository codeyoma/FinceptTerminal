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
#include "screens/krx_quant/KrxChartToolbar.h"
#include "screens/krx_quant/KrxDrawingPopup.h"
#include "screens/krx_quant/KrxLeaderboard.h"
#include "screens/krx_quant/KrxQuantScreen.h"
#include "screens/krx_quant/KrxStatsWindow.h"
#include "screens/krx_quant/KrxStudyPanel.h"
#include "services/krx_quant/ChartDrawings.h"
#include "services/krx_quant/KrxLive.h"
#include "services/krx_quant/KrxQuantService.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QScreen>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <optional>
#ifdef HAS_QT_WEBENGINE
#    include <QWebEngineView>
#endif

namespace {

/// KRX_CAPTURE_DRAWINGS's drawings (krx-quant #224): as a symbol keeps them (custom/README.md), but
/// a point's "time" may be a whole number, bars back from the last 1-minute bar read (0 the last,
/// -10 ten before it; the bar's middle), and its "price" then "open", "high", "low" or "close" of
/// that bar.
QList<fincept::krx_quant::Drawing> capture_drawings(const QString& json, const QList<fincept::krx_quant::Bar>& bars) {
    QJsonArray resolved;
    for (const QJsonValue& value : QJsonDocument::fromJson(json.toUtf8()).array()) {
        QJsonObject drawing = value.toObject();
        QJsonArray points;
        for (const QJsonValue& p : drawing.value("points").toArray()) {
            QJsonObject point = p.toObject();
            std::optional<fincept::krx_quant::Bar> bar;
            if (const QJsonValue time = point.value("time"); time.isDouble() && !bars.isEmpty()) {
                bar = bars[std::clamp<qsizetype>(bars.size() - 1 + time.toInt(), 0, bars.size() - 1)];
                point["time"] = bar->start.addSecs(30).toString(Qt::ISODate);
            }
            if (const QString price = point.value("price").toString(); bar && !price.isEmpty())
                point["price"] = price == "open"   ? bar->open
                                 : price == "high" ? bar->high
                                 : price == "low"  ? bar->low
                                                   : bar->close;
            points.append(point);
        }
        drawing["points"] = points;
        resolved.append(drawing);
    }
    return fincept::krx_quant::parse_drawings(QString::fromUtf8(QJsonDocument(resolved).toJson()));
}

} // namespace

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
    // KRX_CAPTURE_MAGNET=<0|1>: the drawing menu's magnet (krx-quant #224).
    if (qEnvironmentVariableIsSet("KRX_CAPTURE_MAGNET"))
        fincept::AppConfig::instance().set(fincept::krx_quant::kMagnetKey,
                                           qEnvironmentVariableIntValue("KRX_CAPTURE_MAGNET") != 0);

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
    if (auto* leaderboard = screen.findChild<fincept::screens::KrxLeaderboard*>()) {
        for (const auto& predictor : details) // as a double-click on its line asks
            QMetaObject::invokeMethod(leaderboard, "detail_requested", Q_ARG(QString, predictor));
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
        // KRX_CAPTURE_DRAWINGS=<JSON array>: the symbol's drawings now (krx-quant #224), as if drawn, those
        // it kept removed; "[]" removes them all. The format is the one kept (custom/README.md), and
        // capture_drawings above takes bars back from the last and their prices too.
        auto* chart = screen.findChild<fincept::screens::KrxChart*>();
        auto* live = screen.findChild<fincept::services::KrxLive*>();
        if (const QString set = qEnvironmentVariable("KRX_CAPTURE_DRAWINGS"); chart && live && !set.isEmpty()) {
            const QString kept =
                fincept::AppConfig::instance()
                    .get(fincept::krx_quant::prefs_group(live->selected()) + fincept::krx_quant::kPrefsDrawings)
                    .toString();
            for (const auto& d : fincept::krx_quant::parse_drawings(kept))
                emit chart->drawing_removed(d.id);
            for (const auto& d : capture_drawings(set, live->bars()))
                emit chart->drawing_added(d, QPoint());
        }
    });
    // KRX_CAPTURE_SELECT=<n>: the n-th drawing (1 the first) selected at 0.6 of the wait, as a click on
    // it: its handles show (krx-quant #224).
    if (const int n = qEnvironmentVariableIntValue("KRX_CAPTURE_SELECT"); n > 0) {
        QTimer::singleShot(seconds * 600, &app, [&screen, n]() {
            if (auto* chart = screen.findChild<fincept::screens::KrxChart*>())
                chart->select_drawing(n);
        });
    }
    // KRX_CAPTURE_MENU=<minutes|style|options|indicators|drawing|edit>: that menu of the row above the
    // chart opened at 0.9 of the wait, and saved as <out>-menu.png (a menu is a window of its own).
    // "indicators" is the indicators panel (krx-quant #222), KRX_CAPTURE_INDICATOR=<indicator> picked in
    // it; "drawing" the drawing menu, "edit" the popup of the drawing KRX_CAPTURE_SELECT names (the
    // first without it) as a double-click on it opens it (krx-quant #224).
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
            if (which == "edit") {
                auto* chart = screen.findChild<fincept::screens::KrxChart*>();
                auto* popup = screen.findChild<fincept::screens::KrxDrawingPopup*>();
                if (!chart || !popup)
                    return;
                const int n = std::max(1, qEnvironmentVariableIntValue("KRX_CAPTURE_SELECT"));
                emit chart->drawing_edit_requested(n, chart->mapToGlobal(chart->rect().center()));
                menu = popup;
                return;
            }
            const QString name = which == "style"     ? "krxStyle"
                                 : which == "options" ? "krxOptions"
                                 : which == "drawing" ? "krxDrawing"
                                                      : "krxMinutes";
            if (auto* button = screen.findChild<QToolButton*>(name); button && button->menu()) {
                button->menu()->popup(button->mapToGlobal(QPoint(0, button->height())));
                menu = button->menu();
            }
        });
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
    // KRX_CAPTURE_DRAW=<tool>@<x>,<y>[@<x>,<y>…][;<tool>@…]: as picking that drawing tool (krx-quant #224)
    // and clicking the chart at those places (each a share of the chart's width and height, on the
    // prices), a drawing every 1.5 s from half the wait: the page places them as clicks do and the
    // screen keeps them. E.g. "trend_line@0.2,0.3@0.6,0.2;horizontal@0.5,0.4".
    if (const QString draw = qEnvironmentVariable("KRX_CAPTURE_DRAW"); !draw.isEmpty()) {
        const QStringList drawings = draw.split(';', Qt::SkipEmptyParts);
        for (qsizetype k = 0; k < drawings.size(); ++k) {
            QStringList parts = drawings[k].split('@', Qt::SkipEmptyParts);
            const auto tool = fincept::krx_quant::parse_tool(parts.value(0).trimmed());
            if (!tool)
                continue;
            parts.removeFirst();
            QStringList clicks;
            for (qsizetype i = 0; i < parts.size(); ++i)
                clicks << QStringLiteral("setTimeout(() => click(%1, %2), %3);")
                              .arg(parts[i].section(',', 0, 0).toDouble())
                              .arg(parts[i].section(',', 1, 1).toDouble())
                              .arg(150 * (i + 1));
            QTimer::singleShot(seconds * 500 + static_cast<int>(k) * 1500, &app, [&screen, tool = *tool, clicks]() {
                auto* toolbar = screen.findChild<fincept::screens::KrxChartToolbar*>();
                auto* chart = screen.findChild<fincept::screens::KrxChart*>();
                auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr;
                if (!toolbar || !view)
                    return;
                emit toolbar->tool_chosen(tool);
                QTimer::singleShot(300, view, [view, clicks]() {
                    view->page()->runJavaScript(
                        QStringLiteral(
                            "(function(){const click = (fx, fy) => {const x = innerWidth * fx, y = innerHeight * fy;"
                            "const at = document.elementFromPoint(x, y);"
                            "const event = (type) => new MouseEvent(type, {clientX: x, clientY: y, button: 0,"
                            "  buttons: type === 'mousedown' ? 1 : 0, bubbles: true, cancelable: true, view: window});"
                            "for (const type of ['mousemove', 'mousedown', 'mouseup']) at.dispatchEvent(event(type));};"
                            "%1})();")
                            .arg(clicks.join(QString())));
                });
            });
        }
    }
    // KRX_CAPTURE_JS=<script>: that script run on the chart's page at 0.7 of the wait, e.g. synthetic mouse
    // and key events to try the drawings' clicks, drags, Delete and Esc (krx-quant #224).
    if (const QString script = qEnvironmentVariable("KRX_CAPTURE_JS"); !script.isEmpty()) {
        QTimer::singleShot(seconds * 700, &app, [&screen, script]() {
            auto* chart = screen.findChild<fincept::screens::KrxChart*>();
            if (auto* view = chart ? chart->findChild<QWebEngineView*>() : nullptr)
                view->page()->runJavaScript(script);
        });
    }
    // KRX_CAPTURE_KEY=<Escape|Delete|Backspace>: that key pressed at 0.75 of the wait, sent to the widget
    // with the keyboard (the chart takes it when a drawing tool is chosen, krx-quant #224), which is
    // named on stderr.
    if (const QString key = qEnvironmentVariable("KRX_CAPTURE_KEY"); !key.isEmpty()) {
        QTimer::singleShot(seconds * 750, &app, [key]() {
            const int code = key == "Delete" ? Qt::Key_Delete : key == "Backspace" ? Qt::Key_Backspace : Qt::Key_Escape;
            QWidget* target = QApplication::focusWidget();
            QTextStream(stderr) << "key " << key << " to " << (target ? target->metaObject()->className() : "nothing")
                                << "\n";
            if (!target)
                return;
            QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
            QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
            QApplication::sendEvent(target, &press);
            QApplication::sendEvent(target, &release);
        });
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
        for (qsizetype i = 0; i < windows.size() && i < details.size(); ++i)
            save(windows[i], file.dir().filePath(
                                 QStringLiteral("%1-%2.%3").arg(file.completeBaseName(), details[i], file.suffix())));
        // A drawing's popup a double-click opened (KRX_CAPTURE_JS, krx-quant #224) is saved as a menu.
        if (auto* popup = screen.findChild<fincept::screens::KrxDrawingPopup*>(); !menu && popup && popup->isVisible())
            menu = popup;
        if (menu)
            save(menu, file.dir().filePath(QStringLiteral("%1-menu.%2").arg(file.completeBaseName(), file.suffix())));
        app.quit();
    });
    app.exec();
    return status;
}
