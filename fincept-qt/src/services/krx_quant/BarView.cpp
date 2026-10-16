#include "services/krx_quant/BarView.h"

#include "services/krx_quant/ChartSlots.h"

#include <QLocale>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fincept::krx_quant {

namespace {

const QTimeZone& kst() {
    static const QTimeZone zone = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    return zone;
}

QDate kst_day(const QDateTime& t) {
    return t.toTimeZone(kst()).date();
}

QString count_text(double n) {
    return QLocale(QLocale::Korean).toString(n, 'f', 0);
}

/// A move in won with its sign: "+100원", "-1,500원", "0원".
QString won_change(double change) {
    const QString sign = change > 0 ? QStringLiteral("+") : change < 0 ? QStringLiteral("-") : QString();
    return sign + price_text(std::abs(change)) + QStringLiteral("원");
}

/// A bar as the chart names it, by its close (ChartSlots' empty_line): "10:26 봉" on the day of
/// ``day``, "10-15 19:59 봉" on another.
QString bar_name(const QDateTime& bar_close, const QDate& day) {
    return QStringLiteral("%1 봉").arg(kst_text(bar_close, kst_day(bar_close) == day ? "HH:mm" : "MM-dd HH:mm"));
}

/// A kind's answer at the bar close: any of its predictions' (they all have the same).
std::optional<KindAnswer> answer_of(const QList<KindPrediction>& at_close) {
    for (const auto& p : at_close)
        if (p.answer)
            return p.answer;
    return std::nullopt;
}

/// A class's title in ``kind``; the class itself when the kind does not name it.
QString class_title(const std::optional<KindInfo>& kind, const QString& klass) {
    if (!kind)
        return klass;
    const qsizetype at = kind->classes.indexOf(klass);
    return at >= 0 && at < kind->class_titles.size() ? kind->class_titles[at] : klass;
}

/// Whether any of ``predictions`` was made (a forecast that is scored).
template <typename P>
bool any_made(const QList<P>& predictions) {
    return std::any_of(predictions.cbegin(), predictions.cend(),
                       [](const P& p) { return p.status == QLatin1String("ok"); });
}

/// Where a level was first reached and the quote then: "10:29:28 · 매수 1호가 81,500".
QString reach_text(const QDateTime& at, std::optional<double> price, const QString& quote) {
    if (!at.isValid())
        return QStringLiteral("닿지 않음");
    QString text = kst_text(at, "HH:mm:ss");
    if (price)
        text += QStringLiteral(" · %1 %2").arg(quote, price_text(*price));
    return text;
}

/// A kind's answer on a line of its own: "채점 전", "채점 불가 (why)", or what ``answered`` says
/// of a class; " · 특수 상황" when one touched its four minutes.
QString answer_line(const std::optional<KindAnswer>& answer, const QString& answered) {
    if (!answer)
        return QStringLiteral("채점 전");
    QString text = answer->klass.isEmpty()
                       ? (answer->unscorable.isEmpty() ? QStringLiteral("채점 불가")
                                                       : QStringLiteral("채점 불가 (%1)").arg(answer->unscorable))
                       : answered;
    if (answer->special)
        text += QStringLiteral(" · 특수 상황");
    return text;
}

/// A kind's title: the engine's, else the screen's.
QString kind_title(const QString& name, const std::optional<KindInfo>& info) {
    if (info && !info->title.isEmpty())
        return info->title;
    return name == QLatin1String(kRangeKind)     ? QStringLiteral("변동폭")
           : name == QLatin1String(kBarrierKind) ? QStringLiteral("장벽")
                                                 : name;
}

} // namespace

QString bar_key(const QString& symbol, const QDateTime& bar_close) {
    return symbol + '@' + bar_close.toTimeZone(kst()).toString(Qt::ISODate);
}

QString bar_title(const QString& name, const QString& symbol, const QDateTime& bar_close) {
    const QString who = name.isEmpty() ? symbol : QStringLiteral("%1 %2").arg(name, symbol);
    return QStringLiteral("%1 · %2 봉").arg(who, kst_text(bar_close, "yyyy-MM-dd HH:mm"));
}

QDateTime bar_scoring_at(const BarRead& read) {
    return truth_of(read.predictions, read.bar_close).target_at;
}

