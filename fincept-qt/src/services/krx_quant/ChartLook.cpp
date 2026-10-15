#include "services/krx_quant/ChartLook.h"

#include <algorithm>

namespace fincept::krx_quant {

namespace {

constexpr qint64 kDaySeconds = 24 * 3600;

/// The KST date of a chart time (chart_time is KST read as UTC), as days since the epoch.
qint64 day_of(qint64 time) {
    return time >= 0 ? time / kDaySeconds : (time - kDaySeconds + 1) / kDaySeconds;
}

} // namespace

QString style_key(ChartStyle style) {
    switch (style) {
        case ChartStyle::Bar:
            return QStringLiteral("bar");
        case ChartStyle::Candle:
            return QStringLiteral("candle");
        case ChartStyle::Line:
            return QStringLiteral("line");
        case ChartStyle::Area:
            return QStringLiteral("area");
        case ChartStyle::Renko:
            return QStringLiteral("renko");
        case ChartStyle::Kagi:
            return QStringLiteral("kagi");
        case ChartStyle::PointFigure:
            return QStringLiteral("point_figure");
        case ChartStyle::LineBreak:
            return QStringLiteral("line_break");
        case ChartStyle::HeikinAshi:
            return QStringLiteral("heikin_ashi");
        case ChartStyle::Hollow:
            return QStringLiteral("hollow");
        case ChartStyle::Baseline:
            return QStringLiteral("baseline");
    }
    return QStringLiteral("candle");
}

std::optional<ChartStyle> parse_style(const QString& key) {
    for (const ChartStyle style : kChartStyles)
        if (style_key(style) == key)
            return style;
    return std::nullopt;
}

QString style_title(ChartStyle style) {
    switch (style) {
        case ChartStyle::Bar:
            return QStringLiteral("바");
        case ChartStyle::Candle:
            return QStringLiteral("캔들");
        case ChartStyle::Line:
            return QStringLiteral("라인");
        case ChartStyle::Area:
            return QStringLiteral("영역");
        case ChartStyle::Renko:
            return QStringLiteral("렌코");
        case ChartStyle::Kagi:
            return QStringLiteral("카기");
        case ChartStyle::PointFigure:
            return QStringLiteral("포인트 앤 피겨");
        case ChartStyle::LineBreak:
            return QStringLiteral("라인 브레이크");
        case ChartStyle::HeikinAshi:
            return QStringLiteral("하이킨아시");
        case ChartStyle::Hollow:
            return QStringLiteral("투명캔들");
        case ChartStyle::Baseline:
            return QStringLiteral("베이스라인");
    }
    return {};
}

bool timeless(ChartStyle style) {
    return style == ChartStyle::Renko || style == ChartStyle::Kagi || style == ChartStyle::PointFigure ||
           style == ChartStyle::LineBreak;
}

bool drawable(ChartStyle style) {
    return !timeless(style);
}

bool valid_view(int view) {
    return std::find(kMinuteViews.begin(), kMinuteViews.end(), view) != kMinuteViews.end() || view == kDayView ||
           view == kWeekView || view == kMonthView || view == kYearView;
}

QString prefs_group(const QString& symbol) {
    return QStringLiteral("krx_quant/chart/%1/").arg(symbol);
}

ChartPrefs parse_prefs(const QVariant& view, const QVariant& style) {
    ChartPrefs prefs;
    bool number = false;
    const int kept = view.toInt(&number);
    if (view.isValid() && number && valid_view(kept))
        prefs.view = kept;
    if (const auto parsed = parse_style(style.toString()); parsed && drawable(*parsed))
        prefs.style = *parsed;
    return prefs;
}

QList<ChartBar> heikin_ashi(const QList<ChartBar>& bars) {
    QList<ChartBar> out;
    out.reserve(bars.size());
    for (const auto& b : bars) {
        ChartBar h = b;
        h.close = (b.open + b.high + b.low + b.close) / 4.0;
        h.open = out.isEmpty() ? (b.open + b.close) / 2.0 : (out.last().open + out.last().close) / 2.0;
        h.high = std::max({b.high, h.open, h.close});
        h.low = std::min({b.low, h.open, h.close});
        h.rising = h.close >= h.open;
        out << h;
    }
    return out;
}

std::optional<double> baseline_base(const QList<ChartBar>& bars, int view, std::optional<qint64> first_in_view) {
    if (bars.isEmpty())
        return std::nullopt;
    if (candle_view(view)) {
        if (!first_in_view)
            return bars.first().close;
        for (const auto& b : bars)
            if (b.time >= *first_in_view)
                return b.close;
        return bars.last().close; // in view past the last bar
    }
    const qint64 latest = day_of(bars.last().time);
    for (auto it = bars.crbegin(); it != bars.crend(); ++it)
        if (day_of(it->time) < latest)
            return it->close;
    return bars.first().open;
}

QList<qint64> day_starts(const QList<ChartBar>& bars, int view) {
    QList<qint64> out;
    if (candle_view(view))
        return out;
    for (qsizetype i = 0; i < bars.size(); ++i) {
        const qint64 time = bars[i].time;
        const bool starts =
            i == 0 ? time - day_of(time) * kDaySeconds <= 9 * 3600 : day_of(time) != day_of(bars[i - 1].time);
        if (starts)
            out << time;
    }
    return out;
}

} // namespace fincept::krx_quant
