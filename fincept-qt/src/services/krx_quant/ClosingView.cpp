#include "services/krx_quant/ClosingView.h"

#include "services/krx_quant/StatsView.h"

#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::krx_quant::closing {

namespace {

const QTimeZone& kst() {
    static const QTimeZone zone("Asia/Seoul");
    return zone;
}

QString dash() {
    return QStringLiteral("—");
}

QString minus() {
    return QStringLiteral("−");
}

QString weekday(const QDate& day) {
    static const QStringList names = {"월", "화", "수", "목", "금", "토", "일"};
    return day.isValid() ? names[day.dayOfWeek() - 1] : QString();
}

QString date_text(const QDate& day) {
    return day.isValid() ? QStringLiteral("%1(%2)").arg(day.toString(Qt::ISODate), weekday(day)) : dash();
}

/// ``value`` with ``decimals`` places, thousands grouped, "+" or "−" before it (none for 0).
QString signed_number(double value, int decimals) {
    const QString text = number_text(std::abs(value), decimals);
    const double shown = std::round(std::abs(value) * std::pow(10, decimals));
    if (shown == 0)
        return number_text(0, decimals);
    return (value > 0 ? QStringLiteral("+") : minus()) + text;
}

QString percent_text(double share) {
    return number_text(share * 100, 1) + QStringLiteral("%");
}

QDateTime kst_at(const QDate& day, int hour, int minute, int second = 0) {
    return QDateTime(day, QTime(hour, minute, second), kst());
}

/// Whether the demand is the minute's: during continuous trading, ``at`` is the minute of ``as_of``.
bool minute_demand(const ClosingFlow& f) {
    return f.at.isValid() && f.as_of.isValid() && f.at.secsTo(f.as_of) < 60;
}

StatsCell rate_cell(const std::optional<double>& rate, int n, const QString& what) {
    StatsCell cell;
    cell.thin = n < kThin;
    cell.text = rate ? QStringLiteral("%1 (%2일)").arg(percent_text(*rate)).arg(n) : dash();
    cell.tip = rate ? QStringLiteral("%1: 둘 다 움직인 %2일 가운데").arg(what).arg(n)
                    : QStringLiteral("%1: 셀 날이 없습니다").arg(what);
    if (cell.thin)
        cell.tip += QStringLiteral("\n표본이 %1일 미만이라 우연일 수 있습니다").arg(kThin);
    return cell;
}

QList<StatsCell> stats_cells(const ClosingStats& s) {
    StatsCell days{QStringLiteral("%1일").arg(s.days), QStringLiteral("그 묶음의 날 수"), s.days < kThin};
    StatsCell median{s.auction_bp_median ? number_text(*s.auction_bp_median, 1) + QStringLiteral("bp") : dash(),
                     QStringLiteral("|종가 단일가 움직임|(종가 ÷ 15:20 직전가 − 1)의 중앙값"), s.days < kThin};
    if (days.thin) {
        days.tip += QStringLiteral("\n표본이 %1일 미만이라 우연일 수 있습니다").arg(kThin);
        median.tip += QStringLiteral("\n표본이 %1일 미만이라 우연일 수 있습니다").arg(kThin);
    }
    return {
        days, median,
        rate_cell(s.agree, s.agree_n, QStringLiteral("단일가가 15:20 수요(수요가 없는 날은 그날 흐름) 쪽으로 간 날")),
        rate_cell(s.next_gap_opposite, s.next_gap_n, QStringLiteral("다음날 시가 갭이 단일가와 반대인 날")),
        rate_cell(s.next_close_opposite, s.next_close_n, QStringLiteral("다음날 종가 → 종가가 단일가와 반대인 날"))};
}

} // namespace

