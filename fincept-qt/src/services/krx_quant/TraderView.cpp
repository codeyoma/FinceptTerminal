#include "services/krx_quant/TraderView.h"

#include <QLocale>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::krx_quant::trader {

namespace {

const QLocale& korean() {
    static const QLocale locale(QLocale::Korean, QLocale::SouthKorea);
    return locale;
}

QString dash() {
    return QStringLiteral("—");
}

QDateTime in_kst(const QDateTime& t) {
    static const QTimeZone kst("Asia/Seoul");
    return t.toTimeZone(kst);
}

/// "10-08(목)".
QString day_text(const QDate& day) {
    static const char* const names[] = {"월", "화", "수", "목", "금", "토", "일"};
    if (!day.isValid())
        return dash();
    return QStringLiteral("%1(%2)").arg(day.toString("MM-dd"), QString::fromUtf8(names[day.dayOfWeek() - 1]));
}

Tone sign_tone(double won) {
    const double rounded = std::round(won);
    return rounded > 0 ? Tone::Positive : rounded < 0 ? Tone::Negative : Tone::Plain;
}

QString source_label(const QString& source) {
    if (source == QLatin1String("api"))
        return QStringLiteral("화면");
    if (source == QLatin1String("cli"))
        return QStringLiteral("서버");
    return source;
}

QString pause_kind(bool sell_now) {
    return sell_now ? QStringLiteral("정지하고 즉시 매도") : QStringLiteral("새 진입 정지");
}

QString role_label(const QString& role) {
    if (role == QLatin1String("consensus"))
        return QStringLiteral("합의");
    if (role == QLatin1String("top"))
        return QStringLiteral("1위");
    if (role == QLatin1String("baseline"))
        return QStringLiteral("기준");
    return role;
}

QString cell_text(const QString& kind, const QString& predictor, const QString& role = {}) {
    QString text = QStringLiteral("%1 · %2").arg(kind_label(kind), predictor_label(predictor));
    if (!role.isEmpty())
        text += QStringLiteral(" (%1)").arg(role_label(role));
    return text;
}

QString optional_percent(const std::optional<double>& share) {
    return share ? percent_text(*share) : dash();
}

/// A loss limit's card: its use now, the limit in won and percent, and when it was reached.
Card limit_card(const QString& title, const std::optional<TraderLimit>& limit, bool cumulative) {
    Card card;
    card.title = title;
    if (!limit) {
        card.value = dash();
        card.tone = Tone::Dim;
        return card;
    }
    card.value = percent_text(limit->used);
    card.lines << QStringLiteral("한도 -%1 (운용 상한의 %2%)")
                      .arg(won_text(limit->won), korean().toString(limit->percent, 'g', 4));
    if (limit->reached_at.isValid()) {
        card.lines << QStringLiteral("닿음 %1").arg(kst_text(limit->reached_at, "MM-dd HH:mm:ss"));
        card.tone = Tone::Negative;
    } else if (limit->used >= 0.5) {
        card.tone = Tone::Warning;
    }
    card.tip = cumulative ? QStringLiteral("누적 손익(비용을 뺀 실현 + 열린 포지션의 평가)이 누적 손실 한도에서 쓴 몫입니다. "
                                           "닿으면 열린 포지션을 바로 팔고, 서버의 krx-quant trade rearm으로 다시 켤 "
                                           "때까지 사지 않습니다.")
                          : QStringLiteral("오늘(0시부터) 손익(비용을 뺀 실현 + 열린 포지션의 평가)이 하루 손실 한도에서 쓴 "
                                           "몫입니다. 닿으면 열린 포지션을 바로 팔고 그날은 사지 않습니다. 다음 거래일에 "
                                           "저절로 다시 돕니다.");
    return card;
}

/// A P&L's card: its net, its realized part with the trades exited, its open part and its lowest.
Card pnl_card(const QString& title, const std::optional<TraderPnl>& pnl, bool cumulative) {
    Card card;
    card.title = title;
    if (!pnl) {
        card.value = dash();
        card.tone = Tone::Dim;
        return card;
    }
    card.value = signed_won_text(pnl->net);
    card.tone = sign_tone(pnl->net);
    if (cumulative)
        card.lines << QStringLiteral("%1부터").arg(kst_text(pnl->since, "yyyy-MM-dd HH:mm"));
    card.lines << (cumulative ? QStringLiteral("실현 %1").arg(signed_won_text(pnl->realized))
                              : QStringLiteral("실현 %1 · 매매 %2").arg(signed_won_text(pnl->realized)).arg(pnl->trades));
    card.lines << QStringLiteral("평가 %1").arg(signed_won_text(pnl->unrealized));
    card.lines << QStringLiteral("가장 낮을 때 %1").arg(signed_won_text(pnl->worst));
    card.tip = cumulative ? QStringLiteral("그 모드를 처음 켠 때부터(서버에서 다시 켰으면 그때부터) 청산한 매매의 순손익(수수료와 "
                                           "매도세를 뺌)과 열린 포지션을 지금 매수 1호가에 팔면 받을 순손익의 합입니다.")
                          : QStringLiteral("오늘 0시부터 청산한 매매의 순손익(수수료와 매도세를 뺌)과 열린 포지션을 지금 매수 "
                                           "1호가에 팔면 받을 순손익의 합입니다.");
    return card;
}

/// Whether ``controls`` hold a pause asked for and not read yet that covers ``sell_now``'s: one that
/// sells covers both buttons.
const TraderControl* waiting_pause(const QList<TraderControl>& controls, bool sell_now) {
    for (const auto& c : controls)
        if (c.action == QLatin1String("pause") && !c.applied_at.isValid() && (!sell_now || c.sell_now.value_or(false)))
            return &c;
    return nullptr;
}

} // namespace

