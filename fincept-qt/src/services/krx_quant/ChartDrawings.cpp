#include "services/krx_quant/ChartDrawings.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::krx_quant {

namespace {

constexpr qint64 kDaySeconds = 24 * 3600;
constexpr qint64 kKstSeconds = 9 * 3600;
/// The trading day the drawings measure by: 08:00 to 20:00 KST (NXT), on weekdays.
constexpr qint64 kOpenSeconds = 8 * 3600;
constexpr qint64 kSessionSeconds = 12 * 3600;
constexpr qint64 kWeekSessionSeconds = 5 * kSessionSeconds;
/// 1970-01-05, a Monday, as days since the epoch: the trading time's 0.
constexpr qint64 kMondayDay = 4;

qint64 floor_div(qint64 a, qint64 b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/// The trading time of the moment ``t`` (chart_time): the seconds within 08:00–20:00 on weekdays
/// since Monday 1970-01-05. Flat over nights and weekends.
qint64 trading(qint64 t) {
    const qint64 day = floor_div(t, kDaySeconds);
    const qint64 into = t - day * kDaySeconds;
    const qint64 week = floor_div(day - kMondayDay, 7);
    const qint64 weekday = day - kMondayDay - week * 7; // 0 Monday … 6 Sunday
    qint64 out = week * kWeekSessionSeconds + std::min<qint64>(weekday, 5) * kSessionSeconds;
    if (weekday < 5)
        out += std::clamp<qint64>(into - kOpenSeconds, 0, kSessionSeconds);
    return out;
}

/// The moment of the trading time ``v``: where a night or a weekend leaves several, the latest
/// (the next trading start). To the second.
qint64 from_trading(double v) {
    const auto week = static_cast<qint64>(std::floor(v / kWeekSessionSeconds));
    const double rest = v - static_cast<double>(week) * kWeekSessionSeconds; // 0 to a week's
    const qint64 weekday = std::clamp<qint64>(static_cast<qint64>(std::floor(rest / kSessionSeconds)), 0, 4);
    const double into = rest - static_cast<double>(weekday) * kSessionSeconds;
    return (kMondayDay + week * 7 + weekday) * kDaySeconds + kOpenSeconds + std::llround(into);
}

QDate date_of(qint64 t) {
    return QDate(1970, 1, 1).addDays(floor_div(t, kDaySeconds));
}

qint64 time_of(const QDate& date) {
    return QDate(1970, 1, 1).daysTo(date) * kDaySeconds;
}

/// The view's slot holding ``t`` starts: its N minutes on the clock, its day, its week's Monday,
/// its month's 1st or its year's 1 January.
qint64 slot_start(qint64 t, int view) {
    if (!candle_view(view)) {
        const qint64 length = std::max(1, view) * 60;
        return floor_div(t, length) * length;
    }
    return time_of(period_start(date_of(t), view));
}

/// The start of the slot after the one starting at ``start``.
qint64 slot_end(qint64 start, int view) {
    if (!candle_view(view))
        return start + std::max(1, view) * 60;
    const QDate date = date_of(start);
    switch (view) {
        case kWeekView:
            return time_of(date.addDays(7));
        case kMonthView:
            return time_of(QDate(date.year(), date.month(), 1).addMonths(1));
        case kYearView:
            return time_of(QDate(date.year() + 1, 1, 1));
        default:
            return start + kDaySeconds;
    }
}

/// How many of the view's slots there are from a fixed start to the moment ``t``, counted in
/// trading time: a minute view's N trading minutes, a trading day, a week of five, and a month or
/// a year each one, as far into it as its trading time goes.
double slot_count(qint64 t, int view) {
    if (!candle_view(view))
        return static_cast<double>(trading(t)) / (std::max(1, view) * 60);
    if (view == kDayView)
        return static_cast<double>(trading(t)) / kSessionSeconds;
    if (view == kWeekView)
        return static_cast<double>(trading(t)) / kWeekSessionSeconds;
    const QDate date = date_of(t);
    const qint64 start = slot_start(t, view);
    const qint64 end = slot_end(start, view);
    const double whole = view == kMonthView ? date.year() * 12.0 + (date.month() - 1) : date.year();
    const qint64 span = trading(end) - trading(start);
    return whole + (span > 0 ? static_cast<double>(trading(t) - trading(start)) / span : 0.0);
}

/// The moment ``v`` slots in (as slots), the latest where a night or a weekend leaves several.
qint64 from_slot_count(double v, int view) {
    if (!candle_view(view))
        return from_trading(v * std::max(1, view) * 60);
    if (view == kDayView)
        return from_trading(v * kSessionSeconds);
    if (view == kWeekView)
        return from_trading(v * kWeekSessionSeconds);
    const auto whole = static_cast<qint64>(std::floor(v));
    const double into = v - static_cast<double>(whole);
    const QDate first = view == kMonthView ? QDate(static_cast<int>(floor_div(whole, 12)),
                                                   static_cast<int>(whole - floor_div(whole, 12) * 12) + 1, 1)
                                           : QDate(static_cast<int>(whole), 1, 1);
    const qint64 start = time_of(first);
    const qint64 a = trading(start);
    const qint64 b = trading(slot_end(start, view));
    return from_trading(static_cast<double>(a) + into * static_cast<double>(b - a));
}

/// The last bar starting at or before ``time``; -1 when the first starts after it.
qsizetype bar_at(const QList<ChartBar>& bars, qint64 time) {
    const auto it =
        std::upper_bound(bars.begin(), bars.end(), time, [](qint64 t, const ChartBar& b) { return t < b.time; });
    return (it - bars.begin()) - 1;
}

bool valid_color(const QString& color) {
    static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    return hex.match(color).hasMatch();
}

QString iso_kst(qint64 time) {
    return QDateTime::fromSecsSinceEpoch(time - kKstSeconds, QTimeZone::fromSecondsAheadOfUtc(kKstSeconds))
        .toString(Qt::ISODate);
}

} // namespace

QString tool_key(DrawingTool tool) {
    switch (tool) {
        case DrawingTool::TrendLine:
            return QStringLiteral("trend_line");
        case DrawingTool::Ray:
            return QStringLiteral("ray");
        case DrawingTool::Horizontal:
            return QStringLiteral("horizontal");
        case DrawingTool::Vertical:
            return QStringLiteral("vertical");
        case DrawingTool::Cross:
            return QStringLiteral("cross");
        case DrawingTool::Rectangle:
            return QStringLiteral("rectangle");
        case DrawingTool::Ellipse:
            return QStringLiteral("ellipse");
        case DrawingTool::Pitchfork:
            return QStringLiteral("pitchfork");
        case DrawingTool::FibRetracement:
            return QStringLiteral("fib_retracement");
        case DrawingTool::FibChannel:
            return QStringLiteral("fib_channel");
        case DrawingTool::FibFan:
            return QStringLiteral("fib_fan");
        case DrawingTool::Text:
            return QStringLiteral("text");
    }
    return QStringLiteral("trend_line");
}

std::optional<DrawingTool> parse_tool(const QString& key) {
    for (const DrawingTool tool : kDrawingTools)
        if (tool_key(tool) == key)
            return tool;
    return std::nullopt;
}

QString tool_title(DrawingTool tool) {
    switch (tool) {
        case DrawingTool::TrendLine:
            return QStringLiteral("추세선");
        case DrawingTool::Ray:
            return QStringLiteral("반직선");
        case DrawingTool::Horizontal:
            return QStringLiteral("수평선");
        case DrawingTool::Vertical:
            return QStringLiteral("수직선");
        case DrawingTool::Cross:
            return QStringLiteral("크로스");
        case DrawingTool::Rectangle:
            return QStringLiteral("사각형");
        case DrawingTool::Ellipse:
            return QStringLiteral("타원");
        case DrawingTool::Pitchfork:
            return QStringLiteral("피치포크");
        case DrawingTool::FibRetracement:
            return QStringLiteral("피보나치 되돌림");
        case DrawingTool::FibChannel:
            return QStringLiteral("피보나치 채널");
        case DrawingTool::FibFan:
            return QStringLiteral("피보나치 속도 저항 팬");
        case DrawingTool::Text:
            return QStringLiteral("텍스트");
    }
    return {};
}

int tool_points(DrawingTool tool) {
    switch (tool) {
        case DrawingTool::Horizontal:
        case DrawingTool::Vertical:
        case DrawingTool::Cross:
        case DrawingTool::Text:
            return 1;
        case DrawingTool::Pitchfork:
        case DrawingTool::FibChannel:
            return 3;
        default:
            return 2;
    }
}

QString line_style_key(LineStyle style) {
    switch (style) {
        case LineStyle::Dotted:
            return QStringLiteral("dotted");
        case LineStyle::Dashed:
            return QStringLiteral("dashed");
        case LineStyle::Solid:
            break;
    }
    return QStringLiteral("solid");
}

std::optional<LineStyle> parse_line_style(const QString& key) {
    for (const LineStyle style : kLineStyles)
        if (line_style_key(style) == key)
            return style;
    return std::nullopt;
}

QString line_style_title(LineStyle style) {
    switch (style) {
        case LineStyle::Dotted:
            return QStringLiteral("점선");
        case LineStyle::Dashed:
            return QStringLiteral("파선");
        case LineStyle::Solid:
            break;
    }
    return QStringLiteral("실선");
}

Drawing new_drawing(DrawingTool tool, const QList<DrawingPoint>& points) {
    Drawing d;
    d.tool = tool;
    d.points = points;
    const bool fibonacci =
        tool == DrawingTool::FibRetracement || tool == DrawingTool::FibChannel || tool == DrawingTool::FibFan;
    d.width = fibonacci ? 1 : 2;
    return d;
}

int next_drawing_id(const QList<Drawing>& drawings) {
    int most = 0;
    for (const auto& d : drawings)
        most = std::max(most, d.id);
    return most + 1;
}

QString drawings_value(const QList<Drawing>& drawings) {
    QJsonArray all;
    for (const auto& d : drawings) {
        QJsonArray points;
        for (const auto& p : d.points)
            points.append(QJsonObject{{"time", iso_kst(p.time)}, {"price", p.price}});
        QJsonObject one{{"tool", tool_key(d.tool)},
                        {"points", points},
                        {"color", d.color},
                        {"width", d.width},
                        {"style", line_style_key(d.style)}};
        if (d.tool == DrawingTool::Text)
            one.insert("text", d.text);
        all.append(one);
    }
    return QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact));
}