int poll_ms(const std::optional<ClosingFlow>& flow, const std::optional<ClosingDays>& days, bool failed,
            const QDateTime& now) {
    if (failed || !flow)
        return kFastMs;
    if (minute_demand(*flow))
        return kFastMs;
    const QDateTime local = now.toTimeZone(kst());
    const QDate today = local.date();
    const auto until = [&local](const QDateTime& t) {
        return static_cast<int>(std::clamp<qint64>(local.msecsTo(t), 1000, 25LL * 3600 * 1000));
    };
    if (!flow->at.isValid()) { // before the open: at 09:00, then every minute on a late-open day
        const QDateTime open = kst_at(today, 9, 0, 5);
        return local < open ? until(open) : kFastMs;
    }
    const QDateTime midnight = kst_at(today.addDays(1), 0, 0, 5);
    if (flow->day == today) { // a trading day after 15:20: the day's row comes once the days are computed
        const QDateTime evening = kst_at(today, 20, 30);
        const bool computed = days && days->computed_at.isValid() && days->computed_at >= evening;
        if (!computed) {
            const QDateTime check = kst_at(today, 20, 35);
            return local < check ? until(check) : std::min(kSlowMs, until(midnight));
        }
    }
    return until(midnight);
}

bool days_due(const std::optional<ClosingDays>& days, const QString& symbol, const QDateTime& read_at,
              const std::optional<ClosingFlow>& flow, const QDateTime& now) {
    if (!days || days->symbol != symbol || !read_at.isValid())
        return true;
    const QDateTime local = now.toTimeZone(kst());
    if (read_at.toTimeZone(kst()).date() != local.date())
        return true;
    const QDateTime evening = kst_at(local.date(), 20, 30);
    const bool computed = days->computed_at.isValid() && days->computed_at >= evening;
    return flow && flow->day == local.date() && local >= evening && !computed;
}

bool krx_source(const QString& source) {
    return !source.isEmpty() && source != QLatin1String("kis");
}

bool shows_krx(const std::optional<ClosingFlow>& flow, const std::optional<ClosingDays>& days) {
    if (flow && std::any_of(flow->products.begin(), flow->products.end(),
                            [](const ClosingProduct& p) { return krx_source(p.net_assets_source); }))
        return true;
    return days && std::any_of(days->days.begin(), days->days.end(),
                               [](const ClosingDay& d) { return krx_source(d.net_assets_source); });
}

QString source_label(const QString& source) {
    if (source.isEmpty())
        return {};
    return krx_source(source) ? QString::fromUtf8(kKrxLabel) : QStringLiteral("KIS");
}

Side side_of(const std::optional<double>& value) {
    if (!value || std::llround(*value) == 0)
        return Side::None;
    return *value > 0 ? Side::Buy : Side::Sell;
}

QString won_text(const std::optional<double>& won, bool sign) {
    if (!won)
        return dash();
    const double value = *won;
    if (std::llround(value) == 0)
        return QStringLiteral("0원");
    const double eok = value / 1e8;
    QString text;
    if (std::abs(eok) >= 0.1)
        text = number_text(std::abs(eok), 1) + QStringLiteral("억원");
    else
        text = number_text(std::abs(value) / 1e4, 0) + QStringLiteral("만원");
    if (value < 0)
        return minus() + text;
    return sign ? QStringLiteral("+") + text : text;
}

QString side_text(const std::optional<double>& won) {
    switch (side_of(won)) {
        case Side::Buy:
            return QStringLiteral("매수");
        case Side::Sell:
            return QStringLiteral("매도");
        case Side::None:
            break;
    }
    return {};
}

QString ratio_text(const std::optional<double>& ratio, bool upper) {
    if (!ratio)
        return dash();
    const QString text = signed_number(*ratio, 2) + QStringLiteral("배");
    return upper ? text + QStringLiteral(" (상한)") : text;
}

QString bp_text(const std::optional<double>& bp) {
    return bp ? signed_number(*bp, 1) + QStringLiteral("bp") : dash();
}

QString structure_label(const QString& structure) {
    if (structure == QLatin1String("spot_futures"))
        return QStringLiteral("현물+선물형");
    if (structure == QLatin1String("futures"))
        return QStringLiteral("선물형");
    return structure;
}

