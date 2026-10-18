#include "services/krx_quant/StatsView.h"

#include <QLocale>
#include <QTime>

#include <algorithm>
#include <cmath>
#include <utility>

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
        QStringList lines{
            days_text(days),
            QStringLiteral("%1 예측을 %1 정답으로 센다. 정밀도와 커버리지는 %2(으)로 센다.")
                .arg(kind->title, action.join(QStringLiteral("·"))),
        };
        if (kind->daily) { // once a trading day (krx-quant #264)
            QStringList judged;
            for (const int d : kind->daily->probation_days)
                judged << QString::number(d);
            lines << QStringLiteral("하루 한 번 예측이다. 채점 한 건은 종목 하나의 하루이고, 기간의 날은 예측이 있는 "
                                    "최근 거래일이다. 그날 정답은 KRX 일봉이 저장된 뒤(%1부터)라 오늘은 밤까지 비어 "
                                    "있다.")
                         .arg(kind->daily->answer_from.isEmpty() ? QStringLiteral("20:35") : kind->daily->answer_from);
            if (!judged.isEmpty())
                lines << QStringLiteral("상태(수습·벤치)는 순위와 따로, 정답이 있는 날 %1일째에 판정한다(최근 %2일).")
                             .arg(judged.join(QStringLiteral("·")))
                             .arg(kind->daily->recent_days);
        }
        lines << QStringLiteral("순위에는 채점 %1건 이상, 기준 예측과의 짝도 그만큼 있어야 오른다.").arg(min_sample)
              << QStringLiteral("순위는 가장 약한 lift의 하한 순이다.") << luck_tip(*kind)
              << QStringLiteral("줄을 두 번 누르면 그 예측의 상세 통계가 열린다.");
        return lines.join('\n');
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

QString special_tip(const std::optional<KindInfo>& kind) {
    if (kind && kind->daily) // a day (krx-quant #264)
        return QStringLiteral("그날 서킷브레이커·사이드카가 있던 날의 예측도 센다(기본은 뺀다). VI는 특수 상황이 "
                              "아니다(그날 그 종목의 VI 수는 정답에 남는다).");
    return QStringLiteral("VI·서킷브레이커·사이드카가 4분 구간에 낀 예측도 센다(기본은 뺀다).");
}

QString lift_header_tip(const QString& header, const std::optional<KindInfo>& kind) {
    if (kind && kind->daily) { // a symbol's day each (krx-quant #264)
        if (header == QStringLiteral("95% 구간"))
            return QStringLiteral(
                "예측(종목 × 날)마다 따로 센 설명용 구간이다. 같은 날의 종목들은 함께 움직여 실제보다 "
                "확실해 보일 수 있다. p도 그렇다.");
        if (header == QStringLiteral("짝"))
            return QStringLiteral("같은 종목·날에 둘 다 채점된 예측의 수다.");
    }
    return column_tip(header);
}

QString daily_pnl_header(const KindInfo& kind) {
    return QStringLiteral("%1 손익(bp, 연구용)").arg(kind.title);
}

QString daily_pnl_tip() {
    return QStringLiteral(
        "거래당 평균, 시가 대비 bp, 연구용 값이다. 상승 예측은 시가 단일가에 사서 종가 단일가에 판다고 친다. 하락은 "
        "주식 공매도로 반대로, 보합은 거래하지 않는다. 비용(매도세와 수수료 두 번)을 뺀다. 두 끝이 단일가라 스프레드는 "
        "없다. 칸의 툴팁과 상세 창에는 하락을 그 종목의 인버스 상품으로 친 값(매도세 없음)도 있다. 인버스가 없는 "
        "종목(삼성전기)과 상장(2026-05-27) 전의 하락은 빠진다. 보합 폭에는 비용을 넣지 않았다.");
}

