#include "screens/krx_quant/KrxChart.h"

#include "services/krx_quant/ChartModel.h"
#include "ui/theme/Theme.h"

#include <QColor>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLibraryInfo>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#ifdef HAS_QT_WEBENGINE
#    include <QWebChannel>
#    include <QWebEngineView>
#endif

namespace fincept::screens {

namespace {

/// A color as the page takes it, with ``alpha`` (0–1).
QString css(const QColor& color, double alpha = 1.0) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(alpha);
}

QColor direction_color(krx_quant::Direction d) {
    if (d == krx_quant::Direction::Up)
        return QColor(ui::colors::POSITIVE());
    if (d == krx_quant::Direction::Down)
        return QColor(ui::colors::NEGATIVE());
    return QColor(ui::colors::TEXT_SECONDARY());
}

QString shape_of(krx_quant::Direction d) {
    if (d == krx_quant::Direction::Up)
        return QStringLiteral("arrowUp");
    if (d == krx_quant::Direction::Down)
        return QStringLiteral("arrowDown");
    return QStringLiteral("circle");
}

QString compact(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

#ifdef HAS_QT_WEBENGINE
/// Whether the WebEngine runtime shipped with the app (its helper process is there):
/// making a web view without it aborts the app. As upstream's KLineChartWidget checks, and
/// on macOS also inside QtWebEngineCore.framework, where macdeployqt puts the helper.
bool webengine_runtime() {
    if (const QString set = qEnvironmentVariable("QTWEBENGINEPROCESS_PATH"); !set.isEmpty())
        return QFileInfo::exists(set);
#    ifdef Q_OS_WIN
    const QString process = QStringLiteral("QtWebEngineProcess.exe");
#    else
    const QString process = QStringLiteral("QtWebEngineProcess");
#    endif
    const QString app = QCoreApplication::applicationDirPath();
    QStringList candidates;
    for (const QString& where : {QLibraryInfo::path(QLibraryInfo::LibraryExecutablesPath), app, app + "/bin"})
        candidates << where + "/" + process;
#    ifdef Q_OS_MACOS
    const QString helper =
        QStringLiteral("QtWebEngineCore.framework/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess");
    candidates << app + "/../Frameworks/" + helper << QLibraryInfo::path(QLibraryInfo::LibrariesPath) + "/" + helper;
#    endif
    return std::any_of(candidates.begin(), candidates.end(), [](const QString& c) { return QFileInfo::exists(c); });
}
#endif

} // namespace

void KrxChartBridge::ready() {
    emit page_ready();
}

void KrxChartBridge::clicked(const QString& bar_close) {
    emit icon_clicked(bar_close);
}

void KrxChartBridge::moved(double from) {
    emit view_moved(from);
}

void KrxChartBridge::range(double from, double to) {
    emit range_moved(from, to);
}

KrxChart::KrxChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(220);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    bridge_ = new KrxChartBridge(this);
    connect(bridge_, &KrxChartBridge::page_ready, this, [this]() {
        emit bridge_->theme_changed(theme_);
        if (!data_.isEmpty())
            emit bridge_->data_changed(data_);
        // A page (re)loaded has no boxes: all of them.
        QJsonArray all;
        for (const auto& json : std::as_const(hovers_))
            all.append(QJsonDocument::fromJson(json.toUtf8()).object());
        emit bridge_->hovers_changed(compact(QJsonObject{{"reset", true}, {"set", all}, {"drop", QJsonArray{}}}));
    });
    connect(bridge_, &KrxChartBridge::icon_clicked, this, [this](const QString& bar_close) {
        if (const QDateTime at = QDateTime::fromString(bar_close, Qt::ISODate); at.isValid())
            emit icon_clicked(at);
    });
    connect(bridge_, &KrxChartBridge::view_moved, this, &KrxChart::view_moved);
    connect(bridge_, &KrxChartBridge::range_moved, this,
            [this](double from, double to) { emit view_range(static_cast<qint64>(from), static_cast<qint64>(to)); });
    unavailable_ = new QLabel(tr("차트를 열 수 없음"));
    unavailable_->setAlignment(Qt::AlignCenter);
    unavailable_->setToolTip(tr("이 빌드에 Qt WebEngine이 없거나 그 실행 파일이 빠졌다."));
    layout->addWidget(unavailable_);
#ifdef HAS_QT_WEBENGINE
    unavailable_->setVisible(!webengine_runtime());
#endif
    restyle();
}

void KrxChart::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensure_view();
}

void KrxChart::ensure_view() {
#ifdef HAS_QT_WEBENGINE
    if (view_ || !webengine_runtime())
        return;
    view_ = new QWebEngineView(this);
    view_->setContextMenuPolicy(Qt::NoContextMenu);
    auto* channel = new QWebChannel(view_->page());
    channel->registerObject(QStringLiteral("bridge"), bridge_);
    view_->page()->setWebChannel(channel);
    view_->page()->setBackgroundColor(QColor(ui::colors::BG_BASE()));
    view_->load(QUrl(QStringLiteral("qrc:/krx/chart/index.html")));
    layout()->addWidget(view_);
#endif
}

