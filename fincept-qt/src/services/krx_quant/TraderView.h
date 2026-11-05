#pragma once

#include "services/krx_quant/ChartModel.h"
#include "services/krx_quant/EngineData.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/// What the KRX auto trader screen shows (codeyoma/krx-quant#397, #401, #601), from the engine's GET /v1/trader,
/// /v1/trader/days, /v1/trader/trades, /v1/trader/policies and /v1/trader/decisions: the header's mode badge,
/// status, champion and live gate, the cards, a book's decisions of the day with their totals and the chart's
/// hollow marks of its calls that traded nothing, the days' curve, the trades table (the champion's or a
/// policy version's), the recent requests, the policy versions with the champion's history, and what the
/// two pause buttons ask before they send POST /v1/trader/pause. The screen only draws them; resuming is the server's
/// alone, so nothing here offers it.
///
/// Qt Core only (no network, no widgets), unit-tested on its own (tests/tst_krx_trader.cpp).
namespace fincept::krx_quant::trader {

/// How often the screen reads the trader's state again: every kHeldMs while it runs with a position
/// open (its bid and P&L move by the second), every kSessionMs in the continuous session (09:00–15:20
/// KST on a weekday: a buy may come at any bar close), else every kIdleMs. A holiday on a weekday is
/// read as a session day: the screen does not know the market's calendar.
inline constexpr int kHeldMs = 2'000;
inline constexpr int kSessionMs = 5'000;
inline constexpr int kIdleMs = 300'000;
bool in_session(const QDateTime& now);
int poll_ms(const std::optional<TraderState>& last, const QDateTime& now);

/// The screen's 1-minute chart (codeyoma/krx-quant#537) is read again as each minute turns: a bar is over
/// then, and the engine has it, and the buys and sells filled in it, a moment later. kChartSettleMs is that
/// moment.
inline constexpr int kChartSettleMs = 3'000;
/// The milliseconds from ``now`` to the next such read: kChartSettleMs past the next minute's turn, or past
/// this one's while that is still ahead.
int chart_ms(const QDateTime& now);

/// What the trades, the days and the policy versions hang on in a state: its mode, day, trades exited
/// that day (the mock practice's too, codeyoma/krx-quant#533), open trade, status and counts' starts, and
/// the champion and each running policy's status, paper days and trades (codeyoma/krx-quant#401). A state
/// with another key than the last read's has them read again (KrxTraderLive); so does kIdleMs passing.
QString history_key(const TraderState& state);

/// How a value is coloured. Live and Simulated are the chart's colours for the live account's trades
/// and the others' (paper, KIS's mock trading; codeyoma/krx-quant#398).
enum class Tone { Plain, Dim, Positive, Negative, Warning, Live, Simulated };

/// A badge of the header: its text, what it means, its colour.
struct Badge {
    QString text;
    QString tip;
    Tone tone = Tone::Plain;
    bool operator==(const Badge&) const = default;
};
/// The mode: "모의", "KIS 모의", "실전"; "실전 대기" when KRX_TRADER_MODE is live but the gate or an execution
/// check is not passed yet, so it runs as paper (codeyoma/krx-quant#408), its tooltip naming what is missing;
/// "—" before the trader wrote its state.
Badge mode_badge(const TraderState& state);
/// Beside the mode (codeyoma/krx-quant#427): "연습" while the mock book practises (no champion cell to
/// trade, KRX_TRADER_MOCK_PRACTICE on) or holds a practice position, its tooltip naming the practice cell
/// and what counts it (the loss limits not since codeyoma/krx-quant#533); none otherwise, and from a trader
/// before #427.
std::optional<Badge> practice_badge(const TraderState& state);
/// The status: "도는 중", "새 진입 정지", "정지", "주문 실패" (codeyoma/krx-quant#405), "하루 한도", "누적 한도";
/// "응답 없음" when the trader has
/// not written it for a while (the values under it are its last), "상태 없음" before it first did.
Badge status_badge(const TraderState& state);
/// The line after them: "상태 10-12 10:30:00 · 시작 10-12 10:05 · 정지 10-08 15:02 화면(새 진입 정지)".
QString header_line(const TraderState& state);

/// The champion and the live gate's progress (codeyoma/krx-quant#400, #401): "챔피언 정책 1 · 관문 모의
/// 12/20일 · 하한 -0.3%" (the lower bound of the daily net P&L over the capital), "… · 관문 넘음" once
/// passed, "챔피언 정책 없음" before the first; empty from an engine before #400. From an engine that judges
/// checkpoints (codeyoma/krx-quant#408): "챔피언 정책 1 · 관문 판정 이력 20일째(지금 12일) · 하한 -0.3%", and
/// "챔피언 정책 1 · 관문 넘음(1번째 판정)" once passed.
QString gate_text(const TraderState& state);
/// Its tooltip: what the gate asks for and that passing it sends no live order by itself.
QString gate_tip(const TraderState& state);
/// Its colour: Positive once the gate is passed, else Plain.
Tone gate_tone(const TraderState& state);
/// The gate in full, over the policy table (codeyoma/krx-quant#401): "실전 관문 · 챔피언 정책 1의 모의 기록
/// 2/20일 · 날마다 평균 -5,209원 · 95% 하한 -26,882원 (운용 상한의 -2.7%) · 못 넘음: 18일 더, 하한 ≤ 0";
/// "… · 넘음 …" once passed; before the first champion, that it counts from it; empty from an engine
/// before #400. With checkpoints (codeyoma/krx-quant#408) the bound is at the next checkpoint's level and
/// what it is short of starts with the next checkpoint and its α: "못 넘음: 다음 판정 1번째, 이력 20일째(지금
/// 2일, α 1.52%), 18일 더, 하한 ≤ 0"; "넘음(1번째 판정, 10-13): …" once passed.
QString gate_line(const TraderState& state);

/// The trader's clock now: its state's time, moved on by what passed since it was read
/// (``answered_at`` and ``now`` on the screen's clock); ``now`` before it wrote one.
QDateTime trader_now(const TraderState& state, const QDateTime& answered_at, const QDateTime& now);
/// What is left to a time barrier at ``now``: "1분 0초 남음", "12초 남음", "지남".
QString remaining_text(const QDateTime& barrier, const QDateTime& now);

/// A card: its title, its value (large), the lines under it, its tooltip and the value's colour.
struct Card {
    QString title;
    QString value;
    QStringList lines;
    QString tip;
    Tone tone = Tone::Plain;
};
/// The cards, in order: the capital, the cash, the position (quantity, entry price, target, stop,
/// time left to its time barrier at ``now``, the trader's clock), today's and the cumulative net P&L
/// (what the loss limits count; codeyoma/krx-quant#533: with practice P&L, a line of the mock practice's and
/// the account's under it), the daily and the cumulative limit's use (#533: with the mock practice on or
/// practice P&L, that the limit leaves it out); the risk rules (codeyoma/krx-quant#475: the day's buy orders,
/// the losses in a row and the cooldown at ``now``, the loss budget) and the review cycle (#469: its day,
/// its review day and the challengers' cohort, #473), "—" from an engine before them.
QList<Card> cards(const TraderState& state, const QDateTime& now);
/// The number of cards ``cards`` gives, whatever the state.
inline constexpr int kCards = 9;

/// A row of the trades table, newest entry first as the engine gives them.
struct TradeRow {
    qint64 id = 0; ///< the trade's
    QString entry; ///< "10-12 10:27:05 · 78,600"
    QString exit;  ///< "10:31:00 · 78,700"; "보유 중"
    QString quantity;
    QString reason; ///< trader_exit_label's; "—" while held, "매도 중" while a pause or a limit sells it
    QString cost;   ///< the costs; while held, the buy's commission
    QString net;    ///< after costs; while held, the open position's P&L at the bid ("평가 +5,015원")
    QString cell;   ///< "장벽 4분 · breiman (1위)"
    QString probability; ///< "68.0% (θ 60.0%)"
    QString p_star;      ///< "61.0%"; "—" before the cost map picked the cells (#395)
    QString low;         ///< the cell's hit rate's lower bound
    QString mode;
    /// The policy version it was made under: "정책 1"; "정책 2 · 도전" for a challenger's paper trade
    /// (codeyoma/krx-quant#400); "연습" for the mock book's practice trade, of no policy
    /// (codeyoma/krx-quant#427); "—" before the first champion.
    QString policy;
    QString tip; ///< the trade's barriers, its decision and sizing
    QString cost_tip;
    QString net_tip;
    QString why_tip; ///< the probability's, p*'s and the lower bound's
    Tone reason_tone = Tone::Plain;
    Tone net_tone = Tone::Plain;
    Tone policy_tone = Tone::Plain;
    bool held = false;
    bool live = false;
};
QList<TradeRow> trade_rows(const QList<TraderTrade>& trades, const std::optional<TraderState>& state);

/// A choice of whose trades the trades table shows (codeyoma/krx-quant#401): the champion's, the trader's
/// own (no ``version``: GET /v1/trader/trades as it is, the default), or a policy version's (``policy=``:
/// its paper trades as a challenger and its trades as the champion).
struct PolicyChoice {
    std::optional<qint64> version;
    QString text; ///< "챔피언의 매매", "정책 2 · 도전"
    QString tip;
    bool operator==(const PolicyChoice&) const = default;
};
/// The choices: the champion's first, then every policy version in the policy table's order; from the
/// state's running ones before the versions are read.
QList<PolicyChoice> trade_policy_choices(const std::optional<TraderPolicies>& policies,
                                         const std::optional<TraderState>& state);
/// The trades table's line: "챔피언의 매매 11개"; "정책 2의 매매 2개".
QString trades_line(const std::optional<qint64>& policy, qsizetype count);

/// A policy's status in Korean (codeyoma/krx-quant#400): "챔피언", "도전", "대기", "전 챔피언", "내려짐".
QString policy_status_label(const QString& status);

/// A row of the policy versions table (codeyoma/krx-quant#401): the champion first, then the challengers,
/// the waiting, the former champions and the dropped, each newest version first. Its paper record and a
/// running one's standing come from the state (read every few seconds) when it has them, else from the
/// policy versions.
struct PolicyRow {
    qint64 version = 0;
    QString version_text; ///< "정책 2"
    QString status;       ///< policy_status_label's
    QString created;      ///< "10-08 22:31"
    QString cells;        ///< "장벽 4분 · breiman (1위)", several in rank order joined by " / "; "칸 없음"
    QString thresholds;   ///< each cell's θ in that order: "65.0%"
    /// What each cell entered on, in that order (codeyoma/krx-quant#472, #475): its net per signal's lower
    /// bound in ticks ("하한 +0.04틱"); a policy fitted before #471 its Kelly fraction λ ("λ 0.5"); "—".
    QString basis;
    QString days;         ///< its paper record: "2일"
    QString trades;       ///< "4개"
    QString net;
    QString mean; ///< a day
    QString low;  ///< the mean's 95% lower bound; "—" from fewer than two days
    /// The champion's live gate ("관문 2/20일 · 못 넘음"), a challenger's standing against it ("함께 1/10일 ·
    /// 날 수 부족", "이김 · 함께 12/10일 · 차이 하한 +63원"), "자리 기다림", "10-29 정책 2에게 짐", "10-29 내려짐".
    QString judgement;
    QString tip;           ///< when and why it was fitted, on what, when it got a slot or was dropped
    QString cells_tip;     ///< each cell's fitted values, and the cells the cost map passed it left out
    QString paper_tip;     ///< its first and last day, the mean, the bound and over what
    QString judgement_tip; ///< the rule behind the judgement and its figures
    Tone status_tone = Tone::Plain;
    Tone net_tone = Tone::Plain;
    Tone low_tone = Tone::Plain;
    Tone judgement_tone = Tone::Plain;
};
QList<PolicyRow> policy_rows(const TraderPolicies& policies, const std::optional<TraderState>& state);
/// The policy table's title line: "챔피언 정책 1 · 도전 1개 · 대기 0개 · 전 챔피언 0개 · 내려짐 0개"; why it is
/// empty before the first nightly fit.
QString policies_line(const TraderPolicies& policies);
/// The challengers' cohort over the policy table (codeyoma/krx-quant#473), the one formed last (the
/// state's, else the policy versions' newest): "도전 정책 묶음 · 주기 1(10-12–11-06) · 정책 2 · 챔피언 정책 1과
/// 견줌 · 검토 날 11-06 장 마감 뒤 한 번 판정", once judged with its verdict; before the first, that the
/// next review night forms one; empty from an engine before #473 (and its tooltip empty).
struct CohortLine {
    QString text;
    QString tip;
    Tone tone = Tone::Plain;
};
CohortLine cohort_line(const std::optional<TraderPolicies>& policies, const std::optional<TraderState>& state);

/// A row of the champion's and the challengers' history (codeyoma/krx-quant#401), newest first: a change
/// of the champion with its reason and figures, a challenger's slot given, a challenger dropped.
struct HistoryRow {
    QString key; ///< unique among the rows
    QString at;  ///< "10-29 22:30"
    QString version;
    QString what; ///< "처음 챔피언", "챔피언 (정책 1 → 2)", "도전 자리 받음", "내려짐"
    QString why;
    QString tip;
    Tone tone = Tone::Plain;
};
QList<HistoryRow> history_rows(const TraderPolicies& policies);

/// The modes the days hold, the latest day's first.
QStringList day_modes(const QList<TraderDay>& days);
/// The mode the curve shows first: the trader's now if it has days, else the latest day's.
QString default_mode(const QList<TraderDay>& days, const std::optional<TraderState>& state);
/// A day of the curve: its net P&L (what the loss limits count) and the running sum from the first day
/// shown, with its tooltip (codeyoma/krx-quant#533: with practice P&L that day, the practice's and the
/// account's; none on a day before it).
struct DayPoint {
    QDate day;
    double net = 0;
    double sum = 0;
    QString tip;
    bool halted = false; ///< a loss limit was reached that day
    bool operator==(const DayPoint&) const = default;
};
/// ``mode``'s days, oldest first.
QList<DayPoint> day_points(const QList<TraderDay>& days, const QString& mode);
/// The curve's title line: "모의 3일 · 순손익 합 -8,496원 · 이익 1일 · 손실 2일"; why it is empty.
QString days_line(const QList<DayPoint>& points, const QString& mode);

/// A row of the recent pauses, resumes and re-arms, newest first.
struct ControlRow {
    qint64 id = 0; ///< the request's
    QString at;
    QString what;    ///< "새 진입 정지", "정지하고 즉시 매도", "재개", "누적 한도 다시 켜기 (모의)"
    QString who;     ///< "화면", "서버"
    QString applied; ///< "읽음 08:41:00"; "아직 읽지 않음"
    bool waiting = false;
};
QList<ControlRow> control_rows(const TraderState& state);

/// What a pause button asks before it sends: the confirm window's title, its text and its button.
struct Question {
    QString title;
    QString text;
    QString accept;
};
Question pause_question(bool sell_now, const std::optional<TraderState>& state);
/// Whether a pause button can send now (``enabled``), and its tooltip: why not when it cannot (the
/// state not read yet, the same pause in force, or asked for and not read yet).
struct Action {
    bool enabled = false;
    QString tip;
};
Action pause_action(bool sell_now, const std::optional<TraderState>& state);
/// The line by the buttons: resuming is the server's alone.
QString resume_note();

// ---- The decisions (codeyoma/krx-quant#601) ------------------------------------------------------------

/// The decisions read at once (GET /v1/trader/decisions?limit=): the engine's most. A book's day is read
/// whole, page after page (DecisionPages), each time the panel reads.
inline constexpr int kDecisionPage = 1000;
/// The book the decisions panel shows first: the mock book's practice (codeyoma/krx-quant#427).
inline constexpr const char* kDefaultBook = "practice";

/// A book the decisions panel can show (GET /v1/trader/decisions?book=).
struct BookChoice {
    QString book; ///< "practice", "champion", "shadow", "challenger"
    QString text; ///< "연습", "챔피언", "그림자", "도전"
    QString tip;
    bool operator==(const BookChoice&) const = default;
};
/// The books, the practice's first (the default), then the champion's, the shadow's and the challengers'.
QList<BookChoice> book_choices();
/// A book's name in Korean: "연습", "챔피언", "그림자", "도전"; an unknown one as it is.
QString book_label(const QString& book);
/// One of a day's books by its mode and policy: "연습 · KIS 모의", "챔피언 정책 1 · 실전", "도전 정책 2 · 모의";
/// "챔피언 · 모의" before the first champion.
QString book_entry_label(const TraderDecisionBook& book);

/// Why a call bought nothing, in Korean (krx-quant design.md 14장 "진입", the trader's ``decisions.reason``):
/// a skip's ("오름 아님", "θ 아래", "보유 중", "매수 한도", "쉬는 중", …) or a buy's that did not fill ("호가 없음",
/// "이미 목표가", "1주 미만", …); an unknown one as it is.
QString reason_label(const QString& reason);
/// What it means: the rule behind it, in a sentence; empty for an unknown one.
QString reason_tip(const QString& reason);

/// A book's day of decisions being read (codeyoma/krx-quant#601): the pages joined so far, each of the day's
/// books (a mode and a policy each) newest first, and which of them are read back to their first that day.
struct DecisionPages {
    TraderDecisions read;
    QList<bool> done; ///< by read.books' index
    bool operator==(const DecisionPages&) const = default;
};
/// The pages once the latest one (read without ``before``) is in: a book with ``more`` is not done.
DecisionPages decision_pages(const TraderDecisions& latest);
/// The next page to read: its ``before``; none once every book is read back to its first decision that day.
/// It is the latest of the earliest decisions held of the books not done, a millisecond past it: the engine's
/// times are to the microsecond and this screen's to the millisecond, so that millisecond's decisions come
/// again (joined by id, not doubled) and none is missed; the engine never splits a moment's decisions
/// between two pages. The page holds the other books' decisions before it too.
std::optional<QDateTime> decisions_before(const DecisionPages& pages);
/// ``pages`` with ``page`` (read with ``before``, decisions_before's) joined in: each book's decisions by id,
/// the page's in place of those held. A book is done once a page says it has no more, or once the page
/// asked for it brings none earlier than those held (a millisecond holding more than a page: no further).
void join_decisions(DecisionPages& pages, const TraderDecisions& page, const QDateTime& before);

/// The decisions panel's header line: "10-12(월) · 결정 300개"; "10-12(월) · 결정 없음"; "엔진이 아직 지원하지
/// 않음" from an engine without them (``served`` false, krx-quant #584); "읽는 중" while the first read is under
/// way; empty before.
QString decisions_line(const std::optional<TraderDecisions>& decisions, bool served, bool reading);
/// A line of a book's totals of the day and its tooltip: "연습 · KIS 모의 · 결정 300 · 산 것 18 · 못 산 것 2(호가
/// 없음 2) · 건너뜀 280(오름 아님 230 · 매수 한도 30 · …)"; with the answers in, what came of them: "· 정답
/// 난 것: 산 것 12/18 맞음 · 건너뛴 것 40/250 놓친 오름" (each part only with an answered call of it).
struct TotalsLine {
    QString text;
    QString tip;
};
QList<TotalsLine> totals_lines(const TraderDecisions& decisions);

/// A row of the decisions table, newest first over the day's books.
struct DecisionRow {
    qint64 id = 0;       ///< the decision's (unique over the books)
    QString at;          ///< when it was decided: "10:05:02"
    QString book;        ///< book_entry_label's
    QString cell;        ///< "장벽 4분 · breiman"
    QString probability; ///< the rise's: "41.0%"; "—" without a forecast
    QString threshold;   ///< θ: "60.0%"; "—"
    QString p_star;      ///< "58.0%"; "—"
    QString action;      ///< "매수", "매수 못 함", "호가 기다림", "건너뜀"
    QString reason;      ///< reason_label's; "—" for a buy filled or waiting
    /// The engine's answer: "상승", "보합", "하락" for the direction, "위 먼저", "시간 만료", "아래 먼저" for a
    /// barrier; "—" before it.
    QString answer;
    /// What came of the call: a buy's "맞음" (the rise came) or "틀림"; a skip's "놓친 오름" (it came) or
    /// "맞음"; "—" before the answer.
    QString right;
    QString tip;        ///< its prediction, bar close, display and decision times, its trade
    QString reason_tip; ///< reason_tip's
    QString right_tip;
    Tone action_tone = Tone::Plain;
    Tone right_tone = Tone::Dim;
};
QList<DecisionRow> decision_rows(const TraderDecisions& decisions);

/// The chart's hollow marks (codeyoma/krx-quant#601): every bar with a call of ``decisions`` that traded
/// nothing (a skip, or a buy that did not fill; not a buy filled, which the chart marks as a trade, nor
/// one waiting for its quote), one a bar, in time order.
QList<SkipIcon> skip_icons(const TraderDecisions& decisions);
/// Such a call's line in its bar's hover box: "연습 건너뜀 · 장벽 4분 · breiman · 41.0% (θ 60.0%) · 오름 아님",
/// "연습 매수 못 함 · … · 호가 없음", the answer's once it is in ("· 정답 위 먼저 · 놓친 오름").
QString skip_line(const TraderDecision& decision, const TraderDecisionBook& book);
/// ``hovers`` (the 1-minute bars' boxes) with the lines of the calls skip_icons marks last in their bar's
/// box, in time order; a bar without a box gets one of its lines alone.
QList<BarHover> with_skip_lines(QList<BarHover> hovers, const TraderDecisions& decisions);

/// "1,000,000원"; "+1,922원", "-162원", "0원"; "61.0%".
QString won_text(double won);
QString signed_won_text(double won);
QString percent_text(double share, int decimals = 1);
/// The cell's kind: "방향 4분", "장벽 4분", "장벽 15분", "장벽 30분"; an unknown one as it is.
QString kind_label(const QString& kind);

} // namespace fincept::krx_quant::trader
