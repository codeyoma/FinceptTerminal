#include "services/krx_quant/TraderView.h"

#include <QHash>
#include <QLocale>
#include <QMap>
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

/// Whether a practice P&L (codeyoma/krx-quant#533) holds anything: a trade exited or a won of P&L.
bool practised(const std::optional<TraderPnlPart>& p) {
    return p && (p->trades > 0 || std::round(p->realized) != 0 || std::round(p->unrealized) != 0 ||
                 std::round(p->net) != 0);
}

bool practised(const std::optional<TraderDayPnl>& p) {
    return p && (p->trades > 0 || std::round(p->net) != 0 || std::round(p->cumulative) != 0);
}

/// Whether the trader counts the mock practice's P&L apart from the loss limits' (codeyoma/krx-quant#533): its
/// state gives ``practice`` beside ``today`` or ``cumulative``. A trader before it counted the practice in them.
bool practice_apart(const TraderState& state) {
    return (state.today && state.today->practice) || (state.cumulative && state.cumulative->practice);
}

/// What the practice trades count for, said on the practice badge and a practice trade's tooltip.
QString practice_counts_text(bool apart) {
    return apart ? QStringLiteral("연습 매매는 승격, 실전 관문, 정책의 모의 기록과 손실 한도(codeyoma/krx-quant#533)에는 세지 "
                                  "않고, 위험 규칙(그날 매수 주문 수, 연달은 순손실)과 실전의 실행 점검(KIS 대조, 체결 차이, "
                                  "왕복 매매 수)에는 셉니다.")
                 : QStringLiteral("연습 매매는 승격, 실전 관문, 정책의 모의 기록에는 세지 않고, 손실 한도와 실전의 실행 점검(KIS "
                                  "대조, 체결 차이, 왕복 매매 수)에는 셉니다.");
}

/// A loss limit's card: its use now, the limit in won and percent, and when it was reached; with
/// ``practice_left_out`` (codeyoma/krx-quant#533: the mock practice on, or practice P&L there) that the limit
/// does not count the practice.
Card limit_card(const QString& title, const std::optional<TraderLimit>& limit, bool cumulative,
                bool practice_left_out) {
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
    if (practice_left_out)
        card.lines << QStringLiteral("연습은 한도에 세지 않음");
    card.tip = cumulative ? QStringLiteral("누적 손익(비용을 뺀 실현 + 열린 포지션의 평가)이 누적 손실 한도에서 쓴 몫입니다. "
                                           "닿으면 열린 포지션을 바로 팔고, 서버의 krx-quant trade rearm으로 다시 켤 "
                                           "때까지 사지 않습니다.")
                          : QStringLiteral("오늘(0시부터) 손익(비용을 뺀 실현 + 열린 포지션의 평가)이 하루 손실 한도에서 쓴 "
                                           "몫입니다. 닿으면 열린 포지션을 바로 팔고 그날은 사지 않습니다. 다음 거래일에 "
                                           "저절로 다시 돕니다.");
    if (practice_left_out)
        card.tip += QStringLiteral(" KIS 모의 연습 매매의 손익은 한도에 세지 않습니다(codeyoma/krx-quant#533): %1 카드의 값이 "
                                   "한도가 세는 손익(챔피언의 매매)이고, 연습과 계좌(KIS)의 손익은 그 아래 줄에 있습니다. "
                                   "챔피언의 손실로 한도에 닿으면 연습도 멈춥니다.")
                        .arg(cumulative ? QStringLiteral("누적 순손익") : QStringLiteral("오늘 순손익"));
    return card;
}

/// "실현 -9,140원 · 매매 20 · 평가 0원": a practice or account P&L in full, on its card's tooltip.
QString part_tip(const TraderPnlPart& p, bool cumulative) {
    return cumulative ? QStringLiteral("실현 %1 · 평가 %2").arg(signed_won_text(p.realized), signed_won_text(p.unrealized))
                      : QStringLiteral("실현 %1 · 매매 %2 · 평가 %3")
                            .arg(signed_won_text(p.realized))
                            .arg(p.trades)
                            .arg(signed_won_text(p.unrealized));
}

/// A P&L's card: its net, its realized part with the trades exited, its open part and its lowest. With
/// practice P&L (codeyoma/krx-quant#533: the mock practice traded) a line of the practice's and the
/// account's under them; the value stays what the loss limits count.
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
    if (practised(pnl->practice)) {
        QString line = QStringLiteral("연습 %1").arg(signed_won_text(pnl->practice->net));
        if (pnl->account)
            line += QStringLiteral(" · 계좌 %1").arg(signed_won_text(pnl->account->net));
        card.lines << line;
        card.tip += QStringLiteral("\n위 값은 손실 한도가 세는 손익(챔피언의 매매)입니다. KIS 모의 연습 매매의 손익은 한도에 세지 "
                                   "않고 따로 셉니다(codeyoma/krx-quant#533).\n연습: %1 · 합 %2")
                        .arg(part_tip(*pnl->practice, cumulative), signed_won_text(pnl->practice->net));
        if (pnl->account)
            card.tip += QStringLiteral("\n계좌(KIS, 한도가 세는 것과 연습의 합): %1 · 합 %2")
                            .arg(part_tip(*pnl->account, cumulative), signed_won_text(pnl->account->net));
    }
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

/// A confidence level: "95%", "98.33%".
QString level_text(double level) {
    return korean().toString(level * 100, 'g', 4) + QStringLiteral("%");
}

/// A Kelly fraction λ: "0.5", "0.25", "1".
QString fraction_text(double fraction) {
    return korean().toString(fraction, 'g', 3);
}

/// Ticks with their sign: "+0.50틱", "-1.25틱", "0.00틱".
QString ticks_text(double ticks) {
    const QString sign = ticks > 0.005 ? QStringLiteral("+") : QString();
    return sign + korean().toString(ticks, 'f', 2) + QStringLiteral("틱");
}

/// Seconds: "0.2초", "12.0초".
QString seconds_text(double seconds) {
    return korean().toString(seconds, 'f', 1) + QStringLiteral("초");
}

/// A share of a count: "40%"; "—" of none.
QString share_text(int n, int of) {
    return of > 0 ? korean().toString(std::round(100.0 * n / of), 'f', 0) + QStringLiteral("%") : dash();
}

/// A cell's calibration (codeyoma/krx-quant#475), a line a bin with rises: "[0.6, 0.7) 25건 · 상승 40% · 보합 24% · 하락
/// 36% · 목표 먼저 36% · 순이익 32%(체결 23)".
QStringList calibration_lines(const QList<TraderCalibrationBin>& bins, int below) {
    QStringList out;
    for (const auto& b : bins) {
        if (b.rises == 0)
            continue;
        const QString span = b.to >= 1 ? QStringLiteral("[%1, 1]").arg(korean().toString(b.from, 'g', 3))
                                       : QStringLiteral("[%1, %2)").arg(korean().toString(b.from, 'g', 3),
                                                                        korean().toString(b.to, 'g', 3));
        out << QStringLiteral("%1 %2건 · 상승 %3 · 보합 %4 · 하락 %5 · 목표 먼저 %6 · 순이익 %7(체결 %8)")
                   .arg(span)
                   .arg(b.rises)
                   .arg(share_text(b.up, b.answered), share_text(b.flat, b.answered), share_text(b.down, b.answered),
                        share_text(b.first, b.answered), share_text(b.profitable, b.bought))
                   .arg(b.bought);
    }
    if (below > 0)
        out << QStringLiteral("0.4 아래 %1건(어느 θ도 사지 않는다)").arg(below);
    return out;
}

/// The engine judges challengers as a cohort once, on a review night (codeyoma/krx-quant#473): it gives
/// cohorts, a review cycle or a challenger's review day.
bool cohort_engine(const std::optional<TraderPolicies>& policies, const std::optional<TraderState>& state) {
    if (policies && !policies->cohorts.isEmpty())
        return true;
    if (!state)
        return false;
    if (state->cycle || state->cohort)
        return true;
    for (const auto& p : state->policies)
        if (p.promotion && p.promotion->review.isValid())
            return true;
    return false;
}

/// A cohort member's verdict in Korean (codeyoma/krx-quant#473).
QString cohort_verdict_label(const QString& verdict) {
    static const QMap<QString, QString> labels = {
        {"promoted", QStringLiteral("챔피언이 됨")},
        {"passed", QStringLiteral("넘었지만 다른 정책의 하한이 더 큼")},
        {"not_above", QStringLiteral("하한 ≤ 0")},
        {"undecided", QStringLiteral("날 수 부족(판정 불가)")},
        {"missed", QStringLiteral("판정 못 함")},
    };
    return labels.value(verdict, verdict);
}

/// The risk rules' card (codeyoma/krx-quant#475) at ``now``, the trader's clock.
Card risk_card(const TraderState& state, const QDateTime& now) {
    Card card{QStringLiteral("위험 규칙"), {}, {}, {}, Tone::Plain};
    if (!state.risk) {
        card.value = dash();
        card.tone = Tone::Dim;
        card.tip = state.written() ? QStringLiteral("이 엔진의 trader는 위험 규칙의 상태를 쓰지 않습니다(codeyoma/krx-quant#475 전).")
                                   : QStringLiteral("trader가 아직 상태를 쓰지 않았습니다.");
        return card;
    }
    const TraderRisk& r = *state.risk;
    const bool cooling = r.cooldown_until.isValid() && now < r.cooldown_until;
    const bool limited = r.daily_buys > 0 && r.buys >= r.daily_buys;
    if (cooling) {
        card.value = QStringLiteral("쉬는 중");
        card.lines << QStringLiteral("%1까지 새로 사지 않음").arg(kst_text(r.cooldown_until, "HH:mm:ss"));
        card.tone = Tone::Warning;
    } else if (limited) {
        card.value = QStringLiteral("매수 한도");
        card.tone = Tone::Warning;
    } else {
        card.value = QStringLiteral("매수 %1/%2").arg(r.buys).arg(r.daily_buys);
    }
    if (cooling || limited)
        card.lines << QStringLiteral("오늘 매수 주문 %1/%2").arg(r.buys).arg(r.daily_buys);
    card.lines << QStringLiteral("연달은 순손실 %1/%2 · 쉬면 %3분")
                      .arg(r.losses)
                      .arg(r.loss_streak)
                      .arg(korean().toString(r.cooldown_minutes, 'g', 4));
    if (r.cooldowns > 0)
        card.lines << QStringLiteral("오늘 쉼 %1번").arg(r.cooldowns);
    card.lines << QStringLiteral("손실 예산 %1 (%2%)").arg(won_text(r.budget), korean().toString(r.budget_percent, 'g', 3));
    if (r.day.isValid() && r.day != in_kst(now).date())
        card.lines << QStringLiteral("%1의 값(trader가 그 뒤 쓰지 않음)").arg(r.day.toString("MM-dd"));
    card.tip = QStringLiteral("위험 규칙(codeyoma/krx-quant#471): 순손실로 끝난 매매가 연달아 %1번이면 마지막 청산부터 %2분 동안, "
                              "그날 매수 주문이 %3번이면 그날은 새로 사지 않습니다(KIS 장부는 거부된 주문도 셉니다). 상태는 "
                              "'도는 중' 그대로이고, 그동안 읽은 예측의 이유가 cooldown, buy_limit입니다. 매매마다의 손실 예산은 "
                              "운용 상한의 %4%(%5)이고, 수량 = 손실 예산 ÷ (매수가 − 손절가 + 1틱 + 수수료·매도세)입니다. 날마다 "
                              "0시(KST)에 다시 셉니다. 값은 서버의 .env에서만 오고 좁힐 수만 있습니다.")
                   .arg(r.loss_streak)
                   .arg(korean().toString(r.cooldown_minutes, 'g', 4))
                   .arg(r.daily_buys)
                   .arg(korean().toString(r.budget_percent, 'g', 3), won_text(r.budget));
    return card;
}