QList<BarLine> bar_info_lines(const BarRead& read, const QDateTime& now) {
    const QDateTime start = read.bar_close.addSecs(-60);
    QList<BarLine> out{{QStringLiteral("봉 시각"),
                        QStringLiteral("%1–%2").arg(kst_text(start, "HH:mm"), kst_text(read.bar_close, "HH:mm"))}};
    std::optional<Bar> bar;
    std::optional<Bar> before; // the last bar with trades before it, of any day
    for (const auto& b : read.bars) {
        if (b.start == start)
            bar = b;
        else if (b.start < start && (!before || b.start > before->start))
            before = b;
    }
    if (bar) {
        out << BarLine{QStringLiteral("시가"), price_text(bar->open)}
            << BarLine{QStringLiteral("고가"), price_text(bar->high)}
            << BarLine{QStringLiteral("저가"), price_text(bar->low)}
            << BarLine{QStringLiteral("종가"), price_text(bar->close)}
            << BarLine{QStringLiteral("거래량"), count_text(bar->volume)}
            // The backfill's files of a past day do not know the trades (engine-api.md, bars).
            << BarLine{QStringLiteral("체결 수"), bar->trades ? count_text(*bar->trades) : QStringLiteral("—")};
        QString change = QStringLiteral("—");
        if (before) {
            const double move = bar->close - before->close;
            const QString percent =
                before->close != 0
                    ? (move > 0   ? QStringLiteral("+")
                       : move < 0 ? QStringLiteral("-")
                                  : QString()) +
                          QLocale(QLocale::Korean).toString(std::abs(move) / before->close * 100, 'f', 2) + '%'
                    : QStringLiteral("—");
            change = QStringLiteral("%1 (%2) · %3 종가 %4")
                         .arg(won_change(move), percent, bar_name(before->start.addSecs(60), kst_day(start)),
                              price_text(before->close));
        }
        out << BarLine{QStringLiteral("앞 봉 대비"), change};
    } else {
        // A minute without trades (an empty slot on the chart), or the bar still being made.
        out << BarLine{QStringLiteral("가격"),
                       read.bar_close > now ? QStringLiteral("만드는 중") : QStringLiteral("체결 없음")};
    }
    const QStringList special = special_lines(read.special, chart_time(start), chart_time(bar_scoring_at(read)));
    out << BarLine{QStringLiteral("특수 상황"), special.isEmpty() ? QStringLiteral("없음") : special.join(", ")};
    return out;
}

BarAnchor bar_anchor(const BarRead& read) {
    BarAnchor a;
    if (const Truth truth = truth_of(read.predictions, read.bar_close); truth.touch) {
        a.anchor = truth.touch->anchor;
        a.tick = truth.touch->tick;
        a.k = truth.touch->k;
    }
    // Before it (or from an engine whose label lacks them): what the other kinds' predictions and
    // answers came with at the bar close, the same reference.
    QStringList order = bar_kinds();
    for (auto it = read.kinds.cbegin(); it != read.kinds.cend(); ++it)
        if (!order.contains(it.key()))
            order << it.key();
    for (const auto& kind : order) {
        for (const auto& p : read.kinds.value(kind)) {
            if (!a.anchor)
                a.anchor = p.anchor;
            if (!a.tick)
                a.tick = p.tick;
            if (!a.k)
                a.k = p.flat_ticks;
            if (const auto& answer = p.answer) {
                if (const auto& path = answer->range) {
                    a.anchor = a.anchor ? a.anchor : path->anchor;
                    a.tick = a.tick ? a.tick : path->tick;
                    a.k = a.k ? a.k : path->k;
                }
                if (const auto& path = answer->barrier) {
                    a.anchor = a.anchor ? a.anchor : path->anchor;
                    a.tick = a.tick ? a.tick : path->tick;
                    a.k = a.k ? a.k : path->k;
                }
            }
        }
    }
    return a;
}

