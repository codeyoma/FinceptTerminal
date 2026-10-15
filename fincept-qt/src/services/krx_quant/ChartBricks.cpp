#include "services/krx_quant/ChartBricks.h"

#include "services/krx_quant/ChartStudies.h"

#include <algorithm>
#include <cmath>

namespace fincept::krx_quant {

namespace {

/// A brick from ``open`` to ``close``.
Brick brick(double open, double close) {
    Brick b;
    b.open = open;
    b.close = close;
    b.high = std::max(open, close);
    b.low = std::min(open, close);
    b.rising = close > open;
    return b;
}

/// The box levels at or below and at or above ``price``: multiples of ``box``. A price a hair under
/// a level (as a division leaves it) is on it.
double level_below(double price, double box) {
    return std::floor(price / box + 1e-9) * box;
}

double level_above(double price, double box) {
    return std::ceil(price / box - 1e-9) * box;
}

} // namespace

std::optional<double> atr_size(const QList<ChartBar>& bars) {
    const Values atr = average_true_range(bars, kBrickAtr);
    if (atr.isEmpty() || !atr.last())
        return std::nullopt;
    return std::max(1.0, std::round(*atr.last()));
}

std::optional<double> brick_size(ChartStyle style, const BrickSizes& sizes, std::optional<double> atr) {
    if (!timeless(style))
        return std::nullopt;
    if (const auto it = sizes.constFind(style); it != sizes.cend() && valid_brick_size(style, it.value()))
        return it.value();
    if (style == ChartStyle::LineBreak)
        return kBreakLines;
    return atr;
}

QList<Brick> renko_bricks(const QList<ChartBar>& bars, double box) {
    QList<Brick> out;
    if (bars.isEmpty() || !(box > 0))
        return out;
    double top = bars.first().close; // the last brick's top and bottom; the start before any
    double bottom = top;
    double held = 0; // the volumes of the bars since the last brick
    for (const ChartBar& bar : bars) {
        held += bar.volume;
        const qsizetype before = out.size();
        // A rising brick from the top, or a falling one from the bottom: a reversal is two boxes
        // from the last brick's end, as its other side is a box away.
        while (true) {
            if (bar.close >= top + box) {
                out << brick(top, top + box);
                bottom = top;
                top += box;
            } else if (bar.close <= bottom - box) {
                out << brick(bottom, bottom - box);
                top = bottom;
                bottom -= box;
            } else {
                break;
            }
        }
        const qsizetype made = out.size() - before;
        for (qsizetype i = before; i < out.size(); ++i) {
            out[i].time = bar.time;
            out[i].volume = held / static_cast<double>(made);
        }
        if (made > 0)
            held = 0;
    }
    return out;
}

QList<Brick> kagi_lines(const QList<ChartBar>& bars, double reversal) {
    QList<Brick> out;
    if (bars.isEmpty() || !(reversal > 0))
        return out;
    const double start = bars.first().close;
    Brick line = brick(start, start); // the line being drawn
    int way = 0;                      // 1 rising, -1 falling; 0 before the first
    const auto hold = [&line](const ChartBar& bar) {
        line.volume += bar.volume;
        line.time = bar.time;
    };
    for (const ChartBar& bar : bars) {
        const double c = bar.close;
        if (way == 0) {
            hold(bar);
            if (c >= start + reversal || c <= start - reversal) { // the first line, with the bars so far
                way = c > start ? 1 : -1;
                const Brick held = line;
                line = brick(start, c);
                line.volume = held.volume;
                line.time = held.time;
            }
            continue;
        }
        const bool on = way > 0 ? c > line.close : c < line.close;
        const bool back = way > 0 ? c <= line.close - reversal : c >= line.close + reversal;
        if (on) {
            const Brick held = line;
            line = brick(held.open, c);
            line.volume = held.volume;
            line.time = held.time;
            hold(bar);
        } else if (back) {
            out << line;
            line = brick(line.close, c);
            way = -way;
            hold(bar);
        } else {
            hold(bar);
        }
    }
    if (way != 0)
        out << line;
    // Thick (yang) and thin (yin): the first line as its way; a rising line past the shoulder before
    // turns thick there, a falling one past the waist before turns thin.
    bool thick = !out.isEmpty() && out.first().rising;
    std::optional<double> shoulder;
    std::optional<double> waist;
    for (Brick& b : out) {
        b.thick = thick;
        if (b.rising && !thick && shoulder && b.close > *shoulder) {
            b.turn = *shoulder;
            thick = true;
        } else if (!b.rising && thick && waist && b.close < *waist) {
            b.turn = *waist;
            thick = false;
        }
        (b.rising ? shoulder : waist) = b.close;
    }
    return out;
}

QList<Brick> figure_columns(const QList<ChartBar>& bars, double box, int reversal) {
    QList<Brick> out;
    if (bars.isEmpty() || !(box > 0) || reversal < 1)
        return out;
    const double start = bars.first().close;
    Brick column; // the column being drawn
    int way = 0;  // 1 X, -1 O; 0 before the first
    const auto hold = [&column](const ChartBar& bar) {
        column.volume += bar.volume;
        column.time = bar.time;
    };
    const auto set = [&column](double first, double last) {
        const Brick shape = brick(first, last);
        column.open = shape.open;
        column.close = shape.close;
        column.high = shape.high;
        column.low = shape.low;
        column.rising = shape.rising;
    };
    for (const ChartBar& bar : bars) {
        const double c = bar.close;
        const double up = level_below(c, box);   // the highest box an X column reaches
        const double down = level_above(c, box); // the lowest an O column reaches
        if (way == 0) {
            hold(bar);
            if (up >= level_below(start, box) + box) {
                way = 1;
                set(level_below(start, box), up);
            } else if (down <= level_above(start, box) - box) {
                way = -1;
                set(level_above(start, box), down);
            }
            continue;
        }
        if (way > 0 && up > column.close) {
            set(column.open, up);
            hold(bar);
        } else if (way < 0 && down < column.close) {
            set(column.open, down);
            hold(bar);
        } else if (way > 0 && down <= column.close - reversal * box) {
            out << column;
            column = Brick{};
            set(out.last().close - box, down);
            way = -1;
            hold(bar);
        } else if (way < 0 && up >= column.close + reversal * box) {
            out << column;
            column = Brick{};
            set(out.last().close + box, up);
            way = 1;
            hold(bar);
        } else {
            hold(bar);
        }
    }
    if (way != 0)
        out << column;
    return out;
}

QList<Brick> break_lines(const QList<ChartBar>& bars, int lines) {
    QList<Brick> out;
    if (bars.isEmpty() || lines < 1)
        return out;
    const double start = bars.first().close;
    double held = 0;
    for (const ChartBar& bar : bars) {
        held += bar.volume;
        const double c = bar.close;
        std::optional<Brick> line;
        if (out.isEmpty()) {
            if (c != start)
                line = brick(start, c);
        } else {
            const Brick& last = out.last();
            // The extremes of the last ``lines`` lines (all of them while fewer): past them it turns.
            double low = last.low;
            double high = last.high;
            for (qsizetype i = std::max<qsizetype>(0, out.size() - lines); i < out.size(); ++i) {
                low = std::min(low, out[i].low);
                high = std::max(high, out[i].high);
            }
            if (last.rising ? c > last.close : c < last.close)
                line = brick(last.close, c);
            else if (last.rising ? c < low : c > high)
                line = brick(last.open, c);
        }
        if (line) {
            line->volume = held;
            line->time = bar.time;
            out << *line;
            held = 0;
        }
    }
    return out;
}

QList<Brick> make_bricks(ChartStyle style, const QList<ChartBar>& bars, double size) {
    switch (style) {
        case ChartStyle::Renko:
            return renko_bricks(bars, size);
        case ChartStyle::Kagi:
            return kagi_lines(bars, size);
        case ChartStyle::PointFigure:
            return figure_columns(bars, size);
        case ChartStyle::LineBreak:
            return break_lines(bars, static_cast<int>(size));
        default:
            return {};
    }
}

std::optional<qint64> brick_start(const QList<ChartBar>& bars, std::optional<qint64> kept, bool start_whole) {
    if (bars.isEmpty())
        return std::nullopt;
    // Reading the bars before only puts bars in front: the start stays while its bar is held.
    if (kept && bars.first().time <= *kept)
        return kept;
    if (bars.size() >= kBrickBars || start_whole || bars.size() >= kMaxHistoryBars)
        return bars.first().time;
    return std::nullopt;
}

QList<ChartBar> bars_from(const QList<ChartBar>& bars, qint64 start) {
    QList<ChartBar> out;
    for (const ChartBar& bar : bars)
        if (bar.time >= start)
            out << bar;
    return out;
}

qint64 brick_slot(qsizetype index) {
    return (static_cast<qint64>(index) + 1) * 60;
}

QList<ChartBar> brick_bars(const QList<Brick>& bricks) {
    QList<ChartBar> out;
    out.reserve(bricks.size());
    for (qsizetype i = 0; i < bricks.size(); ++i) {
        const Brick& b = bricks[i];
        out << ChartBar{brick_slot(i), b.open, b.high, b.low, b.close, b.volume, b.rising};
    }
    return out;
}

} // namespace fincept::krx_quant
