#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/// What the KRX auto trader screen shows (codeyoma/krx-quant#397), from the engine's GET /v1/trader,
/// /v1/trader/days and /v1/trader/trades: the header's mode badge and status, the cards, the days'
/// curve, the trades table and the recent requests, and what the two pause buttons ask before they
/// send POST /v1/trader/pause. The screen only draws them; resuming is the server's alone, so nothing
/// here offers it.
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

/// What the trades and the days hang on in a state: its mode, day, trades exited that day, open
/// trade, status and counts' starts. A state with another key than the last read's has them read
/// again (KrxTraderLive); so does kIdleMs passing.
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
/// The mode: "모의", "KIS 모의", "실전"; "—" before the trader wrote its state.
Badge mode_badge(const TraderState& state);
/// The status: "도는 중", "새 진입 정지", "정지", "하루 한도", "누적 한도"; "응답 없음" when the trader has
/// not written it for a while (the values under it are its last), "상태 없음" before it first did.
Badge status_badge(const TraderState& state);
/// The line after them: "상태 10-12 10:30:00 · 시작 10-12 10:05 · 정지 10-08 15:02 화면(새 진입 정지)".
QString header_line(const TraderState& state);

/// The champion and the live gate's progress (codeyoma/krx-quant#400): "챔피언 정책 1 · 관문 모의 12/20일 ·
/// 하한 -0.3%" (the lower bound of the daily net P&L over the capital), "… · 관문 넘음" once passed,
/// "챔피언 정책 없음" before the first; empty from an engine before #400. The policy table and the gate's
/// detail are codeyoma/krx-quant#401's.
QString gate_text(const TraderState& state);
/// Its tooltip: what the gate asks for and that passing it sends no live order by itself.
QString gate_tip(const TraderState& state);

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
/// time left to its time barrier at ``now``, the trader's clock), today's and the cumulative net P&L,
/// the daily and the cumulative limit's use.
QList<Card> cards(const TraderState& state, const QDateTime& now);

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
    QString tip; ///< the trade's barriers, its decision and sizing
    QString cost_tip;
    QString net_tip;
    QString why_tip; ///< the probability's, p*'s and the lower bound's
    Tone reason_tone = Tone::Plain;
    Tone net_tone = Tone::Plain;
    bool held = false;
    bool live = false;
};
QList<TradeRow> trade_rows(const QList<TraderTrade>& trades, const std::optional<TraderState>& state);

/// The modes the days hold, the latest day's first.
QStringList day_modes(const QList<TraderDay>& days);
/// The mode the curve shows first: the trader's now if it has days, else the latest day's.
QString default_mode(const QList<TraderDay>& days, const std::optional<TraderState>& state);
/// A day of the curve: its net P&L and the running sum from the first day shown, with its tooltip.
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

/// "1,000,000원"; "+1,922원", "-162원", "0원"; "61.0%".
QString won_text(double won);
QString signed_won_text(double won);
QString percent_text(double share, int decimals = 1);
/// The cell's kind: "방향 4분", "장벽 4분", "장벽 15분", "장벽 30분"; an unknown one as it is.
QString kind_label(const QString& kind);

} // namespace fincept::krx_quant::trader