QList<BarLine> answer_lines(const BarRead& read, const std::optional<QList<KindInfo>>& kinds) {
    const Truth truth = truth_of(read.predictions, read.bar_close);
    const BarAnchor a = bar_anchor(read);
    const QString none = QStringLiteral("—");
    QList<BarLine> out{
        {QStringLiteral("봉 마감 중간가"), a.anchor ? price_text(*a.anchor) : none},
        {QStringLiteral("호가단위"), a.tick ? price_text(*a.tick) + QStringLiteral("원") : none},
        {QStringLiteral("k (보합 폭)"), a.k ? QStringLiteral("%1틱").arg(*a.k) : none},
    };
    // The touch targets: the label's once it is in, else the bar close's mid-point ± k ticks.
    std::optional<double> upper, lower;
    if (truth.touch) {
        upper = truth.touch->upper;
        lower = truth.touch->lower;
    } else if (!truth.scored && a.anchor && a.tick && a.k) {
        upper = *a.anchor + *a.k * *a.tick;
        lower = *a.anchor - *a.k * *a.tick;
    }
    out << BarLine{QStringLiteral("위·아래 도달 목표"),
                   upper && lower ? QStringLiteral("위 %1 · 아래 %2").arg(price_text(*upper), price_text(*lower))
                                  : none};
    out << BarLine{QStringLiteral("도달 정답"), truth_text(truth)};
    if (truth.scored && truth.touch && truth.happened != Direction::None) {
        const Touch& t = *truth.touch;
        out << BarLine{QStringLiteral("위 목표에 처음 닿음"),
                       reach_text(t.upper_reached ? t.upper_reached->at : QDateTime(),
                                  t.upper_reached ? std::optional(t.upper_reached->price) : std::nullopt,
                                  QStringLiteral("매수 1호가"))}
            << BarLine{QStringLiteral("아래 목표에 처음 닿음"),
                       reach_text(t.lower_reached ? t.lower_reached->at : QDateTime(),
                                  t.lower_reached ? std::optional(t.lower_reached->price) : std::nullopt,
                                  QStringLiteral("매도 1호가"))};
    }

    // The range (krx-quant #180): r and its class, the highest and lowest mid-points and when.
    if (const auto range = kind_named(kinds, kRangeKind)) {
        const auto answer = answer_of(read.kinds.value(kRangeKind));
        const QString r =
            answer && answer->range && answer->range->r
                ? QStringLiteral("%1틱 · %2").arg(ticks_text(*answer->range->r), class_title(range, answer->klass))
                : class_title(range, answer ? answer->klass : QString());
        out << BarLine{QStringLiteral("실제 변동폭 r"), answer_line(answer, r)};
        if (answer && answer->range && !answer->klass.isEmpty()) {
            const RangePath& path = *answer->range;
            const auto mid = [](std::optional<double> price, const QDateTime& at) {
                if (!price)
                    return QStringLiteral("—");
                return at.isValid() ? QStringLiteral("%1 (%2)").arg(price_text(*price), kst_text(at, "HH:mm:ss"))
                                    : price_text(*price);
            };
            out << BarLine{QStringLiteral("실제 최고 중간가"), mid(path.max_mid, path.max_mid_at)}
                << BarLine{QStringLiteral("실제 최저 중간가"), mid(path.min_mid, path.min_mid_at)};
        }
    }

    // The barrier (krx-quant #183): w, U and L, the one reached first and where each was.
    if (const auto barrier = kind_named(kinds, kBarrierKind)) {
        const QList<KindPrediction> at_close = read.kinds.value(kBarrierKind);
        const QString barriers = barrier_text(at_close);
        out << BarLine{QStringLiteral("장벽 w · U · L"), barriers.isEmpty() ? none : barriers};
        const auto answer = answer_of(at_close);
        QString first = class_title(barrier, answer ? answer->klass : QString());
        if (answer && answer->barrier) {
            const QDateTime reached = answer->klass == QLatin1String("up_first")     ? answer->barrier->up_at
                                      : answer->klass == QLatin1String("down_first") ? answer->barrier->down_at
                                                                                     : QDateTime();
            if (reached.isValid())
                first += QStringLiteral(" (%1 닿음)").arg(kst_text(reached, "HH:mm:ss"));
        }
        out << BarLine{QStringLiteral("먼저 닿은 장벽"), answer_line(answer, first)};
        if (answer && answer->barrier && !answer->klass.isEmpty()) {
            const BarrierPath& path = *answer->barrier;
            out << BarLine{QStringLiteral("U에 처음 닿음"),
                           reach_text(path.up_at, path.up_bid, QStringLiteral("매수 1호가"))}
                << BarLine{QStringLiteral("L에 처음 닿음"),
                           reach_text(path.down_at, path.down_ask, QStringLiteral("매도 1호가"))};
        }
    }
    out << BarLine{QStringLiteral("채점 시각"), kst_text(bar_scoring_at(read), "HH:mm")};
    return out;
}

QStringList bar_kinds() {
    return {QString::fromLatin1(kRangeKind), QString::fromLatin1(kBarrierKind)};
}