QString reason_text(const QString& reason) {
    if (reason.isEmpty())
        return {};
    if (reason == QLatin1String("no_products"))
        return QStringLiteral("수요 없음: 이 종목을 기초로 한 단일종목 레버리지·인버스 ETF가 없습니다(대조군)");
    if (reason == QLatin1String("before_open"))
        return QStringLiteral("장 전입니다: 수요는 연속매매가 열리면 계산합니다");
    if (reason == QLatin1String("no_price"))
        return QStringLiteral("수요 없음: 그 시각의 기준가가 없습니다");
    if (reason == QLatin1String("no_previous_close"))
        return QStringLiteral("수요 없음: 전 거래일 KRX 종가가 없습니다");
    if (reason == QLatin1String("no_net_assets"))
        return QStringLiteral("수요 없음: 순자산이 있는 상품이 없습니다");
    return QStringLiteral("수요 없음: %1").arg(reason);
}

QString denominator_reason_text(const QString& reason) {
    if (reason.isEmpty())
        return {};
    if (reason == QLatin1String("no_recorded_auction"))
        return QStringLiteral("녹화 없음");
    if (reason == QLatin1String("not_computed"))
        return QStringLiteral("지난날 표 계산 전");
    return reason;
}

QString day_line(const ClosingFlow& f) {
    QString when;
    if (!f.at.isValid())
        when = QStringLiteral("장 전");
    else if (f.source == QLatin1String("stored"))
        when = QStringLiteral("15:20 저장값");
    else if (minute_demand(f))
        when = QStringLiteral("%1 지금 가격으로 계산").arg(kst_text(f.at, "HH:mm"));
    else
        when = QStringLiteral("%1 기준").arg(kst_text(f.at, "HH:mm"));
    return QStringLiteral("%1 · %2").arg(date_text(f.day), when);
}

QString headline(const ClosingFlow& f) {
    if (!f.demand)
        return reason_text(f.reason.isEmpty() ? QStringLiteral("no_net_assets") : f.reason);
    const QString side = side_text(f.demand);
    return side.isEmpty() ? won_text(f.demand) : QStringLiteral("%1 %2").arg(side, won_text(f.demand));
}