bool in_session(const QDateTime& now) {
    const QDateTime local = in_kst(now);
    const int weekday = local.date().dayOfWeek();
    const QTime t = local.time();
    return weekday >= 1 && weekday <= 5 && t >= QTime(9, 0) && t < QTime(15, 20);
}

int poll_ms(const std::optional<TraderState>& last, const QDateTime& now) {
    if (last && last->alive && last->position)
        return kHeldMs;
    return in_session(now) ? kSessionMs : kIdleMs;
}

QString history_key(const TraderState& state) {
    const QStringList parts{
        state.mode,
        state.status,
        state.started_at.toString(Qt::ISODateWithMs),
        state.today ? state.today->day.toString(Qt::ISODate) : QString(),
        state.today ? QString::number(state.today->trades) : QString(),
        state.cumulative ? state.cumulative->since.toString(Qt::ISODateWithMs) : QString(),
        state.position ? QString::number(state.position->trade_id) : QString(),
    };
    return parts.join('|');
}

Badge mode_badge(const TraderState& state) {
    if (!state.written())
        return {dash(), QStringLiteral("trader가 아직 상태를 쓰지 않았습니다."), Tone::Dim};
    Badge badge{trader_mode_label(state.mode), {}, Tone::Simulated};
    if (state.mode == QLatin1String("paper"))
        badge.tip = QStringLiteral("내부 모의: 실시간 호가로 체결을 흉내 냅니다. 주문을 내지 않습니다.");
    else if (state.mode == QLatin1String("mock"))
        badge.tip = QStringLiteral("KIS 모의투자: KIS 모의투자 계좌에 주문합니다.");
    else if (state.mode == QLatin1String("live")) {
        badge.tip = QStringLiteral("실전: 실전 계좌에 주문합니다.");
        badge.tone = Tone::Live;
    }
    badge.tip += QStringLiteral(" 모드는 서버의 .env(KRX_TRADER_MODE)로만 바뀝니다.");
    return badge;
}