QList<KindSection> kind_sections(const BarRead& read, const std::optional<QList<KindInfo>>& kinds) {
    QList<KindSection> out;
    for (const auto& name : bar_kinds()) {
        KindSection section;
        section.kind = name;
        section.info = kind_named(kinds, name);
        section.title = QStringLiteral("%1 예측").arg(kind_title(name, section.info));
        if (!section.info) {
            section.note = QStringLiteral("엔진이 이 종류를 예측하지 않는다");
        } else if (const auto failure = read.kind_failures.constFind(name); failure != read.kind_failures.cend()) {
            section.note = QStringLiteral("읽지 못함: %1").arg(*failure);
        } else {
            const QList<KindPrediction> at_close = read.kinds.value(name);
            section.columns = kind_panel_columns(section.info, true);
            section.rows = kind_rows(at_close, *section.info);
            if (at_close.isEmpty()) {
                section.note = QStringLiteral("그 봉 마감의 예측 없음");
            } else {
                QStringList parts{kind_answer_text(answer_of(at_close), *section.info)};
                const QString more = name == QLatin1String(kRangeKind)
                                         ? widths_text(bar_close_k(at_close, std::nullopt))
                                         : barrier_text(at_close);
                if (!more.isEmpty())
                    parts << more;
                section.note = parts.join(QStringLiteral(" · "));
            }
        }
        out << section;
    }
    return out;
}

MiniChart mini_chart(const BarRead& read, const QDateTime& now) {
    MiniChart m;
    const QDateTime start = read.bar_close.addSecs(-60);
    const QDateTime scoring = bar_scoring_at(read);
    m.from = chart_time(start);
    m.window_from = chart_time(read.bar_close);
    m.window_to = chart_time(scoring);
    m.open = now < scoring;
    m.now = chart_time(now);
    const QList<ChartBar> bars = chart_bars(read.bars);
    for (qint64 t = m.from; t < m.window_to; t += 60) {
        const auto it =
            std::find_if(bars.cbegin(), bars.cend(), [t](const ChartBar& b) { return b.time == t && !b.empty; });
        if (it != bars.cend()) {
            m.candles << *it;
        } else {
            ChartBar empty;
            empty.time = t;
            empty.empty = true;
            m.candles << empty;
        }
    }

    // The touch targets (krx-quant #167): the label's and where each was first reached; before it,
    // the bar close's mid-point ± k ticks the other kinds came with, dashed.
    const Truth truth = truth_of(read.predictions, read.bar_close);
    const BarAnchor a = bar_anchor(read);
    if (truth.scored) {
        if (const auto& t = truth.touch) {
            if (t->upper)
                m.levels << MiniLevel{QStringLiteral("위 목표"), *t->upper, true, false};
            if (t->lower)
                m.levels << MiniLevel{QStringLiteral("아래 목표"), *t->lower, false, false};
            if (const auto& r = t->upper_reached)
                m.points << MiniPoint{chart_time(r->at), r->price, true,
                                      QStringLiteral("위 목표에 닿음 %1 · 매수 1호가 %2")
                                          .arg(kst_text(r->at, "HH:mm:ss"), price_text(r->price))};
            if (const auto& r = t->lower_reached)
                m.points << MiniPoint{chart_time(r->at), r->price, false,
                                      QStringLiteral("아래 목표에 닿음 %1 · 매도 1호가 %2")
                                          .arg(kst_text(r->at, "HH:mm:ss"), price_text(r->price))};
        }
    } else if (a.anchor && a.tick && a.k) {
        const double span = *a.k * *a.tick;
        m.levels << MiniLevel{QStringLiteral("위 목표"), *a.anchor + span, true, true}
                 << MiniLevel{QStringLiteral("아래 목표"), *a.anchor - span, false, true};
    }

    // The range's highest and lowest mid-points once its answer is in (krx-quant #182).
    if (const auto answer = answer_of(read.kinds.value(kRangeKind)); answer && answer->range) {
        const RangePath& path = *answer->range;
        if (path.max_mid) {
            m.levels << MiniLevel{QStringLiteral("최고 중간가"), *path.max_mid, true, false};
            if (path.max_mid_at.isValid())
                m.points << MiniPoint{chart_time(path.max_mid_at), *path.max_mid, true,
                                      QStringLiteral("최고 중간가 %1 · %2")
                                          .arg(kst_text(path.max_mid_at, "HH:mm:ss"), price_text(*path.max_mid))};
        }
        if (path.min_mid) {
            m.levels << MiniLevel{QStringLiteral("최저 중간가"), *path.min_mid, false, false};
            if (path.min_mid_at.isValid())
                m.points << MiniPoint{chart_time(path.min_mid_at), *path.min_mid, false,
                                      QStringLiteral("최저 중간가 %1 · %2")
                                          .arg(kst_text(path.min_mid_at, "HH:mm:ss"), price_text(*path.min_mid))};
        }
    }

    // The barrier's U and L (krx-quant #185; before its answer from the predictions, #210) and, once
    // it is in, where each was first reached: the best bid at U or above, the best ask at L or below.
    const QList<KindPrediction> barrier = read.kinds.value(kBarrierKind);
    const auto barrier_answer = answer_of(barrier);
    if (const auto barriers = bar_close_barriers(barrier)) {
        m.levels << MiniLevel{QStringLiteral("U"), barriers->upper, true, !barrier_answer}
                 << MiniLevel{QStringLiteral("L"), barriers->lower, false, !barrier_answer};
    }
    if (barrier_answer && barrier_answer->barrier) {
        const BarrierPath& path = *barrier_answer->barrier;
        if (const auto price = path.up_bid ? path.up_bid : path.upper; path.up_at.isValid() && price)
            m.points << MiniPoint{chart_time(path.up_at), *price, true,
                                  QStringLiteral("U에 닿음 %1 · 매수 1호가 %2")
                                      .arg(kst_text(path.up_at, "HH:mm:ss"), price_text(*price))};
        if (const auto price = path.down_ask ? path.down_ask : path.lower; path.down_at.isValid() && price)
            m.points << MiniPoint{chart_time(path.down_at), *price, false,
                                  QStringLiteral("L에 닿음 %1 · 매도 1호가 %2")
                                      .arg(kst_text(path.down_at, "HH:mm:ss"), price_text(*price))};
    }

    // The prices shown: everything drawn, with a margin.
    std::optional<double> low, high;
    const auto take = [&low, &high](double price) {
        low = low ? std::min(*low, price) : price;
        high = high ? std::max(*high, price) : price;
    };
    for (const auto& s : m.candles)
        if (!s.empty) {
            take(s.low);
            take(s.high);
        }
    for (const auto& l : m.levels)
        take(l.price);
    for (const auto& p : m.points)
        take(p.price);
    if (low && high) {
        const double span = *high - *low;
        const double pad = span > 0 ? span * 0.08 : std::max(a.tick.value_or(1.0), std::abs(*high) * 0.0005);
        m.low = *low - pad;
        m.high = *high + pad;
    }
    return m;
}