QList<CardLine> card_lines(const ClosingFlow& f) {
    QList<CardLine> lines;
    const bool products = !f.products.isEmpty();
    if (products) {
        lines.push_back({QStringLiteral("현물+선물형"), won_text(f.spot_futures),
                         QStringLiteral("현물에 선물을 더하는 구조라 단일가와 선물의 나눔을 모릅니다. 그 합 전부를 "
                                        "단일가 비율의 분자로 씁니다."),
                         side_of(f.spot_futures), !f.spot_futures});
        lines.push_back({QStringLiteral("선물형"), won_text(f.futures),
                         QStringLiteral("개별주식선물(15:45까지)로 집행하므로 단일가 비율의 분자에 넣지 않습니다."),
                         side_of(f.futures), !f.futures});
        const QString ratio_tip = QStringLiteral("현물+선물형 수요 ÷ 분모. 현물+선물형이 일부를 선물로 집행할 수 있어 "
                                                 "단일가에 가는 몫의 상한입니다.");
        lines.push_back(
            {QStringLiteral("단일가 비율"),
             f.ratio ? ratio_text(f.ratio, true)
                     : (f.denominator_reason.isEmpty() ? dash() : denominator_reason_text(f.denominator_reason)),
             ratio_tip, side_of(f.ratio), !f.ratio});
        lines.push_back({QStringLiteral("전체 비율"), ratio_text(f.ratio_total),
                         QStringLiteral("두 구조의 합 ÷ 분모(선물형도 넣은 것)"), side_of(f.ratio_total),
                         !f.ratio_total});
    }
    CardLine denominator{QStringLiteral("분모"), {}, {}, Side::None, false};
    denominator.tip = QStringLiteral("그날 전 20거래일의 녹화한 종가 단일가 체결대금(종가 단일가 시작부터 장 마감 1분 "
                                     "뒤까지 가격 × 수량)의 중앙값. 녹화는 2026-10-12에 시작했습니다.");
    if (f.denominator) {
        denominator.value =
            QStringLiteral("%1 (20거래일 중 %2일)").arg(won_text(f.denominator, false)).arg(f.denominator_days);
    } else {
        denominator.value = f.denominator_reason.isEmpty() ? dash() : denominator_reason_text(f.denominator_reason);
        denominator.dim = true;
        if (f.denominator_reason == QLatin1String("no_recorded_auction"))
            denominator.tip += QStringLiteral("\n그 20거래일에 녹화한 종가 단일가가 없습니다.");
        else if (f.denominator_reason == QLatin1String("not_computed"))
            denominator.tip += QStringLiteral("\n엔진이 지난날 표를 아직 계산하지 않았습니다.");
    }
    lines.push_back(denominator);
    // r's side by its basis points (side_of rounds to whole units): under half a bp, no color.
    CardLine r{QStringLiteral("r"), dash(), QStringLiteral("기준가(통합 중간가) ÷ 전 거래일 KRX 종가 − 1"),
               side_of(f.r ? std::optional<double>(*f.r * 1e4) : std::nullopt), false};
    if (f.r) {
        r.value = signed_number(*f.r * 100, 2) + QStringLiteral("%");
        if (f.price && f.previous_close)
            r.value += QStringLiteral(" (%1 ÷ %2)").arg(price_text(*f.price), price_text(*f.previous_close));
    } else {
        r.dim = true;
    }
    lines.push_back(r);
    QString when = dash();
    if (f.at.isValid())
        when = f.source == QLatin1String("stored") ? QStringLiteral("%1 (저장한 15:20 값)").arg(kst_text(f.at, "HH:mm"))
                                                   : QStringLiteral("%1 (그 직전 기준가)").arg(kst_text(f.at, "HH:mm"));
    lines.push_back({QStringLiteral("계산 시각"), when,
                     QStringLiteral("연속매매 동안은 물을 때의 분 직전 기준가로, 15:20부터는 저장한 15:20 값입니다. "
                                    "엔진 시계 %1")
                         .arg(kst_text(f.as_of, "MM-dd HH:mm:ss")),
                     Side::None, !f.at.isValid()});
    return lines;
}

QStringList notes(const ClosingFlow& f) {
    if (f.products.isEmpty())
        return {QStringLiteral("대조군: 레버리지 ETF가 없는 종목의 종가 단일가·다음날 통계를 005930·000660과 "
                               "견줍니다.")};
    QStringList out{QStringLiteral("공식으로 계산한 값: A × (L² − L) × r. 실제 집행이 아닙니다(금융위원회의 "
                                   "2026-07-28 분산 요청 뒤의 집행 시각은 모릅니다)."),
                    QStringLiteral("홍콩 상품·ETN 제외: 실제 수요는 더 클 수 있습니다.")};
    if (!f.missing.isEmpty())
        out << QStringLiteral("순자산 없음 %1종(%2): 합에서 뺐습니다.")
                   .arg(f.missing.size())
                   .arg(f.missing.join(QStringLiteral(", ")));
    return out;
}

