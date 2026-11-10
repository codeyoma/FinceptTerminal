#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

/// What the KRX "할 일" screen shows (codeyoma/krx-quant#674, spec #671), as text, from the engine's
/// GET /v1/action answer: a card a symbol, its word (wait, buy, sell), its line, the trade's plan, and the
/// cell's record, in plain Korean without the engine's terms. The screen only draws it.
///
/// Qt Core only (no network, no widgets), unit-tested on its own (tests/tst_krx_action.cpp).
namespace fincept::krx_quant::action {

/// How often the screen reads the cards again: every kFastMs while a card buys, sells or holds, every
/// kSessionMs from 08:55 to 15:35 KST on a weekday (a buy is shown for a minute), else every kSlowMs.
inline constexpr int kFastMs = 1'000;
inline constexpr int kSessionMs = 2'000;
inline constexpr int kSlowMs = 60'000;

int poll_ms(const std::optional<ActionBoard>& last, const QDateTime& now);

/// A card as the screen draws it.
struct CardText {
    QString word;        ///< "wait", "buy", "sell"
    QString tone;        ///< "buy" (positive), "sell" (negative), "wait" (dim)
    QString title;       ///< the stock: "삼성전자 005930"
    QString line;        ///< the engine's line of Korean
    QStringList plan;    ///< the trade's prices and times, a line each; empty without a trade
    QString record;      ///< the cell's record in a line; empty without one
    QString record_tip;  ///< what the record is, in plain words
};
CardText card_text(const ActionCard& card, const QDateTime& now);

/// The plan's lines: "이익 실현 70,700원", "손절 69,300원", "늦어도 10:14에 매도 (3분 남음)", and while held
/// "지금 70,200원 · 산 값보다 +100원 (1주, 비용 전)"; after a sale "판 값 70,700원 · 산 값 70,100원".
QStringList plan_lines(const ActionCard& card, const QDateTime& now);
/// "지난 20일 이 신호대로 943번 샀다면 512번 이익 · 비용 빼고 1주당 평균 +31원 (나쁘게 봐도 +15원)".
QString record_line(const ActionRecord& record);

/// The header's line: the trader's mode, said plainly ("자동 매매: KIS 모의 계좌 · 진짜 돈이 아닙니다").
QString mode_line(const ActionBoard& board);

/// The alerts to raise for a new answer: a card that turned buy or sell since ``seen`` (a symbol's last
/// alert key), once each a trade's buy and its sale. ``seen`` is updated with every card's key. The first answer (``seen`` empty)
/// raises none: the screen opened on a buy is not a new buy.
QStringList alerts(const ActionBoard& board, QMap<QString, QString>& seen, bool first);

} // namespace fincept::krx_quant::action