double mini_x(const MiniChart& mini, qint64 time, double left, double width) {
    const double span = static_cast<double>(mini.window_to - mini.from);
    if (span <= 0)
        return left;
    return left + static_cast<double>(time - mini.from) / span * width;
}

double mini_y(const MiniChart& mini, double price, double top, double height) {
    if (mini.high <= mini.low)
        return top + height / 2;
    return top + (mini.high - price) / (mini.high - mini.low) * height;
}

QList<double> spread_labels(const QList<double>& ys, double gap, double top, double bottom) {
    QList<qsizetype> order(ys.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&ys](qsizetype a, qsizetype b) { return ys[a] < ys[b]; });
    QList<double> placed(ys.size());
    // Down from the top, each at least ``gap`` below the one above; then up from the bottom, so the
    // lowest stays within; then down again, so the highest does.
    for (qsizetype i = 0; i < order.size(); ++i) {
        double y = std::max(ys[order[i]], top);
        if (i > 0)
            y = std::max(y, placed[order[i - 1]] + gap);
        placed[order[i]] = y;
    }
    for (qsizetype i = order.size() - 1; i >= 0; --i) {
        double y = std::min(placed[order[i]], bottom);
        if (i + 1 < order.size())
            y = std::min(y, placed[order[i + 1]] - gap);
        placed[order[i]] = y;
    }
    for (qsizetype i = 0; i < order.size(); ++i)
        if (placed[order[i]] < top)
            placed[order[i]] = i > 0 ? std::max(top, placed[order[i - 1]] + gap) : top;
    return placed;
}

bool bar_settled(const BarRead& read, const QDateTime& now) {
    if (kst_day(read.bar_close) < kst_day(now))
        return true; // an earlier day's: nothing more comes
    const QDateTime scoring = bar_scoring_at(read);
    if (now < scoring)
        return false;
    if (now >= scoring.addSecs(kBarSettleSeconds))
        return true;
    if (any_made(read.predictions) && !truth_of(read.predictions, read.bar_close).scored)
        return false;
    for (auto it = read.kinds.cbegin(); it != read.kinds.cend(); ++it)
        if (any_made(it.value()) && !answer_of(it.value()))
            return false;
    return true;
}

QString bar_state_text(const BarRead& read, const QDateTime& now) {
    if (!bar_settled(read, now))
        return QStringLiteral("채점 전 · 갱신 중");
    const Truth truth = truth_of(read.predictions, read.bar_close);
    if (!truth.scored)
        return QStringLiteral("채점되지 않음");
    return truth.happened == Direction::None ? QStringLiteral("채점 불가") : QStringLiteral("채점됨");
}

} // namespace fincept::krx_quant