/// The review cycle's card (codeyoma/krx-quant#469, #473).
Card cycle_card(const TraderState& state) {
    Card card{QStringLiteral("검토 주기"), {}, {}, {}, Tone::Plain};
    if (!state.cycle) {
        card.value = dash();
        card.tone = Tone::Dim;
        card.tip = QStringLiteral("검토 주기가 없습니다: 평가 버전이 아직 시작되지 않았거나(trader의 첫 시작 전), 이 엔진이 "
                                  "codeyoma/krx-quant#469 전입니다.");
        return card;
    }
    const TraderCycle& c = *state.cycle;
    card.value = c.day > 0 ? QStringLiteral("%1/%2일째").arg(c.day).arg(c.days) : QStringLiteral("첫날 전");
    card.lines << QStringLiteral("주기 %1 · %2 – %3").arg(c.number).arg(day_text(c.start), day_text(c.review));
    if (c.day == c.days) {
        card.lines << QStringLiteral("오늘 장 마감 뒤 검토");
        card.tone = Tone::Warning;
    } else {
        card.lines << QStringLiteral("검토 날 %1 장 마감 뒤").arg(day_text(c.review));
    }
    if (state.cohort && state.cohort->cycle == c.number && state.cohort->evaluation == c.evaluation) {
        QStringList members;
        for (qint64 v : state.cohort->members)
            members << QStringLiteral("정책 %1").arg(v);
        card.lines << (members.isEmpty() ? QStringLiteral("묶음: 도전 정책 없음") : QStringLiteral("묶음: %1").arg(members.join(", ")));
    } else if (state.cohort || !state.policies.isEmpty()) {
        card.lines << QStringLiteral("이 주기의 묶음 없음");
    }
    card.tip = QStringLiteral("평가 버전 %1(%2부터)의 검토 주기 %3입니다. 챔피언 정책(칸, 순서, θ)은 검토와 검토 사이에 바뀌지 "
                              "않습니다. 도전 정책은 주기의 첫날 묶음(셋까지)으로 들어와 챔피언 옆에서 모의로 돌고, 검토 날 "
                              "장이 끝난 뒤 한 번만 판정합니다. 바뀐 챔피언은 다음 거래일부터 매매합니다(codeyoma/krx-quant#469, "
                              "#473).")
                   .arg(c.evaluation)
                   .arg(day_text(c.since))
                   .arg(c.number);
    return card;
}

/// The table's order of the statuses: the champion, the challengers, the waiting, the former, the dropped.
int status_rank(const QString& status) {
    static const QStringList order{"champion", "challenger", "waiting", "former", "dropped"};
    const auto i = order.indexOf(status);
    return i < 0 ? static_cast<int>(order.size()) : static_cast<int>(i);
}

/// ``version``'s entry among the state's running policies; none when it is not running or not read.
const TraderRunning* running_of(const std::optional<TraderState>& state, qint64 version) {
    if (!state)
        return nullptr;
    for (const auto& p : state->policies)
        if (p.version == version)
            return &p;
    return nullptr;
}

/// The policies in the table's order: by status, then the newest version first.
QList<TraderPolicy> table_order(const QList<TraderPolicy>& policies) {
    QList<TraderPolicy> out = policies;
    std::stable_sort(out.begin(), out.end(), [](const TraderPolicy& a, const TraderPolicy& b) {
        const int ra = status_rank(a.status), rb = status_rank(b.status);
        return ra != rb ? ra < rb : a.version > b.version;
    });
    return out;
}

QString why_left_out(const TraderLeftOut& c) {
    QString why;
    if (c.why == QLatin1String("few_trades")) {
        if (c.most_days) // codeyoma/krx-quant#472: the signals bought alone a share
            why = QStringLiteral("1주씩 따로 산 신호가 모자람(가장 많을 때 %1건, %2일)").arg(c.most_trades.value_or(0)).arg(*c.most_days);
        else
            why = c.most_trades ? QStringLiteral("모의 매매가 모자람(가장 많을 때 %1개)").arg(*c.most_trades)
                                : QStringLiteral("모의 매매가 모자람");
    } else if (c.why == QLatin1String("no_gain")) {
        why = QStringLiteral("넣어도 정책의 로그 성장이 늘지 않음");
    } else if (c.why == QLatin1String("few_days")) { // codeyoma/krx-quant#472
        why = c.recorded_days ? QStringLiteral("녹화한 날이 모자람(%1일)").arg(*c.recorded_days)
                              : QStringLiteral("녹화한 날이 모자람");
    } else if (c.why == QLatin1String("no_edge")) { // codeyoma/krx-quant#472
        why = QStringLiteral("신호당 순손익의 하한이 0을 넘는 θ가 없음");
        if (c.best_threshold && c.best_low)
            why += QStringLiteral("(가장 높은 θ %1: 하한 %2)").arg(percent_text(*c.best_threshold), ticks_text(*c.best_low));
    } else {
        why = c.why;
    }
    if (!c.verdict.isEmpty())
        why += QStringLiteral(" · 비용 지도 판정 %1").arg(c.verdict);
    return why;
}