QList<Drawing> parse_drawings(const QVariant& kept) {
    QList<Drawing> out;
    if (!kept.isValid())
        return out;
    const QJsonDocument doc = QJsonDocument::fromJson(kept.toString().toUtf8());
    if (!doc.isArray())
        return out;
    for (const QJsonValue& value : doc.array()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const auto tool = parse_tool(object.value("tool").toString());
        if (!tool)
            continue;
        const QJsonArray points = object.value("points").toArray();
        if (points.size() != tool_points(*tool))
            continue;
        QList<DrawingPoint> placed;
        for (const QJsonValue& p : points) {
            const QJsonObject point = p.toObject();
            const QDateTime at = QDateTime::fromString(point.value("time").toString(), Qt::ISODate);
            const QJsonValue price = point.value("price");
            if (!at.isValid() || !price.isDouble() || !std::isfinite(price.toDouble()))
                break;
            placed << DrawingPoint{chart_time(at), price.toDouble()};
        }
        if (placed.size() != points.size())
            continue;
        Drawing d = new_drawing(*tool, placed);
        if (const QString color = object.value("color").toString(); valid_color(color))
            d.color = color;
        const QJsonValue width = object.value("width");
        if (width.isDouble() && width.toDouble() == std::floor(width.toDouble()) &&
            width.toDouble() >= kMinDrawingWidth && width.toDouble() <= kMaxDrawingWidth)
            d.width = width.toInt();
        if (const auto style = parse_line_style(object.value("style").toString()))
            d.style = *style;
        if (*tool == DrawingTool::Text) {
            d.text = object.value("text").toString().left(kMaxDrawingText);
            if (d.text.trimmed().isEmpty())
                continue;
        }
        d.id = static_cast<int>(out.size()) + 1;
        out << d;
    }
    return out;
}

