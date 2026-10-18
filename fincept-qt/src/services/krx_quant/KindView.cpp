#include "services/krx_quant/KindView.h"

#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::krx_quant {

namespace {

/// The kinds the selector offers, in its order, with the screen's titles for a kind the engine
/// does not list.
const QList<QPair<QString, QString>>& selector_kinds() {
    static const QList<QPair<QString, QString>> kinds = {{kDirectionKind, QStringLiteral("방향")},
                                                         {kBarrierKind, QStringLiteral("장벽")},
                                                         {kRangeKind, QStringLiteral("변동폭")},
                                                         {kOpenToCloseKind, QStringLiteral("시가→종가")}};
    return kinds;
}

/// The kinds this screen can show: the range's since krx-quant #182, the barrier's since #185,
/// the barrier's horizon ladder since #263, open to close since #264.
bool shown_here(const QString& kind) {
    return kind == QLatin1String(kDirectionKind) || kind == QLatin1String(kRangeKind) || is_barrier_kind(kind) ||
           kind == QLatin1String(kOpenToCloseKind);
}

const QTimeZone& kst_zone() {
    static const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    return kst;
}

/// A signed percent of a change, two decimals: "+0.85%", "-0.85%", "0.00%".
QString change_text(double change) {
    const double hundredths = std::round(change * 10000) / 100; // percent, two decimals
    const QString text = QString::number(hundredths == 0 ? 0.0 : hundredths, 'f', 2) + "%";
    return hundredths > 0 ? "+" + text : text;
}

/// A daily kind's prediction time, "08:50"; the engine's 08:50 when it does not say.
QString predict_at_of(const KindInfo& kind) {
    return kind.daily && !kind.daily->predict_at.isEmpty() ? kind.daily->predict_at : QStringLiteral("08:50");
}

QString answer_from_of(const KindInfo& kind) {
    return kind.daily && !kind.daily->answer_from.isEmpty() ? kind.daily->answer_from : QStringLiteral("20:35");
}

bool is_barrier(const KindInfo& kind) {
    return is_barrier_kind(kind.name);
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
/// baselines' "no_previous" (the window before could not be scored; a daily kind's, the trading
/// day before had no answer by its prediction time, krx-quant #264).
QString kind_status_label(const QString& status, const KindInfo& kind) {
    if (status == QLatin1String("no_previous"))
        return is_daily(kind) ? QStringLiteral("전 거래일 정답 없음") : QStringLiteral("직전 창 없음");
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

/// When a bar close is scored: any forecast's target time, else its kind's horizon on.
QDateTime target_of(const QList<KindPrediction>& at_close, const QDateTime& bar_close, const KindInfo& kind) {
    for (const auto& p : at_close)
        if (p.target_at.isValid())
            return p.target_at;
    return bar_close.addSecs(60 * kind.horizon_minutes);
}

/// The tip of a kind the engine makes and this screen shows.
QString choice_tip(const QString& name, const std::optional<KindInfo>& made) {
    QStringList lines;
    if (made && !made->class_titles.isEmpty())
        lines << QStringLiteral("%1의 확률").arg(made->class_titles.join(QStringLiteral("·")));
    if (made && is_daily(*made)) { // once a trading day (krx-quant #264)
        const DailySchedule& day = *made->daily;
        lines << QStringLiteral("하루 한 번, 거래일 %1에 그날 %2 → %3의 방향을 예측한다. 정답은 그날 KRX 일봉이 "
                                "저장된 뒤(%4부터)다.")
                     .arg(predict_at_of(*made), day.from_title, day.to_title, answer_from_of(*made));
        lines << QStringLiteral("예측 패널, 순위표, 상세 통계 창이 이 종류로 바뀌고, 일 보기 차트에 날마다의 예측과 "
                                "결과가 붙는다. 분 보기의 아이콘·호버·선택 합의와 신호 알림은 방향 그대로다.");
        return lines.join('\n');
    }
    if (made && made->horizon_minutes != 4) // the barrier's horizon ladder (krx-quant #263)
        lines << QStringLiteral("봉 마감부터 %1분 동안의 장벽이다.").arg(made->horizon_minutes);
    if (name != QLatin1String(kDirectionKind))
        lines << QStringLiteral("예측 패널, 순위표, 상세 통계 창이 이 종류로 바뀐다. 차트의 아이콘·선택 "
                                "합의·신호 알림은 방향 그대로다.");
    return lines.join('\n');
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
            choice.tip = choice_tip(name, made);
        }
        out << choice;
        // The barrier's horizon ladder (krx-quant #263) after it, as the engine lists it: only
        // the rungs it makes, none greyed.
        if (name == QLatin1String(kBarrierKind) && kinds)
            for (const auto& k : *kinds)
                if (k.name != QLatin1String(kBarrierKind) && is_barrier_kind(k.name))
                    out << KindChoice{k.name, k.title.isEmpty() ? k.name : k.title, true, choice_tip(k.name, k)};
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

std::optional<Barriers> bar_close_barriers(const QList<KindPrediction>& at_close) {
    if (const auto answer = answer_of(at_close)) { // the answer's, as it has them
        if (answer->barrier && answer->barrier->upper && answer->barrier->lower)
            return Barriers{*answer->barrier->upper, *answer->barrier->lower};
        return std::nullopt;
    }
    // Before it: the bar close's mid-point, tick and w the predictions came with (the same for each).
    for (const auto& p : at_close)
        if (p.anchor && p.tick && p.width_ticks) {
            const double span = *p.width_ticks * *p.tick;
            return Barriers{*p.anchor + span, *p.anchor - span};
        }
    return std::nullopt;
}

QString barrier_text(const QList<KindPrediction>& at_close) {
    const auto w = bar_close_width(at_close);
    if (!w)
        return {};
    QStringList parts{QStringLiteral("w %1틱").arg(*w)};
    if (const auto barriers = bar_close_barriers(at_close))
        parts << QStringLiteral("U %1").arg(price_text(barriers->upper))
              << QStringLiteral("L %1").arg(price_text(barriers->lower));
    return parts.join(QStringLiteral(" · "));
}

QString kind_answer_text(const std::optional<KindAnswer>& answer, const KindInfo& kind) {
    if (is_daily(kind)) // its day (krx-quant #264)
        return daily_answer_text(answer, kind);
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
    if (kind && is_daily(*kind)) { // once a trading day (krx-quant #264): its answer that night
        const DailySchedule& day = *kind->daily;
        columns << PanelColumn{QStringLiteral("예측 클래스"),
                               QStringLiteral("세 확률 중 가장 큰 것이다(같으면 %1, 그다음 %2). 채점은 이것으로 한다.")
                                   .arg(kind->class_titles.value(1), kind->class_titles.value(0))};
        QStringList tips;
        if (kind->classes == QStringList{"up", "flat", "down"})
            tips = {QStringLiteral("그날 %1 → %2의 변화(%2 ÷ %1 − 1)가 +보합 폭보다 클 확률. 보합 폭은 제목에 있다.")
                        .arg(day.from_title, day.to_title),
                    QStringLiteral("그 변화가 ±보합 폭 안일 확률"), QStringLiteral("그 변화가 −보합 폭보다 작을 확률")};
        for (qsizetype i = 0; i < kind->class_titles.size(); ++i)
            columns << PanelColumn{kind->class_titles[i], i < tips.size() ? tips[i] : QString()};
        columns << PanelColumn{QStringLiteral("결과"),
                               QStringLiteral(
                                   "그날 KRX 일봉의 %1·%2로 채점한다. 일봉은 장이 끝난 뒤 저장되어 %3부터 나온다.")
                                   .arg(day.from_title, day.to_title, answer_from_of(*kind))}
                << PanelColumn{QStringLiteral("모델"), {}};
        return columns;
    }
    columns << (barrier ? PanelColumn{QStringLiteral("예측 클래스"),
                                      QStringLiteral("세 확률 중 가장 큰 것이다(같으면 시간 만료, 그다음 위 먼저). "
                                                     "채점은 이것으로 한다.")}
                        : PanelColumn{QStringLiteral("구간"), {}});
    if (kind) {
        QStringList tips;
        const int minutes = kind->horizon_minutes; // 4, or a barrier of the ladder's (krx-quant #263)
        if (kind->name == QLatin1String(kRangeKind))
            // What the columns are: the widths in ticks are on the panel's title.
            tips = {QStringLiteral("%1분 동안 중간가 변동폭(최고 − 최저)이 k틱보다 작을 확률").arg(minutes),
                    QStringLiteral("k틱 이상 2k틱 미만일 확률"), QStringLiteral("2k틱 이상일 확률")};
        else if (barrier)
            // w, U and L are on the panel's title.
            tips = {QStringLiteral("%1분 안에 매수 1호가가 위 장벽 U(봉 마감 중간가 + w틱)에 아래 장벽보다 "
                                   "먼저 닿을 확률")
                        .arg(minutes),
                    QStringLiteral("%1분 동안 어느 장벽에도 닿지 않을 확률").arg(minutes),
                    QStringLiteral("%1분 안에 매도 1호가가 아래 장벽 L(봉 마감 중간가 − w틱)에 위 장벽보다 "
                                   "먼저 닿을 확률")
                        .arg(minutes)};
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
            row.top = kind_status_label(p.status, kind);
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
    const QString target = kst_text(target_of(at_close, bar_close, kind), "HH:mm");
    const QString times = QStringLiteral("%1 봉 → %2 목표").arg(kst_text(bar_close, "HH:mm"), target);
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
            parts << kind_status_label(p.status, kind);
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
        hover.window_to = chart_time(target_of(at_close, bar_close, kind));
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
        // The barrier's U and L (krx-quant #185; before the answer, from the predictions, #210) and,
        // once the answer is in, where each was first reached: the best bid at U or above, the best
        // ask at L or below, the quote then (as the touch targets, #167).
        if (is_barrier(kind))
            if (const auto barriers = bar_close_barriers(at_close)) {
                hover.upper = barriers->upper;
                hover.lower = barriers->lower;
            }
        if (const auto answer = answer_of(at_close); answer && answer->barrier) {
            const BarrierPath& path = *answer->barrier;
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
    return KindRead{from, static_cast<int>(std::min<qint64>(minutes, kPredictionPage))};
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

// ---- A kind predicted once a trading day (krx-quant #264) ---------------------------------------

namespace {

/// Why a daily answer could not be scored, in Korean; the engine's word for another reason.
QString daily_unscorable(const QString& why) {
    if (why == QLatin1String("no band"))
        return QStringLiteral("보합 폭 없음");
    if (why == QLatin1String("no candle"))
        return QStringLiteral("일봉 없음");
    return why;
}

/// The class index's direction, as the chart's icons draw it: the first class up, the middle flat,
/// the last down (open to close's up, flat, down).
Direction direction_at(int klass) {
    return klass == 0 ? Direction::Up : klass == 2 ? Direction::Down : klass == 1 ? Direction::Flat : Direction::None;
}

QString arrow_at(int klass) {
    return klass == 0 ? QStringLiteral("▲") : klass == 2 ? QStringLiteral("▼") : QStringLiteral("–");
}

/// The likeliest of three probabilities, a tie going to the middle, then the first; -1 without three.
int likeliest_of(const QList<double>& p) {
    if (p.size() != 3)
        return -1;
    const double best = std::max({p[0], p[1], p[2]});
    if (p[1] == best)
        return 1;
    return p[0] == best ? 0 : 2;
}

/// The days of ``closes`` (prediction times) by day.
QMap<QDate, QList<KindPrediction>> by_day(const QMap<QDateTime, QList<KindPrediction>>& closes) {
    QMap<QDate, QList<KindPrediction>> out;
    for (auto it = closes.cbegin(); it != closes.cend(); ++it)
        out[daily_day(it.key())] += it.value();
    return out;
}

/// The day's prediction time and its predictions' answer.
QDateTime close_of(const QList<KindPrediction>& at_day) {
    return at_day.isEmpty() ? QDateTime() : at_day.first().bar_close;
}

QString day_head(const QDateTime& bar_close, const KindInfo& kind) {
    return QStringLiteral("%1 %2 (%3 예측)").arg(kst_text(bar_close, "MM-dd"), kind.title, predict_at_of(kind));
}

} // namespace

QDate daily_day(const QDateTime& bar_close) {
    return bar_close.toTimeZone(kst_zone()).date();
}

QDateTime daily_close_on(const QDateTime& t, const KindInfo& kind) {
    if (!is_daily(kind) || !t.isValid())
        return {};
    return QDateTime(daily_day(t), QTime::fromString(predict_at_of(kind), "HH:mm"), kst_zone());
}

std::optional<double> daily_band(const QList<KindPrediction>& at_day) {
    if (const auto answer = answer_of(at_day); answer && answer->open_to_close && answer->open_to_close->band)
        return answer->open_to_close->band;
    for (const auto& p : at_day)
        if (p.band)
            return p.band;
    return std::nullopt;
}

QString band_text(double band) {
    return QStringLiteral("보합 ±%1%").arg(QString::number(band * 100, 'f', 2));
}

QString daily_answer_text(const std::optional<KindAnswer>& answer, const KindInfo& kind) {
    if (!answer)
        return QStringLiteral("채점 전 (%1부터)").arg(answer_from_of(kind));
    QString text;
    if (answer->klass.isEmpty()) {
        text = answer->unscorable.isEmpty()
                   ? QStringLiteral("채점 불가")
                   : QStringLiteral("채점 불가 (%1)").arg(daily_unscorable(answer->unscorable));
    } else {
        text = QStringLiteral("정답 %1").arg(class_title(kind, answer->klass));
        if (const auto& day = answer->open_to_close)
            text += QStringLiteral(" %1 (%2 → %3)")
                        .arg(change_text(day->change), price_text(day->open), price_text(day->close));
    }
    if (answer->special)
        text += QStringLiteral(" · 특수 상황");
    if (answer->open_to_close && answer->open_to_close->vi > 0)
        text += QStringLiteral(" · VI %1회").arg(answer->open_to_close->vi);
    return text;
}

QString daily_title(const QString& symbol, const KindInfo& kind, const QDateTime& bar_close,
                    const QList<KindPrediction>& at_day, bool last_day) {
    if (!bar_close.isValid())
        return QStringLiteral("%1 · %2 · 예측 없음").arg(symbol, kind.title);
    if (at_day.isEmpty()) // a day pinned without one: a holiday, or before the kind ran
        return QStringLiteral("%1 · %2 %3 · 예측 없음").arg(symbol, kst_text(bar_close, "MM-dd"), kind.title);
    QString head = QStringLiteral("%1 · %2 %3 (%4 예측")
                       .arg(symbol, kst_text(bar_close, "MM-dd"), kind.title, predict_at_of(kind));
    head += last_day ? QStringLiteral(", 지난 거래일)") : QStringLiteral(")");
    QStringList parts{head};
    if (const auto band = daily_band(at_day))
        parts << band_text(*band);
    parts << daily_answer_text(answer_of(at_day), kind);
    return parts.join(QStringLiteral(" · "));
}

QString daily_tip(const KindInfo& kind) {
    if (!kind.daily)
        return {};
    const DailySchedule& day = *kind.daily;
    QStringList lines{
        QStringLiteral("하루 한 번 예측이다. 거래일 %1에, 그날 %2부터 %3까지의 방향을 %4 셋 중 하나로 예측한다.")
            .arg(predict_at_of(kind), day.from_title, day.to_title, kind.class_titles.join(QStringLiteral("·"))),
        QStringLiteral("정답은 그날 KRX 일봉의 시가와 종가(두 단일가)로, 일봉이 저장된 뒤(%1부터) 나온다.")
            .arg(answer_from_of(kind)),
        QStringLiteral("종가 ÷ 시가 − 1이 ±보합 폭 안이면 보합이다. 보합 폭은 그날 전의 날만으로 종목의 일 변동성에서 "
                       "정해, 세 클래스가 대략 1/3씩 나온다."),
    };
    if (!day.probation_days.isEmpty()) {
        QStringList days;
        for (const int d : day.probation_days)
            days << QString::number(d);
        lines << QStringLiteral("상태(수습·정식·벤치)는 정답이 있는 날 %1일째에 판정한다(최근 %2일). 하루에 종목마다 "
                                "값이 하나라 10일 수습은 짧다.")
                     .arg(days.join(QStringLiteral("·")))
                     .arg(day.recent_days);
    }
    lines << QStringLiteral("차트의 일 보기에 날마다의 예측과 결과가 붙는다.");
    return lines.join('\n');
}

std::optional<DailyCall> daily_call(const QList<KindPrediction>& at_day, const KindInfo& kind) {
    if (kind.classes.size() != 3)
        return std::nullopt;
    QList<KindPrediction> made;
    for (const auto& p : at_day)
        if (p.status == QLatin1String("ok") && !is_kind_baseline(kind, p.algorithm))
            made << p;
    if (made.isEmpty())
        return std::nullopt;
    std::sort(made.begin(), made.end(),
              [](const KindPrediction& a, const KindPrediction& b) { return a.algorithm < b.algorithm; });
    DailyCall call;
    call.probabilities = {0, 0, 0};
    for (const auto& p : made) {
        for (int i = 0; i < 3; ++i)
            call.probabilities[i] += p.value("p_" + kind.classes[i]).value_or(0);
        call.algorithms << p.algorithm;
    }
    for (auto& v : call.probabilities)
        v /= static_cast<double>(made.size());
    call.klass = likeliest_of(call.probabilities);
    return call;
}

QList<ChartIcon> daily_icons(const QList<Bar>& days, const QMap<QDateTime, QList<KindPrediction>>& closes,
                             const KindInfo& kind) {
    const QMap<QDate, QList<KindPrediction>> held = by_day(closes);
    QList<ChartIcon> out;
    for (const auto& candle : days) {
        const auto it = held.constFind(candle.start.toTimeZone(kst_zone()).date());
        if (it == held.cend())
            continue;
        const auto call = daily_call(*it, kind);
        if (!call || call->klass < 0)
            continue;
        const auto answer = answer_of(*it);
        ChartIcon icon;
        icon.time = chart_time(candle.start);
        icon.bar_close = close_of(*it);
        icon.direction = direction_at(call->klass);
        icon.arrow = arrow_at(call->klass);
        icon.result = verdict_mark(kind_verdict(call->klass, answer, kind));
        icon.faint = answer && answer->special;
        out << icon;
    }
    return out;
}

QList<BarHover> daily_hovers(const QList<Bar>& days, const QMap<QDateTime, QList<KindPrediction>>& closes,
                             const KindInfo& kind, const QMap<QString, QString>& standings) {
    const QMap<QDate, QList<KindPrediction>> held = by_day(closes);
    QList<BarHover> out;
    for (const auto& candle : days) {
        const auto it = held.constFind(candle.start.toTimeZone(kst_zone()).date());
        if (it == held.cend() || it->isEmpty())
            continue;
        const QList<KindPrediction>& at_day = *it;
        const auto answer = answer_of(at_day);
        QStringList first{day_head(close_of(at_day), kind), daily_answer_text(answer, kind)};
        if (const auto band = daily_band(at_day))
            first << band_text(*band);
        QStringList lines{first.join(QStringLiteral(" · "))};
        // The icon's call when it is the mean of several algorithms; one algorithm's is its own line.
        if (const auto call = daily_call(at_day, kind); call && call->algorithms.size() > 1 && call->klass >= 0) {
            QStringList parts{QStringLiteral("알고리즘 평균 (%1)").arg(call->algorithms.size()),
                              class_title(kind, kind.classes[call->klass])};
            QStringList probabilities;
            for (int i = 0; i < 3; ++i)
                probabilities << QStringLiteral("%1 %2").arg(class_title(kind, kind.classes[i]),
                                                             percent(call->probabilities[i]));
            parts << probabilities.join(QStringLiteral(" · ")) << verdict_mark(kind_verdict(call->klass, answer, kind));
            lines << parts.join(QStringLiteral(" · "));
        }
        for (const auto& p : in_panel_order(at_day, kind)) {
            QStringList parts{predictor_label(p.algorithm)};
            if (const QString standing = standings.value(p.algorithm); !standing.isEmpty())
                parts << standing_label(standing);
            if (p.status != QLatin1String("ok")) {
                parts << kind_status_label(p.status, kind);
                lines << parts.join(QStringLiteral(" · "));
                continue;
            }
            const int top = likeliest_class(p, kind);
            if (top >= 0)
                parts << class_title(kind, kind.classes[top]) << probabilities_text(p, kind);
            parts << verdict_mark(kind_verdict(top, answer, kind));
            lines << parts.join(QStringLiteral(" · "));
        }
        BarHover hover;
        hover.time = chart_time(candle.start);
        hover.lines = lines;
        out << hover; // no window: a day is no bar close
    }
    return out;
}

DailyScore daily_score(const QMap<QDateTime, QList<KindPrediction>>& closes, const KindInfo& kind, qint64 from,
                       qint64 to) {
    DailyScore out;
    QSet<QString> algorithms;
    const QMap<QDate, QList<KindPrediction>> held = by_day(closes);
    for (auto it = held.cbegin(); it != held.cend(); ++it) {
        const qint64 time = chart_time(QDateTime(it.key(), QTime(0, 0), kst_zone()));
        if (time < from || time > to)
            continue;
        const auto call = daily_call(*it, kind);
        if (!call || call->klass < 0)
            continue;
        for (const auto& a : call->algorithms)
            algorithms.insert(a);
        const auto answer = answer_of(*it);
        if (!answer || answer->klass.isEmpty())
            continue; // not answered yet, or could not be
        if (answer->special) {
            ++out.score.special;
            continue;
        }
        const int happened = static_cast<int>(kind.classes.indexOf(answer->klass));
        const bool hit = happened == call->klass;
        ++out.score.n;
        out.score.hits += hit;
        out.score.flat_hits += happened == 1;
        if (call->klass == 0) {
            ++out.score.up_n;
            out.score.up_hits += hit;
        } else if (call->klass == 2) {
            ++out.score.down_n;
            out.score.down_hits += hit;
        }
        ++out.score.days;
    }
    out.algorithms = QStringList(algorithms.cbegin(), algorithms.cend());
    out.algorithms.sort();
    return out;
}

QString daily_score_text(const DailyScore& daily) {
    if (daily.algorithms.isEmpty())
        return {};
    const SelectionScore& score = daily.score;
    const auto rate = [](int k, int n) {
        return n == 0 ? QStringLiteral("—") : QString::number(std::lround(100.0 * k / n)) + "%";
    };
    QStringList parts{daily.algorithms.size() == 1 ? predictor_label(daily.algorithms.first())
                                                   : QStringLiteral("알고리즘 평균 (%1)").arg(daily.algorithms.size())};
    if (score.n == 0) {
        parts << QStringLiteral("채점된 날 없음");
    } else {
        parts << QStringLiteral("맞음 %1").arg(rate(score.hits, score.n))
              << QStringLiteral("늘 보합 %1").arg(rate(score.flat_hits, score.n))
              << QStringLiteral("▲ 맞음 %1 (%2)").arg(rate(score.up_hits, score.up_n)).arg(score.up_n)
              << QStringLiteral("▼ 맞음 %1 (%2)").arg(rate(score.down_hits, score.down_n)).arg(score.down_n)
              << QStringLiteral("%1일").arg(score.days);
    }
    if (score.special > 0)
        parts << QStringLiteral("특수 %1일 제외").arg(score.special);
    return parts.join(QStringLiteral(" · "));
}

std::optional<KindRead> daily_read(const QDateTime& from, const QDate& first_day, bool latest) {
    if (!from.isValid())
        return KindRead{{}, kDailyPage}; // the latest year first
    if (latest)
        return KindRead{{}, kDailyRefresh}; // the latest days again: their answers since
    if (from <= from_the_start() || !first_day.isValid() || daily_day(from) <= first_day)
        return std::nullopt; // the engine has none before, nothing is held, or back to the first day held
    // A day a prediction time: as many as there are days from the first held on, the engine's most at once.
    const qint64 days = first_day.daysTo(daily_day(from)) + 1;
    return KindRead{from, static_cast<int>(std::min<qint64>(days, kPredictionPage))};
}

QDateTime daily_from_after(const QDateTime& from, const QList<KindPrediction>& page) {
    if (page.isEmpty())
        return from_the_start(); // nothing before: all of them are read
    QDateTime earliest = from;
    for (const auto& p : page)
        if (!earliest.isValid() || p.bar_close < earliest)
            earliest = p.bar_close;
    return earliest;
}

} // namespace fincept::krx_quant
