#include "services/krx_quant/ChartSlots.h"

#include "services/krx_quant/ChartDrawings.h"

#include <QDateTime>
#include <QMap>
#include <QTimeZone>

#include <algorithm>

namespace fincept::krx_quant {

namespace {

constexpr qint64 kDaySeconds = 24 * 3600;

/// The KST day of a chart time (KST read as UTC), as days since the epoch.
qint64 day_of(qint64 time) {
    return time >= 0 ? time / kDaySeconds : -((-time + kDaySeconds - 1) / kDaySeconds);
}

/// A slot of the view, in seconds.
qint64 step_of(int minutes) {
    return static_cast<qint64>(std::max(1, minutes)) * 60;
}

ChartBar empty_bar(qint64 time) {
    ChartBar b;
    b.time = time;
    b.empty = true;
    return b;
}

/// A chart time's clock: "13:10", or "13:10:05" with ``seconds``.
QString clock(qint64 time, bool seconds) {
    return QDateTime::fromSecsSinceEpoch(time, QTimeZone::UTC).toString(seconds ? "HH:mm:ss" : "HH:mm");
}

} // namespace

QList<ChartBar> fill_slots(const QList<ChartBar>& bars, int minutes) {
    if (candle_view(minutes) || bars.size() < 2)
        return bars;
    const qint64 step = step_of(minutes);
    QList<ChartBar> out;
    out.reserve(bars.size());
    for (qsizetype i = 0; i < bars.size(); ++i) {
        if (i > 0 && day_of(bars[i - 1].time) == day_of(bars[i].time))
            for (qint64 t = bars[i - 1].time + step; t < bars[i].time; t += step)
                out << empty_bar(t);
        out << bars[i];
    }
    return out;
}

QList<ChartBar> real_bars(const QList<ChartBar>& slotted) {
    QList<ChartBar> out;
    out.reserve(slotted.size());
    for (const auto& b : slotted)
        if (!b.empty)
            out << b;
    return out;
}

LiveSlots append_live_slots(QList<ChartBar>& slotted, const ChartBar& live, int minutes) {
    LiveSlots out;
    if (!slotted.isEmpty() && slotted.last().time == live.time) {
        slotted.last() = live;
        return out;
    }
    if (!slotted.isEmpty() && slotted.last().time > live.time)
        return out;
    if (!slotted.isEmpty() && !candle_view(minutes) && day_of(slotted.last().time) == day_of(live.time)) {
        const qint64 step = step_of(minutes);
        for (qint64 t = slotted.last().time + step; t < live.time; t += step) {
            slotted << empty_bar(t);
            out.filled << t;
        }
    }
    slotted << live;
    out.appended = true;
    return out;
}

QString special_title(const SpecialWindow& window) {
    // A VI's kind as KIS reports it (krx-quant #236): static, dynamic or both.
    if (window.kind == QLatin1String("vi")) {
        static const QMap<QString, QString> vi = {
            {"static", QStringLiteral("정적 VI")},
            {"dynamic", QStringLiteral("동적 VI")},
            {"both", QStringLiteral("정적·동적 VI")},
        };
        if (const auto it = vi.constFind(window.detail); it != vi.cend())
            return *it;
    }
    const QString name = special_label(window.kind);
    return window.detail.isEmpty() ? name : name + ' ' + window.detail;
}

QList<SpecialBand> special_bands(const QList<ChartBar>& slotted, const QList<SpecialWindow>& windows, int minutes) {
    QList<SpecialBand> out;
    if (candle_view(minutes) || slotted.isEmpty())
        return out;
    const qint64 first = slotted.first().time;
    for (const auto& w : windows) {
        if (!w.start.isValid() || !w.end.isValid())
            continue;
        const qint64 start = chart_time(w.start);
        const qint64 end = chart_time(w.end);
        if (end <= first || end <= start) // over before the bars read
            continue;
        const auto from = logical_at(slotted, start, minutes);
        const auto to = logical_at(slotted, end, minutes);
        if (!from || !to || *to - *from < 1e-6) // within a night between two days
            continue;
        out << SpecialBand{w.kind, *from, *to, special_title(w)};
    }
    return out;
}

QStringList special_lines(const QList<SpecialWindow>& windows, qint64 from, qint64 to) {
    QStringList out;
    for (const auto& w : windows) {
        if (!w.start.isValid() || !w.end.isValid())
            continue;
        const qint64 start = chart_time(w.start);
        const qint64 end = chart_time(w.end);
        if (start >= to || end <= from)
            continue;
        const bool seconds = start % 60 != 0 || end % 60 != 0;
        out << QStringLiteral("%1 %2–%3").arg(special_title(w), clock(start, seconds), clock(end, seconds));
    }
    return out;
}

QString empty_line(qint64 time, int minutes) {
    // Its span, not its start alone: a real bar's box names it by its close ("09:37 봉", krx-quant #159).
    return QStringLiteral("%1–%2 · 체결 없음").arg(clock(time, false), clock(time + step_of(minutes), false));
}

QList<BarHover> slot_hovers(const QList<ChartBar>& slotted, const QList<BarHover>& hovers,
                            const QList<SpecialWindow>& windows, int minutes) {
    QList<BarHover> out;
    if (candle_view(minutes))
        return out;
    QMap<qint64, BarHover> boxes; // the real bars', by time
    for (const auto& h : hovers)
        boxes.insert(h.time, h);
    const qint64 step = step_of(minutes);
    for (const auto& b : slotted) {
        const QStringList lines = special_lines(windows, b.time, b.time + step);
        const auto it = boxes.find(b.time);
        if (b.empty) {
            BarHover box;
            box.time = b.time;
            box.lines = QStringList{empty_line(b.time, minutes)} + lines;
            out << box;
            if (it != boxes.end())
                boxes.erase(it);
            continue;
        }
        if (it != boxes.end()) {
            BarHover box = *it;
            box.lines += lines;
            out << box;
            boxes.erase(it);
        } else if (!lines.isEmpty()) {
            BarHover box;
            box.time = b.time;
            box.lines = lines;
            out << box;
        }
    }
    for (const auto& h : std::as_const(boxes)) // not on the bars drawn: as they came
        out << h;
    std::stable_sort(out.begin(), out.end(), [](const BarHover& a, const BarHover& b) { return a.time < b.time; });
    return out;
}

} // namespace fincept::krx_quant
