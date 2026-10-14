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

QString verdict_mark(Mark::Result r) {
    return result_of(r);
}

QString verdict_text(Mark::Result r) {
    switch (r) {
        case Mark::Result::Hit:
            return QStringLiteral("✓ 맞음");
        case Mark::Result::Miss:
            return QStringLiteral("✗ 틀림");
        case Mark::Result::Unscorable:
            return QStringLiteral("채점 불가");
        case Mark::Result::Pending:
            break;
    }
    return QStringLiteral("채점 전");
}

/// What happened after a bar close: "실제 상승 +500원", or why it is not known yet.
QString truth_text(const Truth& truth) {
    if (!truth.scored)
        return QStringLiteral("채점 전");
    if (truth.happened == Direction::None)
        return truth.unscorable.isEmpty() ? QStringLiteral("채점 불가")
                                          : QStringLiteral("채점 불가 (%1)").arg(truth.unscorable);
    QString text = QStringLiteral("실제 %1").arg(direction_label(truth.happened));
    if (truth.mid_start && truth.mid_target) {
        const double move = *truth.mid_target - *truth.mid_start;
        const QString won = QLocale(QLocale::Korean).toString(std::abs(move), 'f', 0);
        text += QStringLiteral(" %1%2원").arg(move > 0 ? "+" : move < 0 ? "-" : "±", won);
    }
    if (truth.special)
        text += QStringLiteral(" · 특수 상황");
    return text;
}

/// The touch label beside it (krx-quant #167): "도달 상승 09:32:30", "도달 보합", "도달 채점 불가 (…)".
QString touch_text(const Touch& t) {
    switch (t.direction) {
        case Direction::Up:
            return t.upper_reached ? QStringLiteral("도달 상승 %1").arg(kst_text(t.upper_reached->at, "HH:mm:ss"))
                                   : QStringLiteral("도달 상승");
        case Direction::Down:
            return t.lower_reached ? QStringLiteral("도달 하락 %1").arg(kst_text(t.lower_reached->at, "HH:mm:ss"))
                                   : QStringLiteral("도달 하락");
        case Direction::Flat:
            return QStringLiteral("도달 보합");
        case Direction::None:
            break;
    }
    return t.unscorable.isEmpty() ? QStringLiteral("도달 채점 불가")
                                  : QStringLiteral("도달 채점 불가 (%1)").arg(t.unscorable);
}

QString probabilities(const Prediction& p) {
    return QStringLiteral("상승 %1 · 보합 %2 · 하락 %3").arg(percent(p.p_up), percent(p.p_flat), percent(p.p_down));
}

/// A consensus's line of a hover box: its class, three probabilities, how its models leaned,
/// its result, and whether it was a signal.
QString consensus_line(const QString& name, const Prediction& c, const Truth& truth, const QString& counted,
                       bool signal) {
    const Direction d = most_likely(c);
    QStringList parts{name, direction_label(d), probabilities(c)};
    if (!counted.isEmpty())
        parts << counted;
    parts << verdict_mark(verdict(d, truth));
    if (signal)
        parts << QStringLiteral("신호");
    return parts.join(QStringLiteral(" · "));
}

/// When a mark's bar close is scored: its truth's target time.
QDateTime target_of(const Truth& truth, const QDateTime& bar_close) {
    return truth.target_at.isValid() ? truth.target_at : bar_close.addSecs(240);
}

/// Whether a mark's consensus is a signal under ``rule``.
bool signalled(const Mark& m, const SignalRule& rule) {
    const Prediction& p = m.prediction;
    if (!p.votes || !judge(p, *p.votes, rule))
        return false;
    return !(m.selected && m.special); // a selected consensus on a special bar is never emphasised
}

} // namespace

