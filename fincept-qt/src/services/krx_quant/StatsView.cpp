#include "services/krx_quant/StatsView.h"

#include <QLocale>
#include <QTime>

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
        rows.push_back({predictor_label(l.baseline), signed_text(gain.value, 3), interval_text(gain),
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

QString days_text(const QStringList& days) {
    if (days.isEmpty())
        return QStringLiteral("예측이 있는 날 없음");
    if (days.size() == 1)
        return days.first();
    return QStringLiteral("%1 ~ %2 (%3일)").arg(days.first(), days.last()).arg(days.size());
}

QString period_label(const QString& period) {
    static const QMap<QString, QString> labels = {
        {"today", QStringLiteral("오늘")}, {"5d", QStringLiteral("5일")}, {"20d", QStringLiteral("20일")}};
    return labels.value(period, period);
}

QString basis_label(const QString& basis) {
    static const QMap<QString, QString> labels = {{"b", QStringLiteral("표시 시점 (B)")},
                                                  {"a", QStringLiteral("봉 마감 (A)")}};
    return labels.value(basis, basis);
}

QString query_text(const AlgorithmStats& stats) {
    return QStringList{period_label(stats.period), basis_label(stats.basis),
                       stats.symbol == "all" ? QStringLiteral("전체 종목") : stats.symbol,
                       stats.special == "include" ? QStringLiteral("특수 상황 포함") : QStringLiteral("특수 상황 제외")}
        .join(QStringLiteral(" · "));
}

} // namespace fincept::krx_quant
