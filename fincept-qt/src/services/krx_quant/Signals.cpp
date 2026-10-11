#include "services/krx_quant/Signals.h"

#include <QTimeZone>

#include <cmath>

namespace fincept::krx_quant {

QString Signal::text() const {
    static const QTimeZone kst("Asia/Seoul");
    return QStringLiteral("%1 %2 %3% · 정식 %4개 중 %5개 같은 방향 · %6 봉 마감, %7까지")
        .arg(symbol, direction_label(direction))
        .arg(std::lround(probability * 100))
        .arg(members)
        .arg(agree)
        .arg(bar_close.toTimeZone(kst).toString("HH:mm"), target_at.toTimeZone(kst).toString("HH:mm"));
}

std::optional<Signal> judge(const Prediction& consensus, const QList<Prediction>& pooled, const SignalRule& rule) {
    if (consensus.status != "ok" || !consensus.p_up || !consensus.p_down)
        return std::nullopt;
    Direction direction = Direction::None;
    double probability = 0;
    if (*consensus.p_up >= rule.min_probability) {
        direction = Direction::Up;
        probability = *consensus.p_up;
    } else if (*consensus.p_down >= rule.min_probability) {
        direction = Direction::Down;
        probability = *consensus.p_down;
    } else {
        return std::nullopt;
    }
    int agree = 0;
    for (const auto& member : pooled)
        agree += most_likely(member) == direction;
    if (agree < rule.min_agree)
        return std::nullopt;
    return Signal{consensus.symbol,
                  consensus.bar_close,
                  consensus.target_at,
                  direction,
                  probability,
                  agree,
                  static_cast<int>(pooled.size())};
}

std::optional<Signal> Alerts::consider(const Prediction& consensus, const QList<Prediction>& pooled,
                                       const QDateTime& now) {
    const QPair<QString, qint64> bar{consensus.symbol, consensus.bar_close.toMSecsSinceEpoch()};
    if (judged_.contains(bar))
        return std::nullopt;
    judged_.insert(bar);
    if (!consensus.target_at.isValid() || now >= consensus.target_at)
        return std::nullopt; // its 4 minutes are over: nothing to act on
    auto signal = judge(consensus, pooled, rule_);
    if (!signal)
        return std::nullopt;
    const QPair<QString, int> move{signal->symbol, static_cast<int>(signal->direction)};
    if (running_.contains(move) && signal->bar_close < running_.value(move))
        return std::nullopt;
    running_.insert(move, signal->target_at);
    return signal;
}

} // namespace fincept::krx_quant