QList<ChartIcon> chart_icons(const QList<Mark>& marks, const SignalRule& rule) {
    QList<ChartIcon> out;
    for (const auto& m : marks) {
        if (m.predicted == Direction::None)
            continue;
        ChartIcon icon;
        // The bar that closed at the bar close started a minute before.
        icon.time = chart_time(m.bar_close.addSecs(-60));
        icon.bar_close = m.bar_close;
        icon.direction = m.predicted;
        icon.arrow = arrow_of(m.predicted);
        icon.result = result_of(m.result);
        icon.faint = m.special;
        icon.signal = signalled(m, rule);
        out << icon;
    }
    std::sort(out.begin(), out.end(), [](const ChartIcon& a, const ChartIcon& b) { return a.time < b.time; });
    return out;
}

QStringList hover_box(const QList<Prediction>& at_close, const QDateTime& bar_close, const QStringList& selection,
                      const QMap<QString, QString>& standings, const SignalRule& rule) {
    const Truth truth = truth_of(at_close, bar_close);
    const QString times = QStringLiteral("%1 봉 → %2 목표")
                              .arg(kst_text(bar_close, "HH:mm"), kst_text(target_of(truth, bar_close), "HH:mm"));
    if (at_close.isEmpty())
        return {times, QStringLiteral("예측 없음")};
    QStringList lines{QStringLiteral("%1 · %2").arg(times, truth_text(truth))};
    if (truth.touch)
        lines.first() += QStringLiteral(" · ") + touch_text(*truth.touch);
    if (!selection.isEmpty()) {
        const QString chosen = QString::number(selection.size());
        if (const auto pooled = select_consensus(at_close, selection)) {
            const Votes& v = *pooled->consensus.votes;
            Mark m;
            m.prediction = pooled->consensus;
            m.selected = true;
            m.special = truth.special;
            const QString counted = QStringLiteral("상승 %1 보합 %2 하락 %3 · %4/%5 모델")
                                        .arg(v.up)
                                        .arg(v.flat)
                                        .arg(v.down)
                                        .arg(pooled->made)
                                        .arg(chosen);
            lines << consensus_line(QStringLiteral("선택 합의"), pooled->consensus, truth, counted, signalled(m, rule));
        } else {
            lines << QStringLiteral("선택 합의 · 예측 없음 · 0/%1 모델").arg(chosen);
        }
        for (const auto& name : selection) {
            const auto made = std::find_if(at_close.cbegin(), at_close.cend(),
                                           [&name](const Prediction& p) { return p.algorithm == name; });
            QStringList parts{predictor_label(name)};
            if (const QString standing = standings.value(name); !standing.isEmpty())
                parts << standing_label(standing);
            if (made == at_close.cend()) {
                parts << QStringLiteral("예측 없음");
            } else if (made->status != "ok") {
                parts << status_label(made->status);
            } else {
                const Direction d = most_likely(*made);
                parts << direction_label(d) << probabilities(*made) << verdict_mark(verdict(d, truth));
            }
            lines << parts.join(QStringLiteral(" · "));
        }
    }
    const auto final = std::find_if(at_close.cbegin(), at_close.cend(), [](const Prediction& p) {
        return p.algorithm == QLatin1String("consensus-final");
    });
    if (final == at_close.cend()) {
        lines << QStringLiteral("최종 합의 · 예측 없음");
    } else if (final->status != "ok") {
        lines << QStringLiteral("최종 합의 · %1").arg(status_label(final->status));
    } else {
        const Direction d = most_likely(*final);
        const QString counted =
            final->votes ? QStringLiteral("같은 방향 %1/%2").arg(final->votes->of(d)).arg(final->votes->total())
                         : QString();
        const bool signal = final->votes && judge(*final, *final->votes, rule).has_value();
        lines << consensus_line(QStringLiteral("최종 합의"), *final, truth, counted, signal);
    }
    return lines;
}

