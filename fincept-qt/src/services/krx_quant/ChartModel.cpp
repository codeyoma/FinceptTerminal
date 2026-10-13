#include "services/krx_quant/ChartModel.h"

#include <QLocale>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

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

namespace {

QString percent(const std::optional<double>& p) {
    return p ? QString::number(std::lround(*p * 100)) + "%" : QStringLiteral("—");
}

QString blocked_label(const QString& why) {
    if (why == "special")
        return QStringLiteral("특수 상황");
    if (why == "no_reference")
        return QStringLiteral("기준가 없음");
    if (why == "expired")
        return QStringLiteral("목표 시각 지남");
    return why;
}

QString signal_line(const Mark& m, const SignalRule& rule) {
    const Prediction& p = m.prediction;
    if (!p.blocked.isEmpty())
        return QStringLiteral("신호 아님 (엔진이 막음: %1)").arg(blocked_label(p.blocked));
    if (!p.votes)
        return QStringLiteral("신호 알 수 없음");
    const auto signal = judge(p, *p.votes, rule);
    if (!signal)
        return QStringLiteral("신호 아님");
    return QStringLiteral("%1 신호 (%2 ≥ %3, 같은 방향 %4 ≥ %5)")
        .arg(direction_label(signal->direction), percent(signal->probability), percent(rule.min_probability))
        .arg(signal->agree)
        .arg(rule.min_agree);
}

QString result_line(const Mark& m, const QString& basis) {
    if (m.result == Mark::Result::Pending)
        return QStringLiteral("채점 전");
    if (m.result == Mark::Result::Unscorable)
        return m.unscorable.isEmpty() ? QStringLiteral("채점 불가")
                                      : QStringLiteral("채점 불가 (%1)").arg(m.unscorable);
    const auto& o = m.prediction.outcome;
    const std::optional<double> start = !o ? std::nullopt : basis == "a" ? o->mid_close : o->mid_display;
    const QString verdict = m.result == Mark::Result::Hit ? QStringLiteral("✓ 맞음") : QStringLiteral("✗ 틀림");
    if (!start || !o->mid_target)
        return verdict;
    const double move = *o->mid_target - *start;
    const QString won = QLocale(QLocale::Korean).toString(std::abs(move), 'f', 0);
    return QStringLiteral("실제 %1%2원 · %3").arg(move > 0 ? "+" : move < 0 ? "-" : "±", won, verdict);
}

} // namespace

/// When a prediction is scored: its own target time, or four minutes on for one without.
static QDateTime target_of(const Mark& m) {
    return m.prediction.target_at.isValid() ? m.prediction.target_at : m.bar_close.addSecs(240);
}

QStringList hover_lines(const Mark& m, const QString& basis, const SignalRule& rule) {
    const Prediction& p = m.prediction;
    const QString members = p.votes
                                ? QStringLiteral("같은 방향 %1/%2").arg(p.votes->of(m.predicted)).arg(p.votes->total())
                                : QStringLiteral("같은 방향 모델 수 알 수 없음");
    return {QStringLiteral("%1 봉 → %2 목표").arg(kst_text(m.bar_close, "HH:mm"), kst_text(target_of(m), "HH:mm")),
            QStringLiteral("합의 %1 · 상승 %2 · 보합 %3 · 하락 %4")
                .arg(direction_label(m.predicted), percent(p.p_up), percent(p.p_flat), percent(p.p_down)),
            members, signal_line(m, rule), result_line(m, basis)};
}

QList<ChartIcon> chart_icons(const QList<Mark>& marks, const QList<Bar>& bars, const QString& basis,
                             const SignalRule& rule) {
    QSet<qint64> drawn;
    for (const auto& b : bars)
        drawn.insert(chart_time(b.start));
    const qint64 last = bars.isEmpty() ? 0 : chart_time(bars.last().start);
    QList<ChartIcon> out;
    for (const auto& m : marks) {
        if (m.predicted == Direction::None)
            continue;
        ChartIcon icon;
        // The bar that closed at the bar close started a minute before; the one it aims at
        // closes four minutes later.
        icon.time = chart_time(m.bar_close.addSecs(-60));
        icon.bar_close = m.bar_close;
        icon.direction = m.predicted;
        icon.arrow = arrow_of(m.predicted);
        icon.result = result_of(m.result);
        icon.faint = m.special;
        icon.hover = hover_lines(m, basis, rule);
        icon.target = chart_time(target_of(m).addSecs(-60)); // the bar closing then
        icon.target_shown = drawn.contains(icon.target);
        icon.target_after_last =
            icon.target_shown || bars.isEmpty() ? 0 : static_cast<int>(std::max<qint64>(0, (icon.target - last) / 60));
        const Prediction& p = m.prediction;
        icon.signal = p.votes && judge(p, *p.votes, rule).has_value();
        out << icon;
    }
    std::sort(out.begin(), out.end(), [](const ChartIcon& a, const ChartIcon& b) { return a.time < b.time; });
    return out;
}