Badge status_badge(const TraderState& state) {
    if (!state.written())
        return {QStringLiteral("상태 없음"),
                QStringLiteral("trader가 아직 상태를 쓰지 않았습니다(시작 전이거나, 이 엔진에 trader 저장소가 없습니다)."),
                Tone::Dim};
    const QString label = trader_status_label(state.status);
    if (!state.alive)
        return {QStringLiteral("응답 없음"),
                QStringLiteral("trader가 %1 뒤로 상태를 쓰지 않았습니다(멈췄거나 다시 뜨는 중). 아래 값은 그때의 것이고, 그때의 "
                               "상태는 \"%2\"입니다.")
                    .arg(kst_text(state.updated_at, "MM-dd HH:mm:ss"), label),
                Tone::Negative};
    if (state.status == QLatin1String("running"))
        return {label, QStringLiteral("비용 지도가 고른 칸의 상승 예측으로 삽니다."), Tone::Positive};
    if (state.status == QLatin1String("entries_paused"))
        return {label,
                QStringLiteral("새로 사지 않습니다. 들고 있는 것은 원래 규칙(목표가, 손절가, 시간 장벽)대로 팝니다. 다시 켜기는 "
                               "서버의 krx-quant trade resume뿐입니다."),
                Tone::Warning};
    if (state.status == QLatin1String("paused"))
        return {label,
                QStringLiteral("새로 사지 않고, 들고 있던 것은 바로 팝니다. 다시 켜기는 서버의 krx-quant trade resume뿐입니다."),
                Tone::Warning};
    if (state.status == QLatin1String("daily_limit"))
        return {label,
                QStringLiteral("하루 손실 한도에 닿아 오늘은 사지 않습니다(들고 있던 것은 바로 팝니다). 다음 거래일에 저절로 "
                               "다시 돕니다."),
                Tone::Negative};
    if (state.status == QLatin1String("cumulative_limit"))
        return {label,
                QStringLiteral("누적 손실 한도에 닿아 멈췄습니다(들고 있던 것은 바로 팝니다). 서버의 krx-quant trade rearm으로 "
                               "다시 켤 때까지 사지 않습니다."),
                Tone::Negative};
    return {label, {}, Tone::Plain};
}

QString header_line(const TraderState& state) {
    if (!state.written())
        return QStringLiteral("trader가 아직 상태를 쓰지 않았습니다");
    QStringList parts{QStringLiteral("상태 %1").arg(kst_text(state.updated_at, "MM-dd HH:mm:ss"))};
    if (state.started_at.isValid())
        parts << QStringLiteral("시작 %1").arg(kst_text(state.started_at, "MM-dd HH:mm"));
    if (state.pause)
        parts << QStringLiteral("정지 %1 %2(%3)")
                     .arg(kst_text(state.pause->at, "MM-dd HH:mm"), source_label(state.pause->source),
                          pause_kind(state.pause->sell_now));
    return parts.join(QStringLiteral(" · "));
}

QDateTime trader_now(const TraderState& state, const QDateTime& answered_at, const QDateTime& now) {
    if (!state.written() || !answered_at.isValid())
        return now;
    return state.updated_at.addMSecs(std::max<qint64>(0, answered_at.msecsTo(now)));
}

QString remaining_text(const QDateTime& barrier, const QDateTime& now) {
    const qint64 left = now.secsTo(barrier);
    if (!barrier.isValid() || left <= 0)
        return QStringLiteral("지남");
    if (left < 60)
        return QStringLiteral("%1초 남음").arg(left);
    return QStringLiteral("%1분 %2초 남음").arg(left / 60).arg(left % 60);
}