QList<StatsColumn> kind_stats_columns(const KindInfo& kind) {
    QStringList action;
    for (const auto& c : kind.action)
        if (const qsizetype at = kind.classes.indexOf(c); at >= 0 && at < kind.class_titles.size())
            action << kind.class_titles[at];
    const QString called = action.join(QStringLiteral("·"));
    QList<StatsColumn> columns{
        kind.daily
            ? StatsColumn{QStringLiteral("표본(종목·일)"),
                          QStringLiteral("채점된 예측 수다. 하루 한 번 예측이라 종목마다 하루에 하나다(종목 × 날).")}
            : StatsColumn{QStringLiteral("표본"), {}},
        {QStringLiteral("정답률"), {}},
        {called.isEmpty() ? QStringLiteral("정밀도") : QStringLiteral("%1 정밀도").arg(called),
         called.isEmpty() ? QString() : QStringLiteral("%1(이)라고 한 예측 중 맞은 비율이다.").arg(called)},
        {QStringLiteral("커버리지"),
         called.isEmpty() ? QString() : QStringLiteral("%1(이)라고 한 예측의 비율이다.").arg(called)},
        {QStringLiteral("Brier"), {}},
    };
    if (is_barrier_kind(kind.name)) // its barrier P&L (krx-quant #185; the ladder's too, #263)
        columns << StatsColumn{QString::fromUtf8(kBarrierPnlHeader), column_tip(QString::fromUtf8(kBarrierPnlHeader))};
    if (kind.daily) // its open-to-close P&L (krx-quant #264)
        columns << StatsColumn{daily_pnl_header(kind), daily_pnl_tip()};
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
    if (is_barrier_kind(kind.name))
        cells << barrier_pnl_shown(e.barrier);
    if (kind.daily)
        cells << daily_pnl_shown(e.open_to_close);
    if (kind.name == QLatin1String("range")) {
        cells << rate_shown(e.range ? e.range->interval_coverage : std::nullopt)
              << mean_shown(e.range ? e.range->q50_abs_error : std::nullopt, 2);
    }
    cells << Shown{weakest_lift_text(e), {}};
    return cells;
}

namespace {

/// "인버스 상품으로 하락 12건 · 전체 40건 평균 0.31틱 (합계 12.4틱)": a P&L through the inverse product.
QString inverse_text(const QString& what, const BarrierPnl& pnl) {
    QString text = QStringLiteral("%1 %2건").arg(what, number_text(pnl.trades, 0));
    if (pnl.per_trade)
        text += QStringLiteral(" 평균 %1틱 (합계 %2틱)")
                    .arg(number_text(pnl.per_trade->value, 2), number_text(pnl.total_ticks, 1));
    return text;
}

} // namespace

Shown pnl_shown(const Entry& entry) {
    Shown shown = mean_shown(entry.per_trade, 2);
    QStringList tip{QStringLiteral("거래 %1건, 합계 %2틱 (하락은 주식 공매도로 친 값)")
                        .arg(number_text(entry.trades, 0), number_text(entry.total_ticks, 1))};
    if (!shown.tip.isEmpty())
        tip << shown.tip;
    if (entry.inverse)
        tip << inverse_text(QStringLiteral("하락을 인버스 상품으로 친 값: 거래"), *entry.inverse) +
                   QStringLiteral(". 매도세가 없다. 인버스가 없는 종목(삼성전기)과 상장(2026-05-27) 전의 하락은 빠진다.");
    shown.tip = tip.join('\n');
    return shown;
}

Shown barrier_pnl_shown(const std::optional<BarrierStats>& stats) {
    if (!stats)
        return {QStringLiteral("—"), {}};
    Shown shown = mean_shown(stats->pnl.per_trade, 2);
    QStringList tip{QStringLiteral("거래 %1건, 합계 %2틱")
                        .arg(number_text(stats->pnl.trades, 0), number_text(stats->pnl.total_ticks, 1))};
    if (!shown.tip.isEmpty())
        tip << shown.tip;
    const auto side = [](const QString& name, const BarrierPnl& pnl) {
        return pnl.per_trade ? QStringLiteral("%1 %2건 평균 %3틱")
                                   .arg(name, number_text(pnl.trades, 0), number_text(pnl.per_trade->value, 2))
                             : QStringLiteral("%1 %2건").arg(name, number_text(pnl.trades, 0));
    };
    tip << side(QStringLiteral("롱(위 먼저)"), stats->longs) + " · " +
               side(QStringLiteral("숏(아래 먼저, 주식 공매도)"), stats->shorts);
    if (stats->pnl_inverse && stats->shorts_inverse) // krx-quant #267
        tip << inverse_text(QStringLiteral("숏을 인버스 상품으로 친 값: 전체"), *stats->pnl_inverse) + " · " +
                   inverse_text(QStringLiteral("숏"), *stats->shorts_inverse);
    shown.tip = tip.join('\n');
    return shown;
}