QList<CloseRow> close_rows(const QList<Prediction>& predictions, const QMap<QString, QString>& statuses,
                           const QString& basis) {
    std::optional<QStringList> pooled;
    for (const auto& p : predictions)
        if (p.algorithm == "consensus-final")
            pooled = p.pooled;
    auto rank = [](const Prediction& p) {
        if (p.algorithm == "consensus-fast")
            return 0;
        if (p.algorithm == "consensus-final")
            return 1;
        return is_baseline(p.algorithm) ? 3 : 2;
    };
    QList<Prediction> sorted = predictions;
    std::stable_sort(sorted.begin(), sorted.end(), [&rank](const Prediction& a, const Prediction& b) {
        return rank(a) != rank(b) ? rank(a) < rank(b) : a.algorithm < b.algorithm;
    });
    QList<CloseRow> out;
    for (const auto& p : sorted) {
        CloseRow row;
        row.algorithm = p.algorithm;
        row.name = predictor_label(p.algorithm);
        row.consensus = is_consensus(p.algorithm);
        row.baseline = is_baseline(p.algorithm);
        if (!row.consensus && !row.baseline) {
            row.status = statuses.contains(p.algorithm) ? standing_label(statuses.value(p.algorithm)) : QString();
            row.in_consensus = !pooled                         ? QStringLiteral("알 수 없음")
                               : pooled->contains(p.algorithm) ? QStringLiteral("포함")
                                                               : QStringLiteral("제외");
        }
        row.p_up = p.p_up;
        row.p_flat = p.p_flat;
        row.p_down = p.p_down;
        const Direction leaned = p.status == "ok" ? most_likely(p) : Direction::None;
        row.direction = p.status == "ok" ? direction_label(leaned) : status_label(p.status);
        if (p.status != "ok")
            row.result = QString();
        else if (!p.outcome)
            row.result = QStringLiteral("채점 전");
        else if (const Direction happened = basis == "a" ? p.outcome->class_a : p.outcome->class_b;
                 happened == Direction::None)
            row.result = QStringLiteral("채점 불가");
        else
            row.result = happened == leaned ? QStringLiteral("✓ 맞음") : QStringLiteral("✗ 틀림");
        out << row;
    }
    return out;
}

std::pair<double, double> fit_range(int bars) {
    const int last = std::max(0, bars - 1);
    return {static_cast<double>(std::max(0, bars - kFitBars)), last + kRightOffset};
}

std::optional<OlderRead> older_read(const QDateTime& first, double view_from, int minutes) {
    const int limit = std::min(std::max(kOlderBars, kOlderViewBars * minutes), kMaxOlderBars);
    if (!first.isValid())
        return OlderRead{{}, limit, {}};
    if (view_from >= kOlderMargin)
        return std::nullopt;
    return OlderRead{first, limit, first.addSecs(60)};
}

QList<Bar> group_bars(const QList<Bar>& bars, int minutes) {
    if (minutes <= 1)
        return bars;
    const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    QList<Bar> out;
    for (const auto& b : bars) {
        const QDateTime local = b.start.toTimeZone(kst);
        const int minute = local.time().hour() * 60 + local.time().minute();
        const int first = minute - minute % minutes;
        const QDateTime start(local.date(), QTime(first / 60, first % 60), kst);
        if (out.isEmpty() || out.last().start != start) {
            out << Bar{start, b.open, b.high, b.low, b.close, b.volume};
            continue;
        }
        Bar& into = out.last();
        into.high = std::max(into.high, b.high);
        into.low = std::min(into.low, b.low);
        into.close = b.close;
        into.volume += b.volume;
    }
    return out;
}

bool merge_live(QList<Bar>& bars, const Bar& live) {
    if (!live.start.isValid())
        return false;
    if (bars.isEmpty() || live.start > bars.last().start) {
        bars << live;
        return true;
    }
    Bar& last = bars.last();
    if (live.start != last.start || live.volume < last.volume)
        return false; // older than what is shown
    if (live.open == last.open && live.high == last.high && live.low == last.low && live.close == last.close &&
        live.volume == last.volume)
        return false;
    last = live;
    return true;
}

std::optional<ChartBar> last_chart_bar(const QList<Bar>& bars, int minutes) {
    if (bars.isEmpty())
        return std::nullopt;
    // Only the bars of the last one's group: the last group_bars would make of all.
    const QDateTime first = group_bars({bars.last()}, minutes).first().start;
    qsizetype from = bars.size() - 1;
    while (from > 0 && bars[from - 1].start >= first)
        --from;
    return chart_bars(group_bars(bars.mid(from), minutes)).last();
}

QList<Bar> join_bars(const QList<Bar>& older, const QList<Bar>& newer) {
    QMap<QDateTime, Bar> by_start;
    for (const auto& b : older)
        by_start.insert(b.start, b);
    for (const auto& b : newer)
        by_start.insert(b.start, b);
    return by_start.values();
}

} // namespace fincept::krx_quant
