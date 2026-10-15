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
#include <cmath>
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

/// A value to four decimals: the page shows whole won, and the JSON stays short.
double rounded(double value) {
    return std::round(value * 1e4) / 1e4;
}

/// A point as the page takes it: [time, value], or [time] where the line breaks.
QJsonArray point_json(qint64 time, const std::optional<double>& value) {
    return value ? QJsonArray{time, rounded(*value)} : QJsonArray{time};
}

/// What the page draws of the indicators (krx-quant #222), but the volume profile (sent with the
/// view, KrxChart::profile_json).
QJsonObject studies_json(const krx_quant::StudyDrawing& d) {
    QJsonArray series;
    for (const auto& line : d.series) {
        QJsonArray points;
        for (const auto& [time, value] : line.points)
            points.append(point_json(time, value));
        series.append(QJsonObject{
            {"id", line.id}, {"color", line.color}, {"width", line.width}, {"dots", line.dots}, {"points", points}});
    }
    QJsonArray fills;
    for (const auto& f : d.fills)
        fills.append(QJsonObject{{"a", f.first}, {"b", f.second}, {"up", f.up_color}, {"down", f.down_color}});
    QJsonArray marks;
    for (const auto& m : d.marks)
        marks.append(QJsonObject{{"time", m.time}, {"above", m.above}, {"color", m.color}});
    QJsonArray legend;
    for (const auto& row : d.legend)
        legend.append(QJsonObject{{"key", krx_quant::study_key(row.study)},
                                  {"pane", row.pane},
                                  {"title", row.title},
                                  {"settings", row.settings},
                                  {"hidden", row.hidden},
                                  {"values", QJsonArray::fromStringList(row.values)},
                                  {"colors", QJsonArray::fromStringList(row.colors)},
                                  {"note", row.note},
                                  {"missing", row.missing}});
    QJsonObject out{{"series", series},
                    {"fills", fills},
                    {"marks", marks},
                    {"legend", legend},
                    {"picking", d.picking},
                    {"volume", d.volume},
                    {"volume_hidden", d.volume_hidden}};
    if (d.anchor)
        out.insert("anchor", QJsonObject{{"time", *d.anchor}, {"color", d.anchor_color}});
    return out;
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

void KrxChartBridge::anchored(double time) {
    emit anchor_moved(time);
}

void KrxChartBridge::legend(const QString& key, const QString& action) {
    emit legend_clicked(key, action);
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
    connect(bridge_, &KrxChartBridge::range_moved, this, [this](double from, double to) {
        first_in_view_ = static_cast<qint64>(from);
        last_in_view_ = static_cast<qint64>(to);
        rebase(first_in_view_); // a candle view's baseline starts at the first bar in view
        if (drawing_.profile)   // counted again over the bars now in view (krx-quant #222)
            emit bridge_->profile_changed(profile_json());
        emit view_range(static_cast<qint64>(from), static_cast<qint64>(to));
    });
    connect(bridge_, &KrxChartBridge::anchor_moved, this,
            [this](double time) { emit anchor_chosen(static_cast<qint64>(time)); });
    connect(bridge_, &KrxChartBridge::legend_clicked, this, [this](const QString& key, const QString& action) {
        const auto study = krx_quant::parse_study(key);
        if (!study)
            return;
        if (action == QLatin1String("hide"))
            emit study_hide_toggled(*study);
        else if (action == QLatin1String("settings"))
            emit study_settings_requested(*study);
        else if (action == QLatin1String("remove"))
            emit study_removed(*study);
    });
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
                        const QList<krx_quant::BarHover>& hovers, const krx_quant::SignalRule& rule, int minutes,
                        const krx_quant::ChartLook& look) {
    QJsonArray candles;
    QJsonArray volumes;
    const QColor up(ui::colors::POSITIVE());
    const QColor down(ui::colors::NEGATIVE());
    // A candle view's bars are its candles already (krx-quant #111, #112).
    const bool dated = krx_quant::candle_view(minutes); // the axis shows dates only
    const auto shown = krx_quant::chart_bars(dated ? bars : krx_quant::group_bars(bars, minutes));
    shown_ = shown;
    if (minutes != minutes_) { // another view's bars were in view
        first_in_view_.reset();
        last_in_view_.reset();
    }
    minutes_ = minutes;
    look_ = look;
    // A Heikin-Ashi chart's bars are drawn as such (krx-quant #221); the volumes stay the bars'.
    const bool heikin = look.style == krx_quant::ChartStyle::HeikinAshi;
    for (const auto& b : heikin ? krx_quant::heikin_ashi(shown) : shown)
        candles.append(
            QJsonObject{{"time", b.time}, {"open", b.open}, {"high", b.high}, {"low", b.low}, {"close", b.close}});
    for (const auto& b : shown)
        volumes.append(QJsonObject{{"time", b.time}, {"value", b.volume}, {"color", css(b.rising ? up : down, 0.5)}});
    QJsonArray bands; // the days' first bars, filled (krx-quant #221)
    if (look.day_separator)
        for (const qint64 time : krx_quant::day_starts(shown, minutes))
            bands.append(time);
    base_ = look.style == krx_quant::ChartStyle::Baseline ? krx_quant::baseline_base(shown, minutes, first_in_view_)
                                                          : std::nullopt;
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
    QJsonObject data{{"bars", candles}, {"volumes", volumes},
                     {"icons", icons},  {"fit", QJsonObject{{"from", from}, {"to", to}}},
                     {"day", dated},    {"style", krx_quant::style_key(look.style)},
                     {"bands", bands}};
    if (base_)
        data.insert("base", *base_);
    if (!shown.isEmpty()) // the last bar's own close, for the price line of a Heikin-Ashi chart
        data.insert("last", QJsonObject{{"close", shown.last().close}, {"rising", shown.last().rising}});
    // The indicators, over the bars as they are (krx-quant #222), and the volume profile of those in view.
    drawing_ = krx_quant::draw_studies(shown, look.studies, minutes, look.anchor, look.picking, look.first_day_whole);
    data.insert("studies", studies_json(drawing_));
    data.insert("profile", QJsonDocument::fromJson(profile_json().toUtf8()).object());
    data_ = compact(data);
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
    const bool dated = krx_quant::candle_view(minutes);
    const auto last =
        dated ? (bars.isEmpty() ? std::nullopt : std::optional(krx_quant::chart_bars({bars.last()}).first()))
              : krx_quant::last_chart_bar(bars, minutes);
    if (!last)
        return;
    // A Heikin-Ashi bar follows from all those before it (krx-quant #221), and the indicators'
    // values from all of them (krx-quant #222).
    krx_quant::ChartBar drawn = *last;
    const bool heikin = look_.style == krx_quant::ChartStyle::HeikinAshi;
    const bool studies = !drawing_.series.isEmpty() || !drawing_.marks.isEmpty() || drawing_.profile;
    QList<krx_quant::ChartBar> all;
    if (heikin || studies)
        all = krx_quant::chart_bars(dated ? bars : krx_quant::group_bars(bars, minutes));
    if (heikin && !all.isEmpty())
        drawn = krx_quant::heikin_ashi(all).last();
    const QColor color(last->rising ? ui::colors::POSITIVE() : ui::colors::NEGATIVE());
    QJsonObject live{{"bar", QJsonObject{{"time", drawn.time},
                                         {"open", drawn.open},
                                         {"high", drawn.high},
                                         {"low", drawn.low},
                                         {"close", drawn.close}}},
                     {"volume", QJsonObject{{"time", last->time}, {"value", last->volume}, {"color", css(color, 0.5)}}},
                     {"last", QJsonObject{{"close", last->close}, {"rising", last->rising}}}};
    if (studies && !all.isEmpty()) {
        // Each line's point the bar being made moves: its own, a leading span's displacement on,
        // the lagging span's displacement back. The page moves that one (it is each line's last).
        shown_ = all;
        drawing_ =
            krx_quant::draw_studies(all, look_.studies, minutes_, look_.anchor, look_.picking, look_.first_day_whole);
        const qsizetype n = all.size();
        QJsonArray points;
        for (const auto& line : drawing_.series) {
            const qsizetype slot = n - 1 + line.live;
            if (slot < 0)
                continue;
            const qint64 time = slot < n
                                    ? all[slot].time
                                    : krx_quant::future_times(all, minutes_, static_cast<int>(slot - n + 1)).last();
            std::optional<double> value;
            for (auto it = line.points.crbegin(); it != line.points.crend() && it->first >= time; ++it)
                if (it->first == time)
                    value = it->second;
            points.append(QJsonArray{line.id, point_json(time, value)});
        }
        live.insert("studies", points);
        if (!drawing_.marks.isEmpty() || look_.studies.on(krx_quant::Study::Fractal))
            live.insert("marks", studies_json(drawing_).value("marks"));
        if (drawing_.profile)
            live.insert("profile", QJsonDocument::fromJson(profile_json().toUtf8()).object());
    }
    emit bridge_->live_changed(compact(live));
}

QString KrxChart::profile_json() const {
    if (!drawing_.profile || shown_.isEmpty())
        return QStringLiteral("{}");
    // The bars in view as the page last said; before it says, the fitted last ones.
    const qint64 from =
        first_in_view_.value_or(shown_[std::max<qsizetype>(0, shown_.size() - krx_quant::kFitBars)].time);
    const qint64 to = last_in_view_.value_or(shown_.last().time);
    const auto profile = krx_quant::volume_profile(shown_, from, to, drawing_.profile_rows);
    QJsonArray rows;
    const double height = profile.volumes.isEmpty() ? 0 : (profile.high - profile.low) / profile.volumes.size();
    for (qsizetype r = 0; r < profile.volumes.size(); ++r)
        rows.append(QJsonObject{{"low", rounded(profile.low + r * height)},
                                {"high", rounded(profile.low + (r + 1) * height)},
                                {"volume", rounded(profile.volumes[r])}});
    return compact(QJsonObject{{"color", drawing_.profile_color}, {"rows", rows}});
}

void KrxChart::rebase(std::optional<qint64> first_in_view) {
    if (look_.style != krx_quant::ChartStyle::Baseline)
        return;
    const auto base = krx_quant::baseline_base(shown_, minutes_, first_in_view);
    if (!base || base == base_)
        return;
    base_ = base;
    emit bridge_->base_changed(compact(QJsonObject{{"base", *base}}));
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
        {"band", css(QColor(ui::colors::TEXT_DIM()), 0.22)}, // the day separators (krx-quant #221)
        {"up", css(QColor(ui::colors::POSITIVE()))},
        {"down", css(QColor(ui::colors::NEGATIVE()))},
        {"font", families.join(", ")},
        {"size", std::max(9, font.pixelSize() - 3)},
    });
    unavailable_->setStyleSheet(QString("color:%1;background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    emit bridge_->theme_changed(theme_);
}

} // namespace fincept::screens