std::optional<double> logical_at(const QList<ChartBar>& bars, qint64 time, int view) {
    if (bars.isEmpty())
        return std::nullopt;
    const qsizetype n = bars.size();
    if (time < bars.first().time) // before the bars read: in trading slots back from the first
        return -0.5 - (slot_count(bars.first().time, view) - slot_count(time, view));
    const qsizetype i = bar_at(bars, time);
    const qint64 start = bars[i].time;
    const qint64 end = slot_end(slot_start(start, view), view);
    if (time >= end) {
        if (i < n - 1)
            return static_cast<double>(i) + 0.5; // between two bars: at their edge
        return static_cast<double>(n) - 0.5 + (slot_count(time, view) - slot_count(end, view)); // after the last
    }
    const double span = slot_count(end, view) - slot_count(start, view);
    // A bar outside the trading time (none is, as read) goes by the clock.
    const double into = span > 1e-9 ? (slot_count(time, view) - slot_count(start, view)) / span
                                    : static_cast<double>(time - start) / static_cast<double>(end - start);
    return static_cast<double>(i) - 0.5 + std::clamp(into, 0.0, 1.0);
}

std::optional<qint64> time_at(const QList<ChartBar>& bars, double logical, int view) {
    if (bars.isEmpty() || !std::isfinite(logical))
        return std::nullopt;
    const qsizetype n = bars.size();
    if (logical < -0.5)
        return from_slot_count(slot_count(bars.first().time, view) - (-0.5 - logical), view);
    if (logical >= static_cast<double>(n) - 0.5) {
        const qint64 end = slot_end(slot_start(bars.last().time, view), view);
        return from_slot_count(slot_count(end, view) + (logical - (static_cast<double>(n) - 0.5)), view);
    }
    const qsizetype i = std::clamp<qsizetype>(static_cast<qsizetype>(std::floor(logical + 0.5)), 0, n - 1);
    const double into = logical - (static_cast<double>(i) - 0.5);
    const qint64 start = bars[i].time;
    const qint64 end = slot_end(slot_start(start, view), view);
    const double from = slot_count(start, view);
    const double span = slot_count(end, view) - from;
    const qint64 at = span > 1e-9 ? from_slot_count(from + into * span, view)
                                  : start + std::llround(into * static_cast<double>(end - start));
    return std::clamp<qint64>(at, start, end - 1);
}

bool append_live(QList<ChartBar>& bars, const ChartBar& live) {
    if (!bars.isEmpty() && bars.last().time == live.time) {
        bars.last() = live;
        return false;
    }
    if (!bars.isEmpty() && bars.last().time > live.time)
        return false;
    bars.append(live);
    return true;
}

ChartSpot magnet_spot(const QList<ChartBar>& bars, ChartSpot spot) {
    const auto n = static_cast<double>(bars.size());
    if (bars.isEmpty() || !std::isfinite(spot.logical) || spot.logical < -0.5 || spot.logical >= n - 0.5)
        return spot;
    const auto i = static_cast<qsizetype>(std::floor(spot.logical + 0.5));
    const ChartBar& b = bars[std::clamp<qsizetype>(i, 0, bars.size() - 1)];
    double best = b.open;
    for (const double v : {b.high, b.low, b.close})
        if (std::abs(v - spot.price) < std::abs(best - spot.price))
            best = v;
    return {static_cast<double>(i), best};
}

} // namespace fincept::krx_quant