QList<ProductRow> product_rows(const ClosingFlow& f) {
    QList<ProductRow> rows;
    for (const auto& p : f.products) {
        ProductRow row;
        row.code = p.code;
        row.name = p.name.isEmpty() ? dash() : p.name;
        row.leverage =
            (p.leverage < 0 ? minus() : QString()) + QString::number(std::abs(p.leverage)) + QStringLiteral("배");
        row.structure = structure_label(p.structure);
        row.coefficient = QString::number(p.coefficient);
        row.inverse = p.leverage < 0;
        row.missing = !p.net_assets;
        row.net_assets = p.net_assets ? won_text(p.net_assets, false) : QStringLiteral("없음");
        row.net_assets_date = p.net_assets_date.isValid() ? p.net_assets_date.toString("MM-dd") : dash();
        row.source = p.net_assets_source.isEmpty() ? dash() : source_label(p.net_assets_source);
        row.demand = p.demand ? won_text(p.demand) : QStringLiteral("없음 (합에서 뺌)");
        row.side = side_of(p.demand);
        QStringList tip;
        tip << (row.inverse ? QStringLiteral("인버스(L = %1%2): L² − L = %3. 오른 날 마감 매수가 큽니다. 하락 신호를 "
                                             "이 상품으로 실행하면 그 마감 흐름에 노출됩니다.")
                                  .arg(minus())
                                  .arg(std::abs(p.leverage))
                                  .arg(p.coefficient)
                            : QStringLiteral("L = %1: L² − L = %2. 수요는 그날 움직인 방향과 같은 쪽입니다.")
                                  .arg(p.leverage)
                                  .arg(p.coefficient));
        if (row.missing)
            tip << QStringLiteral("전 거래일 순자산을 받지 못해 수요를 계산하지 않았고, 합에서 뺐습니다.");
        else
            tip << QStringLiteral("순자산: %1 자, 출처 %2")
                       .arg(p.net_assets_date.toString(Qt::ISODate), source_label(p.net_assets_source));
        row.tip = tip.join('\n');
        rows.push_back(row);
    }
    return rows;
}

QList<DayRow> day_rows(const ClosingDays& d, bool has_products) {
    QList<DayRow> rows;
    for (auto it = d.days.crbegin(); it != d.days.crend(); ++it) {
        const ClosingDay& day = *it;
        DayRow row;
        row.date = date_text(day.date);
        row.period = day.period;
        row.side = side_of(day.demand);
        if (day.demand) {
            row.demand = won_text(day.demand);
            row.demand_tip =
                QStringLiteral("현물+선물형 %1 · 선물형 %2").arg(won_text(day.spot_futures), won_text(day.futures));
            if (day.demand_source == QLatin1String("live"))
                row.demand_tip += QStringLiteral("\n그날 15:20에 저장한 값");
            else if (day.demand_source == QLatin1String("bars"))
                row.demand_tip +=
                    QStringLiteral("\n저장한 15:20 값이 없어 그날 쓸 순자산과 1분봉의 흐름(15:20 직전가 ÷ "
                                   "전일 종가 − 1)으로 계산한 값");
            if (!day.net_assets_source.isEmpty())
                row.demand_tip += QStringLiteral("\n순자산 출처 %1").arg(source_label(day.net_assets_source));
        } else if (has_products) {
            row.demand = QStringLiteral("순자산 없음");
            row.demand_tip = QStringLiteral("그날 쓸 순자산이 없습니다: 엔진이 순자산을 받기 전의 날입니다(KRX Open "
                                            "API로 지난날을 채우기 전). 방향 일치는 그날 흐름과 견줍니다.");
        } else {
            row.demand = QStringLiteral("상품 없음");
            row.demand_tip = QStringLiteral("이 종목에는 단일종목 레버리지·인버스 ETF가 없습니다.");
        }
        if (day.demand_source == QLatin1String("live"))
            row.source = QStringLiteral("15:20 저장");
        else if (day.demand_source == QLatin1String("bars"))
            row.source = QStringLiteral("1분봉 계산");
        if (day.ratio) {
            row.ratio = ratio_text(day.ratio, true);
            row.ratio_tip = QStringLiteral("현물+선물형 수요 ÷ 분모 %1 (그날 전 20거래일 중 %2일의 녹화한 종가 단일가 "
                                           "체결대금 중앙값). 상한입니다. 전체 비율 %3")
                                .arg(won_text(day.denominator, false))
                                .arg(day.denominator_days)
                                .arg(ratio_text(day.ratio_total));
        } else {
            row.ratio_missing = true;
            if (!day.denominator_reason.isEmpty()) {
                row.ratio = denominator_reason_text(day.denominator_reason);
                row.ratio_tip = day.denominator_reason == QLatin1String("no_recorded_auction")
                                    ? QStringLiteral("그날 전 20거래일에 녹화한 종가 단일가 체결대금이 없어 분모가 "
                                                     "없습니다(녹화는 2026-10-12에 시작했습니다).")
                                    : day.denominator_reason;
            } else {
                row.ratio = dash();
                row.ratio_tip = QStringLiteral("수요가 없는 날입니다.");
            }
        }
        row.auction = QStringLiteral("%1 (%2틱)").arg(bp_text(day.auction_bp), signed_number(day.auction_ticks, 0));
        row.auction_side = side_of(day.auction_bp * 100);
        row.auction_tip = QStringLiteral("종가 ÷ 15:20 직전가 − 1. 그날 15:20까지의 흐름 %1").arg(bp_text(day.flow_bp));
        row.auction_tip +=
            day.auction_value
                ? QStringLiteral("\n그날 종가 단일가 체결대금 %1(녹화)").arg(won_text(day.auction_value, false))
                : QStringLiteral("\n그날 종가 단일가 체결대금: 녹화 없음");
        const bool by_flow = day.compared_with == QLatin1String("flow");
        row.agreed = day.agree;
        if (day.agree)
            row.agree = *day.agree ? QStringLiteral("같음") : QStringLiteral("반대");
        else
            row.agree = dash();
        if (by_flow && day.agree)
            row.agree += QStringLiteral(" (흐름)");
        row.agree_tip = by_flow ? QStringLiteral("수요가 없는 날이라 단일가 움직임을 15:20까지의 그날 흐름과 견줍니다"
                                                 "(#270과 같은 비교).")
                                : QStringLiteral("단일가가 15:20 수요의 방향으로 움직였는지입니다.");
        if (!day.agree)
            row.agree_tip += QStringLiteral(" 단일가나 견주는 것이 움직이지 않아 세지 않습니다.");
        row.next_gap = bp_text(day.next_gap_bp);
        row.next_close = bp_text(day.next_close_bp);
        QStringList special;
        for (const auto& kind : day.special)
            special << special_label(kind);
        row.special = special.join(QStringLiteral(" · "));
        rows.push_back(row);
    }
    return rows;
}