QList<BarrierPnlRow> barrier_pnl_rows(const BarrierStats& stats) {
    QList<BarrierPnlRow> rows;
    QList<std::pair<QString, BarrierPnl>> sides{{QStringLiteral("전체"), stats.pnl},
                                                {QStringLiteral("롱(위 먼저)"), stats.longs},
                                                {QStringLiteral("숏(아래 먼저)"), stats.shorts}};
    if (stats.shorts_inverse) // the shorts through the inverse product (krx-quant #267)
        sides.push_back({QStringLiteral("숏(인버스 상품)"), *stats.shorts_inverse});
    for (const auto& [side, pnl] : sides) {
        BarrierPnlRow row{side, number_text(pnl.trades, 0), number_text(pnl.total_ticks, 1), QStringLiteral("—"),
                          QStringLiteral("—")};
        if (const auto& mean = pnl.per_trade) {
            row.per_trade = number_text(mean->value, 2);
            if (mean->low && mean->high)
                row.interval = QStringLiteral("%1 – %2").arg(number_text(*mean->low, 2), number_text(*mean->high, 2));
        }
        rows << row;
    }
    return rows;
}

QString barrier_summary(const BarrierStats& stats) {
    if (stats.n == 0)
        return QStringLiteral("채점된 예측 없음");
    if (stats.pnl.trades == 0)
        return QStringLiteral("채점 %1개 · 거래 없음 (모두 시간 만료 예측)").arg(number_text(stats.n, 0));
    QStringList parts{QStringLiteral("채점 %1개 중 거래 %2건 (롱 %3 · 숏 %4)")
                          .arg(number_text(stats.n, 0), number_text(stats.pnl.trades, 0),
                               number_text(stats.longs.trades, 0), number_text(stats.shorts.trades, 0))};
    if (const auto& mean = stats.pnl.per_trade) {
        QString text = QStringLiteral("거래당 %1틱").arg(number_text(mean->value, 2));
        if (mean->low && mean->high)
            text += QStringLiteral(" (95% %1 – %2)").arg(number_text(*mean->low, 2), number_text(*mean->high, 2));
        parts << text;
    }
    parts << QStringLiteral("합계 %1틱").arg(number_text(stats.pnl.total_ticks, 1));
    return parts.join(QStringLiteral(" · "));
}

namespace {

/// "인버스 상품으로 하락 12건 평균 6.11bp (합계 116.2bp)": an open-to-close P&L through the inverse product.
QString daily_inverse_text(const QString& what, const DailyPnl& pnl) {
    QString text = QStringLiteral("%1 %2건").arg(what, number_text(pnl.trades, 0));
    if (pnl.per_trade)
        text += QStringLiteral(" 평균 %1bp (합계 %2bp)")
                    .arg(number_text(pnl.per_trade->value, 2), number_text(pnl.total_bp, 1));
    return text;
}

} // namespace

Shown daily_pnl_shown(const std::optional<OpenToCloseStats>& stats) {
    if (!stats)
        return {QStringLiteral("—"), {}};
    Shown shown = mean_shown(stats->pnl.per_trade, 2);
    QStringList tip{QStringLiteral("거래 %1건, 합계 %2bp")
                        .arg(number_text(stats->pnl.trades, 0), number_text(stats->pnl.total_bp, 1))};
    if (!shown.tip.isEmpty())
        tip << shown.tip;
    const auto side = [](const QString& name, const DailyPnl& pnl) {
        return pnl.per_trade ? QStringLiteral("%1 %2건 평균 %3bp")
                                   .arg(name, number_text(pnl.trades, 0), number_text(pnl.per_trade->value, 2))
                             : QStringLiteral("%1 %2건").arg(name, number_text(pnl.trades, 0));
    };
    tip << side(QStringLiteral("롱(상승)"), stats->longs) + " · " +
               side(QStringLiteral("숏(하락, 주식 공매도)"), stats->shorts);
    if (stats->pnl_inverse && stats->shorts_inverse) // krx-quant #267
        tip << daily_inverse_text(QStringLiteral("하락을 인버스 상품으로 친 값: 전체"), *stats->pnl_inverse) + " · " +
                   daily_inverse_text(QStringLiteral("숏"), *stats->shorts_inverse);
    shown.tip = tip.join('\n');
    return shown;
}