QList<Card> cards(const TraderState& state, const QDateTime& now) {
    QList<Card> out;
    Card capital{QStringLiteral("운용 상한"), state.capital ? won_text(*state.capital) : dash(), {}, {}, Tone::Plain};
    capital.tip = QStringLiteral("trader가 쓰는 돈의 상한입니다. 코드의 값과 서버 .env에서만 오고, .env로 좁힐 수는 있어도 "
                                 "넓힐 수는 없습니다. 정책(자기 개선)도 바꾸지 못합니다.");
    out << capital;

    Card cash{QStringLiteral("현금"), state.cash ? won_text(*state.cash) : dash(), {}, {}, Tone::Plain};
    cash.lines << QStringLiteral("주문 가능");
    cash.tip = QStringLiteral("지금 주문에 쓸 수 있는 현금입니다. 모의는 운용 상한 + 청산한 매매의 순손익 − 열린 포지션의 매수 "
                              "금액과 수수료입니다.");
    out << cash;

    Card position{QStringLiteral("포지션"), {}, {}, {}, Tone::Plain};
    if (!state.written()) {
        position.value = dash();
        position.tone = Tone::Dim;
    } else if (!state.position) {
        position.value = QStringLiteral("없음");
        position.tone = Tone::Dim;
        position.tip = QStringLiteral("들고 있는 주식이 없습니다. 한 번에 포지션 하나만 듭니다.");
    } else {
        const TraderPosition& p = *state.position;
        position.value = QStringLiteral("%1주 × %2원").arg(p.quantity).arg(price_text(p.entry_price));
        position.lines << QStringLiteral("목표가 %1원 · 손절가 %2원").arg(price_text(p.target), price_text(p.stop));
        position.lines << QStringLiteral("시간 장벽 %1 · %2")
                              .arg(kst_text(p.time_barrier, "HH:mm:ss"), remaining_text(p.time_barrier, now));
        position.lines << (p.bid ? QStringLiteral("매수 1호가 %1원 · 평가 %2")
                                       .arg(price_text(*p.bid), signed_won_text(p.unrealized))
                                 : QStringLiteral("산 뒤의 호가 아직 없음"));
        position.lines << cell_text(p.kind, p.predictor);
        if (p.selling) {
            position.lines << QStringLiteral("매도 중: 다음 호가의 매수 1호가에 팝니다");
            position.tone = Tone::Warning;
        }
        position.tip = QStringLiteral("매매 %1 · %2 매수. 목표가에 닿으면 익절, 손절가에 닿으면 손절, 시간 장벽까지 둘 다 "
                                      "닿지 않으면 그때 팝니다. 평가는 지금 매수 1호가에 팔면 받을 순손익(수수료와 매도세를 "
                                      "뺌)입니다%3.")
                           .arg(p.trade_id)
                           .arg(kst_text(p.entry_at, "MM-dd HH:mm:ss"),
                                p.bid_at.isValid() ? QStringLiteral(" (호가 %1)").arg(kst_text(p.bid_at, "HH:mm:ss"))
                                                   : QString());
    }
    out << position;
    out << pnl_card(QStringLiteral("오늘 순손익"), state.today, false);
    out << pnl_card(QStringLiteral("누적 순손익"), state.cumulative, true);
    out << limit_card(QStringLiteral("하루 한도 사용"), state.daily_limit, false);
    out << limit_card(QStringLiteral("누적 한도 사용"), state.cumulative_limit, true);
    return out;
}

