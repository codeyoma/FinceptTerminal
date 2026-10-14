#include "services/krx_quant/KindView.h"

#include <algorithm>
#include <cmath>

namespace fincept::krx_quant {

namespace {

/// The kinds the selector offers, in its order, with the screen's titles for a kind the engine
/// does not list.
const QList<QPair<QString, QString>>& selector_kinds() {
    static const QList<QPair<QString, QString>> kinds = {{kDirectionKind, QStringLiteral("방향")},
                                                         {kBarrierKind, QStringLiteral("장벽")},
                                                         {kRangeKind, QStringLiteral("변동폭")}};
    return kinds;
}

/// The kinds this screen can show: the range's since krx-quant #182, the barrier's since #185.
bool shown_here(const QString& kind) {
    return kind == QLatin1String(kDirectionKind) || kind == QLatin1String(kRangeKind) ||
           kind == QLatin1String(kBarrierKind);
}

bool is_barrier(const KindInfo& kind) {
    return kind.name == QLatin1String(kBarrierKind);
}

QString percent(double p) {
    return QString::number(std::lround(p * 100)) + "%";
}

/// A class's title in ``kind``; the class itself when the kind does not name it.
QString class_title(const KindInfo& kind, const QString& klass) {
    const qsizetype at = kind.classes.indexOf(klass);
    return at >= 0 && at < kind.class_titles.size() ? kind.class_titles[at] : klass;
}

/// Why a prediction of a kind was not made, in Korean: the direction's words, and the
/// baselines' "no_previous" (the window before could not be scored).
QString kind_status_label(const QString& status) {
    if (status == QLatin1String("no_previous"))
        return QStringLiteral("직전 창 없음");
    return status_label(status);
}

/// The bar close's answer: any of its predictions' (they all have the same).
std::optional<KindAnswer> answer_of(const QList<KindPrediction>& at_close) {
    for (const auto& p : at_close)
        if (p.answer)
            return p.answer;
    return std::nullopt;
}

/// A forecast's result on its bar close's answer.
Mark::Result kind_verdict(int predicted, const std::optional<KindAnswer>& answer, const KindInfo& kind) {
    if (!answer)
        return Mark::Result::Pending;
    if (answer->klass.isEmpty())
        return Mark::Result::Unscorable;
    return kind.classes.indexOf(answer->klass) == predicted ? Mark::Result::Hit : Mark::Result::Miss;
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

QString verdict_mark(Mark::Result r) {
    switch (r) {
        case Mark::Result::Hit:
            return QStringLiteral("✓");
        case Mark::Result::Miss:
            return QStringLiteral("✗");
        default:
            return QStringLiteral("?");
    }
}

/// The predictions in panel order: the algorithms by name, then the baselines by name.
QList<KindPrediction> in_panel_order(QList<KindPrediction> at_close, const KindInfo& kind) {
    std::stable_sort(at_close.begin(), at_close.end(), [&kind](const KindPrediction& a, const KindPrediction& b) {
        const bool ba = is_kind_baseline(kind, a.algorithm), bb = is_kind_baseline(kind, b.algorithm);
        return ba != bb ? !ba : a.algorithm < b.algorithm;
    });
    return at_close;
}

/// A forecast's three probabilities as the hover writes them, in the kind's class order:
/// "위 먼저 30% · 시간 만료 50% · 아래 먼저 20%".
QString probabilities_text(const KindPrediction& p, const KindInfo& kind) {
    QStringList parts;
    for (const auto& c : kind.classes)
        parts << QStringLiteral("%1 %2").arg(class_title(kind, c), percent(p.value("p_" + c).value_or(0)));
    return parts.join(QStringLiteral(" · "));
}

/// The barrier answer's line: the barrier reached first and when.
QString barrier_answer_text(const KindAnswer& answer, const KindInfo& kind) {
    QString text = QStringLiteral("정답 %1").arg(class_title(kind, answer.klass));
    if (const auto& path = answer.barrier) {
        const QDateTime reached = answer.klass == QLatin1String("up_first")     ? path->up_at
                                  : answer.klass == QLatin1String("down_first") ? path->down_at
                                                                                : QDateTime();
        if (reached.isValid())
            text += QStringLiteral(" (%1 닿음)").arg(kst_text(reached, "HH:mm:ss"));
    }
    return text;
}

/// When a bar close is scored: any forecast's target time, else four minutes on.
QDateTime target_of(const QList<KindPrediction>& at_close, const QDateTime& bar_close) {
    for (const auto& p : at_close)
        if (p.target_at.isValid())
            return p.target_at;
    return bar_close.addSecs(240);
}

} // namespace

QList<KindChoice> kind_choices(const std::optional<QList<KindInfo>>& kinds) {
    QList<KindChoice> out;
    for (const auto& [name, title] : selector_kinds()) {
        const auto made = kind_named(kinds, name);
        KindChoice choice{name, made && !made->title.isEmpty() ? made->title : title, false, {}};
        // The direction is every engine's, an older one's included.
        const bool engine = made.has_value() || name == QLatin1String(kDirectionKind);
        choice.enabled = engine && shown_here(name);
        if (!engine) {
            choice.tip = QStringLiteral("엔진이 이 종류를 예측하지 않는다(옛 엔진이거나 아직 없는 종류).");
        } else if (!shown_here(name)) {
            choice.tip = QStringLiteral("이 화면은 아직 이 종류를 보이지 못한다.");
        } else {
            QStringList lines;
            if (made && !made->class_titles.isEmpty())
                lines << QStringLiteral("%1의 확률").arg(made->class_titles.join(QStringLiteral("·")));
            if (name != QLatin1String(kDirectionKind))
                lines << QStringLiteral("예측 패널, 순위표, 상세 통계 창이 이 종류로 바뀐다. 차트의 아이콘·선택 "
                                        "합의·신호 알림은 방향 그대로다.");
            choice.tip = lines.join('\n');
        }
        out << choice;
    }
    return out;
}

QString shown_kind(const QString& saved, const QList<KindChoice>& choices) {
    for (const auto& c : choices)
        if (c.name == saved && c.enabled)
            return saved;
    return kDirectionKind;
}

std::optional<KindInfo> kind_named(const std::optional<QList<KindInfo>>& kinds, const QString& name) {
    if (!kinds)
        return std::nullopt;
    for (const auto& k : *kinds)
        if (k.name == name)
            return k;
    return std::nullopt;
}

bool is_kind_baseline(const KindInfo& kind, const QString& predictor) {
    return std::any_of(kind.baselines.cbegin(), kind.baselines.cend(),
                       [&predictor](const KindBaseline& b) { return b.name == predictor; });
}

std::optional<AlgorithmInfo> kind_baseline_info(const std::optional<QList<KindInfo>>& kinds, const QString& predictor) {
    if (!kinds)
        return std::nullopt;
    for (const auto& kind : *kinds) {
        for (const auto& b : kind.baselines) {
            if (b.name != predictor)
                continue;
            AlgorithmInfo info;
            info.name = b.name;
            info.kind = QStringLiteral("baseline");
            info.prediction_kind = kind.name;
            info.card = b.card;
            return info;
        }
    }
    return std::nullopt;
}

int likeliest_class(const KindPrediction& p, const KindInfo& kind) {
    if (p.status != QLatin1String("ok") || kind.classes.size() != 3)
        return -1;
    double probability[3];
    for (int i = 0; i < 3; ++i) {
        const auto value = p.value("p_" + kind.classes[i]);
        if (!value)
            return -1;
        probability[i] = *value;
    }
    const double best = std::max({probability[0], probability[1], probability[2]});
    if (probability[1] == best) // a tie goes to the middle class, then the first
        return 1;
    return probability[0] == best ? 0 : 2;
}

QString ticks_text(double ticks) {
    if (std::abs(ticks - std::round(ticks)) < 1e-9)
        return QString::number(static_cast<qint64>(std::llround(ticks)));
    return QString::number(ticks, 'f', 1);
}

std::optional<int> bar_close_k(const QList<KindPrediction>& at_close, std::optional<int> now) {
    if (const auto answer = answer_of(at_close); answer && answer->range && answer->range->k)
        return answer->range->k;
    for (const auto& p : at_close)
        if (p.flat_ticks)
            return p.flat_ticks;
    return now;
}

QString widths_text(std::optional<int> k) {
    if (!k)
        return {};
    return QStringLiteral("k %1틱 · 2k %2틱").arg(*k).arg(2 * *k);
}

std::optional<int> bar_close_width(const QList<KindPrediction>& at_close) {
    if (const auto answer = answer_of(at_close); answer && answer->barrier && answer->barrier->w)
        return answer->barrier->w;
    for (const auto& p : at_close)
        if (p.width_ticks)
            return p.width_ticks;
    return std::nullopt;
}

QString barrier_text(const QList<KindPrediction>& at_close) {
    const auto w = bar_close_width(at_close);
    if (!w)
        return {};
    QStringList parts{QStringLiteral("w %1틱").arg(*w)};
    if (const auto answer = answer_of(at_close); answer && answer->barrier) {
        const BarrierPath& path = *answer->barrier;
        if (path.upper && path.lower)
            parts << QStringLiteral("U %1").arg(price_text(*path.upper))
                  << QStringLiteral("L %1").arg(price_text(*path.lower));
    }
    return parts.join(QStringLiteral(" · "));
}

QString kind_answer_text(const std::optional<KindAnswer>& answer, const KindInfo& kind) {
    if (!answer)
        return QStringLiteral("채점 전");
    if (answer->klass.isEmpty())
        return answer->unscorable.isEmpty() ? QStringLiteral("채점 불가")
                                            : QStringLiteral("채점 불가 (%1)").arg(answer->unscorable);
    QString text =
        answer->range && answer->range->r
            ? QStringLiteral("정답 %1틱 · %2").arg(ticks_text(*answer->range->r), class_title(kind, answer->klass))
        : is_barrier(kind) ? barrier_answer_text(*answer, kind)
                           : QStringLiteral("정답 %1").arg(class_title(kind, answer->klass));
    if (answer->special)
        text += QStringLiteral(" · 특수 상황");
    return text;
}

QList<PanelColumn> kind_panel_columns(const std::optional<KindInfo>& kind, bool pinned) {
    const bool barrier = kind && is_barrier(*kind);
    QList<PanelColumn> columns{{QStringLiteral("예측"), {}}};
    columns << (barrier ? PanelColumn{QStringLiteral("예측 클래스"),
                                      QStringLiteral("세 확률 중 가장 큰 것이다(같으면 시간 만료, 그다음 위 먼저). "
                                                     "채점은 이것으로 한다.")}
                        : PanelColumn{QStringLiteral("구간"), {}});
    if (kind) {
        QStringList tips;
        if (kind->name == QLatin1String(kRangeKind))
            // What the columns are: the widths in ticks are on the panel's title.
            tips = {QStringLiteral("4분 동안 중간가 변동폭(최고 − 최저)이 k틱보다 작을 확률"),
                    QStringLiteral("k틱 이상 2k틱 미만일 확률"), QStringLiteral("2k틱 이상일 확률")};
        else if (barrier)
            // w, U and L are on the panel's title.
            tips = {
                QStringLiteral("4분 안에 매수 1호가가 위 장벽 U(봉 마감 중간가 + w틱)에 아래 장벽보다 먼저 닿을 확률"),
                QStringLiteral("4분 동안 어느 장벽에도 닿지 않을 확률"),
                QStringLiteral("4분 안에 매도 1호가가 아래 장벽 L(봉 마감 중간가 − w틱)에 위 장벽보다 먼저 닿을 확률")};
        for (qsizetype i = 0; i < kind->class_titles.size(); ++i)
            columns << PanelColumn{kind->class_titles[i], i < tips.size() ? tips[i] : QString()};
        if (kind->name == QLatin1String(kRangeKind))
            columns << PanelColumn{QStringLiteral("q10"),
                                   QStringLiteral("변동폭의 10% 분위수(틱): 열에 하나는 이보다 작다")}
                    << PanelColumn{QStringLiteral("q50"), QStringLiteral("변동폭의 중앙값(틱)")}
                    << PanelColumn{QStringLiteral("q90"),
                                   QStringLiteral("변동폭의 90% 분위수(틱): 열에 아홉은 이보다 작다")};
    }
    if (pinned)
        columns << PanelColumn{QStringLiteral("결과"), {}};
    columns << PanelColumn{QStringLiteral("모델"), {}};
    return columns;
}

QList<KindRow> kind_rows(const QList<KindPrediction>& at_close, const KindInfo& kind) {
    const auto answer = answer_of(at_close);
    QList<KindRow> out;
    for (const auto& p : in_panel_order(at_close, kind)) {
        KindRow row;
        row.algorithm = p.algorithm;
        row.name = predictor_label(p.algorithm);
        row.baseline = is_kind_baseline(kind, p.algorithm);
        row.model_version = p.model_version;
        if (p.status != QLatin1String("ok")) {
            row.top = kind_status_label(p.status);
            out << row;
            continue;
        }
        for (const auto& c : kind.classes)
            row.probabilities << p.value("p_" + c);
        const int top = likeliest_class(p, kind);
        row.top = top >= 0 && top < kind.class_titles.size() ? kind.class_titles[top] : QString();
        row.q10 = p.value("q10");
        row.q50 = p.value("q50");
        row.q90 = p.value("q90");
        row.result = verdict_text(kind_verdict(top, answer, kind));
        out << row;
    }
    return out;
}

QStringList kind_hover_box(const QList<KindPrediction>& at_close, const QDateTime& bar_close, const KindInfo& kind,
                           const QMap<QString, QString>& standings) {
    const QString times = QStringLiteral("%1 봉 → %2 목표")
                              .arg(kst_text(bar_close, "HH:mm"), kst_text(target_of(at_close, bar_close), "HH:mm"));
    if (at_close.isEmpty())
        return {times, QStringLiteral("예측 없음")};
    const auto answer = answer_of(at_close);
    QString first = QStringLiteral("%1 · %2").arg(times, kind_answer_text(answer, kind));
    if (is_barrier(kind))
        if (const auto w = bar_close_width(at_close))
            first += QStringLiteral(" · w %1틱").arg(*w);
    QStringList lines{first};
    for (const auto& p : in_panel_order(at_close, kind)) {
        QStringList parts{predictor_label(p.algorithm)};
        if (const QString standing = standings.value(p.algorithm); !standing.isEmpty())
            parts << standing_label(standing);
        if (p.status != QLatin1String("ok")) {
            parts << kind_status_label(p.status);
            lines << parts.join(QStringLiteral(" · "));
            continue;
        }
        const int top = likeliest_class(p, kind);
        if (is_barrier(kind)) { // as the direction's: the class, then the three probabilities
            if (top >= 0)
                parts << class_title(kind, kind.classes[top]) << probabilities_text(p, kind);
        } else if (top >= 0) {
            parts << QStringLiteral("%1 %2").arg(class_title(kind, kind.classes[top]),
                                                 percent(p.value("p_" + kind.classes[top]).value_or(0)));
        }
        if (const auto q10 = p.value("q10"), q90 = p.value("q90"); q10 && q90)
            parts << QStringLiteral("q10~q90 %1~%2틱").arg(ticks_text(*q10), ticks_text(*q90));
        parts << verdict_mark(kind_verdict(top, answer, kind));
        lines << parts.join(QStringLiteral(" · "));
    }
    return lines;
}

QList<BarHover> kind_hovers(const QList<Bar>& bars, const QMap<QDateTime, QList<KindPrediction>>& closes,
                            const KindInfo& kind, const QMap<QString, QString>& standings) {
    QList<BarHover> out;
    out.reserve(bars.size());
    for (const auto& b : bars) {
        const QDateTime bar_close = b.start.addSecs(60);
        const QList<KindPrediction> at_close = closes.value(bar_close);
        BarHover hover;
        hover.time = chart_time(b.start);
        hover.lines = kind_hover_box(at_close, bar_close, kind, standings);
        hover.window_from = chart_time(bar_close);
        hover.window_to = chart_time(target_of(at_close, bar_close));
        // The range's highest and lowest mid-points over its window, once its answer is in.
        if (const auto answer = answer_of(at_close); answer && answer->range) {
            const RangePath& path = *answer->range;
            hover.upper = path.max_mid;
            hover.lower = path.min_mid;
            if (path.max_mid && path.max_mid_at.isValid())
                hover.upper_reached = ChartPoint{chart_time(path.max_mid_at), *path.max_mid};
            if (path.min_mid && path.min_mid_at.isValid())
                hover.lower_reached = ChartPoint{chart_time(path.min_mid_at), *path.min_mid};
        }
        // The barrier's U and L, and where each was first reached: the best bid at U or above,
        // the best ask at L or below, the quote then (krx-quant #185, as the touch targets, #167).
        if (const auto answer = answer_of(at_close); answer && answer->barrier) {
            const BarrierPath& path = *answer->barrier;
            hover.upper = path.upper;
            hover.lower = path.lower;
            if (const auto price = path.up_bid ? path.up_bid : path.upper; path.up_at.isValid() && price)
                hover.upper_reached = ChartPoint{chart_time(path.up_at), *price};
            if (const auto price = path.down_ask ? path.down_ask : path.lower; path.down_at.isValid() && price)
                hover.lower_reached = ChartPoint{chart_time(path.down_at), *price};
        }
        out << hover;
    }
    return out;
}

std::optional<KindRead> kind_read(const QDateTime& from, const QDateTime& oldest_close) {
    if (!oldest_close.isValid())
        return std::nullopt; // no bar shown
    if (!from.isValid())
        return KindRead{}; // the latest page first
    if (from <= oldest_close)
        return std::nullopt; // back to the first bar shown
    // A bar close a minute: as many as there are minutes from the first bar's on, the engine's most at once.
    const qint64 minutes = oldest_close.secsTo(from) / 60 + 1;
    return KindRead{from, static_cast<int>(std::min<qint64>(minutes, kMaxOlderBars))};
}

QDateTime kind_from_after(const QDateTime& from, const QList<KindPrediction>& page, const QDateTime& oldest_close) {
    if (page.isEmpty())
        return oldest_close.isValid() ? oldest_close : from; // nothing before: as far back as shown
    QDateTime earliest = from;
    for (const auto& p : page)
        if (!earliest.isValid() || p.bar_close < earliest)
            earliest = p.bar_close;
    return earliest;
}

} // namespace fincept::krx_quant
