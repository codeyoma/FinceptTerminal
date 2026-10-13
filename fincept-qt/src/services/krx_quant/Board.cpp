#include "services/krx_quant/Board.h"

#include <algorithm>

namespace fincept::krx_quant {

namespace {

constexpr auto kFinal = "consensus-final";

/// Whether ``incoming`` should replace ``shown``, the same prediction: not when it would
/// unscore it (an answer read before the stream brought the outcome).
bool replaces(const Prediction& shown, const Prediction& incoming) {
    return incoming.outcome.has_value() || !shown.outcome.has_value();
}

double probability_of(const Prediction& p, Direction d) {
    switch (d) {
        case Direction::Up:
            return p.p_up.value_or(0);
        case Direction::Flat:
            return p.p_flat.value_or(0);
        case Direction::Down:
            return p.p_down.value_or(0);
        case Direction::None:
            break;
    }
    return 0;
}

} // namespace

void Board::set_symbols(const QList<SymbolQuote>& symbols) {
    symbols_ = symbols;
}

void Board::set_members(const QStringList& members) {
    members_ = members;
}

void Board::set_latest(const Latest& latest) {
    for (const auto& p : latest.predictions)
        put(p);
}

void Board::set_history(const QString& symbol, const QList<Prediction>& finals) {
    for (Prediction p : finals) {
        p.symbol = symbol;
        put(p);
    }
}

QList<Prediction> Board::apply(const Event& event) {
    switch (event.type) {
        case Event::Type::Prediction:
        case Event::Type::Consensus:
        case Event::Type::Outcome:
            for (const auto& p : event.predictions)
                put(p);
            break;
        case Event::Type::Lifecycle:
            if (event.change.to == "active") {
                if (!members_.contains(event.algorithm))
                    members_ << event.algorithm;
            } else {
                members_.removeAll(event.algorithm);
            }
            break;
        case Event::Type::Health:
        case Event::Type::Other:
            break;
    }
    if (event.type != Event::Type::Consensus || event.kind != "final")
        return {};
    QList<Prediction> finals;
    for (const auto& p : event.predictions)
        if (p.algorithm == kFinal)
            finals << p;
    return finals;
}

void Board::forget_predictions() {
    symbols_.clear();
    latest_.clear();
    finals_.clear();
}

bool Board::rewind_to(const QDateTime& now) {
    bool dropped = false;
    for (auto it = latest_.begin(); it != latest_.end();) {
        const bool later = it->bar_close > now;
        dropped |= later;
        it = later ? latest_.erase(it) : std::next(it);
    }
    for (auto& finals : finals_) {
        for (auto it = finals.begin(); it != finals.end();) {
            const bool later = it.key() > now;
            dropped |= later;
            it = later ? finals.erase(it) : std::next(it);
        }
    }
    return dropped;
}

void Board::put(const Prediction& p) {
    Latest& latest = latest_[p.symbol];
    if (!latest.bar_close.isValid() || p.bar_close > latest.bar_close) {
        latest = {p.symbol, p.bar_close, {p}};
    } else if (p.bar_close == latest.bar_close) {
        auto shown = std::find_if(latest.predictions.begin(), latest.predictions.end(),
                                  [&p](const Prediction& q) { return q.algorithm == p.algorithm; });
        if (shown == latest.predictions.end()) {
            latest.predictions << p;
            latest.predictions = in_screen_order(latest.predictions);
        } else if (replaces(*shown, p)) {
            *shown = p;
        }
    }
    if (p.algorithm == kFinal) {
        auto& finals = finals_[p.symbol];
        const auto shown = finals.constFind(p.bar_close);
        if (shown == finals.cend() || replaces(*shown, p))
            finals.insert(p.bar_close, p);
    }
}

Latest Board::latest(const QString& symbol) const {
    return latest_.value(symbol, Latest{symbol, {}, {}});
}

QList<Mark> Board::marks(const QString& symbol, const QString& basis) const {
    QList<Mark> out;
    for (const auto& p : finals_.value(symbol)) {
        const Direction predicted = most_likely(p);
        if (p.status != "ok" || predicted == Direction::None)
            continue;
        Mark mark{p.bar_close, predicted, probability_of(p, predicted), Mark::Result::Pending, false, {}, p};
        if (p.outcome) {
            const Direction happened = basis == "a" ? p.outcome->class_a : p.outcome->class_b;
            mark.special = p.outcome->special;
            if (happened == Direction::None) {
                mark.result = Mark::Result::Unscorable;
                mark.unscorable = p.outcome->unscorable;
            } else {
                mark.result = happened == predicted ? Mark::Result::Hit : Mark::Result::Miss;
            }
        }
        out << mark;
    }
    return out;
}

QList<Prediction> Board::pooled(const QString& symbol, const QDateTime& bar_close) const {
    const Latest shown = latest(symbol);
    if (shown.bar_close != bar_close)
        return {};
    QList<Prediction> out;
    for (const auto& p : shown.predictions)
        if (p.status == "ok" && members_.contains(p.algorithm))
            out << p;
    return out;
}

QDateTime scoring_at(const Latest& latest) {
    for (const auto& p : latest.predictions)
        if (p.target_at.isValid())
            return p.target_at;
    return {};
}

} // namespace fincept::krx_quant
