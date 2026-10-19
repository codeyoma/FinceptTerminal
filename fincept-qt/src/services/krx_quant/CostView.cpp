#include "services/krx_quant/CostView.h"

#include "services/krx_quant/StatsView.h"

#include <algorithm>

namespace fincept::krx_quant::cost {

namespace {

QString dash() {
    return QStringLiteral("—");
}

QString percent(double share) {
    return number_text(share * 100, 1) + "%";
}

QString won(double value) {
    return number_text(value, 0) + QStringLiteral("원");
}

/// A round trip: "23.0bp = 0.96틱".
QString round_trip_text(const CostRoundTrip& trip) {
    return QStringLiteral("%1bp = %2틱").arg(number_text(trip.bp, 1), number_text(trip.ticks, 2));
}

QString edge_text(const std::optional<TickEdge>& edge) {
    if (!edge)
        return dash();
    return QStringLiteral("%1 → 틱 %2, k %3").arg(won(edge->edge), won(edge->tick)).arg(edge->k);
}

/// A width or cost in the row's unit.
QString in_unit(const CostRow& row, double value, int decimals) {
    return row.unit == QLatin1String("bp") ? number_text(value, decimals) + "bp"
                                           : number_text(value, decimals) + QStringLiteral("틱");
}

const std::optional<Breakeven>& breakeven(const CostRow& row, Cost cost) {
    return cost == Cost::Limit ? row.breakeven_limit : row.breakeven_market;
}

} // namespace

int poll_ms(const std::optional<CostMap>& last) {
    if (!last)
        return kFastMs;
    const bool live =
        std::any_of(last->symbols.begin(), last->symbols.end(), [](const CostSymbol& s) { return s.live; });
    return live ? kFastMs : kSlowMs;
}

QString cost_label(Cost cost) {
    return cost == Cost::Limit ? QStringLiteral("지정가") : QStringLiteral("시장가");
}

Verdict verdict(const CostPredictor& predictor, Cost cost) {
    const QString& v = cost == Cost::Limit ? predictor.verdict_limit : predictor.verdict_market;
    if (v == QLatin1String("pass"))
        return Verdict::Pass;
    if (v == QLatin1String("fail"))
        return Verdict::Fail;
    if (v == QLatin1String("few"))
        return Verdict::Few;
    return Verdict::None;
}

QString verdict_text(Verdict verdict) {
    switch (verdict) {
        case Verdict::Pass:
            return QStringLiteral("넘음");
        case Verdict::Fail:
            return QStringLiteral("못 넘음");
        case Verdict::Few:
            return QStringLiteral("표본 부족");
        case Verdict::None:
            break;
    }
    return QStringLiteral("해당 없음");
}

QString card_title(const CostSymbol& symbol) {
    return symbol.name.isEmpty() ? symbol.symbol : QStringLiteral("%1 %2").arg(symbol.name, symbol.symbol);
}

QList<CardLine> card_lines(const CostSymbol& symbol) {
    QList<CardLine> lines;
    if (!symbol.price || !symbol.costs) {
        lines.push_back(
            {QStringLiteral("기준가"), dash(), QStringLiteral("엔진이 이 종목의 기준가를 아직 모릅니다."), false});
        return lines;
    }
    const CostCard& c = *symbol.costs;
    const QString when = symbol.live
                             ? QStringLiteral("%1 실시간").arg(kst_text(symbol.price_at, "HH:mm"))
                             : QStringLiteral("%1 마지막 봉 마감").arg(kst_text(symbol.price_at, "MM-dd HH:mm"));
    lines.push_back({QStringLiteral("기준가"), QStringLiteral("%1 (%2)").arg(won(*symbol.price), when),
                     symbol.live ? QStringLiteral("지금의 통합 중간가입니다.")
                                 : QStringLiteral("장 밖이라 그 종목의 가장 늦은 봉 마감의 중간가입니다."),
                     false});
    lines.push_back({QStringLiteral("틱"), QStringLiteral("%1 = %2bp").arg(won(c.tick), number_text(c.tick_bp, 1)),
                     QStringLiteral("이 가격의 호가단위와 그것의 bp(호가단위 ÷ 가격 × 10⁴)입니다."), false});
    lines.push_back({QStringLiteral("매도세"), number_text(c.sell_tax_bp, 1) + "bp", {}, false});
    lines.push_back({QStringLiteral("수수료(양쪽)"), number_text(c.fees_bp, 1) + "bp", {}, false});
    lines.push_back({QStringLiteral("스프레드 1틱"), number_text(c.spread_bp, 1) + "bp",
                     QStringLiteral("실제 스프레드가 아니라 1틱으로 가정한 값입니다(리서치의 비용표와 같은 가정)."),
                     false});
    lines.push_back({QStringLiteral("지정가 왕복"), round_trip_text(c.limit),
                     QStringLiteral("매도세 + 수수료 양쪽: 정답 문턱 k가 덮는 비용입니다."), false});
    lines.push_back({QStringLiteral("시장가 왕복"), round_trip_text(c.market),
                     QStringLiteral("지정가 왕복 + 스프레드 1틱: 매도 호가에 사서 매수 호가에 팝니다."), false});
    const bool short_of = c.k < c.market.ticks;
    lines.push_back(
        {QStringLiteral("정답 문턱 k"),
         short_of ? QStringLiteral("%1틱 (시장가 왕복보다 작음)").arg(c.k) : QStringLiteral("%1틱").arg(c.k),
         short_of ? QStringLiteral("k가 시장가 왕복보다 작아, 4분 방향을 시장가로 드나들면 다 맞혀도 "
                                   "손해입니다(p* > 100%).")
                  : QStringLiteral("4분 방향 정답의 문턱입니다: 매도세와 수수료를 덮는 가장 적은 틱."),
         short_of});
    if (c.inverse)
        lines.push_back(
            {QStringLiteral("하락: 인버스 %1X 왕복").arg(c.inverse->leverage),
             round_trip_text({c.inverse->bp, c.inverse->ticks}) + QStringLiteral(" (하한)"),
             QStringLiteral("단일종목 인버스 %1X로 하락을 칠 때 상품 명목의 수수료 양쪽만을 기초 주식의 움직임으로 "
                            "환산한 값입니다(ETF 매도에는 거래세가 없습니다). 상품의 스프레드, 괴리, 선물 "
                            "basis는 빠져 있어 하한입니다. 상장 %2.")
                 .arg(c.inverse->leverage)
                 .arg(c.inverse->listed.toString(Qt::ISODate)),
             false});
    else
        lines.push_back({QStringLiteral("하락: 인버스"), QStringLiteral("거래 수단 없음"),
                         QStringLiteral("이 종목에는 단일종목 인버스 상품이 없습니다. 하락은 공매도로만 칩니다."),
                         false});
    lines.push_back({QStringLiteral("아래 틱 경계"), edge_text(c.below),
                     QStringLiteral("가격이 이 경계 아래로 가면 호가단위와 k가 이렇게 바뀝니다."), false});
    lines.push_back({QStringLiteral("위 틱 경계"), edge_text(c.above),
                     QStringLiteral("가격이 이 경계에 닿으면 호가단위와 k가 이렇게 바뀝니다."), false});
    return lines;
}

QString horizon_text(const CostRow& row) {
    if (!row.horizon_minutes)
        return row.title + QStringLiteral(" (하루)");
    if (row.title.contains(QStringLiteral("분")))
        return row.title;
    return QStringLiteral("%1 %2분").arg(row.title).arg(*row.horizon_minutes);
}

QString width_text(const CostRow& row) {
    if (!row.width)
        return dash();
    return in_unit(row, *row.width, row.unit == QLatin1String("bp") ? 1 : 0);
}

QString cost_text(const CostRow& row) {
    if (!row.cost_limit || !row.cost_market)
        return dash();
    if (row.unit == QLatin1String("bp"))
        return in_unit(row, *row.cost_limit, 1) + QStringLiteral(" (단일가)");
    return QStringLiteral("지정가 %1 · 시장가 %2")
        .arg(number_text(*row.cost_limit, 2), in_unit(row, *row.cost_market, 2));
}

QString breakeven_text(const CostRow& row, Cost cost) {
    const auto& b = breakeven(row, cost);
    if (!b)
        return QStringLiteral("해당 없음");
    const QString text = QStringLiteral("p* %1 · C/W %2").arg(percent(b->p_star), number_text(b->c_over_w, 2));
    return b->p_star > 1 ? text + QStringLiteral(" (불가능)") : text;
}

QString breakeven_tip(const CostRow& row) {
    if (!row.breakeven_limit)
        return row.breakeven_missing.isEmpty() ? QStringLiteral("손익분기가 없습니다.")
                                               : QStringLiteral("p* 해당 없음: %1.").arg(row.breakeven_missing);
    QString tip =
        QStringLiteral("대칭 목표 ±W, 왕복 비용 C에서 상승·하락 신호가 본전이 되는 맞힘률 p* = (W + C) ÷ 2W입니다. "
                       "P(상승|신호) − P(하락|신호) ≥ C/W와 같습니다. p*가 100%를 넘으면 다 맞혀도 손해입니다.");
    if (row.reference_price)
        tip += QStringLiteral(
                   "\nW와 C는 그 기간 봉 마감 %1개 가운데 지정가 C/W가 가운데인 봉 마감(중간가 %2) 하나의 값입니다. "
                   "봉 마감마다 폭과 가격이 달라 근사입니다.")
                   .arg(row.bar_closes)
                   .arg(won(*row.reference_price));
    return tip;
}

QString horizon_tip(const CostRow& row) {
    QStringList days;
    for (const auto& d : row.days)
        days << d.toString(Qt::ISODate);
    return QStringLiteral("%1\n정답: %2\n날: %3").arg(horizon_text(row), row.rule, days_text(days));
}

QString role_label(Role role) {
    switch (role) {
        case Role::Consensus:
            return QStringLiteral("최종 합의");
        case Role::Top:
            return QStringLiteral("순위 1위");
        case Role::Baseline:
            break;
    }
    return QStringLiteral("가장 센 기준 예측");
}

const std::optional<CostPredictor>& predictor(const CostRow& row, Role role) {
    switch (role) {
        case Role::Consensus:
            return row.consensus;
        case Role::Top:
            return row.top;
        case Role::Baseline:
            break;
    }
    return row.baseline;
}

PredictorCell predictor_cell(const CostRow& row, Role role, Cost cost, int min_sample) {
    const auto& p = predictor(row, role);
    if (!p) {
        switch (role) {
            case Role::Consensus:
                return {QStringLiteral("합의 없음"), QStringLiteral("합의는 방향에만 있습니다."), Verdict::None};
            case Role::Top:
                return {QStringLiteral("순위에 든 알고리즘 없음"),
                        QStringLiteral("이 종류의 순위표에 순위가 매겨진 알고리즘이 없습니다(합의 빼고)."),
                        Verdict::None};
            case Role::Baseline:
                break;
        }
        return {QStringLiteral("신호를 낸 기준 예측 없음"), QStringLiteral("상승·하락 신호를 낸 기준 예측이 없습니다."),
                Verdict::None};
    }
    QStringList lines{p->algorithm};
    if (p->precision && p->precision->low)
        lines << QStringLiteral("%1 (하한 %2)").arg(percent(p->precision->value), percent(*p->precision->low));
    else
        lines << (p->precision ? percent(p->precision->value) : dash());
    lines << QStringLiteral("edge %1 · 신호 %2")
                 .arg(p->edge ? signed_text(*p->edge, 2) : dash(), number_text(p->calls, 0));
    const auto pnl_text = [&row](const std::optional<CostPnl>& pnl) {
        return pnl && pnl->per_trade ? in_unit(row, pnl->per_trade->value, 2) : dash();
    };
    lines << QStringLiteral("손익 %1 (연구용)").arg(pnl_text(p->pnl));
    const Verdict v = verdict(*p, cost);
    QString tip;
    const auto& b = breakeven(row, cost);
    switch (v) {
        case Verdict::Pass:
            tip = QStringLiteral("%1: 맞힘률 하한 %2 > p* %3")
                      .arg(verdict_text(v), percent(p->precision->low.value_or(0)), percent(b->p_star));
            break;
        case Verdict::Fail:
            tip = QStringLiteral("%1: 맞힘률 하한 %2 ≤ p* %3")
                      .arg(verdict_text(v), percent(p->precision->low.value_or(0)), percent(b->p_star));
            break;
        case Verdict::Few:
            tip = QStringLiteral("%1: 신호 %2개 < 최소 표본 %3").arg(verdict_text(v)).arg(p->calls).arg(min_sample);
            break;
        case Verdict::None:
            tip = QStringLiteral("p* 해당 없음: 판정하지 않습니다.");
            break;
    }
    tip += QStringLiteral(" (%1 왕복)").arg(cost_label(cost));
    if (p->rank)
        tip += QStringLiteral("\n순위 %1").arg(*p->rank);
    tip += QStringLiteral("\nedge: 신호 가운데 맞은 몫 − 반대로 틀린 몫(P(상승|신호) − P(하락|신호)). C/W와 견줍니다.");
    if (p->pnl_inverse && p->pnl_inverse->per_trade)
        tip += QStringLiteral("\n하락을 인버스로 친 손익: %1 (%2건, 연구용)")
                   .arg(in_unit(row, p->pnl_inverse->per_trade->value, 2))
                   .arg(p->pnl_inverse->trades);
    tip += QStringLiteral("\n두 번 누르면 상세 통계 창이 열립니다.");
    return {lines.join('\n'), tip, v};
}

QList<RegimeLine> regime_lines(const BaseRates& rates) {
    QList<RegimeLine> lines;
    for (const auto& r : rates.rows)
        lines.push_back({r.symbol, r.period, number_text(r.days, 0), number_text(r.windows, 0), percent(r.up),
                         percent(r.flat), percent(r.down), QString::number(r.k),
                         r.persist
                             ? QStringLiteral("%1 (n %2)").arg(percent(*r.persist), number_text(r.persist_windows, 0))
                             : dash()});
    return lines;
}

QString regimes_line(const CostMap& map) {
    if (!map.regimes)
        return map.regimes_missing.isEmpty() ? QStringLiteral("기간 기준율 없음") : map.regimes_missing;
    const BaseRates& r = *map.regimes;
    QString line = QStringLiteral("계산 %1").arg(kst_text(r.computed_at, "MM-dd HH:mm"));
    if (r.first_day.isValid())
        line +=
            QStringLiteral(" · 1분봉 %1 – %2").arg(r.first_day.toString(Qt::ISODate), r.last_day.toString(Qt::ISODate));
    return line;
}

QString as_of_line(const CostMap& map) {
    return QStringLiteral("%1 기준 · %2 · 최소 표본 %3 · 스프레드 %4틱 가정")
        .arg(kst_text(map.as_of, "yyyy-MM-dd HH:mm"), period_label(map.period))
        .arg(map.min_sample)
        .arg(map.spread_ticks);
}

} // namespace fincept::krx_quant::cost