QList<TradeRow> trade_rows(const QList<TraderTrade>& trades, const std::optional<TraderState>& state) {
    QList<TradeRow> rows;
    for (const auto& t : trades) {
        TradeRow r;
        r.id = t.id;
        r.held = t.held();
        r.live = t.mode == QLatin1String("live");
        r.mode = trader_mode_label(t.mode);
        r.entry = QStringLiteral("%1 · %2").arg(kst_text(t.entry_at, "MM-dd HH:mm:ss"), price_text(t.entry_price));
        r.quantity = QStringLiteral("%1주").arg(t.quantity);
        const bool open_now = state && state->position && state->position->trade_id == t.id;
        if (r.held) {
            r.exit = QStringLiteral("보유 중");
            r.reason = open_now && state->position->selling ? QStringLiteral("매도 중") : dash();
            r.reason_tone = open_now && state->position->selling ? Tone::Warning : Tone::Dim;
            r.cost = QStringLiteral("%1 (매수)").arg(won_text(t.buy_fee));
            r.cost_tip = QStringLiteral("매수 수수료 %1. 매도 수수료와 매도세는 팔 때 더합니다.").arg(won_text(t.buy_fee));
            if (open_now) {
                r.net = QStringLiteral("평가 %1").arg(signed_won_text(state->position->unrealized));
                r.net_tip = QStringLiteral("지금 매수 1호가에 팔면 받을 순손익(수수료와 매도세를 뺌)입니다.");
            } else {
                r.net = dash();
            }
            r.net_tone = Tone::Dim;
        } else {
            const bool same_day = in_kst(t.exit_at).date() == in_kst(t.entry_at).date();
            r.exit = QStringLiteral("%1 · %2").arg(kst_text(t.exit_at, same_day ? "HH:mm:ss" : "MM-dd HH:mm:ss"),
                                                    t.exit_price ? price_text(*t.exit_price) : dash());
            r.reason = trader_exit_label(t.exit_reason);
            if (t.exit_reason == QLatin1String("take_profit"))
                r.reason_tone = Tone::Positive;
            else if (t.exit_reason == QLatin1String("stop_loss") || t.exit_reason == QLatin1String("daily_limit") ||
                     t.exit_reason == QLatin1String("cumulative_limit"))
                r.reason_tone = Tone::Negative;
            else if (t.exit_reason == QLatin1String("pause"))
                r.reason_tone = Tone::Warning;
            r.cost = t.cost ? won_text(*t.cost) : dash();
            r.cost_tip = QStringLiteral("매수 수수료 %1 + 매도 수수료 %2 + 매도세 %3")
                             .arg(won_text(t.buy_fee), t.sell_fee ? won_text(*t.sell_fee) : dash(),
                                  t.tax ? won_text(*t.tax) : dash());
            r.net = t.net ? signed_won_text(*t.net) : dash();
            r.net_tone = t.net ? sign_tone(*t.net) : Tone::Dim;
            r.net_tip = QStringLiteral("총손익 %1 − 비용 %2")
                            .arg(t.gross ? signed_won_text(*t.gross) : dash(), t.cost ? won_text(*t.cost) : dash());
        }
        r.cell = cell_text(t.kind, t.predictor, t.role);
        r.probability = t.probability ? (t.threshold ? QStringLiteral("%1 (θ %2)").arg(percent_text(*t.probability),
                                                                                       percent_text(*t.threshold))
                                                     : percent_text(*t.probability))
                                      : dash();
        r.p_star = optional_percent(t.p_star);
        r.low = optional_percent(t.precision_low);
        QStringList why{QStringLiteral("오름 확률(방향은 상승, 장벽은 위 먼저)이 문턱 θ 이상이면 삽니다.")};
        if (t.p_star || t.precision_low)
            why << QStringLiteral("p*: 비용 지도가 그 칸을 고를 때 시장가 왕복의 손익분기 맞힘률. 하한: 그 칸의 맞힘률 95% "
                                  "하한(Wilson). 하한이 p*를 넘는 칸만 매매합니다.");
        else
            why << QStringLiteral("p*와 하한: 비용 지도가 칸을 고르기(#395) 전의 매매라 없습니다.");
        if (t.kelly)
            why << QStringLiteral("Kelly f* %1 → 산 비율 %2")
                       .arg(korean().toString(*t.kelly, 'f', 2), optional_percent(t.fraction));
        r.why_tip = why.join('\n');
        QStringList tip{QStringLiteral("매매 %1 · %2 · 봉 마감 %3 · 정한 때 %4")
                            .arg(QString::number(t.id), r.mode, kst_text(t.bar_close, "MM-dd HH:mm"),
                                 kst_text(t.decided_at, "HH:mm:ss"))};
        tip << QStringLiteral("기준가 %1 · 틱 %2 · 폭 %3틱 → 목표가 %4 · 손절가 %5 · 시간 장벽 %6")
                   .arg(t.anchor ? price_text(*t.anchor) : dash(), t.tick ? price_text(*t.tick) : dash())
                   .arg(t.width_ticks)
                   .arg(price_text(t.target), price_text(t.stop), kst_text(t.time_barrier, "HH:mm:ss"));
        if (t.policy)
            tip << QStringLiteral("정책 버전 %1").arg(*t.policy);
        r.tip = tip.join('\n');
        rows << r;
    }
    return rows;
}

QStringList day_modes(const QList<TraderDay>& days) {
    QList<TraderDay> latest_first = days;
    std::stable_sort(latest_first.begin(), latest_first.end(),
                     [](const TraderDay& a, const TraderDay& b) { return a.day > b.day; });
    QStringList modes;
    for (const auto& d : latest_first)
        if (!modes.contains(d.mode))
            modes << d.mode;
    return modes;
}

QString default_mode(const QList<TraderDay>& days, const std::optional<TraderState>& state) {
    const QStringList modes = day_modes(days);
    if (state && state->written() && modes.contains(state->mode))
        return state->mode;
    return modes.value(0);
}