/// A challenger's standing against the champion now: its judgement's text, colour and tooltip.
void set_standing(PolicyRow& row, const TraderPromotion& p) {
    const QString common = QStringLiteral("함께 %1/%2일").arg(p.days).arg(p.required);
    const QString low = p.low ? signed_won_text(*p.low) : dash();
    if (p.passed) {
        row.judgement = QStringLiteral("이김 · %1 · 차이 하한 %2").arg(common, low);
        row.judgement_tone = Tone::Positive;
    } else if (p.why == QLatin1String("not_above")) {
        row.judgement = QStringLiteral("%1 · 차이 하한 %2 ≤ 0").arg(common, low);
        row.judgement_tone = Tone::Warning;
    } else {
        row.judgement = p.days == 0 ? QStringLiteral("%1 · 다음 거래일부터").arg(common)
                                    : QStringLiteral("%1 · 날 수 부족").arg(common);
        row.judgement_tone = Tone::Plain;
    }
    QStringList tip{QStringLiteral("챔피언 정책 %1과 함께 돈 거래일 %2일(챔피언이 장중에 멈춘 날은 뺌).").arg(p.champion).arg(p.days),
                    QStringLiteral("날마다 (도전 − 챔피언) 순손익: 평균 %1, %2 하한 %3 (도전 정책 %4개로 Bonferroni 보정).")
                        .arg(p.mean ? signed_won_text(*p.mean) : dash(), level_text(p.level), low)
                        .arg(p.challengers),
                    QStringLiteral("함께 돈 날이 %1일 이상이고 하한이 0보다 크면 이깁니다. 교체는 장이 끝난 뒤 밤 작업이 합니다.")
                        .arg(p.required)};
    if (p.review.isValid()) { // codeyoma/krx-quant#473: judged once, on its cohort's review night
        row.judgement += QStringLiteral(" · 검토 %1").arg(p.review.toString("MM-dd"));
        tip.removeLast();
        tip << QStringLiteral("수치는 참고입니다: 묶음은 검토 날(%1) 장이 끝난 뒤 한 번만 판정합니다. 그때 함께 돈 날이 %2일 이상이고 "
                              "하한이 0보다 큰 것 가운데 하한이 가장 큰 것이 챔피언이 되고, 나머지는 모두 내려집니다. 날이 "
                              "모자라면 판정 불가로 내려집니다.")
                   .arg(day_text(p.review))
                   .arg(p.required);
        if (p.passed)
            tip << QStringLiteral("지금의 날들로는 넘습니다.");
    } else if (p.passed) {
        tip << QStringLiteral("지금 판정하면 이깁니다.");
    } else {
        tip << QStringLiteral("%1일까지 이기지 못하면 그 밤에 내려집니다(남은 %2일).")
                   .arg(p.until)
                   .arg(std::max(0, p.until - p.days));
    }
    row.judgement_tip = tip.join('\n');
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

int chart_ms(const QDateTime& now) {
    const qint64 into = now.toMSecsSinceEpoch() % 60'000; // the minute's turn is the same in every zone
    const qint64 ms = kChartSettleMs - into;
    return static_cast<int>(ms > 0 ? ms : ms + 60'000);
}

QString history_key(const TraderState& state) {
    const QStringList parts{
        state.mode,
        state.status,
        state.started_at.toString(Qt::ISODateWithMs),
        state.today ? state.today->day.toString(Qt::ISODate) : QString(),
        state.today ? QString::number(state.today->trades) : QString(),
        // codeyoma/krx-quant#533: a practice trade exited counts here, not in ``trades``.
        state.today && state.today->practice ? QString::number(state.today->practice->trades) : QString(),
        state.cumulative ? state.cumulative->since.toString(Qt::ISODateWithMs) : QString(),
        state.position ? QString::number(state.position->trade_id) : QString(),
        state.champion ? QString::number(*state.champion) : QString(),
    };
    QStringList running;
    for (const auto& p : state.policies)
        running << QStringLiteral("%1:%2:%3:%4").arg(p.version).arg(p.status).arg(p.paper.days).arg(p.paper.trades);
    return (parts + running).join('|');
}

Badge mode_badge(const TraderState& state) {
    if (!state.written())
        return {dash(), QStringLiteral("trader가 아직 상태를 쓰지 않았습니다."), Tone::Dim};
    if (state.live && state.live->configured && state.mode != QLatin1String("live")) {
        // codeyoma/krx-quant#408: KRX_TRADER_MODE=live, the gate or an execution check not passed yet.
        QStringList missing;
        if (state.live->missing.contains(QStringLiteral("gate")) && !state.live->gate.isEmpty())
            missing << state.live->gate;
        for (const auto& c : state.live->checks)
            if (!c.passed)
                missing << c.said;
        QString tip = QStringLiteral("실전 모드(KRX_TRADER_MODE=live)지만 실전 관문이나 실행 점검이 모자라 아직 실제 주문을 "
                                     "내지 않고 모의로 돕니다. 모두 채우면 08:30 고르기에서 trader가 스스로 다시 시작해 실전 "
                                     "계좌에 주문합니다.");
        if (!missing.isEmpty())
            tip += QStringLiteral("\n모자란 것:\n· ") + missing.join(QStringLiteral("\n· "));
        tip += QStringLiteral("\n모드는 서버의 .env(KRX_TRADER_MODE)로만 바뀝니다.");
        return {QStringLiteral("실전 대기"), tip, Tone::Warning};
    }
    Badge badge{trader_mode_label(state.mode), {}, Tone::Simulated};
    if (state.mode == QLatin1String("paper"))
        badge.tip = QStringLiteral("내부 모의: 실시간 호가로 체결을 흉내 냅니다. 주문을 내지 않습니다.");
    else if (state.mode == QLatin1String("mock"))
        badge.tip = QStringLiteral("KIS 모의투자: KIS 모의투자 계좌에 주문합니다.");
    else if (state.mode == QLatin1String("live")) {
        badge.tip = QStringLiteral("실전: 실전 관문과 실행 점검을 넘은 챔피언이 실전 계좌에 주문합니다.");
        if (state.live && state.live->cap && !state.live->lifted) // codeyoma/krx-quant#408: the first period
            badge.tip += QStringLiteral(" 첫 단계라 운용 상한 %1까지만 삽니다(실전 20거래일, 왕복 매매 30건 동안).")
                             .arg(won_text(*state.live->cap));
        if (state.live && state.live->breached)
            badge.tip += QStringLiteral(" 첫 단계의 점검을 벗어나 새 진입이 멈춰 있습니다(서버의 krx-quant trade resume).");
        badge.tone = Tone::Live;
    }
    badge.tip += QStringLiteral(" 모드는 서버의 .env(KRX_TRADER_MODE)로만 바뀝니다.");
    if (state.practice && state.practice->enabled) { // codeyoma/krx-quant#427
        badge.tip += QStringLiteral(" KIS 모의 연습(KRX_TRADER_MOCK_PRACTICE)이 켜져 있습니다.");
        const TraderPractice& p = *state.practice;
        static const QMap<QString, QString> why = { // codeyoma/krx-quant#474
            {"champion", QStringLiteral("챔피언 칸이 매매한다")},
            {"not_frozen", QStringLiteral("이 검토 주기의 연습 칸을 아직 고르지 않았다(밤 작업이 고른다)")},
            {"gone", QStringLiteral("고정한 연습 칸의 예측자가 없어져 주기 끝까지 쉰다")},
            {"no_cost_map", QStringLiteral("비용 지도를 읽지 못했다")},
            {"no_policy", QStringLiteral("챔피언 정책을 읽지 못했다")},
        };
        if (!p.active && !p.why.isEmpty())
            badge.tip += QStringLiteral(" 지금 연습하지 않는 까닭: %1.").arg(why.value(p.why, p.why));
        if (p.frozen_cycle > 0)
            badge.tip += QStringLiteral(" 검토 주기 %1의 연습 칸: %2 · θ %3.")
                             .arg(p.frozen_cycle)
                             .arg(cell_text(p.frozen_kind, p.frozen_predictor, p.frozen_role),
                                  p.frozen_threshold ? percent_text(*p.frozen_threshold) : dash());
    }
    return badge;
}

std::optional<Badge> practice_badge(const TraderState& state) {
    if (!state.written() || !state.practice)
        return std::nullopt;
    const TraderPractice& p = *state.practice;
    const bool holding = state.position && state.position->practice;
    if (!p.active && !holding)
        return std::nullopt;
    QStringList tip;
    const bool frozen = p.frozen_cycle > 0; // codeyoma/krx-quant#474
    const std::optional<double> theta = frozen && p.frozen_threshold ? p.frozen_threshold : p.threshold;
    if (p.active)
        tip << QStringLiteral("KIS 모의 연습 중입니다: 매매할 챔피언 칸이 없어 연습 칸 %1을 %2주씩, %3로 KIS 모의투자에 "
                              "삽니다(KRX_TRADER_MOCK_PRACTICE).")
                   .arg(cell_text(p.kind, p.predictor, p.role))
                   .arg(p.quantity > 0 ? p.quantity : 1)
                   .arg(theta ? QStringLiteral("문턱 θ %1").arg(percent_text(*theta)) : QStringLiteral("기본 문턱 θ"));
    if (frozen)
        tip << QStringLiteral("연습 칸: 검토 주기 %1에 고정한 칸입니다(%2에 밤 작업이 고름: 녹화를 재생해 1주씩 잰 신호당 "
                              "순손익의 하한이 가장 큰 칸과 θ). 주기 동안 바꾸지 않습니다(codeyoma/krx-quant#474).")
                   .arg(p.frozen_cycle)
                   .arg(kst_text(p.frozen_at, "MM-dd HH:mm"));
    else if (p.active && p.low && p.p_star)
        tip << QStringLiteral("연습 칸: 판정이 있는 칸 가운데 맞힘률 하한 − p*가 가장 큰 칸(하한 %1, p* %2)입니다.")
                   .arg(percent_text(*p.low), percent_text(*p.p_star));
    if (holding)
        tip << QStringLiteral("연습 포지션을 들고 있습니다: 원래 규칙(목표가, 손절가, 시간 장벽)대로 팝니다. 그동안은 챔피언 칸도 "
                              "사지 않습니다(포지션은 하나).");
    tip << practice_counts_text(practice_apart(state)) + QStringLiteral(" 챔피언 칸이 다시 생기면 새 연습을 멈춥니다.");
    return Badge{QStringLiteral("연습"), tip.join('\n'), Tone::Warning};
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
    if (state.status == QLatin1String("order_failed")) // codeyoma/krx-quant#405
        return {label,
                QStringLiteral("오늘 KIS 주문이 실패해(거부, 응답 없음, 보내지 못함) 오늘은 새로 사지 않습니다. 들고 있는 것은 "
                               "원래 규칙(목표가, 손절가, 시간 장벽)대로 팝니다. 다음 거래일에 저절로 다시 돕니다."),
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

/// A checkpoint's one-sided α (codeyoma/krx-quant#408): "1.52%".
QString alpha_text(double alpha) {
    return korean().toString(alpha * 100, 'f', 2) + QStringLiteral("%");
}

QString gate_text(const TraderState& state) {
    if (!state.gate)
        return {};
    if (!state.champion)
        return QStringLiteral("챔피언 정책 없음");
    const TraderGate& g = *state.gate;
    QString low = dash();
    if (g.low)
        low = state.capital && *state.capital > 0 ? percent_text(*g.low / *state.capital) : signed_won_text(*g.low);
    if (g.checkpoints) { // codeyoma/krx-quant#408: judged at checkpoints of the trader's paper history
        if (g.passed)
            return QStringLiteral("챔피언 정책 %1 · 관문 넘음(%2번째 판정)").arg(*state.champion).arg(g.passed_k);
        return QStringLiteral("챔피언 정책 %1 · 관문 판정 이력 %2일째(지금 %3일) · 하한 %4")
            .arg(QString::number(*state.champion))
            .arg(g.next_day)
            .arg(g.history)
            .arg(low);
    }
    QString text = QStringLiteral("챔피언 정책 %1 · 관문 모의 %2/%3일 · 하한 %4")
                       .arg(QString::number(*state.champion))
                       .arg(g.days)
                       .arg(g.required)
                       .arg(low);
    if (g.passed)
        text += QStringLiteral(" · 관문 넘음");
    return text;
}

QString gate_tip(const TraderState& state) {
    if (!state.gate)
        return {};
    if (!state.champion)
        return QStringLiteral("첫 밤 정책 맞추기 전입니다. 그때까지는 기본 문턱과 Kelly 비율로 사고, 실전 관문은 첫 챔피언의 "
                              "모의 기록부터 셉니다.");
    const TraderGate& g = *state.gate;
    if (g.checkpoints) // codeyoma/krx-quant#408
        return QStringLiteral("실전 관문: trader의 모의 이력(챔피언이 돈 거래일) 20일마다 한 번 판정합니다. k번째 판정에서는 그날 "
                              "챔피언이 챔피언으로 돈 날의 모의 기록이 %1거래일 이상이고 날마다 순손익(매매 없는 날 0)의 평균이 "
                              "한쪽 α_k = 2.5% × 6 ÷ (π²k²)로 0보다 커야 넘습니다(평생 2.5% 이하). 도전 정책이었던 날은 세지 "
                              "않습니다: 승격을 그 날들로 판정했습니다. 넘은 챔피언은 넘은 채이고, 챔피언이 바뀌어도 k는 이어서 "
                              "셉니다. 지금 이력 %2일, 챔피언으로 돈 날 %3일, 평균 %4, 다음 판정(%5번째, 이력 %6일째, α %7)의 "
                              "%8 하한 %9. 관문을 넘어도 저절로 실전이 되지 않습니다: 모드는 서버에서 바꾸고, live는 실행 점검도 "
                              "넘어야 주문합니다. 정책마다의 모의 기록과 승격 판정은 아래 정책 버전 표에 있습니다.")
            .arg(g.required)
            .arg(g.history)
            .arg(g.days)
            .arg(g.mean ? signed_won_text(*g.mean) : dash())
            .arg(g.next_k)
            .arg(g.next_day)
            .arg(alpha_text(g.next_alpha), level_text(g.level), g.low ? signed_won_text(*g.low) : dash());
    return QStringLiteral("실전 관문: 챔피언의 모의 기록이 %1거래일 이상이고, 날마다 순손익(매매 없는 날 0)의 평균의 %2 하한이 "
                          "0보다 커야 합니다. 지금 %3일, 평균 %4, 하한 %5(운용 상한 대비). 관문을 넘어도 저절로 실전이 되지 "
                          "않습니다: 모드는 서버에서 바꿉니다. 정책마다의 모의 기록과 승격 판정은 아래 정책 버전 표에 있습니다.")
        .arg(g.required)
        .arg(percent_text(g.level, 0))
        .arg(g.days)
        .arg(g.mean ? signed_won_text(*g.mean) : dash(), g.low ? signed_won_text(*g.low) : dash());
}

Tone gate_tone(const TraderState& state) {
    return state.gate && state.champion && state.gate->passed ? Tone::Positive : Tone::Plain;
}

QString gate_line(const TraderState& state) {
    if (!state.gate)
        return {};
    if (!state.champion)
        return QStringLiteral("실전 관문 · 첫 챔피언 전입니다. 첫 밤 정책 맞추기가 챔피언을 정하면 그 정책의 모의 기록으로 "
                              "%1거래일을 셉니다.")
            .arg(state.gate->required);
    const TraderGate& g = *state.gate;
    QString record = QStringLiteral("챔피언 정책 %1의 모의 기록 %2/%3일");
    if (g.checkpoints) // codeyoma/krx-quant#425: the gate counts the champion's days as the champion alone
        record = QStringLiteral("챔피언 정책 %1의 챔피언으로 돈 모의 기록 %2/%3일");
    QStringList parts{QStringLiteral("실전 관문"), record.arg(*state.champion).arg(g.days).arg(g.required),
                      QStringLiteral("날마다 평균 %1").arg(g.mean ? signed_won_text(*g.mean) : dash())};
    QString low = QStringLiteral("%1 하한 %2").arg(level_text(g.level), g.low ? signed_won_text(*g.low) : dash());
    if (g.low && state.capital && *state.capital > 0)
        low += QStringLiteral(" (운용 상한의 %1)").arg(percent_text(*g.low / *state.capital));
    parts << low;
    if (g.passed) {
        if (g.checkpoints && g.passed_k > 0) // codeyoma/krx-quant#408
            parts << QStringLiteral("넘음(%1번째 판정, %2): 실전 모드는 서버에서 켭니다")
                         .arg(g.passed_k)
                         .arg(g.passed_day.toString(QStringLiteral("MM-dd")));
        else
            parts << QStringLiteral("넘음: 실전 모드는 서버에서 켭니다");
    } else {
        QStringList short_of;
        if (g.checkpoints) // codeyoma/krx-quant#408: the next checkpoint and its α, in place of the days left
            short_of << QStringLiteral("다음 판정 %1번째, 이력 %2일째(지금 %3일, α %4)")
                            .arg(g.next_k)
                            .arg(g.next_day)
                            .arg(g.history)
                            .arg(alpha_text(g.next_alpha));
        if (g.days < g.required)
            short_of << QStringLiteral("%1일 더").arg(g.required - g.days);
        if (!g.low)
            short_of << QStringLiteral("하한은 2일부터");
        else if (*g.low <= 0)
            short_of << QStringLiteral("하한 ≤ 0");
        parts << (short_of.isEmpty() ? QStringLiteral("못 넘음")
                                     : QStringLiteral("못 넘음: %1").arg(short_of.join(QStringLiteral(", "))));
    }
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
        position.lines << (p.practice ? QStringLiteral("%1 · 연습").arg(cell_text(p.kind, p.predictor))
                                      : cell_text(p.kind, p.predictor)); // codeyoma/krx-quant#427
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
    // codeyoma/krx-quant#533: the limits leave the practice out; said while it is on, or has P&L there.
    const bool practising = state.mode == QLatin1String("mock") && state.practice && state.practice->enabled;
    out << limit_card(QStringLiteral("하루 한도 사용"), state.daily_limit, false,
                      state.today && state.today->practice && (practising || practised(state.today->practice)));
    out << limit_card(QStringLiteral("누적 한도 사용"), state.cumulative_limit, true,
                      state.cumulative && state.cumulative->practice &&
                          (practising || practised(state.cumulative->practice)));
    out << risk_card(state, now);   // codeyoma/krx-quant#475
    out << cycle_card(state);       // codeyoma/krx-quant#469
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
            if (!t.stalls.isEmpty()) // codeyoma/krx-quant#407: the collector's quotes stalled while it was open
                r.reason += QStringLiteral(" · 호가 끊김");
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
        if (!t.champion)
            tip << QStringLiteral("도전 정책의 모의 매매입니다: 챔피언 옆에서 같은 호가로 흉내 내고, 한도와 정지에 멈추지 않으며 "
                                  "차트에 보이지 않습니다.");
        if (t.practice) // codeyoma/krx-quant#427
            tip << (state && practice_apart(*state)
                        ? QStringLiteral("KIS 모의 연습 매매입니다: 매매할 챔피언 칸이 없던 날 연습 칸을 1주씩 샀습니다. 정책이 "
                                         "없어 승격·실전 관문·모의 기록에 세지 않고, 손실 한도에도 세지 않습니다"
                                         "(codeyoma/krx-quant#533). 위험 규칙과 실전의 실행 점검에는 셉니다.")
                        : QStringLiteral("KIS 모의 연습 매매입니다: 매매할 챔피언 칸이 없던 날 연습 칸을 1주씩 샀습니다. 정책이 "
                                         "없어 승격·실전 관문·모의 기록에는 세지 않고, 손실 한도와 실전의 실행 점검에는 "
                                         "셉니다."));
        for (const auto& effect : t.stalls) // codeyoma/krx-quant#407
            tip << QStringLiteral("수집기 호가 끊김: %1").arg(trader_stall_label(effect));
        if (!t.clock.isEmpty()) { // codeyoma/krx-quant#475: the execution cost
            const auto ticks = [](const std::optional<double>& v) { return v ? ticks_text(*v) : QStringLiteral("?"); };
            const auto secs = [](const std::optional<double>& v) { return v ? seconds_text(*v) : QStringLiteral("?"); };
            QString line = QStringLiteral("실행: 매수 %1(결정 때 중간가 대비) · 결정 뒤 주문 %2, 체결 %3")
                               .arg(ticks(t.entry_ticks), secs(t.entry_placed), secs(t.entry_filled));
            if (t.exit_reference == QLatin1String("target"))
                line += QStringLiteral(" · 익절 %1(목표가 대비) · 매수 체결 뒤 주문 %2").arg(ticks(t.exit_ticks), secs(t.exit_placed));
            else if (t.exit_reference == QLatin1String("mid"))
                line += QStringLiteral(" · 매도 %1(결정 때 중간가 대비) · 결정 뒤 주문 %2, 체결 %3")
                            .arg(ticks(t.exit_ticks), secs(t.exit_placed), secs(t.exit_filled));
            tip << line;
            tip << (t.clock == QLatin1String("trader")
                        ? QStringLiteral("틱은 0보다 크면 불리합니다. 시각은 trader가 KIS 주문 체결 조회에서 체결을 읽은 때입니다(KIS가 "
                                         "알린 체결 시각이 아닙니다).")
                        : QStringLiteral("틱은 0보다 크면 불리합니다. 시각은 체결한 호가를 받은 때입니다(모의 체결)."));
        }
        r.tip = tip.join('\n');
        if (t.practice) { // codeyoma/krx-quant#427: of no policy
            r.policy = QStringLiteral("연습");
            r.policy_tone = Tone::Warning;
        } else if (!t.policy) {
            r.policy = dash();
            r.policy_tone = Tone::Dim;
        } else if (t.champion) {
            r.policy = QStringLiteral("정책 %1").arg(*t.policy);
        } else {
            r.policy = QStringLiteral("정책 %1 · 도전").arg(*t.policy);
            r.policy_tone = Tone::Simulated;
        }
        rows << r;
    }
    return rows;
}

QString policy_status_label(const QString& status) {
    static const QMap<QString, QString> labels = {
        {"champion", QStringLiteral("챔피언")}, {"challenger", QStringLiteral("도전")}, {"waiting", QStringLiteral("대기")},
        {"former", QStringLiteral("전 챔피언")}, {"dropped", QStringLiteral("내려짐")},
    };
    return labels.value(status, status);
}

QList<PolicyChoice> trade_policy_choices(const std::optional<TraderPolicies>& policies,
                                         const std::optional<TraderState>& state) {
    QList<PolicyChoice> out{{std::nullopt, QStringLiteral("챔피언의 매매"),
                             QStringLiteral("trader가 낸 매매: 챔피언의 장부가 낸 것(첫 챔피언 전의 매매도)입니다. 한도가 "
                                            "세고 차트에 보이는 매매입니다.")}};
    const auto choice = [](qint64 version, const QString& status) {
        return PolicyChoice{version, QStringLiteral("정책 %1 · %2").arg(version).arg(policy_status_label(status)),
                            QStringLiteral("정책 %1의 매매: 도전 정책으로 모의로 돈 매매와 챔피언으로 낸 매매입니다.").arg(version)};
    };
    if (policies && !policies->policies.isEmpty()) {
        for (const auto& p : table_order(policies->policies))
            out << choice(p.version, p.status);
    } else if (state) {
        for (const auto& p : state->policies)
            out << choice(p.version, p.status);
    }
    return out;
}

QString trades_line(const std::optional<qint64>& policy, qsizetype count) {
    return policy ? QStringLiteral("정책 %1의 매매 %2개").arg(*policy).arg(count)
                  : QStringLiteral("챔피언의 매매 %1개").arg(count);
}

QList<PolicyRow> policy_rows(const TraderPolicies& policies, const std::optional<TraderState>& state) {
    QList<PolicyRow> rows;
    const bool cohorts = cohort_engine(policies, state); // codeyoma/krx-quant#473
    for (const auto& p : table_order(policies.policies)) {
        PolicyRow r;
        r.version = p.version;
        r.version_text = QStringLiteral("정책 %1").arg(p.version);
        r.status = policy_status_label(p.status);
        r.status_tone = p.status == QLatin1String("champion")     ? Tone::Positive
                        : p.status == QLatin1String("challenger") ? Tone::Plain
                                                                  : Tone::Dim;
        r.created = kst_text(p.created_at, "MM-dd HH:mm");

        // Its values: the cells in rank order, each one's θ and what it entered on in the same order: the
        // lower bound of its net per signal (codeyoma/krx-quant#472), a λ before #471, else none.
        QStringList cells, thresholds, basis, cells_tip;
        for (const auto& c : p.cells) {
            cells << cell_text(c.kind, c.predictor, c.role);
            thresholds << percent_text(c.threshold);
            basis << (c.bound_low      ? QStringLiteral("하한 %1").arg(ticks_text(*c.bound_low))
                      : c.kelly_fraction ? QStringLiteral("λ %1").arg(fraction_text(*c.kelly_fraction))
                                         : dash());
            QStringList fitted{c.kelly_fraction
                                   ? QStringLiteral("θ %1 · λ %2").arg(percent_text(c.threshold), fraction_text(*c.kelly_fraction))
                                   : QStringLiteral("θ %1").arg(percent_text(c.threshold))};
            if (c.bound_value || c.bound_low) // codeyoma/krx-quant#472
                fitted << QStringLiteral("1주씩 따로 잰 신호당 %1(하한 %2%3%4)")
                              .arg(c.bound_value ? ticks_text(*c.bound_value) : dash(),
                                   c.bound_low ? ticks_text(*c.bound_low) : dash(),
                                   c.bound_level ? QStringLiteral(", %1 수준").arg(level_text(*c.bound_level)) : QString(),
                                   c.recorded_days ? QStringLiteral(", 녹화한 날 %1일").arg(*c.recorded_days) : QString());
            if (c.p_star || c.low)
                fitted << QStringLiteral("비용 지도 p* %1 · 하한 %2%3")
                              .arg(optional_percent(c.p_star), optional_percent(c.low),
                                   c.verdict.isEmpty() ? QString() : QStringLiteral(" · 판정 %1(기록일 뿐)").arg(c.verdict));
            if (c.kelly && c.fraction) // a policy fitted before codeyoma/krx-quant#471
                fitted << QStringLiteral("f* %1 → 비율 %2")
                              .arg(korean().toString(*c.kelly, 'f', 2), optional_percent(c.fraction));
            fitted << QStringLiteral("정책 재생 모의 매매 %1개(이익 %2) · 순손익 %3")
                          .arg(c.fitted_trades)
                          .arg(c.wins)
                          .arg(c.fitted_net ? signed_won_text(*c.fitted_net) : dash());
            cells_tip << QStringLiteral("%1: %2").arg(cell_text(c.kind, c.predictor, c.role), fitted.join(QStringLiteral(", ")));
            const QStringList calibrated = calibration_lines(c.calibration, c.calibration_below);
            if (!calibrated.isEmpty()) // codeyoma/krx-quant#475
                cells_tip << QStringLiteral("  보정: ") + calibrated.join(QStringLiteral("\n  보정: "));
        }
        const QString joint = QStringLiteral(" / ");
        r.cells = cells.isEmpty() ? QStringLiteral("칸 없음") : cells.join(joint);
        r.thresholds = thresholds.isEmpty() ? dash() : thresholds.join(joint);
        r.basis = basis.isEmpty() ? dash() : basis.join(joint);
        if (p.cells.isEmpty())
            cells_tip << QStringLiteral("매매할 칸이 없는 정책입니다(그 밤 넣을 칸이 없었다). 챔피언이면 trader는 사지 않습니다.");
        cells_tip.prepend(QStringLiteral("순위대로의 칸. θ: 사는 오름 확률의 문턱. 밤 맞추기는 칸마다 녹화를 재생해 1주씩 따로 잰 "
                                         "신호당 순손익의 날 단위 하한이 0을 넘는 가장 낮은 θ로 넣습니다(codeyoma/krx-quant#472). "
                                         "λ(Kelly 비율)는 #471 전에 맞춘 정책에만 있고 읽지 않습니다. 보정: 점수 구간마다 오름 "
                                         "신호의 결과(설명일 뿐 θ를 바꾸지 않는다, #475)."));
        for (const auto& c : p.left_out) {
            cells_tip << QStringLiteral("뺀 칸 %1: %2").arg(cell_text(c.kind, c.predictor, c.role), why_left_out(c));
            const QStringList calibrated = calibration_lines(c.calibration, c.calibration_below);
            if (!calibrated.isEmpty())
                cells_tip << QStringLiteral("  보정: ") + calibrated.join(QStringLiteral("\n  보정: "));
        }
        r.cells_tip = cells_tip.join('\n');

        // Its paper record: the state's while it runs, the versions' otherwise.
        const TraderRunning* running = running_of(state, p.version);
        const TraderPaper& paper = running ? running->paper : p.paper;
        r.days = QStringLiteral("%1일").arg(paper.days);
        r.trades = QStringLiteral("%1개").arg(paper.trades);
        r.net = signed_won_text(paper.net);
        r.net_tone = paper.days == 0 ? Tone::Dim : sign_tone(paper.net);
        r.mean = paper.mean ? signed_won_text(*paper.mean) : dash();
        r.low = paper.low ? signed_won_text(*paper.low) : dash();
        r.low_tone = !paper.low ? Tone::Dim : *paper.low > 0 ? Tone::Positive : Tone::Plain;
        QStringList paper_tip;
        if (paper.days == 0)
            paper_tip << QStringLiteral("아직 모의로 돈 거래일이 없습니다.");
        else
            paper_tip << QStringLiteral("%1 ~ %2, %3거래일(도전 정책일 때와 챔피언일 때 모두). 매매 없는 날은 0입니다.")
                             .arg(day_text(paper.since), day_text(paper.last))
                             .arg(paper.days);
        paper_tip << QStringLiteral("날마다 순손익(그날 들어간 매매의 순손익 합, 원)의 평균 %1과 그 95% 하한 %2(2일부터, 겹침 1의 "
                                    "거래일 단위 t).")
                         .arg(r.mean, r.low);
        if (paper.low && state && state->capital && *state->capital > 0)
            paper_tip << QStringLiteral("하한은 운용 상한의 %1입니다.").arg(percent_text(*paper.low / *state->capital));
        r.paper_tip = paper_tip.join('\n');

        // Its judgement: the gate for the champion, the standing for a challenger.
        if (p.status == QLatin1String("champion")) {
            if (state && state->gate && state->gate->policy == p.version) {
                const TraderGate& g = *state->gate;
                r.judgement = g.passed ? QStringLiteral("관문 넘음") : QStringLiteral("관문 %1/%2일 · 못 넘음").arg(g.days).arg(g.required);
                r.judgement_tone = g.passed ? Tone::Positive : Tone::Plain;
                r.judgement_tip = gate_line(*state) + QStringLiteral("\n") + gate_tip(*state);
            } else {
                r.judgement = dash();
                r.judgement_tone = Tone::Dim;
            }
        } else if (p.status == QLatin1String("challenger")) {
            if (running && running->promotion) {
                set_standing(r, *running->promotion);
            } else {
                r.judgement = dash();
                r.judgement_tone = Tone::Dim;
            }
        } else if (p.status == QLatin1String("waiting")) {
            r.judgement_tone = Tone::Dim;
            if (cohorts) { // codeyoma/krx-quant#473
                r.judgement = QStringLiteral("다음 묶음 기다림");
                r.judgement_tip = QStringLiteral("다음 도전 정책 묶음을 기다립니다. 검토 날 밤에 기다리는 정책 가운데 새것부터 셋까지 "
                                                 "다음 주기의 묶음으로 들어가 그 첫날부터 모의로 돌고, 들지 못한 것은 내려집니다.");
            } else {
                r.judgement = QStringLiteral("자리 기다림");
                r.judgement_tip = QStringLiteral("도전 정책의 자리 셋이 차 있어 기다립니다. 자리가 나면 기다리는 것 가운데 가장 새 것부터 "
                                                 "들어가 다음 고르기(08:30)부터 모의로 돕니다.");
            }
        } else if (p.status == QLatin1String("former")) {
            r.judgement = QStringLiteral("전 챔피언");
            for (const auto& c : policies.champions)
                if (c.detail && c.detail->from == p.version) {
                    r.judgement = QStringLiteral("%1 정책 %2에게 짐").arg(kst_text(c.at, "MM-dd")).arg(c.version);
                    break; // the newest such change
                }
            r.judgement_tone = Tone::Dim;
            r.judgement_tip = QStringLiteral("챔피언이었다가 도전 정책에 졌습니다. 더 돌지 않습니다.");
        } else if (p.status == QLatin1String("dropped")) {
            r.judgement = p.dropped_at.isValid() ? QStringLiteral("%1 내려짐").arg(kst_text(p.dropped_at, "MM-dd"))
                                                 : QStringLiteral("내려짐");
            r.judgement_tone = Tone::Dim;
            r.judgement_tip = cohorts ? QStringLiteral("묶음의 판정에서 뽑히지 않았거나 묶음에 들지 못해 내려졌습니다. 다시 돌지 "
                                                       "않습니다(codeyoma/krx-quant#473).")
                                      : QStringLiteral("함께 돈 거래일 20일까지 챔피언을 이기지 못해 내려졌습니다. 다시 돌지 않습니다.");
        }

        QStringList tip{QStringLiteral("정책 %1 · 맞춘 때 %2").arg(QString::number(p.version), kst_text(p.created_at, "yyyy-MM-dd HH:mm")),
                        p.reason};
        if (p.cost_map_at.isValid())
            tip << QStringLiteral("비용 지도 %1%2")
                       .arg(kst_text(p.cost_map_at, "MM-dd HH:mm"),
                            p.passed ? QStringLiteral(" · 통과 %1칸").arg(*p.passed) : QString());
        if (p.simulated_trades)
            tip << QStringLiteral("맞춘 날들의 모의: 매매 %1개 · 순손익 %2")
                       .arg(*p.simulated_trades)
                       .arg(p.simulated_net ? signed_won_text(*p.simulated_net) : dash());
        if (p.admitted_at.isValid())
            tip << QStringLiteral("%1 %2")
                       .arg(cohorts ? QStringLiteral("묶음에 듦") : QStringLiteral("도전 자리 받음"),
                            kst_text(p.admitted_at, "MM-dd HH:mm"));
        if (p.dropped_at.isValid())
            tip << QStringLiteral("내려짐 %1").arg(kst_text(p.dropped_at, "MM-dd HH:mm"));
        r.tip = tip.join('\n');
        rows << r;
    }
    return rows;
}

QString policies_line(const TraderPolicies& policies) {
    if (policies.policies.isEmpty())
        return QStringLiteral("정책 버전이 아직 없습니다(첫 밤 정책 맞추기 전)");
    QMap<QString, int> count;
    for (const auto& p : policies.policies)
        ++count[p.status];
    QStringList parts{policies.champion ? QStringLiteral("챔피언 정책 %1").arg(*policies.champion)
                                        : QStringLiteral("챔피언 없음")};
    for (const char* status : {"challenger", "waiting", "former", "dropped"})
        parts << QStringLiteral("%1 %2개").arg(policy_status_label(QString::fromLatin1(status))).arg(count.value(status));
    return parts.join(QStringLiteral(" · "));
}

CohortLine cohort_line(const std::optional<TraderPolicies>& policies, const std::optional<TraderState>& state) {
    if (!cohort_engine(policies, state))
        return {};
    const TraderCohort* found = state && state->cohort ? &*state->cohort
                                : policies && !policies->cohorts.isEmpty() ? &policies->cohorts.first()
                                                                           : nullptr;
    if (!found)
        return {QStringLiteral("도전 정책 묶음: 아직 없음 · 검토 날 밤에 기다리는 정책 가운데 새것부터 셋까지로 다음 주기의 묶음을 "
                               "만듭니다"),
                QStringLiteral("도전 정책은 검토 주기의 첫날 묶음으로 들어와, 그때 고정한 챔피언과 그 주기 동안 모의로 견줍니다. "
                               "판정은 검토 날 장이 끝난 뒤 한 번뿐입니다(codeyoma/krx-quant#473)."),
                Tone::Dim};
    const TraderCohort& c = *found;
    QStringList members;
    for (qint64 v : c.members)
        members << QStringLiteral("정책 %1").arg(v);
    QStringList parts{QStringLiteral("도전 정책 묶음 · 주기 %1(%2–%3)").arg(c.cycle).arg(c.start.toString("MM-dd"), c.review.toString("MM-dd")),
                      members.isEmpty() ? QStringLiteral("도전 정책 없음") : members.join(QStringLiteral(", "))};
    if (c.champion)
        parts << QStringLiteral("챔피언 정책 %1과 견줌").arg(*c.champion);
    CohortLine line;
    QStringList tip{QStringLiteral("주기 %1의 묶음: %2에 만들었다. 정책들은 %3부터 %4까지 고정한 챔피언과 모의로 견주고, 검토 날 장이 "
                                   "끝난 뒤 한 번만 판정한다. 함께 10일 이상이고 날마다 (도전 − 챔피언) 순손익의 하한(묶음 안 "
                                   "Bonferroni)이 0보다 큰 것 가운데 하한이 가장 큰 것이 챔피언이 되고, 나머지는 모두 내려진다.")
                        .arg(c.cycle)
                        .arg(kst_text(c.formed_at, "MM-dd HH:mm"), day_text(c.start), day_text(c.review))};
    if (!c.judged) {
        parts << QStringLiteral("검토 날 %1 장 마감 뒤 한 번 판정").arg(c.review.toString("MM-dd"));
    } else {
        QString verdict;
        if (c.outcome == QLatin1String("promoted") && c.promoted) {
            verdict = QStringLiteral("정책 %1 챔피언이 됨").arg(*c.promoted);
            line.tone = Tone::Positive;
        } else if (c.outcome == QLatin1String("kept")) {
            verdict = QStringLiteral("챔피언 그대로");
        } else if (c.outcome == QLatin1String("missed")) {
            verdict = QStringLiteral("판정 못 함(검토 날 밤 작업이 돌지 않음)");
            line.tone = Tone::Warning;
        } else {
            verdict = c.outcome;
        }
        parts << QStringLiteral("판정 %1: %2").arg(kst_text(c.judged_at, "MM-dd"), verdict);
        for (const auto& m : c.verdicts)
            tip << QStringLiteral("정책 %1: %2 · 함께 %3일 · 평균 %4 · 하한 %5%6")
                       .arg(m.version)
                       .arg(cohort_verdict_label(m.verdict), m.days ? QString::number(*m.days) : dash(),
                            m.mean ? signed_won_text(*m.mean) : dash(), m.low ? signed_won_text(*m.low) : dash(),
                            c.level ? QStringLiteral(" (%1)").arg(level_text(*c.level)) : QString());
    }
    for (const auto& [version, why] : c.passed_over)
        tip << QStringLiteral("묶음에 들지 못해 내린 정책 %1(%2)").arg(version).arg(why);
    line.text = parts.join(QStringLiteral(" · "));
    line.tip = tip.join('\n');
    return line;
}

QList<HistoryRow> history_rows(const TraderPolicies& policies) {
    struct Event {
        QDateTime at;
        int rank = 0; ///< at one time: the champion's change, a cohort's verdict, a drop, then a slot given
        HistoryRow row;
    };
    QList<Event> events;
    for (const auto& c : policies.champions) {
        HistoryRow r;
        r.key = QStringLiteral("c%1@%2").arg(c.version).arg(c.at.toMSecsSinceEpoch());
        r.at = kst_text(c.at, "MM-dd HH:mm");
        r.version = QStringLiteral("정책 %1").arg(c.version);
        r.why = c.reason;
        r.tone = Tone::Positive;
        if (!c.detail) {
            r.what = QStringLiteral("처음 챔피언");
            r.tip = QStringLiteral("처음 맞춘 정책이 챔피언이 됩니다(모의만 합니다).");
        } else {
            const TraderPromotionDetail& d = *c.detail;
            r.what = QStringLiteral("챔피언 (정책 %1 → %2)").arg(d.from).arg(c.version);
            r.tip = QStringLiteral("함께 돈 거래일 %1일(필요 %2일) · 날마다 (도전 − 챔피언) 순손익 평균 %3 · %4 하한 %5 (도전 정책 %6개로 "
                                   "보정)")
                        .arg(d.days)
                        .arg(d.required)
                        .arg(d.mean ? signed_won_text(*d.mean) : dash(), level_text(d.level),
                             d.low ? signed_won_text(*d.low) : dash())
                        .arg(d.challengers);
        }
        events << Event{c.at, 0, r};
    }
    // codeyoma/krx-quant#473: each policy's cohort, and the one that passed it over.
    const auto cohort_of = [&policies](qint64 version) -> const TraderCohort* {
        for (const auto& c : policies.cohorts)
            if (c.members.contains(version))
                return &c;
        return nullptr;
    };
    const auto passed_over = [&policies](qint64 version) -> const TraderCohort* {
        for (const auto& c : policies.cohorts)
            for (const auto& over : c.passed_over)
                if (over.first == version)
                    return &c;
        return nullptr;
    };
    for (const auto& c : policies.cohorts) {
        if (!c.judged || !c.judged_at.isValid())
            continue;
        HistoryRow r;
        r.key = QStringLiteral("j%1:%2").arg(c.evaluation).arg(c.cycle);
        r.at = kst_text(c.judged_at, "MM-dd HH:mm");
        QStringList members;
        for (qint64 v : c.members)
            members << QStringLiteral("정책 %1").arg(v);
        r.version = members.isEmpty() ? dash() : members.join(QStringLiteral(", "));
        r.what = QStringLiteral("묶음 판정 (주기 %1)").arg(c.cycle);
        r.why = c.outcome == QLatin1String("promoted") && c.promoted
                    ? QStringLiteral("정책 %1이 챔피언 정책 %2을 이겼다").arg(*c.promoted).arg(c.champion.value_or(0))
                : c.outcome == QLatin1String("kept") ? QStringLiteral("챔피언 그대로: 하한이 0을 넘은 정책이 없다")
                                                     : QStringLiteral("판정 못 함: 검토 날 밤 작업이 돌지 않았다");
        QStringList tip;
        for (const auto& m : c.verdicts)
            tip << QStringLiteral("정책 %1: %2").arg(m.version).arg(cohort_verdict_label(m.verdict));
        r.tip = tip.join('\n');
        r.tone = c.outcome == QLatin1String("promoted") ? Tone::Positive : Tone::Plain;
        events << Event{c.judged_at, 1, r};
    }
    for (const auto& p : policies.policies) {
        const TraderCohort* in = cohort_of(p.version);
        if (p.dropped_at.isValid()) {
            HistoryRow r;
            r.key = QStringLiteral("d%1").arg(p.version);
            r.at = kst_text(p.dropped_at, "MM-dd HH:mm");
            r.version = QStringLiteral("정책 %1").arg(p.version);
            r.what = QStringLiteral("내려짐");
            if (const TraderCohort* over = passed_over(p.version))
                r.why = QStringLiteral("주기 %1의 묶음에 들지 못했다").arg(over->cycle);
            else if (in)
                r.why = QStringLiteral("주기 %1 묶음의 판정에서 뽑히지 않았다").arg(in->cycle);
            else
                r.why = QStringLiteral("함께 돈 거래일 20일까지 챔피언을 이기지 못했다");
            r.tone = Tone::Dim;
            events << Event{p.dropped_at, 2, r};
        }
        if (p.admitted_at.isValid()) {
            HistoryRow r;
            r.key = QStringLiteral("a%1").arg(p.version);
            r.at = kst_text(p.admitted_at, "MM-dd HH:mm");
            r.version = QStringLiteral("정책 %1").arg(p.version);
            if (in) {
                r.what = QStringLiteral("묶음에 듦 (주기 %1)").arg(in->cycle);
                r.why = QStringLiteral("%1부터 검토 날 %2까지 챔피언 옆에서 모의로 돈다").arg(day_text(in->start), day_text(in->review));
            } else {
                r.what = QStringLiteral("도전 자리 받음");
                r.why = QStringLiteral("자리 셋 가운데 하나를 받아, 다음 고르기(08:30)부터 챔피언 옆에서 모의로 돈다");
            }
            r.tip = p.reason;
            events << Event{p.admitted_at, 3, r};
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.at != b.at ? a.at > b.at : a.rank < b.rank;
    });
    QList<HistoryRow> rows;
    for (const auto& e : events)
        rows << e.row;
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
                        QStringLiteral("순손익 %1 · 매매 %2").arg(signed_won_text(d.net)).arg(d.trades)};
        if (practised(d.practice)) { // codeyoma/krx-quant#533; none on a day before it (null)
            tip.last() += QStringLiteral(" (한도가 세는 것)");
            tip << QStringLiteral("연습 %1 · 매매 %2 · 누적 %3 (한도에 세지 않음)")
                       .arg(signed_won_text(d.practice->net))
                       .arg(d.practice->trades)
                       .arg(signed_won_text(d.practice->cumulative));
            if (d.account)
                tip << QStringLiteral("계좌(KIS) %1 · 매매 %2 · 누적 %3")
                           .arg(signed_won_text(d.account->net))
                           .arg(d.account->trades)
                           .arg(signed_won_text(d.account->cumulative));
        }
        tip << QStringLiteral("합 %1 (보이는 첫날부터)").arg(signed_won_text(sum));
        tip << QStringLiteral("하루 가장 낮을 때 %1 (한도의 %2)")
                   .arg(signed_won_text(d.daily_worst), percent_text(d.daily_used));
        tip << QStringLiteral("누적 %1 (가장 낮을 때 %2, 한도의 %3)")
                   .arg(signed_won_text(d.cumulative_net), signed_won_text(d.cumulative_worst),
                        percent_text(d.cumulative_used));
        tip << QStringLiteral("그날 마지막 상태: %1").arg(trader_status_label(d.status));
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

// ---- The decisions (codeyoma/krx-quant#601) ------------------------------------------------------------

QList<BookChoice> book_choices() {
    return {
        {QStringLiteral("practice"), QStringLiteral("연습"),
         QStringLiteral(
             "연습: mock 장부가 연습 칸을 고른 동안(codeyoma/krx-quant#427) 그 칸의 예측마다 한 일. 정책이 없다.")},
        {QStringLiteral("champion"), QStringLiteral("챔피언"),
         QStringLiteral(
             "챔피언: trader 자신의 장부. 챔피언 정책 아래 trader가 돈 모드의 결정이고, 그 매매가 한도에 세고 차트에 "
             "보인다.")},
        {QStringLiteral("shadow"), QStringLiteral("그림자"),
         QStringLiteral(
             "그림자: mock·live에서 KIS 장부 옆에서 챔피언 정책을 모의로 체결하는 장부. 그 매매는 차트에 없다.")},
        {QStringLiteral("challenger"), QStringLiteral("도전"),
         QStringLiteral("도전: 도전 정책마다의 모의 장부. 그 매매는 차트에 없다.")},
    };
}

QString book_label(const QString& book) {
    for (const auto& choice : book_choices())
        if (choice.book == book)
            return choice.text;
    return book;
}

QString book_entry_label(const TraderDecisionBook& book) {
    QString text = book_label(book.book);
    if (book.policy)
        text += QStringLiteral(" 정책 %1").arg(*book.policy);
    return text + QStringLiteral(" · ") + trader_mode_label(book.mode);
}

namespace {

/// A reason's Korean name and what it means (krx-quant design.md 14장 "진입", "모의 체결", "KIS 모의").
struct Reason {
    const char* key;
    const char* label;
    const char* tip;
};

const Reason* reason_of(const QString& key) {
    static const Reason reasons[] = {
        {"not_picked", "고른 칸 아님", "읽는 사이 칸을 다시 골라, 이 칸이 고른 칸이 아니었다."},
        {"not_ok", "예측 없음", "엔진이 이 봉 마감의 예측을 만들지 못했다."},
        {"blocked", "합의 막힘", "엔진이 최종 합의를 막았다."},
        {"special", "특수 상황", "VI, 서킷브레이커, 사이드카 창이 그 예측의 창(봉 마감부터 시간 장벽까지)에 닿았다."},
        {"stale", "늦게 읽음", "표시된 뒤 10초가 지나 읽었다. 멈춰 있던 trader는 놓친 예측으로 사지 않는다."},
        {"expired", "시간 장벽 지남", "결정이나 사려던 호가가 그 예측의 시간 장벽 뒤였다."},
        {"not_up", "오름 아님", "가장 큰 클래스가 오름(방향은 상승, 장벽은 위 먼저)이 아니었다."},
        {"below_threshold", "θ 아래", "오름이지만 그 확률이 그 칸의 문턱 θ보다 낮았다."},
        {"no_reference", "기준가 없음", "봉 마감의 중간가가 없어 목표가와 손절가를 정할 수 없었다."},
        {"closed", "휴장", "거래일이 아니었다."},
        {"before_entry", "진입 전", "봉 마감이 개장 + 5분(보통 09:05) 전이었다."},
        {"past_last_exit", "청산 마감 뒤", "시간 장벽이 종가 단일가 − 5분(보통 15:15) 뒤였다."},
        {"holding", "보유 중", "들고 있는 포지션이나 사려고 기다리는 매수가 있었다. 포지션은 하나다."},
        {"reconciling", "계좌 맞추는 중", "시작할 때의 KIS 계좌 맞추기가 아직 끝나지 않았다."},
        {"outranked", "순위에 밀림", "같은 봉 마감에 순위가 더 높은 칸이 샀다."},
        {"quotes_stalled", "호가 끊김", "수집기의 호가가 5초 넘게 끊겨 새로 사지 않았다."},
        {"entries_paused", "새 진입 정지", "새 진입 정지 중이었다."},
        {"paused", "정지", "정지 중이었다."},
        {"daily_limit", "하루 한도", "하루 손실 한도에 닿아 멈춰 있었다."},
        {"cumulative_limit", "누적 한도", "누적 손실 한도에 닿아 멈춰 있었다."},
        {"order_failed", "주문 실패", "그날 KIS 주문이 실패해 새로 사지 않았다."},
        {"buy_limit", "매수 한도", "그날 매수 주문이 위험 규칙의 한도(기본 20번)에 닿았다."},
        {"cooldown", "쉬는 중",
         "순손실로 끝난 매매가 연달아 위험 규칙의 수(기본 3번)에 닿아, 그 청산부터 쉬는 동안(기본 30분)이었다."},
        {"outside_held", "장부 밖 보유", "실전 계좌가 장부 밖에서 005930을 들고 있었다."},
        {"target_reached", "이미 목표가", "사려던 호가의 매도 1호가가 이미 목표가 이상이었다."},
        {"no_quote", "호가 없음", "결정 뒤 5초 안에 호가가 오지 않았다."},
        {"no_shares", "1주 미만", "손실 예산과 현금으로 1주도 살 수 없었다."},
        {"bad_stop", "손절가 위", "손절가가 매도 1호가 이상이었다: 사기 전에 손절가 아래로 떨어졌다."},
        {"restarted", "다시 시작", "사려고 기다리는 동안 trader가 다시 시작했다."},
        {"unfilled", "체결 없음", "KIS 매수 주문이 취소 전까지 체결되지 않았다(호가가 움직였다)."},
        {"?", "이유 없음", "이유 없이 남은 결정이다(trader는 늘 이유를 남긴다)."},
    };
    for (const auto& r : reasons)
        if (key == QLatin1String(r.key))
            return &r;
    return nullptr;
}

/// The engine's answer of a call (krx-quant #584: the kind's rise, middle or fall): "상승", "보합", "하락" for
/// the direction; "위 먼저", "시간 만료", "아래 먼저" for a barrier; "—" before it.
QString answer_label(const QString& kind, const QString& answer) {
    const bool direction = kind == QLatin1String("direction");
    if (answer == QLatin1String("up"))
        return direction ? QStringLiteral("상승") : QStringLiteral("위 먼저");
    if (answer == QLatin1String("flat"))
        return direction ? QStringLiteral("보합") : QStringLiteral("시간 만료");
    if (answer == QLatin1String("down"))
        return direction ? QStringLiteral("하락") : QStringLiteral("아래 먼저");
    return answer.isEmpty() ? dash() : answer;
}

bool same_book(const TraderDecisionBook& a, const TraderDecisionBook& b) {
    return a.book == b.book && a.mode == b.mode && a.policy == b.policy;
}

/// The earliest decision held of ``book``; invalid without any.
QDateTime earliest_of(const TraderDecisionBook& book) {
    QDateTime earliest;
    for (const auto& d : book.decisions)
        if (!earliest.isValid() || d.decided_at < earliest)
            earliest = d.decided_at;
    return earliest;
}

bool newer(const TraderDecision& a, const TraderDecision& b) {
    return a.decided_at != b.decided_at ? a.decided_at > b.decided_at : a.id > b.id;
}

/// Whether a call traded nothing: a skip, or a buy that did not fill (not one waiting for its quote).
bool traded_nothing(const TraderDecision& d) {
    return !d.trade_id && !(d.buy && d.reason.isEmpty());
}

/// The 1-minute bar ``at`` falls in: its minute's start, as chart_time.
qint64 bar_of(const QDateTime& at) {
    const qint64 time = chart_time(at);
    return time - ((time % 60) + 60) % 60;
}

/// "오름 아님 230 · 매수 한도 30": each reason's name and count.
QString counts_text(const QList<std::pair<QString, int>>& counts) {
    QStringList parts;
    for (const auto& [reason, n] : counts)
        parts << QStringLiteral("%1 %2").arg(reason_label(reason), korean().toString(n));
    return parts.join(QStringLiteral(" · "));
}

int sum_of(const QList<std::pair<QString, int>>& counts) {
    int sum = 0;
    for (const auto& c : counts)
        sum += c.second;
    return sum;
}

} // namespace

QString reason_label(const QString& reason) {
    const Reason* r = reason_of(reason);
    return r ? QString::fromUtf8(r->label) : reason;
}

QString reason_tip(const QString& reason) {
    const Reason* r = reason_of(reason);
    return r ? QString::fromUtf8(r->tip) : QString();
}

DecisionPages decision_pages(const TraderDecisions& latest) {
    DecisionPages pages{latest, {}};
    for (const auto& book : latest.books)
        pages.done << (!book.more || book.decisions.isEmpty());
    return pages;
}

std::optional<QDateTime> decisions_before(const DecisionPages& pages) {
    QDateTime latest; // of the earliest held of the books not done
    for (qsizetype i = 0; i < pages.read.books.size() && i < pages.done.size(); ++i)
        if (!pages.done[i])
            if (const QDateTime earliest = earliest_of(pages.read.books[i]);
                earliest.isValid() && (!latest.isValid() || earliest > latest))
                latest = earliest;
    if (!latest.isValid())
        return std::nullopt;
    return latest.addMSecs(1);
}

void join_decisions(DecisionPages& pages, const TraderDecisions& page, const QDateTime& before) {
    QList<bool> in_page(pages.read.books.size(), false);
    for (const auto& book : page.books) {
        qsizetype i = 0;
        while (i < pages.read.books.size() && !same_book(pages.read.books[i], book))
            ++i;
        if (i == pages.read.books.size()) { // none held of it: as a latest page's
            pages.read.books << book;
            pages.done << (!book.more || book.decisions.isEmpty());
            in_page << true;
            continue;
        }
        in_page[i] = true;
        TraderDecisionBook& held = pages.read.books[i];
        const QDateTime was = earliest_of(held);
        QHash<qint64, qsizetype> at; // a decision's place, by its id
        for (qsizetype k = 0; k < held.decisions.size(); ++k)
            at.insert(held.decisions[k].id, k);
        for (const auto& d : book.decisions) {
            if (const auto found = at.constFind(d.id); found != at.constEnd()) {
                held.decisions[*found] = d;
            } else {
                at.insert(d.id, held.decisions.size());
                held.decisions << d;
            }
        }
        std::stable_sort(held.decisions.begin(), held.decisions.end(), newer);
        if (pages.done[i])
            continue;
        // Asked for this book (its earliest a millisecond before ``before``), the page brings none earlier:
        // a millisecond holds more than a page, and asking again would bring the same.
        const bool asked = was.isValid() && was.addMSecs(1) == before;
        const QDateTime earliest = earliest_of(book);
        if (!book.more || (asked && (!earliest.isValid() || earliest >= was)))
            pages.done[i] = true;
    }
    // A book not done that the page leaves out has nothing before ``before``.
    for (qsizetype i = 0; i < in_page.size() && i < pages.done.size(); ++i)
        if (!in_page[i])
            pages.done[i] = true;
}

QString decisions_line(const std::optional<TraderDecisions>& decisions, bool served, bool reading) {
    if (!served)
        return QStringLiteral("엔진이 아직 지원하지 않음");
    if (!decisions)
        return reading ? QStringLiteral("읽는 중") : QString();
    int count = 0;
    for (const auto& book : decisions->books)
        count += book.totals.decisions;
    const QString day = decisions->day.isValid() ? day_text(decisions->day) + QStringLiteral(" · ") : QString();
    return count > 0 ? day + QStringLiteral("결정 %1개").arg(korean().toString(count))
                     : day + QStringLiteral("결정 없음");
}

QList<TotalsLine> totals_lines(const TraderDecisions& decisions) {
    QList<TotalsLine> out;
    for (const auto& book : decisions.books) {
        const TraderDecisionTotals& t = book.totals;
        QStringList parts{book_entry_label(book), QStringLiteral("결정 %1").arg(korean().toString(t.decisions)),
                          QStringLiteral("산 것 %1").arg(korean().toString(t.bought))};
        if (t.waiting > 0)
            parts << QStringLiteral("호가 기다림 %1").arg(korean().toString(t.waiting));
        if (const int unfilled = sum_of(t.unfilled); unfilled > 0)
            parts << QStringLiteral("못 산 것 %1(%2)").arg(korean().toString(unfilled), counts_text(t.unfilled));
        if (const int skipped = sum_of(t.skipped); skipped > 0)
            parts << QStringLiteral("건너뜀 %1(%2)").arg(korean().toString(skipped), counts_text(t.skipped));
        // What came of the calls the engine answered: the buys filled right, the skips that missed a rise.
        int bought = 0, bought_right = 0, skipped = 0, missed = 0;
        for (const auto& d : book.decisions) {
            if (!d.right)
                continue;
            if (d.trade_id) {
                ++bought;
                bought_right += *d.right ? 1 : 0;
            } else if (!d.buy) {
                ++skipped;
                missed += *d.right ? 1 : 0;
            }
        }
        QString text = parts.join(QStringLiteral(" · "));
        if (bought + skipped > 0)
            text += QStringLiteral(" · 정답 난 것: 산 것 %1/%2 맞음 · 건너뛴 것 %3/%4 놓친 오름")
                        .arg(korean().toString(bought_right), korean().toString(bought), korean().toString(missed),
                             korean().toString(skipped));
        QStringList tip{
            QStringLiteral("%1의 그날 결정 모두의 합계다(엔진의 totals).").arg(book_entry_label(book)),
            QStringLiteral("산 것: 체결해 매매가 된 매수. 호가 기다림: 결정 뒤 첫 호가를 기다리는 매수. 못 산 것: 사려 "
                           "했지만 체결하지 못한 매수. 건너뜀: 사지 않은 결정.")};
        for (const auto& [title, counts] :
             {std::pair{QStringLiteral("못 산 이유"), t.unfilled}, std::pair{QStringLiteral("건너뛴 이유"), t.skipped}})
            for (const auto& [reason, n] : counts)
                tip << QStringLiteral("%1 · %2 %3: %4")
                           .arg(title, reason_label(reason), korean().toString(n), reason_tip(reason));
        if (bought + skipped > 0)
            tip << QStringLiteral(
                "정답 난 것: 엔진이 정답을 낸 결정만 센다. 산 것은 오름이 왔으면 맞음, 건너뛴 것은 오름이 "
                "왔으면 놓친 오름이다.");
        out << TotalsLine{text, tip.join('\n')};
    }
    return out;
}

QList<DecisionRow> decision_rows(const TraderDecisions& decisions) {
    QList<std::pair<const TraderDecision*, const TraderDecisionBook*>> all;
    for (const auto& book : decisions.books)
        for (const auto& d : book.decisions)
            all.push_back({&d, &book});
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return newer(*a.first, *b.first); });
    QList<DecisionRow> rows;
    rows.reserve(all.size());
    for (const auto& [d, book] : all) {
        DecisionRow row;
        row.id = d->id;
        row.at = kst_text(d->decided_at, "HH:mm:ss");
        row.book = book_entry_label(*book);
        row.cell = kind_label(d->kind) + QStringLiteral(" · ") + d->predictor;
        row.probability = d->probability ? percent_text(*d->probability) : dash();
        row.threshold = d->threshold ? percent_text(*d->threshold) : dash();
        row.p_star = d->p_star ? percent_text(*d->p_star) : dash();
        if (d->trade_id) {
            row.action = QStringLiteral("매수");
            row.action_tone = Tone::Positive;
        } else if (d->buy) {
            row.action = d->reason.isEmpty() ? QStringLiteral("호가 기다림") : QStringLiteral("매수 못 함");
            row.action_tone = Tone::Warning;
        } else {
            row.action = QStringLiteral("건너뜀");
            row.action_tone = Tone::Dim;
        }
        row.reason = d->reason.isEmpty() ? dash() : reason_label(d->reason);
        row.reason_tip =
            d->reason.isEmpty() ? QString() : QStringLiteral("%1 (%2)").arg(reason_tip(d->reason), d->reason);
        row.answer = answer_label(d->kind, d->answer);
        if (!d->right) {
            row.right = dash();
            row.right_tip = QStringLiteral("엔진이 아직 이 예측의 정답을 내지 않았다.");
        } else if (d->buy) {
            row.right = *d->right ? QStringLiteral("맞음") : QStringLiteral("틀림");
            // A buy that did not fill traded nothing: its call alone, faint.
            row.right_tone = !d->trade_id ? Tone::Dim : *d->right ? Tone::Positive : Tone::Negative;
            row.right_tip = *d->right ? QStringLiteral("산 결정이고 오름이 왔다.")
                                      : QStringLiteral("산 결정이고 오름이 오지 않았다.");
            if (!d->trade_id)
                row.right_tip += QStringLiteral(" 체결하지 못해 매매는 없다.");
        } else {
            row.right = *d->right ? QStringLiteral("놓친 오름") : QStringLiteral("맞음");
            row.right_tone = *d->right ? Tone::Warning : Tone::Dim;
            row.right_tip = *d->right ? QStringLiteral("건너뛴 결정이고 오름이 왔다.")
                                      : QStringLiteral("건너뛴 결정이고 오름이 오지 않았다.");
        }
        QStringList tip{QStringLiteral("결정 %1 · 예측 %2 (%3 · %4)")
                            .arg(d->id)
                            .arg(d->prediction_id)
                            .arg(kind_label(d->kind), d->predictor),
                        QStringLiteral("봉 마감 %1 · 표시 %2 · 결정 %3")
                            .arg(kst_text(d->bar_close, "HH:mm:ss"), kst_text(d->displayed_at, "HH:mm:ss.zzz"),
                                 kst_text(d->decided_at, "HH:mm:ss.zzz")),
                        QStringLiteral("장부 %1").arg(row.book)};
        if (d->trade_id)
            tip << QStringLiteral("매매 %1").arg(*d->trade_id);
        if (!d->threshold)
            tip << QStringLiteral("θ·p* —: 그 칸이 고르기에 없었거나(고른 칸 아님) 첫 고르기 전이다.");
        row.tip = tip.join('\n');
        rows << row;
    }
    return rows;
}

QList<SkipIcon> skip_icons(const TraderDecisions& decisions) {
    QMap<qint64, bool> live; // by bar: whether a live book's
    for (const auto& book : decisions.books)
        for (const auto& d : book.decisions)
            if (traded_nothing(d)) {
                const qint64 time = bar_of(d.decided_at);
                live.insert(time, live.value(time, false) || book.mode == QLatin1String("live"));
            }
    QList<SkipIcon> out;
    out.reserve(live.size());
    for (auto it = live.cbegin(); it != live.cend(); ++it)
        out << SkipIcon{it.key(), it.value()};
    return out;
}

QString skip_line(const TraderDecision& d, const TraderDecisionBook& book) {
    QString who = book_label(book.book);
    if (book.policy)
        who += QStringLiteral(" 정책 %1").arg(*book.policy);
    QStringList parts{who + (d.buy ? QStringLiteral(" 매수 못 함") : QStringLiteral(" 건너뜀")), kind_label(d.kind),
                      d.predictor};
    if (d.probability)
        parts << (d.threshold
                      ? QStringLiteral("%1 (θ %2)").arg(percent_text(*d.probability), percent_text(*d.threshold))
                      : percent_text(*d.probability));
    if (!d.reason.isEmpty())
        parts << reason_label(d.reason);
    if (!d.answer.isEmpty())
        parts << QStringLiteral("정답 ") + answer_label(d.kind, d.answer);
    if (!d.buy && d.right && *d.right)
        parts << QStringLiteral("놓친 오름");
    return parts.join(QStringLiteral(" · "));
}

QList<BarHover> with_skip_lines(QList<BarHover> hovers, const TraderDecisions& decisions) {
    QList<std::pair<const TraderDecision*, const TraderDecisionBook*>> calls;
    for (const auto& book : decisions.books)
        for (const auto& d : book.decisions)
            if (traded_nothing(d))
                calls.push_back({&d, &book});
    std::stable_sort(calls.begin(), calls.end(),
                     [](const auto& a, const auto& b) { return newer(*b.first, *a.first); });
    QHash<qint64, qsizetype> at; // a box's place, by its bar's start
    for (qsizetype i = 0; i < hovers.size(); ++i)
        at.insert(hovers[i].time, i);
    for (const auto& [d, book] : calls) {
        const qint64 time = bar_of(d->decided_at);
        if (!at.contains(time)) {
            BarHover box;
            box.time = time;
            at.insert(time, hovers.size());
            hovers << box;
        }
        hovers[at.value(time)].lines << skip_line(*d, *book);
    }
    return hovers;
}

} // namespace fincept::krx_quant::trader
