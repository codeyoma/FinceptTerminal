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

void Board::set_history(const QString& symbol, const QList<Prediction>& predictions) {
    for (Prediction p : predictions) {
        p.symbol = symbol;
        put(p);
    }
}

void Board::focus(const QString& symbol) {
    if (symbol == focus_)
        return;
    focus_ = symbol;
    closes_.clear();
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
        case Event::Type::Bar:
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
    closes_.clear();
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
    for (auto it = closes_.begin(); it != closes_.end();) {
        const bool later = it.key() > now;
        dropped |= later;
        it = later ? closes_.erase(it) : std::next(it);
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
    if (!focus_.isEmpty() && p.symbol == focus_ && p.bar_close.isValid()) {
        auto& at_close = closes_[p.bar_close];
        const auto shown = at_close.constFind(p.algorithm);
        if (shown == at_close.cend() || replaces(*shown, p))
            at_close.insert(p.algorithm, p);
    }
}

Latest Board::latest(const QString& symbol) const {
    return latest_.value(symbol, Latest{symbol, {}, {}});
}

QMap<QDateTime, QList<Prediction>> Board::closes(const QString& symbol) const {
    QMap<QDateTime, QList<Prediction>> out;
    if (!focus_.isEmpty() && symbol == focus_) {
        for (auto it = closes_.cbegin(); it != closes_.cend(); ++it)
            out.insert(it.key(), it->values());
    }
    // The final consensus of another symbol, or one read before it was chosen.
    const auto finals = finals_.value(symbol);
    for (auto it = finals.cbegin(); it != finals.cend(); ++it) {
        QList<Prediction>& at_close = out[it.key()];
        const bool there = std::any_of(at_close.cbegin(), at_close.cend(),
                                       [](const Prediction& p) { return p.algorithm == QLatin1String(kFinal); });
        if (!there)
            at_close << *it;
    }
    return out;
}

QList<Mark> Board::marks(const QString& symbol, const QStringList& selection) const {
    QList<Mark> out;
    if (selection.isEmpty()) {
        const auto all = closes(symbol);
        for (const auto& p : finals_.value(symbol)) {
            const Direction predicted = most_likely(p);
            if (p.status != "ok" || predicted == Direction::None)
                continue;
            const Truth truth = truth_of(all.value(p.bar_close, {p}), p.bar_close);
            Mark mark{p.bar_close,
                      predicted,
                      probability_of(p, predicted),
                      verdict(predicted, truth),
                      truth.special,
                      truth.unscorable,
                      p,
                      truth};
            out << mark;
        }
        return out;
    }
    if (focus_.isEmpty() || symbol != focus_)
        return out;
    const QStringList baselines = baselines_of(selection);
    if (baselines.size() == selection.size()) {
        if (baselines.size() != 1)
            return out; // baselines alone are compared in the hover, not drawn
        // One baseline alone: its own forecasts, as it made them.
        for (auto it = closes_.cbegin(); it != closes_.cend(); ++it) {
            const auto made = it->constFind(baselines.first());
            if (made == it->cend() || made->status != "ok")
                continue;
            const Direction predicted = most_likely(*made);
            if (predicted == Direction::None)
                continue;
            const Truth truth = truth_of(it->values(), it.key());
            Mark mark{it.key(),
                      predicted,
                      probability_of(*made, predicted),
                      verdict(predicted, truth),
                      truth.special,
                      truth.unscorable,
                      *made,
                      truth,
                      true,
                      1,
                      1};
            out << mark;
        }
        return out;
    }
    for (auto it = closes_.cbegin(); it != closes_.cend(); ++it) {
        const QList<Prediction> at_close = it->values();
        const auto pooled = select_consensus(at_close, selection);
        if (!pooled)
            continue;
        const Truth truth = truth_of(at_close, it.key());
        Prediction consensus = pooled->consensus;
        consensus.symbol = symbol;
        consensus.target_at = truth.target_at;
        const Direction predicted = most_likely(consensus);
        Mark mark{it.key(),
                  predicted,
                  probability_of(consensus, predicted),
                  verdict(predicted, truth),
                  truth.special,
                  truth.unscorable,
                  consensus,
                  truth,
                  true,
                  pooled->made,
                  pooled->chosen};
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

Truth truth_of(const QList<Prediction>& at_close, const QDateTime& bar_close) {
    Truth truth;
    truth.target_at = bar_close.addSecs(240);
    for (const auto& p : at_close) {
        if (p.status != "ok")
            continue;
        if (p.target_at.isValid())
            truth.target_at = p.target_at;
        if (!p.outcome)
            continue;
        // Every predictor of a bar close is scored on the same mid-points: any one says it.
        const Outcome& o = *p.outcome;
        truth.scored = true;
        truth.happened = o.class_a;
        truth.special = o.special;
        truth.unscorable = o.class_a == Direction::None ? o.unscorable : QString();
        truth.mid_start = o.mid_close;
        truth.mid_target = o.mid_target;
        truth.touch = o.touch;
        break;
    }
    return truth;
}

Mark::Result verdict(Direction predicted, const Truth& truth) {
    if (!truth.scored)
        return Mark::Result::Pending;
    if (truth.happened == Direction::None)
        return Mark::Result::Unscorable;
    return truth.happened == predicted ? Mark::Result::Hit : Mark::Result::Miss;
}

QStringList baselines_of(const QStringList& selection) {
    QStringList out;
    for (const auto& name : selection)
        if (is_baseline(name))
            out << name;
    return out;
}

std::optional<Pooled> select_consensus(const QList<Prediction>& at_close, const QStringList& selection) {
    QList<Prediction> made;
    QString blocked;
    for (const auto& p : at_close) {
        if (p.algorithm == QLatin1String(kFinal))
            blocked = p.blocked;
        if (selection.contains(p.algorithm) && !is_baseline(p.algorithm) && p.status == "ok" && p.p_up && p.p_flat &&
            p.p_down)
            made << p;
    }
    if (made.isEmpty())
        return std::nullopt;
    // Summed in name order, so that the same forecasts always give the same mean, ties included.
    std::sort(made.begin(), made.end(),
              [](const Prediction& a, const Prediction& b) { return a.algorithm < b.algorithm; });
    double up = 0, flat = 0, down = 0;
    Votes votes;
    for (const auto& p : made) {
        up += *p.p_up;
        flat += *p.p_flat;
        down += *p.p_down;
        switch (most_likely(p)) {
            case Direction::Up:
                ++votes.up;
                break;
            case Direction::Flat:
                ++votes.flat;
                break;
            case Direction::Down:
                ++votes.down;
                break;
            case Direction::None:
                break;
        }
    }
    const double n = static_cast<double>(made.size());
    Pooled pooled;
    Prediction& c = pooled.consensus;
    c.algorithm = kSelected;
    c.status = "ok";
    c.bar_close = made.first().bar_close;
    c.p_up = up / n;
    c.p_flat = flat / n;
    c.p_down = down / n;
    c.votes = votes;
    c.blocked = blocked;
    pooled.made = static_cast<int>(made.size());
    pooled.chosen = static_cast<int>(selection.size() - baselines_of(selection).size());
    return pooled;
}

QDateTime scoring_at(const Latest& latest) {
    for (const auto& p : latest.predictions)
        if (p.target_at.isValid())
            return p.target_at;
    return {};
}

} // namespace fincept::krx_quant