QList<BarrierPnlRow> daily_pnl_rows(const OpenToCloseStats& stats) {
    QList<BarrierPnlRow> rows;
    QList<std::pair<QString, DailyPnl>> sides{{QStringLiteral("전체"), stats.pnl},
                                              {QStringLiteral("롱(상승)"), stats.longs},
                                              {QStringLiteral("숏(하락)"), stats.shorts}};
    if (stats.shorts_inverse) // the shorts through the inverse product (krx-quant #267)
        sides.push_back({QStringLiteral("숏(인버스 상품)"), *stats.shorts_inverse});
    for (const auto& [side, pnl] : sides) {
        BarrierPnlRow row{side, number_text(pnl.trades, 0), number_text(pnl.total_bp, 1), QStringLiteral("—"),
                          QStringLiteral("—")};
        if (const auto& mean = pnl.per_trade) {
            row.per_trade = number_text(mean->value, 2);
            if (mean->low && mean->high)
                row.interval = QStringLiteral("%1 – %2").arg(number_text(*mean->low, 2), number_text(*mean->high, 2));
        }
        rows << row;
    }
    return rows;
}

QString daily_pnl_summary(const OpenToCloseStats& stats) {
    if (stats.n == 0)
        return QStringLiteral("채점된 예측 없음");
    if (stats.pnl.trades == 0)
        return QStringLiteral("채점 %1개 · 거래 없음 (모두 보합 예측)").arg(number_text(stats.n, 0));
    QStringList parts{QStringLiteral("채점 %1개 중 거래 %2건 (롱 %3 · 숏 %4)")
                          .arg(number_text(stats.n, 0), number_text(stats.pnl.trades, 0),
                               number_text(stats.longs.trades, 0), number_text(stats.shorts.trades, 0))};
    if (const auto& mean = stats.pnl.per_trade) {
        QString text = QStringLiteral("거래당 %1bp").arg(number_text(mean->value, 2));
        if (mean->low && mean->high)
            text += QStringLiteral(" (95% %1 – %2)").arg(number_text(*mean->low, 2), number_text(*mean->high, 2));
        parts << text;
    }
    parts << QStringLiteral("합계 %1bp").arg(number_text(stats.pnl.total_bp, 1));
    return parts.join(QStringLiteral(" · "));
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
             "4분 끝의 매수 1호가에 판다고 친다. ▼는 주식 공매도로 반대로, ●는 거래하지 않는다. 비용(매도세와 "
             "수수료)을 뺀다. 칸의 툴팁에는 ▼를 그 종목의 인버스 상품으로 친 값(매도세 없음)도 있다. 체결과 잔량은 "
             "반영하지 않아 따라 했을 때의 손익이 아니다.")},
        {QString::fromUtf8(kBarrierPnlHeader),
         QStringLiteral(
             "거래당 평균, 연구용 값이다. 위 먼저는 봉 마감의 매도 1호가에 사서 위 장벽 U에 먼저 닿으면 U에, 아래 "
             "장벽 L에 먼저 닿으면 그때의 매수 1호가에, 어느 쪽에도 닿지 않으면 창 끝(4분, 15·30·60분 장벽은 "
             "그 분)의 매수 1호가에 판다고 친다. "
             "아래 먼저는 주식 공매도로 반대로, 시간 만료는 거래하지 않는다. 비용(매도세와 수수료)을 뺀다. 칸의 "
             "툴팁과 상세 창에는 숏을 그 종목의 인버스 상품으로 친 값(매도세 없음)도 있다. 체결과 잔량은 반영하지 "
             "않아 따라 했을 때의 손익이 아니다.")},
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
    lines << QStringLiteral("lift는 같은 종목·%1의 기준 예측과 정답(1/0) 차이의 평균이다. 열 제목에 마우스를 "
                            "올리면 각 구간의 뜻이 나온다.")
                 .arg(kind && kind->daily ? QStringLiteral("날") : QStringLiteral("봉 마감")); // a day (krx-quant #264)
    return lines.join('\n');
}