QList<BarHover> bar_hovers(const QList<Bar>& bars, const QMap<QDateTime, QList<Prediction>>& closes,
                           const QStringList& selection, const QMap<QString, QString>& standings,
                           const SignalRule& rule) {
    QList<BarHover> out;
    out.reserve(bars.size());
    for (const auto& b : bars) {
        const QDateTime bar_close = b.start.addSecs(60);
        const QList<Prediction> at_close = closes.value(bar_close);
        const Truth truth = truth_of(at_close, bar_close);
        BarHover hover;
        hover.time = chart_time(b.start);
        hover.lines = hover_box(at_close, bar_close, selection, standings, rule);
        hover.window_from = chart_time(bar_close);
        hover.window_to = chart_time(target_of(truth, bar_close));
        if (const auto& t = truth.touch) {
            hover.upper = t->upper;
            hover.lower = t->lower;
            if (t->upper_reached)
                hover.upper_reached = ChartPoint{chart_time(t->upper_reached->at), t->upper_reached->price};
            if (t->lower_reached)
                hover.lower_reached = ChartPoint{chart_time(t->lower_reached->at), t->lower_reached->price};
        }
        out << hover;
    }
    return out;
}

SelectionScore score_of(const QList<Mark>& marks, qint64 from, qint64 to, const QDateTime& now) {
    SelectionScore score;
    for (const auto& m : marks) {
        const qint64 time = chart_time(m.bar_close.addSecs(-60));
        if (time < from || time > to)
            continue;
        const Truth& t = m.truth;
        if (!t.scored || t.happened == Direction::None || t.special)
            continue;
        if (!t.target_at.isValid() || t.target_at > now)
            continue; // its 4 minutes are not over: a window decided early would weigh more
        ++score.n;
        score.hits += m.result == Mark::Result::Hit;
        score.flat_hits += t.happened == Direction::Flat;
    }
    return score;
}

QString score_text(int chosen, const SelectionScore& score) {
    const QString head = QStringLiteral("선택 합의 (%1)").arg(chosen);
    if (score.n == 0)
        return head + QStringLiteral(" · 채점된 봉 없음");
    const auto rate = [&score](int k) { return QString::number(std::lround(100.0 * k / score.n)) + "%"; };
    return QStringLiteral("%1 · 맞음 %2 · 늘 보합 %3 (%4개)")
        .arg(head, rate(score.hits), rate(score.flat_hits))
        .arg(score.n);
}

bool selectable(const QString& predictor) {
    return !predictor.isEmpty() && !is_consensus(predictor) && predictor != QLatin1String(kSelected);
}

QStringList toggled(QStringList selection, const QString& predictor, bool on) {
    selection.removeAll(predictor);
    if (on && selectable(predictor))
        selection << predictor;
    selection.removeDuplicates();
    std::sort(selection.begin(), selection.end());
    return selection;
}

QString selection_value(const QStringList& selection) {
    return selection.join(',');
}

QStringList parse_selection(const QString& saved) {
    QStringList out;
    for (const auto& name : saved.split(',', Qt::SkipEmptyParts))
        out = toggled(out, name.trimmed(), true);
    return out;
}

std::optional<QDateTime> relabel_before(const QDateTime& full_from, const QDateTime& oldest_close) {
    if (!oldest_close.isValid())
        return std::nullopt; // nothing shown
    if (full_from.isValid() && full_from <= oldest_close)
        return std::nullopt; // every predictor's, back to the first bar
    return full_from;        // invalid: from the latest
}

QDateTime full_from_after(const QDateTime& full_from, const QList<Prediction>& page, const QDateTime& oldest_close) {
    if (page.isEmpty())
        return oldest_close.isValid() ? oldest_close : full_from; // nothing before: as far back as shown
    QDateTime earliest = full_from;
    for (const auto& p : page)
        if (!earliest.isValid() || p.bar_close < earliest)
            earliest = p.bar_close;
    return earliest;
}

