#include "services/krx_quant/FlowView.h"

#include "services/krx_quant/StatsView.h"

#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::krx_quant::flow {

namespace {

const QTimeZone& kst() {
    static const QTimeZone zone("Asia/Seoul");
    return zone;
}

QString dash() {
    return QStringLiteral("—");
}

/// A minute's value of a minute series; none for the estimates (not minute series).
std::optional<double> minute_value(const FlowMinute& m, Series series) {
    switch (series) {
        case Series::Price:
            return m.price;
        case Series::Program:
            return m.program_net_value;
        case Series::Member:
            return m.member_foreign_net_qty;
        case Series::EstimateForeign:
        case Series::EstimateInstitution:
            break;
    }
    return std::nullopt;
}

std::optional<double> estimate_value(const FlowEstimate& e, Series series) {
    return series == Series::EstimateForeign ? e.foreign_net_qty : e.institution_net_qty;
}

QString signed_grouped(double value) {
    const QString text = number_text(std::abs(value), 0);
    if (std::llround(value) == 0)
        return text;
    return (value > 0 ? QStringLiteral("+") : QStringLiteral("−")) + text;
}

QString weekday(const QDate& day) {
    static const QStringList names = {"월", "화", "수", "목", "금", "토", "일"};
    return day.isValid() ? names[day.dayOfWeek() - 1] : QString();
}

} // namespace

int poll_ms(const std::optional<Flow>& last, const QDateTime& now) {
    if (!last || !last->open.isValid())
        return kFastMs;
    const QDateTime local = now.toTimeZone(kst());
    if (local.date() != last->day)
        return kSlowMs;
    return local >= last->open && local < last->close.addSecs(10 * 60) ? kFastMs : kSlowMs;
}

QString series_label(Series series) {
    switch (series) {
        case Series::Price:
            return QStringLiteral("가격");
        case Series::Program:
            return QStringLiteral("프로그램 순매수");
        case Series::Member:
            return QStringLiteral("외국계 회원사");
        case Series::EstimateForeign:
            return QStringLiteral("추정 외국인");
        case Series::EstimateInstitution:
            return QStringLiteral("추정 기관");
    }
    return {};
}

QString series_unit(Series series) {
    switch (series) {
        case Series::Price:
            return QStringLiteral("원");
        case Series::Program:
            return QStringLiteral("KIS 금액 단위");
        case Series::Member:
        case Series::EstimateForeign:
        case Series::EstimateInstitution:
            return QStringLiteral("주");
    }
    return {};
}

QList<QList<Point>> segments(const Flow& f, Series series, const QDateTime& now) {
    QList<QList<Point>> out;
    if (series == Series::EstimateForeign || series == Series::EstimateInstitution) {
        QList<Point> steps;
        for (const auto& e : f.estimates) {
            const auto value = estimate_value(e, series);
            if (!value)
                continue;
            const qint64 at = e.received_at.toSecsSinceEpoch();
            if (!steps.isEmpty())
                steps.push_back({at, steps.last().value}); // held until this slot came in
            steps.push_back({at, *value});
        }
        if (!steps.isEmpty()) {
            const QDateTime end = now.isValid() && now < f.close ? now : f.close;
            steps.push_back({std::max(end.toSecsSinceEpoch(), steps.last().at), steps.last().value});
            out.push_back(steps);
        }
        return out;
    }
    QList<Point> run;
    for (const auto& m : f.minutes) {
        if (const auto value = minute_value(m, series)) {
            run.push_back({m.at.toSecsSinceEpoch(), *value});
        } else if (!run.isEmpty()) {
            out.push_back(run);
            run.clear();
        }
    }
    if (!run.isEmpty())
        out.push_back(run);
    return out;
}

QString value_text(const std::optional<double>& value, Series series) {
    if (!value)
        return dash();
    if (series == Series::Price)
        return number_text(*value, 0);
    const QString text = signed_grouped(*value);
    return series_unit(series) == QStringLiteral("주") ? text + QStringLiteral("주") : text;
}

QStringList hover_lines(const Flow& f, int index) {
    if (index < 0 || index >= f.minutes.size())
        return {};
    const FlowMinute& m = f.minutes[index];
    const auto rest = [](const QString& source) {
        return source == QLatin1String("rest") ? QStringLiteral(" (REST)") : QString();
    };
    QStringList lines{kst_text(m.at, "HH:mm")};
    lines << QStringLiteral("%1 %2").arg(series_label(Series::Price), value_text(m.price, Series::Price));
    lines << QStringLiteral("%1 %2%3").arg(series_label(Series::Program),
                                           value_text(m.program_net_value, Series::Program), rest(m.program_source));
    lines << QStringLiteral("%1 %2%3").arg(series_label(Series::Member),
                                           value_text(m.member_foreign_net_qty, Series::Member), rest(m.member_source));
    for (const Series series : {Series::EstimateForeign, Series::EstimateInstitution}) {
        std::optional<double> known;
        QDateTime since;
        for (const auto& e : f.estimates) {
            if (e.received_at > m.at.addSecs(59))
                break;
            if (const auto value = estimate_value(e, series)) {
                known = value;
                since = e.received_at;
            }
        }
        lines << (known ? QStringLiteral("%1 %2 (%3 입력)")
                              .arg(series_label(series), value_text(known, series), kst_text(since, "HH:mm"))
                        : QStringLiteral("%1 %2").arg(series_label(series), dash()));
    }
    return lines;
}

