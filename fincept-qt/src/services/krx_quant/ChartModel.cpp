#include "services/krx_quant/ChartModel.h"

#include <algorithm>

namespace fincept::krx_quant {

namespace {

constexpr qint64 kKstSeconds = 9 * 3600;

QString arrow_of(Direction d) {
    switch (d) {
        case Direction::Up:
            return QStringLiteral("▲");
        case Direction::Down:
            return QStringLiteral("▼");
        default:
            return QStringLiteral("–");
    }
}

QString result_of(Mark::Result r) {
    switch (r) {
        case Mark::Result::Hit:
            return QStringLiteral("✓");
        case Mark::Result::Miss:
            return QStringLiteral("✗");
        default:
            return QStringLiteral("?");
    }
}

} // namespace

qint64 chart_time(const QDateTime& t) {
    return t.toSecsSinceEpoch() + kKstSeconds;
}

QList<ChartBar> chart_bars(const QList<Bar>& bars) {
    QList<ChartBar> out;
    out.reserve(bars.size());
    for (const auto& b : bars)
        out << ChartBar{chart_time(b.start), b.open, b.high, b.low, b.close, b.volume, b.close >= b.open};
    return out;
}

QList<ChartIcon> chart_icons(const QList<Mark>& marks) {
    QList<ChartIcon> out;
    for (const auto& m : marks) {
        if (m.predicted == Direction::None)
            continue;
        // The bar that closed at the bar close started a minute before.
        out << ChartIcon{chart_time(m.bar_close.addSecs(-60)),
                         m.bar_close,
                         m.predicted,
                         arrow_of(m.predicted),
                         result_of(m.result),
                         m.special};
    }
    std::sort(out.begin(), out.end(), [](const ChartIcon& a, const ChartIcon& b) { return a.time < b.time; });
    return out;
}

std::pair<double, double> fit_range(int bars) {
    const int last = std::max(0, bars - 1);
    return {static_cast<double>(std::max(0, bars - kFitBars)), last + kRightOffset};
}

} // namespace fincept::krx_quant