void KrxChart::set_data(const QList<krx_quant::Bar>& bars, const QList<krx_quant::Mark>& marks,
                        const QList<krx_quant::BarHover>& hovers, const krx_quant::SignalRule& rule, int minutes) {
    QJsonArray candles;
    QJsonArray volumes;
    const QColor up(ui::colors::POSITIVE());
    const QColor down(ui::colors::NEGATIVE());
    // A candle view's bars are its candles already (krx-quant #111, #112).
    const bool dated = krx_quant::candle_view(minutes); // the axis shows dates only
    const auto shown = krx_quant::chart_bars(dated ? bars : krx_quant::group_bars(bars, minutes));
    for (const auto& b : shown) {
        candles.append(
            QJsonObject{{"time", b.time}, {"open", b.open}, {"high", b.high}, {"low", b.low}, {"close", b.close}});
        volumes.append(QJsonObject{{"time", b.time}, {"value", b.volume}, {"color", css(b.rising ? up : down, 0.5)}});
    }
    QJsonArray icons;
    // Icons, hover boxes and clicks are the 1-minute view's: a wider bar holds many bar closes.
    const bool minute = minutes == 1;
    const auto drawn = minute ? krx_quant::chart_icons(marks, rule) : QList<krx_quant::ChartIcon>{};
    for (const auto& icon : drawn) {
        // A signal is larger and never faint; a special prediction otherwise is.
        const double alpha = icon.signal ? 1.0 : icon.faint ? 0.35 : 0.8;
        icons.append(QJsonObject{{"time", icon.time},
                                 {"position", "aboveBar"},
                                 {"shape", shape_of(icon.direction)},
                                 {"color", css(direction_color(icon.direction), alpha)},
                                 {"size", icon.signal ? 2 : 1},
                                 {"text", icon.result},
                                 {"bar_close", icon.bar_close.toString(Qt::ISODate)}});
    }
    const auto [from, to] = krx_quant::fit_range(static_cast<int>(shown.size()));
    data_ = compact(QJsonObject{{"bars", candles},
                                {"volumes", volumes},
                                {"icons", icons},
                                {"fit", QJsonObject{{"from", from}, {"to", to}}},
                                {"day", dated}});
    emit bridge_->data_changed(data_);

    // The hover boxes: only those changed go to the page, and those gone are dropped.
    QHash<qint64, QString> boxes;
    if (minute) {
        for (const auto& h : hovers) {
            QJsonObject box{{"time", h.time},
                            {"lines", QJsonArray::fromStringList(h.lines)},
                            {"from", h.window_from},
                            {"to", h.window_to}};
            // The touch targets, once the label is in (krx-quant #167), each as it is.
            if (h.upper)
                box.insert("upper", *h.upper);
            if (h.lower)
                box.insert("lower", *h.lower);
            for (const auto& [key, point] :
                 {std::pair{"upper_reached", h.upper_reached}, std::pair{"lower_reached", h.lower_reached}})
                if (point)
                    box.insert(key, QJsonObject{{"time", point->time}, {"price", point->price}});
            boxes.insert(h.time, compact(box));
        }
    }
    QJsonArray set;
    QJsonArray drop;
    for (auto it = boxes.cbegin(); it != boxes.cend(); ++it)
        if (hovers_.value(it.key()) != it.value())
            set.append(QJsonDocument::fromJson(it.value().toUtf8()).object());
    for (auto it = hovers_.cbegin(); it != hovers_.cend(); ++it)
        if (!boxes.contains(it.key()))
            drop.append(it.key());
    hovers_ = boxes;
    if (!set.isEmpty() || !drop.isEmpty())
        emit bridge_->hovers_changed(compact(QJsonObject{{"reset", false}, {"set", set}, {"drop", drop}}));
}

void KrxChart::update_live(const QList<krx_quant::Bar>& bars, int minutes) {
    // A candle view's last candle is its list's own; a minute view's, of its 1-minute bars.
    const auto last =
        krx_quant::candle_view(minutes)
            ? (bars.isEmpty() ? std::nullopt : std::optional(krx_quant::chart_bars({bars.last()}).first()))
            : krx_quant::last_chart_bar(bars, minutes);
    if (!last)
        return;
    const QColor color(last->rising ? ui::colors::POSITIVE() : ui::colors::NEGATIVE());
    emit bridge_->live_changed(compact(QJsonObject{
        {"bar", QJsonObject{{"time", last->time},
                            {"open", last->open},
                            {"high", last->high},
                            {"low", last->low},
                            {"close", last->close}}},
        {"volume", QJsonObject{{"time", last->time}, {"value", last->volume}, {"color", css(color, 0.5)}}}}));
}

void KrxChart::fit() {
    emit bridge_->fit_requested();
}

void KrxChart::restyle() {
    const QFont font = ui::ThemeManager::instance().current_font();
    QStringList families; // as CSS takes a list: each quoted
    for (const QString& family : font.families().isEmpty() ? QStringList{font.family()} : font.families())
        families << QStringLiteral("\"%1\"").arg(family);
    families << QStringLiteral("monospace");
    theme_ = compact(QJsonObject{
        {"background", css(QColor(ui::colors::BG_BASE()))},
        {"text", css(QColor(ui::colors::TEXT_SECONDARY()))},
        {"grid", css(QColor(ui::colors::BORDER_DIM()))},
        {"border", css(QColor(ui::colors::BORDER_MED()))},
        {"label", css(QColor(ui::colors::BG_RAISED()))},
        // The hover box: as Fincept's tooltips (the global style's QToolTip).
        {"tip_background", css(QColor(ui::colors::BG_SURFACE()))},
        {"tip_border", css(QColor(ui::colors::BORDER_MED()))},
        {"tip_text", css(QColor(ui::colors::TEXT_PRIMARY()))},
        {"link", css(QColor(ui::colors::AMBER()))},
        {"up", css(QColor(ui::colors::POSITIVE()))},
        {"down", css(QColor(ui::colors::NEGATIVE()))},
        {"font", families.join(", ")},
        {"size", std::max(9, font.pixelSize() - 3)},
    });
    unavailable_->setStyleSheet(QString("color:%1;background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    emit bridge_->theme_changed(theme_);
}

} // namespace fincept::screens
