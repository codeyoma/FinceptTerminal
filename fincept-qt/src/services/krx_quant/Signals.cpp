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

namespace {

/// The move a consensus is sure enough of under ``rule``, and how sure; none if neither, or
/// if the engine says not to act on it (a VI or breaker, no reference price, too late), as
/// for its Telegram signals.
std::optional<std::pair<Direction, double>> sure_move(const Prediction& consensus, const SignalRule& rule) {
    if (consensus.status != "ok" || !consensus.p_up || !consensus.p_down || !consensus.blocked.isEmpty())
        return std::nullopt;
    if (*consensus.p_up >= rule.min_probability)
        return std::pair{Direction::Up, *consensus.p_up};
    if (*consensus.p_down >= rule.min_probability)
        return std::pair{Direction::Down, *consensus.p_down};
    return std::nullopt;
}

std::optional<Signal> signal_if(const Prediction& consensus, std::pair<Direction, double> move, int agree, int members,
                                const SignalRule& rule) {
    if (agree < rule.min_agree)
        return std::nullopt;
    return Signal{consensus.symbol, consensus.bar_close, consensus.target_at, move.first, move.second, agree, members};
}

} // namespace

std::optional<Signal> judge(const Prediction& consensus, const QList<Prediction>& pooled, const SignalRule& rule) {
    const auto move = sure_move(consensus, rule);
    if (!move)
        return std::nullopt;
    int agree = 0;
    for (const auto& member : pooled)
        agree += most_likely(member) == move->first;
    return signal_if(consensus, *move, agree, static_cast<int>(pooled.size()), rule);
}

std::optional<Signal> judge(const Prediction& consensus, const Votes& votes, const SignalRule& rule) {
    const auto move = sure_move(consensus, rule);
    if (!move)
        return std::nullopt;
    return signal_if(consensus, *move, votes.of(move->first), votes.total(), rule);
}

std::optional<Signal> Alerts::consider(const Prediction& consensus, const QList<Prediction>& pooled,
                                       const QDateTime& now) {
    // A final consensus pools at least one member: none means they are not known yet (read
    // again after a failure). It is judged once they are, not now.
    if (pooled.isEmpty())
        return std::nullopt;
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
