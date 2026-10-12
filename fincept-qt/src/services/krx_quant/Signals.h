#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>

#include <optional>

/// The KRX screen's signal alerts: the engine's Telegram signal rule (krx-quant
/// docs/engine-api.md, "텔레그램 신호 알림"), with the threshold and N set on the screen.
/// Qt Core only, unit-tested on its own (tests/tst_krx_signals.cpp).
namespace fincept::krx_quant {

struct SignalRule {
    double min_probability = 0.6; ///< of the move, in the final consensus
    int min_agree = 2;            ///< active algorithms predicting the move
};

/// A final consensus sure enough of a move that enough of its members agree.
struct Signal {
    QString symbol;
    QDateTime bar_close;
    QDateTime target_at;
    Direction direction = Direction::None; ///< Up or Down
    double probability = 0;
    int agree = 0;
    int members = 0; ///< the active algorithms it pooled

    /// As the engine's Telegram signal reads, e.g. "005930 상승 64% · 정식 3개 중 2개 같은
    /// 방향 · 09:31 봉 마감, 09:35까지".
    QString text() const;
};

/// The signal of a final consensus, given what it pooled (the active members' forecasts):
/// up if its p_up is at least the threshold, else down if its p_down is; then at least N
/// of the members must predict that move (their likeliest class, a tie going to flat).
/// None for a consensus the engine blocked (``Prediction::blocked``).
std::optional<Signal> judge(const Prediction& consensus, const QList<Prediction>& pooled, const SignalRule& rule);

/// Judges each final consensus once and says which to raise.
class Alerts {
  public:
    explicit Alerts(SignalRule rule = {}) : rule_(rule) {}

    void set_rule(const SignalRule& rule) { rule_ = rule; }
    const SignalRule& rule() const { return rule_; }

    /// A final consensus seen at ``now`` (the engine clock) and what it pooled: the signal
    /// to raise, if any. Each bar close is judged once (the answers and the stream both
    /// bring it), not before its members are known, none whose 4 minutes are over, and a
    /// move already raised for the symbol is not raised again before that signal's target
    /// time.
    std::optional<Signal> consider(const Prediction& consensus, const QList<Prediction>& pooled, const QDateTime& now);

  private:
    SignalRule rule_;
    QSet<QPair<QString, qint64>> judged_;          ///< (symbol, bar close) already judged
    QMap<QPair<QString, int>, QDateTime> running_; ///< (symbol, direction) -> until when it holds
};

} // namespace fincept::krx_quant