QList<CloseRow> close_rows(const QList<Prediction>& predictions, const QMap<QString, QString>& statuses,
                           const QStringList& selection) {
    const QDateTime bar_close = predictions.isEmpty() ? QDateTime() : predictions.first().bar_close;
    const Truth truth = truth_of(predictions, bar_close);
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
    if (!selection.isEmpty()) {
        CloseRow row;
        row.algorithm = kSelected;
        row.name = QStringLiteral("선택 합의");
        row.consensus = true;
        if (const auto made = select_consensus(predictions, selection)) {
            const Prediction& c = made->consensus;
            row.p_up = c.p_up;
            row.p_flat = c.p_flat;
            row.p_down = c.p_down;
            row.direction = direction_label(most_likely(c));
            row.result = verdict_text(verdict(most_likely(c), truth));
        } else {
            row.direction = QStringLiteral("예측 없음");
        }
        out << row;
    }
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
        row.result = p.status == "ok" ? verdict_text(verdict(leaned, truth)) : QString();
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

std::optional<Bar> day_candle(const QList<Bar>& bars, const QString& venue) {
    if (bars.isEmpty())
        return std::nullopt;
    const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(kKstSeconds);
    const QDate day = bars.last().start.toTimeZone(kst).date();
    const bool krx = venue == QLatin1String("KRX");
    std::optional<Bar> candle;
    for (const auto& b : bars) {
        const QDateTime local = b.start.toTimeZone(kst);
        if (local.date() != day)
            continue;
        if (krx && (local.time() < QTime(9, 0) || local.time() > QTime(15, 30)))
            continue; // the regular session's: from the opening auction to the closing one
        if (!candle) {
            candle = Bar{QDateTime(day, QTime(0, 0), kst), b.open, b.high, b.low, b.close, b.volume};
            continue;
        }
        candle->high = std::max(candle->high, b.high);
        candle->low = std::min(candle->low, b.low);
        candle->close = b.close;
        candle->volume += b.volume;
    }
    return candle;
}

QList<Bar> day_candles(const QList<Bar>& stored, const QList<Bar>& day_bars, const QString& venue) {
    const auto today = day_candle(day_bars, venue);
    if (!today)
        return stored;
    return join_bars(stored, {*today});
}

std::optional<QDateTime> older_candles(const QDateTime& first, double view_from) {
    if (!first.isValid())
        return QDateTime();
    if (view_from >= kOlderMargin)
        return std::nullopt;
    return first;
}

QString candle_period(int view) {
    switch (view) {
        case kWeekView:
            return QStringLiteral("week");
        case kMonthView:
            return QStringLiteral("month");
        case kYearView:
            return QStringLiteral("year");
        default:
            return QStringLiteral("day");
    }
}

QDate period_start(const QDate& day, int view) {
    switch (view) {
        case kWeekView:
            return day.addDays(1 - day.dayOfWeek()); // Monday is day 1
        case kMonthView:
            return QDate(day.year(), day.month(), 1);
        case kYearView:
            return QDate(day.year(), 1, 1);
        default:
            return day;
    }
}

QList<Bar> period_candles(const QList<Bar>& stored, const QList<Bar>& days, int view) {
    const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(kKstSeconds);
    QList<Bar> out = stored;
    const QDate stored_until = stored.isEmpty() ? QDate() : stored.last().start.toTimeZone(kst).date();
    for (const auto& d : days) {
        const QDate start = period_start(d.start.toTimeZone(kst).date(), view);
        if (stored_until.isValid() && start <= stored_until)
            continue; // the engine's
        const QDateTime at(start, QTime(0, 0), kst);
        if (out.isEmpty() || out.last().start != at) {
            out << Bar{at, d.open, d.high, d.low, d.close, d.volume};
            continue;
        }
        Bar& into = out.last();
        into.high = std::max(into.high, d.high);
        into.low = std::min(into.low, d.low);
        into.close = d.close;
        into.volume += d.volume;
    }
    return out;
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
