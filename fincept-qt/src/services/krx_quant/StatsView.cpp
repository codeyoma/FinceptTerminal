#include "services/krx_quant/StatsView.h"

#include <QLocale>
#include <QTime>

#include <algorithm>

namespace fincept::krx_quant {

QString number_text(double value, int decimals) {
    static const QLocale locale(QLocale::Korean, QLocale::SouthKorea);
    return locale.toString(value, 'f', decimals);
}

QString signed_text(double value, int decimals) {
    const QString text = number_text(value, decimals);
    return value > 0 ? "+" + text : text;
}

Shown rate_shown(const std::optional<Interval>& rate) {
    if (!rate)
        return {QStringLiteral("—"), {}};
    Shown shown{number_text(rate->value * 100, 1) + "%", {}};
    if (rate->low && rate->high)
        shown.tip = QStringLiteral("95% 구간 %1–%2% (n %3)")
                        .arg(number_text(*rate->low * 100, 1), number_text(*rate->high * 100, 1))
                        .arg(rate->n);
    return shown;
}

Shown mean_shown(const std::optional<Interval>& mean, int decimals) {
    if (!mean)
        return {QStringLiteral("—"), {}};
    Shown shown{number_text(mean->value, decimals), {}};
    if (mean->low && mean->high)
        shown.tip = QStringLiteral("95% 구간 %1 – %2 (n %3)")
                        .arg(number_text(*mean->low, decimals), number_text(*mean->high, decimals))
                        .arg(mean->n);
    return shown;
}

QString weakest_lift_text(const Entry& entry) {
    return entry.lift_low ? signed_text(*entry.lift_low, 3) : QStringLiteral("—");
}

QList<LiftRow> lift_rows(const Entry& entry) {
    const auto p_text = [](const std::optional<double>& p) {
        if (!p)
            return QStringLiteral("—");
        return *p < 0.001 ? QStringLiteral("<0.001") : number_text(*p, 3);
    };
    const auto interval_text = [](const std::optional<Interval>& i) {
        if (!i || !i->low || !i->high)
            return QStringLiteral("—");
        return QStringLiteral("%1 – %2").arg(signed_text(*i->low, 3), signed_text(*i->high, 3));
    };
    QList<LiftRow> rows;
    for (const auto& l : entry.lifts) {
        const QString day_interval = interval_text(l.by_day);
        const QString days = l.by_day ? number_text(l.by_day->n, 0) : QStringLiteral("—");
        if (!l.gain) { // no bar close in common: nothing to measure or test
            rows.push_back({predictor_label(l.baseline), QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("0"),
                            p_text(l.p), p_text(l.q), QStringLiteral("—"), day_interval, days});
            continue;
        }
        const Interval& gain = *l.gain;
        QString verdict = QStringLiteral("—");
        if (l.significant)
            verdict = *l.significant ? QStringLiteral("통과") : QStringLiteral("미통과");
        rows.push_back({predictor_label(l.baseline), signed_text(gain.value, 3), interval_text(l.gain),
                        number_text(gain.n, 0), p_text(l.p), p_text(l.q), verdict, day_interval, days});
    }
    return rows;
}

QString slot_label(const QString& slot) {
    const QTime start = QTime::fromString(slot, "HH:mm");
    if (!start.isValid())
        return slot;
    return QStringLiteral("%1–%2").arg(start.addSecs(60).toString("HH:mm"), start.addSecs(30 * 60).toString("HH:mm"));
}

QList<CalibrationPoint> calibration_points(const QList<CalibrationBin>& bins) {
    const auto percent = [](double share) { return number_text(share * 100, 1) + "%"; };
    QList<CalibrationPoint> points;
    for (const auto& b : bins) {
        if (b.count == 0 || !b.mean_p || !b.observed)
            continue;
        const Interval& seen = *b.observed;
        QString tip = QStringLiteral("확률 %1–%2%: %3건\n평균 예측 %4, 실제 %5")
                          .arg(number_text(b.low * 100, 0), number_text(b.high * 100, 0), number_text(b.count, 0),
                               percent(*b.mean_p), percent(seen.value));
        if (seen.low && seen.high)
            tip += QStringLiteral(" (95% 구간 %1–%2)").arg(number_text(*seen.low * 100, 1), percent(*seen.high));
        points.push_back({*b.mean_p, seen.value, seen.low, seen.high, tip});
    }
    return points;
}

QList<CalibrationSeries> calibration_series(const AlgorithmStats& stats, const std::optional<KindInfo>& kind) {
    QList<QPair<QString, QString>> curves = {{"all", QStringLiteral("전체")},
                                             {"up", QStringLiteral("상승")},
                                             {"flat", QStringLiteral("보합")},
                                             {"down", QStringLiteral("하락")}};
    if (kind && kind->name != QLatin1String("direction")) { // another kind's classes (krx-quant #182)
        curves = {{"all", QStringLiteral("전체")}};
        for (qsizetype i = 0; i < kind->classes.size(); ++i)
            curves.append({kind->classes[i], i < kind->class_titles.size() ? kind->class_titles[i] : kind->classes[i]});
    }
    QList<CalibrationSeries> series;
    for (const auto& [key, name] : curves) {
        auto points = calibration_points(key == "all" ? stats.calibration : stats.calibration_by_class.value(key));
        if (points.isEmpty())
            continue;
        for (auto& point : points)
            point.tip = name + " · " + point.tip;
        series.push_back({key, name, points});
    }
    return series;
}

QString excluded_text(const Excluded& excluded, const QString& special) {
    static const QMap<QString, QString> kStatus = {{"vi", QStringLiteral("VI")},
                                                   {"no_book", QStringLiteral("호가 없음")},
                                                   {"timed_out", QStringLiteral("시간 초과")},
                                                   {"error", QStringLiteral("오류")},
                                                   {"no_members", QStringLiteral("구성원 없음")},
                                                   {"none_finished", QStringLiteral("결과 없음")},
                                                   {"no_previous", QStringLiteral("직전 창 없음")}};
    const bool included = special == "include";
    QStringList parts;
    if (excluded.special > 0 && !included)
        parts << QStringLiteral("특수 상황 %1").arg(excluded.special);
    if (excluded.unscorable > 0)
        parts << QStringLiteral("채점 불가 %1").arg(excluded.unscorable);
    for (auto it = excluded.statuses.begin(); it != excluded.statuses.end(); ++it)
        parts << QStringLiteral("%1 %2").arg(kStatus.value(it.key(), it.key())).arg(it.value());
    const QString left_out =
        parts.isEmpty() ? QStringLiteral("제외 없음") : QStringLiteral("제외: ") + parts.join(" · ");
    // Counted in, not left out: said apart.
    if (excluded.special > 0 && included)
        return QStringLiteral("특수 상황 포함 %1 · %2").arg(excluded.special).arg(left_out);
    return left_out;
}

QString days_text(const QStringList& days) {
    if (days.isEmpty())
        return QStringLiteral("예측이 있는 날 없음");
    if (days.size() == 1)
        return days.first();
    return QStringLiteral("%1 ~ %2 (%3일)").arg(days.first(), days.last()).arg(days.size());
}

QString ranking_rules(const QStringList& days, int min_sample, const std::optional<KindInfo>& kind) {
    if (kind && kind->name != QLatin1String("direction")) {
        QStringList action;
        for (const auto& c : kind->action)
            if (const qsizetype at = kind->classes.indexOf(c); at >= 0 && at < kind->class_titles.size())
                action << kind->class_titles[at];
        return QStringList{
            days_text(days),
            QStringLiteral("%1 예측을 %1 정답으로 센다. 정밀도와 커버리지는 %2(으)로 센다.")
                .arg(kind->title, action.join(QStringLiteral("·"))),
            QStringLiteral("순위에는 채점 %1건 이상, 기준 예측과의 짝도 그만큼 있어야 오른다.").arg(min_sample),
            QStringLiteral("순위는 가장 약한 lift의 하한 순이다."),
            luck_tip(*kind),
            QStringLiteral("줄을 두 번 누르면 그 예측의 상세 통계가 열린다."),
        }
            .join('\n');
    }
    return QStringList{
        days_text(days),
        QStringLiteral("순위에는 채점 %1건 이상, 기준 예측과의 짝도 그만큼 있어야 오른다.").arg(min_sample),
        QStringLiteral("순위는 가장 약한 lift의 하한 순이다."),
        QStringLiteral("운 보정 통과는 기준 예측 셋을 모두 이긴 것이 운으로 보기 어렵다는 뜻이다(q ≤ 0.05)."),
        QStringLiteral("줄을 두 번 누르면 그 예측의 상세 통계가 열린다."),
    }
        .join('\n');
}

QString luck_tip(const KindInfo& kind) {
    QStringList names;
    for (const auto& b : kind.baselines)
        names << predictor_label(b.name);
    return QStringLiteral("운 보정 통과는 기준 예측(%1)을 모두 이긴 것이 운으로 보기 어렵다는 뜻이다(q ≤ 0.05).")
        .arg(names.join(QStringLiteral(", ")));
}

QList<StatsColumn> kind_stats_columns(const KindInfo& kind) {
    QStringList action;
    for (const auto& c : kind.action)
        if (const qsizetype at = kind.classes.indexOf(c); at >= 0 && at < kind.class_titles.size())
            action << kind.class_titles[at];
    const QString called = action.join(QStringLiteral("·"));
    QList<StatsColumn> columns{
        {QStringLiteral("표본"), {}},
        {QStringLiteral("정답률"), {}},
        {called.isEmpty() ? QStringLiteral("정밀도") : QStringLiteral("%1 정밀도").arg(called),
         called.isEmpty() ? QString() : QStringLiteral("%1(이)라고 한 예측 중 맞은 비율이다.").arg(called)},
        {QStringLiteral("커버리지"),
         called.isEmpty() ? QString() : QStringLiteral("%1(이)라고 한 예측의 비율이다.").arg(called)},
        {QStringLiteral("Brier"), {}},
    };
    if (kind.name == QLatin1String("range"))
        columns << StatsColumn{QStringLiteral("구간 적중률"),
                               QStringLiteral("실제 변동폭이 q10~q90 안에 든 비율이다. 분위수가 정직하면 80% 가까이 "
                                              "나온다. 95% 구간(Wilson)은 칸의 툴팁에 있다.")}
                << StatsColumn{QStringLiteral("q50 오차(틱)"),
                               QStringLiteral("실제 변동폭과 q50의 차이(절댓값)의 평균, 틱이다.")};
    columns << StatsColumn{QStringLiteral("가장 약한 lift 하한"), {}};
    return columns;
}

QList<Shown> kind_stats_cells(const Entry& e, const KindInfo& kind) {
    QList<Shown> cells{{number_text(e.n, 0), {}},
                       rate_shown(e.accuracy),
                       rate_shown(e.precision),
                       rate_shown(e.coverage),
                       mean_shown(e.brier, 3)};
    if (kind.name == QLatin1String("range")) {
        cells << rate_shown(e.range ? e.range->interval_coverage : std::nullopt)
              << mean_shown(e.range ? e.range->q50_abs_error : std::nullopt, 2);
    }
    cells << Shown{weakest_lift_text(e), {}};
    return cells;
}

QList<QuantileRow> quantile_rows(const RangeStats& stats) {
    const auto plain = [](const std::optional<Interval>& i) {
        return i ? number_text(i->value, 2) : QStringLiteral("—");
    };
    const auto signed_ = [](const std::optional<Interval>& i) {
        return i ? signed_text(i->value, 2) : QStringLiteral("—");
    };
    const auto span = [](const std::optional<Interval>& i, bool sign) {
        if (!i || !i->low || !i->high)
            return QStringLiteral("—");
        return sign ? QStringLiteral("%1 – %2").arg(signed_text(*i->low, 2), signed_text(*i->high, 2))
                    : QStringLiteral("%1 – %2").arg(number_text(*i->low, 2), number_text(*i->high, 2));
    };
    QList<QuantileRow> rows;
    for (const char* q : {"q10", "q50", "q90"}) {
        const RangeMean pinball = stats.pinball.value(q);
        QuantileRow row{q,
                        plain(pinball.mean),
                        span(pinball.mean, false),
                        span(pinball.by_day, false),
                        pinball.by_day ? number_text(pinball.by_day->n, 0) : QStringLiteral("—"),
                        QStringLiteral("—"),
                        QStringLiteral("—")};
        if (stats.vs_climate) {
            const RangeMean versus = stats.vs_climate->value(q);
            row.vs_climate = signed_(versus.mean);
            row.vs_climate_interval = span(versus.by_day, true);
        }
        rows << row;
    }
    return rows;
}

QString range_summary(const RangeStats& stats) {
    if (stats.n == 0)
        return QStringLiteral("채점된 예측 없음");
    QStringList parts;
    if (const auto& c = stats.interval_coverage) {
        QString text = QStringLiteral("구간 적중률(q10~q90) %1%").arg(number_text(c->value * 100, 1));
        text += c->low && c->high ? QStringLiteral(" (95% %1–%2%, 이상 80%)")
                                        .arg(number_text(*c->low * 100, 1), number_text(*c->high * 100, 1))
                                  : QStringLiteral(" (이상 80%)");
        parts << text;
    }
    if (const auto& e = stats.q50_abs_error) {
        QString text = QStringLiteral("q50 오차 %1틱").arg(number_text(e->value, 2));
        if (e->low && e->high)
            text += QStringLiteral(" (95% %1 – %2)").arg(number_text(*e->low, 2), number_text(*e->high, 2));
        parts << text;
    }
    parts << QStringLiteral("표본 %1").arg(number_text(stats.n, 0));
    return parts.join(QStringLiteral(" · "));
}

QString column_tip(const QString& header) {
    static const QMap<QString, QString> tips = {
        {QStringLiteral("닿음 손익(틱, 연구용)"),
         QStringLiteral(
             "거래당 평균, 연구용 값이다. ▲는 봉 마감의 매도 1호가에 사서 위 목표에 닿으면 목표가, 아니면 "
             "4분 끝의 매수 1호가에 판다고 친다. ▼는 인버스를 가정해 반대로, ●는 거래하지 않는다. 비용(매도세와 "
             "수수료)을 뺀다. 체결과 잔량은 반영하지 않아 따라 했을 때의 손익이 아니다.")},
        {QStringLiteral("95% 구간"), QStringLiteral("봉 마감마다 따로 센 설명용 구간이다. 같은 날의 4분 정답은 서로 "
                                                    "겹쳐 실제보다 확실해 보인다. p도 그렇다.")},
        {QStringLiteral("거래일 95% 구간"), QStringLiteral("하루를 값 하나로 본 구간이다. 상태(수습·정식·벤치)는 "
                                                           "이것으로 정한다. 거래일이 2일보다 적으면 없다.")},
        {QStringLiteral("q"), QStringLiteral("순위표의 다른 lift와 함께 보정한 값이다. 순위 밖이거나 나눠 본 줄은 "
                                             "함께 검정하지 않아 없다.")},
        {QStringLiteral("운 보정"),
         QStringLiteral("기준 예측 셋을 모두 이긴 것이 운으로 보기 어렵다는 뜻이다(q ≤ 0.05).")},
    };
    return tips.value(header);
}

QString lift_tip(const AlgorithmStats& stats, const std::optional<KindInfo>& kind) {
    bool baseline = false; // another kind's baseline (krx-quant #182)
    if (kind)
        baseline = std::any_of(kind->baselines.cbegin(), kind->baselines.cend(),
                               [&stats](const KindBaseline& b) { return b.name == stats.algorithm; });
    QStringList lines;
    if (is_baseline(stats.algorithm) || baseline)
        lines << QStringLiteral("기준 예측 자신이라 lift가 없다.");
    else if (lift_rows(stats.overall).isEmpty())
        lines << QStringLiteral("기준 예측과 짝지을 채점된 예측이 없어 lift가 없다.");
    lines << QStringLiteral("lift는 같은 종목·봉 마감의 기준 예측과 정답(1/0) 차이의 평균이다. 열 제목에 마우스를 "
                            "올리면 각 구간의 뜻이 나온다.");
    return lines.join('\n');
}

QString calibration_tip(const AlgorithmStats& stats, const std::optional<KindInfo>& kind) {
    QStringList lines;
    if (calibration_series(stats, kind).isEmpty())
        lines << QStringLiteral("채점된 예측이 없다.");
    lines << QStringLiteral("확률을 10% 구간으로 나눠, 예측한 확률과 실제로 일어난 비율을 견준다.");
    if (kind && kind->name != QLatin1String("direction"))
        lines << QStringLiteral("전체는 세 클래스를 합친 것이라 클래스별 오차를 숨길 수 있어, %1을 따로 그린다.")
                     .arg(kind->class_titles.join(QStringLiteral("·")));
    else
        lines << QStringLiteral(
            "전체는 세 클래스를 합친 것이라 방향별 오차를 숨길 수 있어, 상승·보합·하락을 따로 그린다.");
    lines << QStringLiteral("점이 점선에 가까울수록 확률을 믿을 수 있다.");
    return lines.join('\n');
}

QString period_label(const QString& period) {
    static const QMap<QString, QString> labels = {
        {"today", QStringLiteral("오늘")}, {"5d", QStringLiteral("5일")}, {"20d", QStringLiteral("20일")}};
    return labels.value(period, period);
}

QString query_text(const AlgorithmStats& stats) {
    return QStringList{period_label(stats.period), stats.symbol == "all" ? QStringLiteral("전체 종목") : stats.symbol,
                       stats.special == "include" ? QStringLiteral("특수 상황 포함") : QStringLiteral("특수 상황 제외")}
        .join(QStringLiteral(" · "));
}

namespace {

/// "name — its meaning (unit · when known)", or the name alone when the engine gave no meaning.
QString meant(const QString& name, const Meaning& m) {
    if (m.meaning.isEmpty())
        return name;
    QStringList notes;
    if (!m.unit.isEmpty())
        notes << m.unit;
    if (!m.known.isEmpty())
        notes << m.known;
    return notes.isEmpty() ? QStringLiteral("%1 — %2").arg(name, m.meaning)
                           : QStringLiteral("%1 — %2 (%3)").arg(name, m.meaning, notes.join(QStringLiteral(" · ")));
}

} // namespace

QList<QPair<QString, QString>> about_rows(const std::optional<AlgorithmInfo>& info, const QString& failure) {
    using Row = QPair<QString, QString>;
    if (!failure.isEmpty())
        return {Row(QStringLiteral("설명"), QStringLiteral("엔진에서 설명을 읽지 못했습니다: %1").arg(failure))};
    if (!info)
        return {Row(QStringLiteral("설명"), QStringLiteral("엔진이 아직 설명을 주지 않음"))};
    QList<Row> rows;
    const auto& card = info->card;
    if (card) {
        rows << Row(QStringLiteral("성격"), card->scientist.isEmpty()
                                                ? card->character
                                                : QStringLiteral("%1 (%2)").arg(card->character, card->scientist));
        rows << Row(QStringLiteral("예측 로직"), card->logic);
        if (!card->forecasting.isEmpty())
            rows << Row(QStringLiteral("예측 방식"), card->forecasting);
        if (!card->failure.isEmpty())
            rows << Row(QStringLiteral("실패 조건"), card->failure);
    }
    if (info->kind != QLatin1String("algorithm")) { // a baseline or the consensus: its card only
        if (card && !card->training.isEmpty())
            rows << Row(QStringLiteral("학습 방법"), card->training);
        if (rows.isEmpty())
            rows << Row(QStringLiteral("설명"), QStringLiteral("엔진이 아직 설명을 주지 않음"));
        return rows;
    }
    if (card && !card->unique_features.isEmpty())
        rows << Row(QStringLiteral("고유 피처"), card->unique_features.join(", "));
    if (!info->features.isEmpty()) {
        if (!info->feature_meanings) { // an older engine: the names only
            rows << Row(QStringLiteral("입력 피처"), info->features.join(", "));
        } else {
            QStringList lines;
            for (const auto& name : info->features)
                lines << meant(name, info->feature_meanings->value(name));
            rows << Row(QStringLiteral("입력 피처"), lines.join("\n"));
        }
    }
    if (card && !card->training.isEmpty())
        rows << Row(QStringLiteral("학습 방법"), card->training);
    if (info->training_days)
        rows << Row(QStringLiteral("학습 기간"), QStringLiteral("최근 %1거래일").arg(*info->training_days));
    if (!info->model_version.isEmpty())
        rows << Row(QStringLiteral("모델"), info->model_version);
    QString trained = info->trained.isEmpty() ? QStringLiteral("학습 기록 없음 (기본값)") : info->trained;
    if (!info->trained_until.isEmpty())
        trained += QStringLiteral(" · %1까지의 데이터").arg(info->trained_until);
    rows << Row(QStringLiteral("학습 결과"), trained);
    if (!info->params.isEmpty()) {
        QStringList params;
        for (const auto& [name, value] : info->params) {
            const QString set = QStringLiteral("%1 = %2").arg(name, value);
            params << (info->param_meanings ? meant(set, info->param_meanings->value(name)) : set);
        }
        rows << Row(QStringLiteral("파라미터"),
                    params.join(info->param_meanings ? QStringLiteral("\n") : QStringLiteral(", ")));
    }
    if (!info->status.isEmpty()) {
        QString status = standing_label(info->status);
        if (info->since.isValid())
            status += QStringLiteral(" · %1부터").arg(kst_text(info->since, "yyyy-MM-dd HH:mm"));
        rows << Row(QStringLiteral("상태"), status);
    }
    if (!info->history.isEmpty()) {
        QStringList lines;
        for (const auto& h : info->history) {
            const QString change = h.from.isEmpty()
                                       ? standing_label(h.to)
                                       : QStringLiteral("%1 → %2").arg(standing_label(h.from), standing_label(h.to));
            lines << (h.reason.isEmpty()
                          ? QStringLiteral("%1 %2").arg(kst_text(h.at, "MM-dd HH:mm"), change)
                          : QStringLiteral("%1 %2 (%3)").arg(kst_text(h.at, "MM-dd HH:mm"), change, h.reason));
        }
        rows << Row(QStringLiteral("상태 이력"), lines.join("\n"));
    }
    return rows;
}

QList<QPair<QString, QString>> learning_rows(const std::optional<AlgorithmInfo>& info) {
    using Row = QPair<QString, QString>;
    if (!info || info->kind != QLatin1String("algorithm"))
        return {};
    QList<Row> rows;
    if (const auto& t = info->training) {
        QStringList parts{QStringLiteral("%1 – %2").arg(t->first_day, t->last_day),
                          QStringLiteral("표본 %1").arg(number_text(t->samples, 0))};
        if (t->recorded_days)
            parts << QStringLiteral("녹화 호가 %1일").arg(*t->recorded_days);
        if (t->bar_days)
            parts << QStringLiteral("1분봉 %1일").arg(*t->bar_days);
        rows << Row(QStringLiteral("학습 데이터"), parts.join(QStringLiteral(" · ")));
        rows << Row(QStringLiteral("학습 손실"),
                    QStringLiteral("%1 (학습 데이터로 잰 값)").arg(number_text(t->loss, 4)));
    } else {
        rows << Row(QStringLiteral("학습 데이터"),
                    QStringLiteral("엔진이 주지 않음 (기본값이거나 이전에 저장한 모델)"));
    }
    const auto model = [](const std::optional<ValidatedModel>& m, const QString& none) {
        if (!m)
            return none;
        QString text = m->version;
        text += m->loss ? QStringLiteral(" · 검증 손실 %1").arg(number_text(*m->loss, 4))
                        : QStringLiteral(" · 검증 손실 없음");
        if (m->trained_on_validation.value_or(false))
            text += QStringLiteral(" · 검증일로 학습한 모델");
        return text;
    };
    if (const auto& v = info->validation) {
        QStringList parts{QStringLiteral("%1 밤").arg(v->day)};
        if (!v->validation_days.isEmpty())
            parts << QStringLiteral("검증일 %1").arg(v->validation_days.join(", "));
        if (v->samples)
            parts << QStringLiteral("표본 %1").arg(number_text(*v->samples, 0));
        rows << Row(QStringLiteral("지난밤 검증"), parts.join(QStringLiteral(" · ")));
        rows << Row(QStringLiteral("새 모델"), model(v->challenger, QStringLiteral("학습하지 않음")));
        rows << Row(QStringLiteral("지금 모델"), model(v->champion, QStringLiteral("검증할 데이터 없음")));
        const QString verdict = v->promoted ? QStringLiteral("승격함") : QStringLiteral("승격하지 않음");
        rows << Row(QStringLiteral("승격"),
                    v->reason.isEmpty() ? verdict : QStringLiteral("%1 · %2").arg(verdict, v->reason));
    } else {
        rows << Row(QStringLiteral("지난밤 검증"), QStringLiteral("밤 학습 리포트 없음"));
    }
    if (const auto& st = info->stage) {
        QString text = QStringLiteral("%1부터 · %2거래일 셈").arg(st->began).arg(st->days);
        if (st->waiting) {
            // Nothing counted until a model trained on the touch target runs (krx-quant #170).
            text += QStringLiteral(" · 새 정답으로 학습한 모델을 기다림");
        } else if (st->next_review) {
            text += st->next_review_on.isEmpty()
                        ? QStringLiteral(" · 다음 판정 %1일째").arg(*st->next_review)
                        : QStringLiteral(" · 다음 판정 %1일째 (%2 예정)").arg(*st->next_review).arg(st->next_review_on);
        } else {
            text += QStringLiteral(" · 마지막 판정이 지났다");
        }
        rows << Row(QStringLiteral("%1 단계").arg(standing_label(st->status)), text);
        if (const auto& w = st->weakest; w && w->value && w->low) {
            QString lift = QStringLiteral("%1 대비 %2 · 거래일 하한 %3 (%4일")
                               .arg(w->baseline, signed_text(*w->value, 3), signed_text(*w->low, 3))
                               .arg(w->n);
            if (st->level)
                lift += QStringLiteral(", 신뢰 %1%").arg(number_text(*st->level * 100, 1));
            rows << Row(QStringLiteral("가장 약한 lift"), lift + ")");
        } else {
            rows << Row(QStringLiteral("가장 약한 lift"),
                        QStringLiteral("아직 없음 (기준 예측마다 거래일 2일 이상 필요)"));
        }
    }
    return rows;
}

} // namespace fincept::krx_quant
