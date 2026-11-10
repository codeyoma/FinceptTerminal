#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QString>

/// What the KRX verdict screen (종합 판단, codeyoma/krx-quant#678) shows, from the engine's GET
/// /v1/verdict answer: each symbol's card (사세요 / 파세요 / 기다리세요, its strength, the maximum
/// holding time with the target and stop, the expected net per horizon), the members' table and the
/// forecasts beside it. The screen only draws them.
///
/// Qt Core only (no network, no widgets), unit-tested on its own (tests/tst_krx_verdict.cpp).
namespace fincept::krx_quant::judgment {

/// How often the screen reads the verdicts again: every kFastMs on a weekday from 08:59 to 15:40 KST
/// (a verdict a bar close, a minute apart), else every kSlowMs.
inline constexpr int kFastMs = 15'000;
inline constexpr int kSlowMs = 300'000;
int poll_ms(const QDateTime& now);

/// What a verdict says, for its color.
enum class Call { Buy, Sell, Wait };
Call call(const SymbolVerdict& v);
/// The card's large word: "사세요", "파세요", "기다리세요".
QString word(const SymbolVerdict& v);
/// A strength in Korean: "강함", "보통", "약함"; empty for "none" or none.
QString strength_text(const QString& strength);
/// The line under the word: a buy's or sell's strength and margin ("강도 강함 · 마진 +0.31"), a sell's
/// meaning, a wait's reason ("참여 알고리즘 없음 — 장벽 알고리즘 검증 대기").
QString reason_text(const SymbolVerdict& v);
/// A buy's "최대 보유 30분 · 11:00까지", an exit's "보유 기한 11:00"; empty otherwise.
QString hold_text(const SymbolVerdict& v);
/// A buy's "목표 218,500 · 손절 199,500"; empty without them.
QString levels_text(const SymbolVerdict& v);
/// The card's title: "SK하이닉스 000660"; the code alone without a name.
QString card_title(const QString& symbol, const QList<SymbolQuote>& symbols);
/// The card's foot: "봉 마감 10:30 · 보유 중".
QString as_of_text(const SymbolVerdict& v);

/// A barrier horizon's kind as the screen names it: "장벽 4분", "장벽 15분"; ``minutes`` 0 reads them off
/// the kind ("barrier_15m"; "barrier" is 4).
QString kind_text(const QString& kind, int minutes = 0);

/// A horizon's line on the card.
struct HorizonLine {
    QString horizon; ///< "15분"
    QString odds;    ///< "위 66% · 아래 8%"; "—" without members
    QString net;     ///< "+95원 (0.9틱)"; "—" without one
    bool chosen = false; ///< the horizon the verdict rests on
    bool positive = false;
    bool negative = false;
    QString tip; ///< the width, families and members, target and stop
};
QList<HorizonLine> horizon_lines(const SymbolVerdict& v);

/// A member's status: "정식", "검증 중", the others as the leaderboard names them; "기록 없음" without one.
QString status_text(const QString& status);
/// A lean: "위 · 강함", "아래 · 약함", "기울지 않음".
QString lean_text(const QString& lean, const QString& strength);
/// A share as a whole percent: "66%".
QString percent_text(double share);

/// A member's row in the members' table, in its columns' order.
struct MemberRow {
    QString name;
    QString kind;
    QString family;
    QString status;
    QString evidence;
    QString up;
    QString timeout;
    QString down;
    QString lean;
    QString weight;
    bool counted = true;
    bool active = false;
    QString tip;
};
QList<MemberRow> member_rows(const SymbolVerdict& v);
/// What the members' table says when it is empty.
QString no_members_text(const SymbolVerdict& v);

/// A forecast beside the verdict: its name, kind, status and output.
struct SideRow {
    QString name;
    QString kind;   ///< "방향 4분", "변동폭"
    QString status; ///< empty for the consensus
    QString output; ///< "상승 62% · 보합 14% · 하락 24% · 기대 +0.3틱"
};
QList<SideRow> side_rows(const SymbolVerdict& v);

/// The verdict of ``symbol`` in ``verdicts``; else the first; none when there is none.
std::optional<SymbolVerdict> chosen(const Verdicts& verdicts, const QString& symbol);
/// What the screen says without verdicts: why the engine gives none.
QString empty_text();
/// An engine before #676 answers 404: say so instead of the bare failure.
QString failure_text(const QString& failure);

} // namespace fincept::krx_quant::judgment
