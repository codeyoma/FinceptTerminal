#include "services/krx_quant/ChartSlots.h"

#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QTimeZone>

#include <algorithm>

namespace fincept::krx_quant {

namespace {

constexpr qint64 kDaySeconds = 24 * 3600;
constexpr qint64 kOpenSeconds = 8 * 3600; ///< the trading day placed by: 08:00 to 20:00 KST (NXT), on weekdays
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

/// The last bar starting at or before ``time``; -1 when the first starts after it.
qsizetype bar_at(const QList<ChartBar>& bars, qint64 time) {
    const auto it =
        std::upper_bound(bars.begin(), bars.end(), time, [](qint64 t, const ChartBar& b) { return t < b.time; });
    return (it - bars.begin()) - 1;
}

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

std::optional<SessionClock> session_clock(const std::optional<EngineStatus>& status, const QDateTime& now) {
    if (!status || !status->session_open.isValid() || !status->session_close.isValid() || !now.isValid())
        return std::nullopt;
    return SessionClock{chart_time(now), chart_time(status->session_open), chart_time(status->session_close)};
}

std::optional<qint64> clock_slot(const std::optional<SessionClock>& clock, int minutes) {
    if (!clock || candle_view(minutes) || clock->now < clock->open || clock->now >= clock->close)
        return std::nullopt;
    const qint64 step = step_of(minutes);
    return clock->now - (clock->now % step + step) % step;
}

QList<ChartBar> fill_slots(const QList<ChartBar>& bars, int minutes, const std::optional<SessionClock>& clock) {
    if (candle_view(minutes) || bars.isEmpty())
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
    // Today while the session is open (krx-quant #238): the slots after the last bar that are over.
    if (clock_slot(clock, minutes) && day_of(bars.last().time) == day_of(clock->now))
        for (qint64 t = bars.last().time + step; t + step <= clock->now; t += step)
            out << empty_bar(t);
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
    if (!slotted.isEmpty() && slotted.last().time >= live.time) {
        // A slot there: the last bar's, or one before it (an empty bar filled up to the clock,
        // krx-quant #238). Without one (an earlier minute than shown), it is left out.
        for (qsizetype i = slotted.size() - 1; i >= 0 && slotted[i].time >= live.time; --i) {
            if (slotted[i].time != live.time)
                continue;
            if (slotted[i].empty)
                out.replaced = live.time;
            out.redraw = i != slotted.size() - 1;
            slotted[i] = live;
            break;
        }
        return out;
    }
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
    if (window.detail.isEmpty())
        return name;
    // A market halt's phase (krx-quant #625: 정지, 단일가, 재개), said as one: "시장 매매 정지 정지" reads
    // as a typo.
    if (window.kind == QLatin1String("market_halt"))
        return QStringLiteral("%1 (%2 단계)").arg(name, window.detail);
    return name + ' ' + window.detail;
}

QString special_band_label(const SpecialWindow& window) {
    // A market halt's band by its name alone (krx-quant #625): its phase moves on while it lasts, the
    // hover lines say it.
    if (window.kind == QLatin1String("market_halt"))
        return special_label(window.kind);
    return special_title(window);
}

QList<SpecialBand> special_bands(const QList<ChartBar>& slotted, const QList<SpecialWindow>& windows, int minutes,
                                 const std::optional<SessionClock>& clock) {
    QList<SpecialBand> out;
    if (candle_view(minutes) || slotted.isEmpty())
        return out;
    const qint64 first = slotted.first().time;
    for (const auto& w : windows) {
        if (!w.start.isValid() || !w.end.isValid())
            continue;
        const qint64 start = chart_time(w.start);
        qint64 end = chart_time(w.end);
        if (clock && start < clock->now && end > clock->now) // still going on: up to the clock (krx-quant #238)
            end = clock->now;
        if (end <= first || end <= start) // over before the bars read
            continue;
        const auto from = logical_at(slotted, start, minutes);
        const auto to = logical_at(slotted, end, minutes);
        if (!from || !to || *to - *from < 1e-6) // within a night between two days
            continue;
        out << SpecialBand{w.kind, *from, *to, special_band_label(w)};
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

QList<BarHover> live_hovers(const QList<ChartBar>& slotted, qint64 live, const LiveSlots& made,
                            const QList<SpecialWindow>& windows, int minutes) {
    QList<ChartBar> changed;
    for (const auto& b : slotted)
        if (made.filled.contains(b.time) || ((made.appended || made.replaced) && b.time == live))
            changed << b;
    return slot_hovers(changed, {}, windows, minutes);
}

} // namespace fincept::krx_quant