namespace {

/// The object particle after ``word``: "을" after a final consonant ("큼을"), else "를" ("먼저를").
QString object_particle(const QString& word) {
    const char16_t last = word.isEmpty() ? u'\0' : word.back().unicode();
    const bool syllable = last >= 0xAC00 && last <= 0xD7A3;
    return syllable && (last - 0xAC00) % 28 == 0 ? QStringLiteral("를") : QStringLiteral("을");
}

} // namespace

QString calibration_tip(const AlgorithmStats& stats, const std::optional<KindInfo>& kind) {
    QStringList lines;
    if (calibration_series(stats, kind).isEmpty())
        lines << QStringLiteral("채점된 예측이 없다.");
    lines << QStringLiteral("확률을 10% 구간으로 나눠, 예측한 확률과 실제로 일어난 비율을 견준다.");
    if (kind && kind->name != QLatin1String("direction")) {
        const QString classes = kind->class_titles.join(QStringLiteral("·"));
        lines << QStringLiteral("전체는 세 클래스를 합친 것이라 클래스별 오차를 숨길 수 있어, %1%2 따로 그린다.")
                     .arg(classes, object_particle(classes));
    } else {
        lines << QStringLiteral(
            "전체는 세 클래스를 합친 것이라 방향별 오차를 숨길 수 있어, 상승·보합·하락을 따로 그린다.");
    }
    lines << QStringLiteral("점이 점선에 가까울수록 확률을 믿을 수 있다.");
    return lines.join('\n');
}

QString period_label(const QString& period) {
    static const QMap<QString, QString> labels = {
        {"today", QStringLiteral("오늘")}, {"5d", QStringLiteral("5일")}, {"20d", QStringLiteral("20일")}};
    return labels.value(period, period);
}

