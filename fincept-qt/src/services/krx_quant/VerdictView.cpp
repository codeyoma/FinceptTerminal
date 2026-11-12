#include "services/krx_quant/VerdictView.h"

#include "services/krx_quant/StatsView.h"

#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>

#include <cmath>

namespace fincept::krx_quant::judgment {

namespace {

const QTimeZone& kst() {
    static const QTimeZone zone("Asia/Seoul");
    return zone;
}

QString dash() {
    return QStringLiteral("—");
}

QString signed_text(double value, int decimals) {
    const QString text = number_text(std::abs(value), decimals);
    if (std::round(value * std::pow(10, decimals)) == 0)
        return text;
    return (value > 0 ? QStringLiteral("+") : QStringLiteral("-")) + text;
}

QString won_text(double won) {
    return signed_text(won, 0) + QStringLiteral("원");
}

/// The minutes of a barrier kind: "barrier" is 4, "barrier_15m" 15; 0 for another kind.
int barrier_minutes(const QString& kind) {
    if (kind == QLatin1String("barrier"))
        return 4;
    static const QRegularExpression suffix(QStringLiteral("^barrier_(\\d+)m$"));
    const auto match = suffix.match(kind);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

} // namespace

int poll_ms(const QDateTime& now) {
    const QDateTime local = now.toTimeZone(kst());
    if (local.date().dayOfWeek() > 5)
        return kSlowMs;
    const QTime t = local.time();
    return t >= QTime(8, 59) && t < QTime(15, 40) ? kFastMs : kSlowMs;
}

Call call(const SymbolVerdict& v) {
    if (v.verdict == QLatin1String("buy"))
        return Call::Buy;
    if (v.verdict == QLatin1String("sell"))
        return Call::Sell;
    return Call::Wait;
}

QString word(const SymbolVerdict& v) {
    switch (call(v)) {
        case Call::Buy:
            return QStringLiteral("사세요");
        case Call::Sell:
            return QStringLiteral("파세요");
        case Call::Wait:
            break;
    }
    return QStringLiteral("기다리세요");
}

QString strength_text(const QString& strength) {
    if (strength == QLatin1String("strong"))
        return QStringLiteral("강함");
    if (strength == QLatin1String("moderate"))
        return QStringLiteral("보통");
    if (strength == QLatin1String("weak"))
        return QStringLiteral("약함");
    return {};
}

QString reason_text(const SymbolVerdict& v) {
    if (call(v) == Call::Wait) {
        if (v.reason == QLatin1String("no_members"))
            return v.version >= 2 ? QStringLiteral("참여 알고리즘 없음 — 기준선보다 나은 알고리즘 없음")
                                  : QStringLiteral("참여 알고리즘 없음 — 장벽 알고리즘 검증 대기");
        if (v.reason == QLatin1String("no_edge"))
            return QStringLiteral("우위 없음 — 어느 지평도 사거나 팔 만큼 기울지 않음");
        if (v.reason == QLatin1String("holding"))
            return QStringLiteral("보유 중 — 남은 지평이 돌아서지 않음");
        return v.reason;
    }
    QStringList parts;
    if (v.meaning == QLatin1String("exit"))
        parts << QStringLiteral("청산: 들고 있는 것을 파세요");
    else if (v.meaning == QLatin1String("avoid"))
        parts << QStringLiteral("회피: 들고 있지 않으면 사지 마세요");
    if (const QString strength = strength_text(v.strength); !strength.isEmpty())
        parts << QStringLiteral("강도 %1").arg(strength);
    if (v.margin)
        parts << QStringLiteral("마진 %1").arg(signed_text(*v.margin, 2));
    return parts.join(QStringLiteral(" · "));
}

QString hold_text(const SymbolVerdict& v) {
    if (!v.deadline.isValid())
        return {};
    const QString until = kst_text(v.deadline, QStringLiteral("HH:mm"));
    if (call(v) == Call::Buy && v.horizon_minutes > 0)
        return QStringLiteral("최대 보유 %1분 · %2까지").arg(v.horizon_minutes).arg(until);
    return QStringLiteral("보유 기한 %1").arg(until);
}

QString levels_text(const SymbolVerdict& v) {
    QStringList parts;
    if (v.target)
        parts << QStringLiteral("목표 %1").arg(price_text(*v.target));
    if (v.stop)
        parts << QStringLiteral("손절 %1").arg(price_text(*v.stop));
    return parts.join(QStringLiteral(" · "));
}

QString card_title(const QString& symbol, const QList<SymbolQuote>& symbols) {
    for (const auto& quote : symbols)
        if (quote.symbol == symbol && !quote.name.isEmpty())
            return QStringLiteral("%1 %2").arg(quote.name, symbol);
    return symbol;
}

QString as_of_text(const SymbolVerdict& v) {
    QString text = QStringLiteral("봉 마감 %1").arg(kst_text(v.bar_close, QStringLiteral("MM-dd HH:mm")));
    if (v.held)
        text += QStringLiteral(" · 보유 중");
    return text;
}

QString kind_text(const QString& kind, int minutes) {
    if (kind == QLatin1String("direction"))
        return QStringLiteral("방향 4분");
    if (kind == QLatin1String("range"))
        return QStringLiteral("변동폭");
    if (kind == QLatin1String("open_to_close"))
        return QStringLiteral("시가→종가");
    const int m = minutes > 0 ? minutes : barrier_minutes(kind);
    if (kind.startsWith(QLatin1String("barrier")) && m > 0)
        return QStringLiteral("장벽 %1분").arg(m);
    return kind;
}

QList<HorizonLine> horizon_lines(const SymbolVerdict& v) {
    QList<HorizonLine> out;
    for (const auto& h : v.horizons) {
        HorizonLine line;
        line.horizon = QStringLiteral("%1분").arg(h.minutes);
        line.odds = h.odds ? QStringLiteral("위 %1 · 아래 %2")
                                 .arg(percent_text(h.odds->up_first), percent_text(h.odds->down_first))
                           : dash();
        if (h.expected_net) {
            line.net = won_text(*h.expected_net);
            if (h.expected_net_ticks)
                line.net += QStringLiteral(" (%1틱)").arg(signed_text(*h.expected_net_ticks, 1));
            line.positive = std::round(*h.expected_net) > 0;
            line.negative = std::round(*h.expected_net) < 0;
        } else {
            line.net = dash();
        }
        line.chosen = h.kind == v.horizon_kind;
        QStringList tip;
        tip << QStringLiteral("%1: 지금 매도 1호가로 산 한 주의 기대 순손익(목표·손절·시간 만료, 왕복 비용 뺌)")
                   .arg(kind_text(h.kind, h.minutes));
        if (h.width_ticks)
            tip << QStringLiteral("폭 ±%1틱").arg(*h.width_ticks);
        tip << QStringLiteral("계열 %1 · 구성원 %2").arg(h.families).arg(h.members);
        if (h.odds)
            tip << QStringLiteral("위 먼저 %1 · 시간 만료 %2 · 아래 먼저 %3")
                       .arg(percent_text(h.odds->up_first), percent_text(h.odds->timeout),
                            percent_text(h.odds->down_first));
        if (!h.lean.isEmpty())
            tip << QStringLiteral("기울기 %1").arg(lean_text(h.lean, h.strength));
        if (h.target && h.stop)
            tip << QStringLiteral("목표 %1 · 손절 %2").arg(price_text(*h.target), price_text(*h.stop));
        if (!h.expected_net)
            tip << QStringLiteral("기대 순손익 없음: 호가가 없거나 매도 1호가가 손절가와 목표가 사이에 없습니다");
        line.tip = tip.join('\n');
        out.push_back(line);
    }
    return out;
}

QString status_text(const QString& status) {
    if (status.isEmpty())
        return QStringLiteral("기록 없음");
    if (status == QLatin1String("probation"))
        return QStringLiteral("검증 중");
    return standing_label(status);
}

QString lean_text(const QString& lean, const QString& strength) {
    const QString s = strength_text(strength);
    QString side;
    if (lean == QLatin1String("up"))
        side = QStringLiteral("위");
    else if (lean == QLatin1String("down"))
        side = QStringLiteral("아래");
    else
        return QStringLiteral("기울지 않음");
    return s.isEmpty() ? side : QStringLiteral("%1 · %2").arg(side, s);
}

QString percent_text(double share) {
    return number_text(share * 100, 0) + QStringLiteral("%");
}

QList<MemberRow> member_rows(const SymbolVerdict& v) {
    QList<MemberRow> out;
    for (const auto& m : v.members) {
        int minutes = 0;
        for (const auto& h : v.horizons)
            if (h.kind == m.kind)
                minutes = h.minutes;
        MemberRow row;
        row.name = predictor_label(m.name);
        row.kind = kind_text(m.kind, minutes);
        row.family = m.family;
        row.status = status_text(m.status);
        row.evidence = number_text(m.evidence, 0);
        row.up = percent_text(m.odds.up_first);
        row.timeout = percent_text(m.odds.timeout);
        row.down = percent_text(m.odds.down_first);
        row.lean = lean_text(m.lean, m.strength);
        row.weight = m.counted ? number_text(m.weight, 2) : QStringLiteral("0 (세지 않음)");
        row.counted = m.counted;
        row.active = m.status == QLatin1String("active");
        row.tip = QStringLiteral("%1 · 마진 %2\n증거: 그 종류의 예측 중 정답이 난 수")
                      .arg(m.name, signed_text(m.margin, 2));
        if (!m.ranked)
            row.tip += QStringLiteral("\n그 지평 순위의 멤버가 아니라 세지 않습니다(무게 0)");
        else if (!m.counted)
            row.tip += QStringLiteral("\n자기 구성원과 함께 있는 합의라 세지 않습니다(무게 0)");
        out.push_back(row);
    }
    return out;
}

QString no_members_text(const SymbolVerdict& v) {
    if (v.reason == QLatin1String("no_members") && v.version >= 2)
        return QStringLiteral("이 봉 마감에 순위의 멤버가 낸 장벽 예측이 없습니다. 기준선보다 나은 알고리즘이 "
                              "순위의 멤버가 되면 여기에 보입니다.");
    if (v.reason == QLatin1String("no_members"))
        return QStringLiteral("이 봉 마감에 예측을 낸 장벽 알고리즘이 없습니다. 장벽 알고리즘이 검증을 거쳐 "
                              "예측을 내기 시작하면 여기에 보입니다.");
    return QStringLiteral("구성원이 없습니다.");
}

QList<SideRow> side_rows(const SymbolVerdict& v) {
    QList<SideRow> out;
    for (const auto& s : v.side) {
        QMap<QString, double> o;
        for (const auto& [key, value] : s.output)
            o.insert(key, value);
        QStringList parts;
        const auto take = [&o, &parts](const char* key, const QString& label) {
            if (const auto it = o.find(QLatin1String(key)); it != o.end()) {
                parts << QStringLiteral("%1 %2").arg(label, percent_text(it.value()));
                o.erase(it);
            }
        };
        take("p_up", QStringLiteral("상승"));
        take("p_flat", QStringLiteral("보합"));
        take("p_down", QStringLiteral("하락"));
        take("p_small", QStringLiteral("작음"));
        take("p_medium", QStringLiteral("중간"));
        take("p_large", QStringLiteral("큼"));
        if (const auto it = o.find(QStringLiteral("expected_ticks")); it != o.end()) {
            parts << QStringLiteral("기대 %1틱").arg(signed_text(it.value(), 1));
            o.erase(it);
        }
        for (auto it = o.cbegin(); it != o.cend(); ++it)
            parts << QStringLiteral("%1 %2").arg(it.key(), number_text(it.value(), 1));
        out.push_back({predictor_label(s.name), kind_text(s.kind),
                       s.status.isEmpty() ? QString() : status_text(s.status), parts.join(QStringLiteral(" · "))});
    }
    return out;
}

QString advisory_label() {
    return QStringLiteral("참고(매매 안 함)");
}

QString advisory_horizon_text(const QString& kind, int sessions) {
    if (kind == QLatin1String("open_to_close"))
        return QStringLiteral("하루");
    if (kind == QLatin1String("open_to_close_5d"))
        return QStringLiteral("1주");
    return sessions > 0 ? QStringLiteral("%1거래일").arg(sessions) : kind;
}

QString day_call_text(const QString& call) {
    if (call == QLatin1String("up"))
        return QStringLiteral("상승");
    if (call == QLatin1String("flat"))
        return QStringLiteral("보합");
    if (call == QLatin1String("down"))
        return QStringLiteral("하락");
    return QStringLiteral("대기");
}

namespace {

QString day_odds_text(const DayOdds& o) {
    return QStringLiteral("상승 %1 · 보합 %2 · 하락 %3")
        .arg(percent_text(o.up), percent_text(o.flat), percent_text(o.down));
}

} // namespace

QList<AdvisoryLine> advisory_lines(const SymbolVerdict& v) {
    QList<AdvisoryLine> out;
    for (const auto& a : v.advisory) {
        AdvisoryLine line;
        line.horizon = advisory_horizon_text(a.kind, a.sessions);
        line.call = day_call_text(a.call);
        line.odds = a.odds ? day_odds_text(*a.odds) : dash();
        line.members = QStringLiteral("멤버 %1").arg(a.members);
        line.up = a.call == QLatin1String("up");
        line.down = a.call == QLatin1String("down");
        QStringList tip;
        tip << QStringLiteral("%1(시가→종가 %2거래일): 그날 08:50 예측을 순위의 멤버로 합친 것입니다. %3")
                   .arg(line.horizon)
                   .arg(a.sessions)
                   .arg(advisory_label());
        if (a.call == QLatin1String("wait"))
            tip << QStringLiteral("멤버의 예측이 없습니다");
        else
            tip << QStringLiteral("계열 %1 · 멤버 %2%3")
                       .arg(a.families)
                       .arg(a.members)
                       .arg(a.margin ? QStringLiteral(" · 마진 %1 (±0.05 넘으면 상승·하락)").arg(signed_text(*a.margin, 2))
                                     : QString());
        for (const auto& f : a.forecasters) {
            QString text = QStringLiteral("%1 (%2): %3")
                               .arg(predictor_label(f.name), status_text(f.status),
                                    f.odds ? day_odds_text(*f.odds) : dash());
            text += f.counted ? QStringLiteral(" · 무게 %1").arg(number_text(f.weight, 2))
                              : (f.ranked ? QStringLiteral(" · 세지 않음") : QStringLiteral(" · 순위 멤버 아님"));
            tip << text;
        }
        line.tip = tip.join('\n');
        out.push_back(line);
    }
    return out;
}

QString no_qualifier_text() {
    return QStringLiteral("기준선보다 나은 알고리즘 없음");
}

namespace {

QString ranking_kind_text(const QString& kind) {
    if (kind == QLatin1String("open_to_close") || kind == QLatin1String("open_to_close_5d"))
        return QStringLiteral("%1 · %2").arg(advisory_horizon_text(kind, 0), advisory_label());
    return kind_text(kind);
}

QString why_text(const RankingRow& r, const RankingKind& k) {
    if (r.why == QLatin1String("few_days"))
        return QStringLiteral("날 부족 (%1일 / %2일)").arg(r.days).arg(k.min_days);
    if (r.why == QLatin1String("few_calls"))
        return QStringLiteral("호출 부족 (%1 / %2)").arg(r.calls).arg(k.min_calls);
    if (r.why == QLatin1String("no_evidence"))
        return QStringLiteral("증거 없음");
    return r.why.isEmpty() ? dash() : r.why;
}

RankingLine ranking_line(const RankingRow& r, const RankingKind& k) {
    RankingLine line;
    line.rank = r.rank ? QString::number(*r.rank) : dash();
    line.algorithm = predictor_label(r.algorithm);
    line.status = status_text(r.status);
    if (r.ranked && r.skill && r.skill_low && r.skill_high)
        line.skill = QStringLiteral("%1 (%2 ~ %3)")
                         .arg(signed_text(*r.skill, 4), signed_text(*r.skill_low, 4), signed_text(*r.skill_high, 4));
    else
        line.skill = why_text(r, k);
    line.precision = r.precision && r.calls > 0 ? percent_text(*r.precision) : dash();
    line.calls = number_text(r.calls, 0);
    line.member = r.member ? QStringLiteral("예") : QStringLiteral("아니오");
    line.nights = QStringLiteral("%1밤").arg(r.nights);
    if (r.pending > 0)
        line.nights += QStringLiteral(" · 반대 %1/%2").arg(r.pending).arg(k.nights);
    line.is_member = r.member;
    line.qualifies = r.qualifies;
    line.ranked = r.ranked;
    QStringList tip;
    tip << r.algorithm;
    if (r.source == QLatin1String("live"))
        tip << QStringLiteral("증거: 기록된 실시간 예측");
    else if (r.source == QLatin1String("walk_forward"))
        tip << QStringLiteral("증거: 사전 검증의 전진 학습");
    tip << QStringLiteral("%1일 · 기후와 짝지은 예측 %2").arg(r.days).arg(number_text(r.n, 0));
    if (r.call_rate)
        tip << QStringLiteral("호출 비율 %1").arg(percent_text(*r.call_rate));
    if (r.longs > 0 && r.net_ticks)
        tip << QStringLiteral("매수 호출 %1 · 비용 뒤 평균 %2틱").arg(number_text(r.longs, 0), signed_text(*r.net_ticks, 1));
    tip << (r.qualifies ? QStringLiteral("이 밤 기술 하한 > 0: 기준선보다 낫습니다")
                        : QStringLiteral("이 밤 기술 하한 ≤ 0이거나 순위 밖입니다"));
    tip << QStringLiteral("%1 상태로 %2밤").arg(r.member ? QStringLiteral("멤버") : QStringLiteral("멤버 아님")).arg(r.nights);
    if (r.pending > 0)
        tip << QStringLiteral("반대 결과가 %1밤 이어짐: %2밤이면 바뀝니다").arg(r.pending).arg(k.nights);
    line.tip = tip.join('\n');
    return line;
}

} // namespace

QList<RankingPanel> ranking_panels(const Ranking& ranking) {
    QList<RankingPanel> out;
    for (const auto& k : ranking.kinds) {
        RankingPanel panel;
        panel.kind = k.kind;
        panel.title = QStringLiteral("순위 · %1").arg(ranking_kind_text(k.kind));
        panel.no_members = k.members.isEmpty();
        QStringList names;
        for (const auto& m : k.members)
            names << predictor_label(m);
        const QString night = k.night.isValid() ? k.night.toString(QStringLiteral("MM-dd")) + QStringLiteral(" 밤")
                                                : dash();
        panel.line = panel.no_members ? QStringLiteral("%1 · %2 — 이 지평은 대기").arg(night, no_qualifier_text())
                                      : QStringLiteral("%1 · 멤버 %2").arg(night, names.join(QStringLiteral(", ")));
        panel.tip = QStringLiteral("기술: 날마다 기후(기준선)의 로그 손실 − 자기 로그 손실의 평균과 %1% 구간. "
                                   "하한이 0보다 크면 기준선보다 낫습니다. 멤버는 결과가 %2밤 이어져야 바뀝니다. "
                                   "최소 증거 %3일 · 호출 %4%5.")
                        .arg(number_text(k.level * 100, 0))
                        .arg(k.nights)
                        .arg(k.min_days)
                        .arg(k.min_calls)
                        .arg(k.window_days ? QStringLiteral(" · 최근 %1일로 셈").arg(*k.window_days) : QString());
        for (const auto& r : k.rows)
            if (r.member)
                panel.rows.push_back(ranking_line(r, k));
        for (const auto& r : k.rows)
            if (!r.member)
                panel.rows.push_back(ranking_line(r, k));
        out.push_back(panel);
    }
    return out;
}

QString ranking_empty_text() {
    return QStringLiteral("아직 순위가 없습니다. 엔진은 날마다 점검 뒤 지평마다 순위를 만듭니다.");
}

QString ranking_failure_text(const QString& failure) {
    if (is_not_found(failure))
        return QStringLiteral("이 엔진은 순위(GET /v1/ranking)를 주지 않습니다. 엔진이 새 버전으로 배포되면 보입니다.");
    return failure;
}

std::optional<SymbolVerdict> chosen(const Verdicts& verdicts, const QString& symbol) {
    for (const auto& v : verdicts.verdicts)
        if (v.symbol == symbol)
            return v;
    if (verdicts.verdicts.isEmpty())
        return std::nullopt;
    return verdicts.verdicts.first();
}

QString empty_text() {
    return QStringLiteral("아직 판정이 없습니다. 엔진은 장중 봉 마감마다 종목별 판정을 만듭니다(예측 전이거나 그 "
                          "봉 마감에 VI가 걸린 종목은 빠집니다).");
}

QString failure_text(const QString& failure) {
    if (is_not_found(failure))
        return QStringLiteral("이 엔진은 종합 판단(GET /v1/verdict)을 주지 않습니다. 엔진이 새 버전으로 "
                              "배포되면 보입니다.");
    return failure;
}

} // namespace fincept::krx_quant::judgment