QStringList summary_headers() {
    return {QStringLiteral("일 수"), QStringLiteral("|단일가| 중앙값"), QStringLiteral("단일가 방향 일치"),
            QStringLiteral("다음날 갭 반대"), QStringLiteral("다음날 종가 반대")};
}

QList<SummaryRow> summary_rows(const ClosingDays& d) {
    QList<SummaryRow> rows;
    const auto add = [&rows](const QString& group, const QString& tip, const ClosingStats& all,
                             const ClosingStats& without) {
        rows.push_back({group, tip, false, stats_cells(all)});
        rows.push_back({group, tip, true, stats_cells(without)});
    };
    for (const auto& p : d.periods) {
        const QString from = p.from.isValid() ? p.from.toString(Qt::ISODate) : QStringLiteral("처음");
        const QString to = p.to.isValid() ? p.to.toString(Qt::ISODate) : QStringLiteral("지금");
        add(p.period, QStringLiteral("기간 %1 – %2 (structure-check와 같은 경계)").arg(from, to), p.all,
            p.without_special);
    }
    for (const auto& t : d.tertiles)
        add(QStringLiteral("비율 3분위 %1 (|비율| %2–%3배)")
                .arg(t.tertile)
                .arg(number_text(t.ratio_from, 2), number_text(t.ratio_to, 2)),
            QStringLiteral("비율(현물+선물형 수요 ÷ 분모)이 있는 날을 |비율|의 크기로 나눈 세 묶음"), t.all,
            t.without_special);
    return rows;
}

QString days_line(const ClosingDays& d) {
    const QString range = QStringLiteral("%1 – %2").arg(d.from.toString(Qt::ISODate), d.to.toString(Qt::ISODate));
    if (!d.computed_at.isValid())
        return QStringLiteral("엔진이 아직 계산하지 않았습니다 · %1").arg(range);
    return QStringLiteral("%1에 계산 · %2 · %3일")
        .arg(kst_text(d.computed_at, "yyyy-MM-dd HH:mm"), range)
        .arg(d.days.size());
}

} // namespace fincept::krx_quant::closing