QList<SkillCell> skill_cells(const Skill& skill) {
    const auto percent = [](double rate) { return number_text(rate * 100, 1) + "%"; };
    const auto score = [](const std::optional<double>& v) {
        if (!v)
            return QStringLiteral("—");
        const double shown = std::round(*v * 1000) / 1000;
        return signed_text(shown == 0 ? 0.0 : shown, 3); // no "-0.000"
    };
    QString independence = QStringLiteral("—");
    QString independence_tip = QStringLiteral(
        "예측한 클래스와 일어난 클래스가 서로 독립이라는 가설의 Pearson χ² 검정이다(독립 관측일 때의 "
        "Pesaran–Timmermann 2009 다범주 방향 검정과 같다). p가 작을수록 예측이 결과와 함께 움직인다. 4분 정답은 "
        "겹치고 종목들은 같이 움직여 p가 실제보다 작다. 설명용이다.");
    if (skill.p && skill.chi2)
        independence = QStringLiteral("p %1 (χ² %2, 자유도 %3)")
                           .arg(*skill.p < 0.001 ? QStringLiteral("<0.001") : number_text(*skill.p, 3),
                                number_text(*skill.chi2, 2))
                           .arg(skill.df);
    else if (skill.df == 0)
        independence_tip += QStringLiteral("\n예측이나 결과가 한 클래스뿐이라 검정할 수 없다.");
    else
        independence_tip += QStringLiteral("\n기대 빈도가 5보다 작은 칸이 있어 검정하지 않는다(Cochran의 규칙: 드문 "
                                           "클래스를 두어 번 맞힌 것만으로 매우 유의하게 나오지 않게).");
    QString kappa_tip = QStringLiteral("Cohen의 kappa = (정답률 − 기대 정답률) ÷ (1 − 기대 정답률). 늘 보합은 0, "
                                       "무작위는 기대값 0, 완벽은 1이다. 음수면 기대보다 못하다.");
    if (!skill.kappa)
        kappa_tip += QStringLiteral("\n모든 예측과 결과가 한 클래스라 낼 수 없다.");
    // Kappa by trading day (krx-quant #278): what the statuses' unit would say of it.
    QString day_kappa = QStringLiteral("—");
    QString day_kappa_tip = QStringLiteral(
        "거래일 단위 kappa. 날마다 그날 예측의 kappa를 내고, 날들의 평균에 t분포 95% 구간을 낸다(lift의 거래일 "
        "구간과 같은 방식). 4분 정답은 날 안에서 겹쳐 독립성 검정의 p가 너무 작지만, 날끼리는 훨씬 독립에 가깝다. "
        "구간 하한이 0을 넘으면 날이 바뀌어도 기준율보다 낫다고 말할 수 있다. 설명용이다.");
    if (const auto& d = skill.kappa_by_day) {
        day_kappa = score(d->value);
        if (d->low && d->high)
            day_kappa += QStringLiteral(" (%1 – %2)").arg(score(d->low), score(d->high));
        day_kappa_tip += QStringLiteral("\n%1일").arg(d->n);
        if (skill.kappa_by_day_p)
            day_kappa_tip += QStringLiteral(" · 한쪽 p %1")
                                 .arg(*skill.kappa_by_day_p < 0.001 ? QStringLiteral("<0.001")
                                                                    : number_text(*skill.kappa_by_day_p, 3));
    } else {
        day_kappa_tip += QStringLiteral("\nkappa가 있는 거래일이 2일보다 적거나(오늘만, 한 클래스뿐인 날은 빠진다) "
                                        "엔진이 주지 않는다.");
    }
    QString gerrity_tip =
        QStringLiteral("Gerrity(1992) 점수. 하락 < 보합 < 상승의 순서를 보고, 두 칸 틀림(상승에 하락)을 한 칸 "
                       "틀림보다 더 깎는다(kappa는 그 차이를 보지 않는다). 어떤 상수 예측도 0, 무작위는 기대값 0, "
                       "완벽은 1이다.");
    if (!skill.gerrity)
        gerrity_tip += QStringLiteral("\n상승이나 하락이 한 번도 일어나지 않아 낼 수 없다.");
    return {
        {QStringLiteral("기대 정답률"), percent(skill.expected_accuracy),
         QStringLiteral("같은 비율로 상승·보합·하락을 결과와 상관없이 냈을 때의 정답률: Σ P(그 클래스를 냄)·P(그 "
                        "클래스가 일어남). 정답률이 이보다 높아야 결과를 보고 낸 것이다.")},
        {QStringLiteral("기대 대비"), signed_text((skill.accuracy - skill.expected_accuracy) * 100, 1) + "%p",
         QStringLiteral("정답률 − 기대 정답률. 양수면 상승·보합·하락의 비율만으로 나오는 것보다 더 맞혔다.")},
        {QStringLiteral("kappa"), score(skill.kappa), kappa_tip},
        {QStringLiteral("거래일 kappa"), day_kappa, day_kappa_tip},
        {QStringLiteral("Gerrity"), score(skill.gerrity), gerrity_tip},
        {QStringLiteral("균형 정답률"), percent(skill.balanced_accuracy),
         QStringLiteral("일어난 클래스마다 그것을 맞힌 비율의 평균. 세 클래스가 다 일어났으면 늘 보합은 33.3%다.")},
        {QStringLiteral("독립성 검정"), independence, independence_tip},
    };
}

QString skill_tip() {
    return QStringLiteral(
        "기준율 대비 실력(krx-quant #262). 보합이 많은 날에는 늘 보합도 정답률이 높다. 여기서는 위 정답률이 상승·"
        "보합·하락의 비율만으로 나오는 정답률보다 나은지 본다. 예측한 클래스 × 일어난 클래스 표(3 × 3)에서 낸다. "
        "설명용이고 상태 판정과 순위는 기준 예측 대비 lift로 한다. 각 열의 뜻은 열 제목이나 값에 마우스를 올리면 "
        "나온다.");
}

