#pragma once

#include "services/krx_quant/EngineData.h"
#include "services/krx_quant/Signals.h"
#include "services/krx_quant/StatsView.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

/// What the dashboard's KRX widgets show (codeyoma/krx-quant#302), as text, from the engine's
/// answers. The widgets only draw it.
///
/// Qt Core only (no network, no widgets), unit-tested on its own (tests/tst_krx_dashboard_view.cpp).
namespace fincept::krx_quant::dashboard {

/// The predictors the leaderboard widget lists at most, before its baseline line.
inline constexpr int kLeaderboardTop = 8;

/// A line of the leaderboard widget: what the user reads first (the hit rate, then the weakest
/// lift by trading day the statuses go by), the research P&L last.
struct LeaderRow {
    QString algorithm; ///< the predictor's name, as the engine gives it
    QString rank;      ///< "1", …; "—" unranked or a baseline
    QString name;      ///< predictor_label
    QString status;    ///< standing_label; empty for the consensus and the baselines
    Shown accuracy;    ///< the three-class hit rate, its 95% interval in the tooltip
    QString day_low;   ///< the weakest lift's lower bound by trading day ("+0.010"); "—" without one
    QString n;         ///< scored predictions, thousands grouped
    QString pnl;       ///< the mean P&L a trade in pnl_unit ("+0.13"); "—" without a trade or for a kind without P&L
    QString pnl_inverse; ///< likewise, the down calls through the single-stock inverse product (krx-quant #267)
    bool baseline = false;
};

/// The ranked predictors, then the unranked ones (too few scored yet), ``top`` at most; then the
/// baseline to beat: "늘 보합" (baseline-flat) of the direction, else each of the kind's baselines.
QList<LeaderRow> leaderboard_rows(const Leaderboard& board, int top = kLeaderboardTop);

/// The unit of the leaderboard's P&L columns for ``kind``: "틱" for the direction and the barriers,
/// "bp" for open_to_close; empty for a kind without P&L (the range).
QString pnl_unit(const QString& kind);

/// A line of the signals widget: a symbol's latest final consensus, and whether it is a signal by
/// the KRX screen's rule (the same judge, the same settings), with the latest one scored.
struct SignalRow {
    QString symbol;
    QString name;     ///< the company's name; empty when the engine does not know it
    QString logo_url; ///< empty from an older engine
    QString price;    ///< the reference price now ("81,250"); "—" without one
    QString bar_close; ///< "09:31"; "—" without a final consensus
    QString until;     ///< when it is scored, "09:35"; "—" likewise
    QString up;        ///< "64%"; "—" without a forecast
    QString flat;
    QString down;
    Direction call = Direction::None; ///< the likeliest class; None without a forecast
    QString members;                  ///< "정식 3개 중 2개 상승": its members leaning its way; "—" when not counted
    /// "상승 신호" or "하락 신호"; why there is none to act on ("막힘: 특수 상황", "합의 대상 없음");
    /// empty when the consensus is not sure enough.
    QString signal;
    Direction signal_direction = Direction::None; ///< Up or Down for a signal, else None
    bool live = false;                            ///< a signal whose four minutes are not over
    QString last;                                 ///< the latest scored final consensus: "09:27 하락 ✓"; "—" none yet
    std::optional<bool> last_hit;                 ///< whether it was right; none when it could not be scored
};

/// A line of the status widget.
struct StatusLine {
    QString label;
    QString value;
    bool trouble = false; ///< drawn as a warning
};

/// What the status widget shows: the first trouble on top (empty when all is well), then a line each
/// for the market phase, the latest bar close, the predictions waiting to be scored, KIS, today's
/// special situations, last night's training, the CPU budget and the engine's code.
struct StatusView {
    QString headline;
    QList<StatusLine> lines;
};
StatusView status_view(const EngineStatus& status);

/// A line of the open-to-close widget (krx-quant #264): a symbol's latest call of the day, the
/// mean of its algorithms (the engine makes no consensus of a daily kind), and the day's answer
/// once in after the close.
struct DailyRow {
    QString symbol;
    QString name;
    QString logo_url;
    QString day;  ///< when it was predicted, "10-19 08:50"; " (지난 거래일)" when it is not ``today``'s; "예측 없음"
    QString up;   ///< "40%"; "—" without a call
    QString flat;
    QString down;
    Direction call = Direction::None; ///< the likeliest class; None without a call
    QString basis;                    ///< "알고리즘 2개 평균"; "—"
    QString band;                     ///< "보합 ±0.47%": how far from the open is still flat; "—"
    QString answer;                   ///< what happened (daily_answer_text); "—" without a call
    std::optional<bool> hit;          ///< whether the call was right; none before the answer or when unscorable
};

/// One row per symbol of ``symbols``, from each symbol's latest predictions of ``kind`` (an
/// open_to_close kind) and ``today`` (the engine clock's date in KST).
QList<DailyRow> open_to_close_rows(const QList<SymbolQuote>& symbols, const QMap<QString, KindLatest>& latest,
                                   const KindInfo& kind, const QDate& today);

/// Why the engine says a final consensus is not to be acted on (``Prediction::blocked``, krx-quant
/// #88), in Korean: "특수 상황", "기준가 없음", "시간 지남"; another reason as it is.
QString blocked_label(const QString& reason);

/// One row per symbol of ``symbols`` (the engine's order), from ``latest`` (each symbol's latest
/// bar close), ``recent`` (each symbol's final consensus over the latest bar closes, any order) and
/// ``rule`` (the KRX screen's). ``now``: the engine clock, past a signal's target time it is no
/// longer live.
QList<SignalRow> signal_rows(const QList<SymbolQuote>& symbols, const QMap<QString, Latest>& latest,
                             const QMap<QString, QList<Prediction>>& recent, const SignalRule& rule,
                             const QDateTime& now);

} // namespace fincept::krx_quant::dashboard