QList<DayPoint> day_points(const QList<TraderDay>& days, const QString& mode) {
    QList<TraderDay> of_mode;
    for (const auto& d : days)
        if (d.mode == mode)
            of_mode << d;
    std::stable_sort(of_mode.begin(), of_mode.end(), [](const TraderDay& a, const TraderDay& b) { return a.day < b.day; });
    QList<DayPoint> points;
    double sum = 0;
    for (const auto& d : of_mode) {
        sum += d.net;
        QStringList tip{QStringLiteral("%1 %2").arg(day_text(d.day), trader_mode_label(d.mode)),
                        QStringLiteral("순손익 %1 · 매매 %2").arg(signed_won_text(d.net)).arg(d.trades),
                        QStringLiteral("합 %1 (보이는 첫날부터)").arg(signed_won_text(sum)),
                        QStringLiteral("하루 가장 낮을 때 %1 (한도의 %2)")
                            .arg(signed_won_text(d.daily_worst), percent_text(d.daily_used)),
                        QStringLiteral("누적 %1 (가장 낮을 때 %2, 한도의 %3)")
                            .arg(signed_won_text(d.cumulative_net), signed_won_text(d.cumulative_worst),
                                 percent_text(d.cumulative_used)),
                        QStringLiteral("그날 마지막 상태: %1").arg(trader_status_label(d.status))};
        if (!d.halt.isEmpty())
            tip << QStringLiteral("%1에 닿은 날").arg(trader_status_label(d.halt));
        points << DayPoint{d.day, d.net, sum, tip.join('\n'), !d.halt.isEmpty()};
    }
    return points;
}

QString days_line(const QList<DayPoint>& points, const QString& mode) {
    if (points.isEmpty())
        return mode.isEmpty() ? QStringLiteral("trader가 돈 거래일이 아직 없습니다")
                              : QStringLiteral("%1 거래일이 없습니다").arg(trader_mode_label(mode));
    const auto gains = std::count_if(points.begin(), points.end(), [](const DayPoint& p) { return std::round(p.net) > 0; });
    const auto losses = std::count_if(points.begin(), points.end(), [](const DayPoint& p) { return std::round(p.net) < 0; });
    return QStringLiteral("%1 %2일 · 순손익 합 %3 · 이익 %4일 · 손실 %5일")
        .arg(trader_mode_label(mode))
        .arg(points.size())
        .arg(signed_won_text(points.last().sum))
        .arg(gains)
        .arg(losses);
}

QList<ControlRow> control_rows(const TraderState& state) {
    QList<ControlRow> rows;
    for (const auto& c : state.controls) {
        ControlRow r;
        r.id = c.id;
        r.at = kst_text(c.at, "MM-dd HH:mm:ss");
        if (c.action == QLatin1String("pause"))
            r.what = pause_kind(c.sell_now.value_or(false));
        else if (c.action == QLatin1String("resume"))
            r.what = QStringLiteral("재개");
        else if (c.action == QLatin1String("rearm"))
            r.what = c.mode.isEmpty() ? QStringLiteral("누적 한도 다시 켜기")
                                      : QStringLiteral("누적 한도 다시 켜기 (%1)").arg(trader_mode_label(c.mode));
        else
            r.what = c.action;
        r.who = source_label(c.source);
        r.waiting = !c.applied_at.isValid();
        r.applied = r.waiting ? QStringLiteral("아직 읽지 않음")
                              : QStringLiteral("읽음 %1").arg(kst_text(c.applied_at, "HH:mm:ss"));
        rows << r;
    }
    return rows;
}