QString volatility_label(const QString& tercile) {
    if (tercile == QLatin1String("low"))
        return QStringLiteral("낮음");
    if (tercile == QLatin1String("mid"))
        return QStringLiteral("중간");
    if (tercile == QLatin1String("high"))
        return QStringLiteral("높음");
    return tercile;
}

QString volatility_tip(const AlgorithmStats& stats) {
    QString tip = QStringLiteral(
        "봉 마감의 변동성 3분위별 성적(krx-quant #262). 변동성은 그 봉 마감에 장벽 종류가 저장한 폭 w(1분 변화의 "
        "지수가중 표준편차 × √4, k보다 좁지 않다)를 중간가의 bp로 바꾼 값이다. 종목마다 그 기간 모든 봉 마감의 w로 "
        "자르므로 예측자마다 같은 봉 마감이 같은 칸에 든다. 변동성이 크면 4분 안에 ±k에 닿기 쉬워, 변동성만 맞혀도 "
        "보합과 움직임을 가를 수 있다. 적중이 어느 변동성에서 나오는지 본다. 이 줄들의 lift는 함께 검정하지 않는다.");
    if (stats.volatility_cuts.isEmpty())
        return tip + QStringLiteral("\n장벽 종류가 폭을 저장한 봉 마감이 없어 나누지 못했다.");
    for (auto it = stats.volatility_cuts.begin(); it != stats.volatility_cuts.end(); ++it) {
        if (it.value().size() < 2)
            continue;
        tip += QStringLiteral("\n%1: 낮음 ≤ %2bp < 중간 ≤ %3bp < 높음")
                   .arg(it.key(), number_text(it.value()[0], 1), number_text(it.value()[1], 1));
    }
    return tip;
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
            // A review that counted simulated days (krx-quant #298) says so before the engine's reason.
            const QString reason = h.simulated ? QStringLiteral("시뮬레이션 판정 · %1").arg(h.reason) : h.reason;
            lines << (reason.isEmpty()
                          ? QStringLiteral("%1 %2").arg(kst_text(h.at, "MM-dd HH:mm"), change)
                          : QStringLiteral("%1 %2 (%3)").arg(kst_text(h.at, "MM-dd HH:mm"), change, reason));
        }
        rows << Row(QStringLiteral("상태 이력"), lines.join("\n"));
    }
    return rows;
}

namespace {

/// An algorithm's latest simulated probation (krx-quant #298) in a line: its days, how it was
/// trained, and its review at once.
QString simulation_text(const Simulation& sim) {
    if (sim.cadence.isEmpty())
        return sim.why.isEmpty() ? QStringLiteral("하지 못함") : QStringLiteral("하지 못함: %1").arg(sim.why);
    QStringList parts{QStringLiteral("%1 – %2 · %3일").arg(sim.first_day, sim.last_day).arg(sim.days),
                      sim.cadence == QLatin1String("daily") ? QStringLiteral("날마다 그 전날까지로 다시 학습")
                                                            : QStringLiteral("첫날 전까지로 한 번 학습")};
    if (!sim.review_day) {
        parts << QStringLiteral("첫 판정일 전");
    } else if (sim.status == QLatin1String("active")) {
        parts << QStringLiteral("%1일째 판정: 정식이 됨").arg(*sim.review_day);
    } else if (sim.status == QLatin1String("clone")) {
        parts << QStringLiteral("%1일째 판정: 복제").arg(*sim.review_day);
    } else if (!sim.status.isEmpty()) {
        parts << QStringLiteral("%1일째 판정: 통과하지 못해 수습이 이어짐").arg(*sim.review_day);
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

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
        if (st->simulated > 0)
            text += QStringLiteral(" (시뮬레이션 %1일 포함)").arg(st->simulated); // krx-quant #298
        if (st->waiting && st->simulated == 0) {
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
    if (const auto& sim = info->simulation)
        rows << Row(QStringLiteral("수습 시뮬레이션"), simulation_text(*sim));
    return rows;
}

} // namespace fincept::krx_quant