QList<TotalRow> total_rows(const Flow& f) {
    const auto& t = f.totals;
    const std::optional<FlowTotals>& p = f.previous_day_same_time;
    const auto previous = [&](auto field, Series series) { return p ? value_text((*p).*field, series) : dash(); };
    return {
        {series_label(Series::Program), value_text(t.program_net_value, Series::Program),
         previous(&FlowTotals::program_net_value, Series::Program), t.program_net_value},
        {series_label(Series::Member), value_text(t.member_foreign_net_qty, Series::Member),
         previous(&FlowTotals::member_foreign_net_qty, Series::Member), t.member_foreign_net_qty},
        {series_label(Series::EstimateForeign), value_text(t.estimate_foreign_net_qty, Series::EstimateForeign),
         previous(&FlowTotals::estimate_foreign_net_qty, Series::EstimateForeign), t.estimate_foreign_net_qty},
        {series_label(Series::EstimateInstitution),
         value_text(t.estimate_institution_net_qty, Series::EstimateInstitution),
         previous(&FlowTotals::estimate_institution_net_qty, Series::EstimateInstitution),
         t.estimate_institution_net_qty},
    };
}

QString previous_header(const Flow& f) {
    if (!f.previous_day_same_time)
        return QStringLiteral("전일");
    const auto& p = *f.previous_day_same_time;
    return QStringLiteral("전일(%1) %2까지").arg(p.day.toString("MM-dd"), kst_text(p.at, "HH:mm"));
}

QString final_line(const Flow& f) {
    const QString estimated = QStringLiteral("추정 마지막: 외국인 %1 · 기관 %2")
                                  .arg(value_text(f.totals.estimate_foreign_net_qty, Series::EstimateForeign),
                                       value_text(f.totals.estimate_institution_net_qty, Series::EstimateInstitution));
    if (!f.final_known)
        return QStringLiteral("확정 수급은 20:30 뒤에 들어옵니다 (%1)").arg(estimated);
    return QStringLiteral("확정 외국인 %1 · 기관 %2 (%3)")
        .arg(value_text(f.final_foreign_net, Series::EstimateForeign),
             value_text(f.final_institution_net, Series::EstimateInstitution), estimated);
}

QList<MemberRow> member_rows(const QList<FlowMember>& members) {
    QList<MemberRow> rows;
    for (const auto& m : members)
        rows.push_back({m.name.isEmpty() ? m.no : m.name,
                        m.qty ? number_text(*m.qty, 0) + QStringLiteral("주") : dash(), m.foreign});
    return rows;
}

QString day_line(const Flow& f) {
    const QString day = QStringLiteral("%1(%2)").arg(f.day.toString(Qt::ISODate), weekday(f.day));
    if (!f.recorded)
        return day + QStringLiteral(" · 녹화 없음");
    QDateTime last;
    QStringList rest_runs;
    QDateTime run_start, run_end;
    for (const auto& m : f.minutes) {
        if (m.program_net_value || m.member_foreign_net_qty)
            last = m.at;
        const bool rest = m.program_source == QLatin1String("rest") || m.member_source == QLatin1String("rest");
        if (rest) {
            if (!run_start.isValid())
                run_start = m.at;
            run_end = m.at;
        } else if (run_start.isValid()) {
            rest_runs << QStringLiteral("%1–%2").arg(kst_text(run_start, "HH:mm"),
                                                     kst_text(run_end.addSecs(60), "HH:mm"));
            run_start = {};
        }
    }
    if (run_start.isValid())
        rest_runs << QStringLiteral("%1–%2").arg(kst_text(run_start, "HH:mm"), kst_text(run_end.addSecs(60), "HH:mm"));
    QString line = day;
    if (last.isValid())
        line += QStringLiteral(" · 마지막 기록 %1").arg(kst_text(last, "HH:mm"));
    if (!rest_runs.isEmpty())
        line += QStringLiteral(" · REST 대체 %1").arg(rest_runs.join(", "));
    return line;
}

} // namespace fincept::krx_quant::flow