Question pause_question(bool sell_now, const std::optional<TraderState>& state) {
    Question q;
    q.title = pause_kind(sell_now);
    q.accept = pause_kind(sell_now);
    const std::optional<TraderPosition> held = state ? state->position : std::nullopt;
    QStringList text;
    if (sell_now) {
        if (held)
            text << QStringLiteral("자동 매매가 새로 사지 않게 하고, 들고 있는 %1주(%2, 평가 %3)를 다음 호가의 매수 1호가에 바로 "
                                   "팝니다.")
                        .arg(held->quantity)
                        .arg(cell_text(held->kind, held->predictor), signed_won_text(held->unrealized));
        else
            text << QStringLiteral("자동 매매가 새로 사지 않게 합니다. 지금 들고 있는 포지션은 없습니다. 정지하는 동안 산 것이 "
                                   "있으면 바로 팝니다.");
        if (state && state->mode == QLatin1String("live"))
            text << QStringLiteral("모드: 실전. 실전 계좌의 매도 주문입니다.");
        else if (state && state->mode == QLatin1String("mock"))
            text << QStringLiteral("모드: KIS 모의. KIS 모의투자 계좌의 매도 주문입니다.");
        else if (state && state->mode == QLatin1String("paper"))
            text << QStringLiteral("모드: 모의. 주문을 내지 않고 체결을 흉내 냅니다.");
    } else {
        text << QStringLiteral("자동 매매가 새로 사지 않게 합니다.");
        text << (held ? QStringLiteral("들고 있는 %1주(%2)는 원래 규칙(목표가, 손절가, 시간 장벽)대로 팝니다.")
                            .arg(held->quantity)
                            .arg(cell_text(held->kind, held->predictor))
                      : QStringLiteral("지금 들고 있는 포지션은 없습니다."));
    }
    text << QString();
    text << QStringLiteral("trader가 1초 안에 읽습니다. 다시 켜는 것은 서버에서만 됩니다(krx-quant trade resume). 이 화면에는 "
                           "재개 버튼이 없습니다.");
    text << QString();
    text << (sell_now ? QStringLiteral("정지하고 즉시 팔까요?") : QStringLiteral("새 진입을 정지할까요?"));
    q.text = text.join('\n');
    return q;
}

Action pause_action(bool sell_now, const std::optional<TraderState>& state) {
    if (!state)
        return {false, QStringLiteral("엔진에서 trader 상태를 읽은 뒤에 누를 수 있습니다.")};
    if (state->pause && (!sell_now || state->pause->sell_now))
        return {false, QStringLiteral("이미 정지돼 있습니다(%1 %2, %3). 다시 켜기는 서버의 krx-quant trade resume뿐입니다.")
                           .arg(kst_text(state->pause->at, "MM-dd HH:mm"), source_label(state->pause->source),
                                pause_kind(state->pause->sell_now))};
    if (const TraderControl* waiting = waiting_pause(state->controls, sell_now))
        return {false, QStringLiteral("%1 %2 요청을 보냈고, trader가 아직 읽지 않았습니다.")
                           .arg(kst_text(waiting->at, "MM-dd HH:mm:ss"), pause_kind(waiting->sell_now.value_or(false)))};
    return {true, sell_now ? QStringLiteral("새로 사지 않게 하고, 들고 있는 것을 바로 팝니다. 확인 창을 거칩니다.")
                           : QStringLiteral("새로 사지 않게 합니다. 들고 있는 것은 원래 규칙대로 팝니다. 확인 창을 거칩니다.")};
}

QString resume_note() {
    return QStringLiteral("재개는 서버에서만 됩니다: krx-quant trade resume (정지), krx-quant trade rearm (누적 한도). 이 화면에서 "
                          "할 수 있는 것은 정지뿐입니다.");
}

QString won_text(double won) {
    return korean().toString(std::round(won), 'f', 0) + QStringLiteral("원");
}

QString signed_won_text(double won) {
    const double rounded = std::round(won);
    const QString sign = rounded > 0 ? QStringLiteral("+") : rounded < 0 ? QStringLiteral("-") : QString();
    return sign + korean().toString(std::abs(rounded), 'f', 0) + QStringLiteral("원");
}

QString percent_text(double share, int decimals) {
    return korean().toString(share * 100, 'f', decimals) + QStringLiteral("%");
}

QString kind_label(const QString& kind) {
    if (kind == QLatin1String("direction"))
        return QStringLiteral("방향 4분");
    if (kind == QLatin1String("barrier"))
        return QStringLiteral("장벽 4분");
    if (kind.startsWith(QLatin1String("barrier_")) && kind.endsWith('m'))
        return QStringLiteral("장벽 %1분").arg(kind.mid(8).chopped(1));
    return kind;
}

} // namespace fincept::krx_quant::trader
