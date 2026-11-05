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
/// docs/engine-api.md, "텔레그램 신호 알림"), with the engine's threshold and N unless the user set
/// their own on the screen (``SignalRule`` is in EngineData.h).
/// Qt Core only, unit-tested on its own (tests/tst_krx_signals.cpp).
namespace fincept::krx_quant {

/// The signal rule values the user saved on the KRX screen (codeyoma/krx-quant#617); none where
/// they follow the engine's.
struct SavedRule {
    std::optional<double> min_probability;
    std::optional<int> min_agree;
    bool any() const { return min_probability || min_agree; }
};

/// The rule in force (codeyoma/krx-quant#617): each value the user saved, else the engine's
/// (``EngineStatus::signal_rule``), else the app's own (``SignalRule{}``) for an engine that does
/// not serve its rule.
SignalRule rule_in_force(const SavedRule& saved, const std::optional<SignalRule>& engine);
/// Where it comes from, as the KRX screen's alert settings say: "엔진 기준", "직접 설정(확률) ·
/// 엔진 기준 70% · 2개", or "기본값 (엔진이 기준을 주지 않음)".
QString rule_source(const SavedRule& saved, const std::optional<SignalRule>& engine);

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
/// The same, from how its members leaned as the engine counted them (``Prediction::votes``,
/// krx-quant #106): for a past consensus, whose members' forecasts are not at hand.
std::optional<Signal> judge(const Prediction& consensus, const Votes& votes, const SignalRule& rule);

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
