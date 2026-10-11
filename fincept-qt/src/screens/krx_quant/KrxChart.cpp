#include "screens/krx_quant/KrxChart.h"

#include "ui/theme/Theme.h"

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace {

constexpr int kPriceAxis = 72; ///< px on the right for the prices
constexpr int kTimeAxis = 20;  ///< px at the bottom for the times
constexpr int kMargin = 8;

constexpr int kSmallest = 40; ///< px of plot below which nothing is drawn (a splitter squeezed it)

QString hhmm(const QDateTime& t) {
    return krx_quant::kst_text(t, "HH:mm");
}

/// How a mark's result looks: its glyph beside the arrow, its words in the tooltip, its color.
struct Look {
    QString glyph;
    QString words;
    QColor color;
};

Look look_of(const krx_quant::Mark& mark) {
    switch (mark.result) {
        case krx_quant::Mark::Result::Hit:
            return {QStringLiteral("✓"), QStringLiteral("✓ 맞음"), QColor(ui::colors::POSITIVE())};
        case krx_quant::Mark::Result::Miss:
            return {QStringLiteral("✗"), QStringLiteral("✗ 틀림"), QColor(ui::colors::NEGATIVE())};
        case krx_quant::Mark::Result::Unscorable:
            return {QStringLiteral("?"),
                    mark.unscorable.isEmpty() ? QStringLiteral("? 채점 불가")
                                              : QStringLiteral("? 채점 불가 (%1)").arg(mark.unscorable),
                    QColor(ui::colors::TEXT_DIM())};
        case krx_quant::Mark::Result::Pending:
            break;
    }
    return {QString(), QStringLiteral("(채점 전)"), QColor(ui::colors::TEXT_DIM())};
}

/// "09:31 봉 마감 합의(최종) 상승 62% ✓ 맞음" for a hovered bar's mark.
QString mark_text(const krx_quant::Mark& mark) {
    QString text = QStringLiteral("%1 봉 마감 합의(최종) %2 %3% %4")
                       .arg(hhmm(mark.bar_close), krx_quant::direction_label(mark.predicted))
                       .arg(std::lround(mark.probability * 100))
                       .arg(look_of(mark).words);
    if (mark.special)
        text += QStringLiteral(" · 특수 상황(통계 제외)");
    return text;
}

} // namespace

struct KrxChart::Frame {
    QRectF plot;
    qsizetype first = 0; ///< in bars_, the first bar shown
    int count = 0;       ///< bars shown
    double slot = 0;     ///< px per bar
    double glyph = 0;    ///< px of a mark
    double low = 0;
    double high = 0;
    double pad = 0; ///< px kept free above and below the bars for the marks

    double x_of(int i) const { return plot.left() + slot * (i + 0.5); }
    double y_of(double price) const {
        const double span = high > low ? high - low : 1.0;
        return plot.top() + pad + (high - price) / span * (plot.height() - 2 * pad);
    }
};

KrxChart::KrxChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(220);
}

void KrxChart::set_data(const QList<krx_quant::Bar>& bars, const QList<krx_quant::Mark>& marks) {
    bars_ = bars;
    marks_.clear();
    for (const auto& mark : marks)
        marks_.insert(mark.bar_close.addSecs(-60), mark); // the bar that closed then started a minute before
    update();
}

void KrxChart::set_window(int minutes) {
    window_ = minutes;
    update();
}

void KrxChart::set_empty_text(const QString& text) {
    empty_text_ = text;
    update();
}

qsizetype KrxChart::first_shown() const {
    return window_ <= 0 || bars_.size() <= window_ ? 0 : bars_.size() - window_;
}

KrxChart::Frame KrxChart::frame() const {
    Frame f;
    f.plot = QRectF(kMargin, kMargin, width() - kMargin - kPriceAxis, height() - kMargin - kTimeAxis);
    if (f.plot.width() < kSmallest || f.plot.height() < kSmallest)
        return f; // no room: nothing is drawn
    f.first = first_shown();
    f.count = static_cast<int>(bars_.size() - f.first);
    if (f.count == 0)
        return f;
    f.slot = f.plot.width() / f.count;
    f.glyph = std::clamp(f.slot * 0.8, 6.0, 12.0);
    f.pad = 2 * f.glyph + 10;
    f.low = bars_[f.first].low;
    f.high = bars_[f.first].high;
    for (qsizetype i = f.first; i < bars_.size(); ++i) {
        f.low = std::min(f.low, bars_[i].low);
        f.high = std::max(f.high, bars_[i].high);
    }
    return f;
}

void KrxChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(ui::colors::BG_SURFACE()));
    const Frame f = frame();
    const auto bar = [this, &f](int i) -> const krx_quant::Bar& { return bars_[f.first + i]; };
    if (f.plot.width() < kSmallest || f.plot.height() < kSmallest)
        return;
    if (f.count == 0) {
        p.setPen(QColor(ui::colors::TEXT_DIM()));
        p.drawText(rect(), Qt::AlignCenter, empty_text_.isEmpty() ? tr("오늘의 1분봉이 아직 없습니다") : empty_text_);
        return;
    }

    // Prices on the right, with their grid lines.
    p.setPen(QColor(ui::colors::BORDER_DIM()));
    QFont small = font();
    small.setPixelSize(11);
    p.setFont(small);
    for (int k = 0; k <= 4; ++k) {
        const double price = f.low + (f.high - f.low) * k / 4.0;
        const double y = f.y_of(price);
        p.setPen(QColor(ui::colors::BORDER_DIM()));
        p.drawLine(QPointF(f.plot.left(), y), QPointF(f.plot.right(), y));
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(QRectF(f.plot.right() + 4, y - 8, kPriceAxis - 6, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   krx_quant::price_text(price));
    }

    // Times at the bottom, at least 60 px apart.
    const int every = std::max(1, static_cast<int>(std::ceil(60.0 / f.slot)));
    p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
    for (int i = 0; i < f.count; i += every) {
        const double left = std::max(0.0, std::min(f.x_of(i) - 30, f.plot.right() - 60));
        p.drawText(QRectF(left, f.plot.bottom() + 2, 60, kTimeAxis - 2), Qt::AlignHCenter | Qt::AlignTop,
                   hhmm(bar(i).start));
    }

    if (hovered_ >= 0 && hovered_ < f.count) {
        p.setPen(QPen(QColor(ui::colors::BORDER_MED()), 1, Qt::DashLine));
        p.drawLine(QPointF(f.x_of(hovered_), f.plot.top()), QPointF(f.x_of(hovered_), f.plot.bottom()));
    }

    // The bars.
    const double body = std::max(1.0, f.slot * 0.6);
    for (int i = 0; i < f.count; ++i) {
        const auto& b = bar(i);
        const QColor color(b.close >= b.open ? ui::colors::POSITIVE() : ui::colors::NEGATIVE());
        const double x = f.x_of(i);
        p.setPen(QPen(color, 1));
        p.drawLine(QPointF(x, f.y_of(b.high)), QPointF(x, f.y_of(b.low)));
        const double top = f.y_of(std::max(b.open, b.close)), bottom = f.y_of(std::min(b.open, b.close));
        p.fillRect(QRectF(x - body / 2, top, body, std::max(1.0, bottom - top)), color);
    }

    // The final consensus: an arrow at the bar that closed then, and its result beside it.
    QFont glyph_font = font();
    glyph_font.setPixelSize(static_cast<int>(f.glyph) + 2);
    glyph_font.setBold(true);
    p.setFont(glyph_font);
    for (int i = 0; i < f.count; ++i) {
        const auto mark = marks_.constFind(bar(i).start);
        if (mark == marks_.cend())
            continue;
        const double x = f.x_of(i), g = f.glyph;
        const bool down = mark->predicted == krx_quant::Direction::Down;
        // Up and flat below the bar, down above it; the result further out.
        const double arrow_y = down ? f.y_of(bar(i).high) - 4 - g : f.y_of(bar(i).low) + 4;
        const double result_y = down ? arrow_y - g - 2 : arrow_y + g + 2;
        p.setOpacity(mark->special ? 0.35 : 1.0);
        QPainterPath arrow;
        if (mark->predicted == krx_quant::Direction::Up) {
            arrow.moveTo(x, arrow_y);
            arrow.lineTo(x - g / 2, arrow_y + g);
            arrow.lineTo(x + g / 2, arrow_y + g);
            p.fillPath(arrow, QColor(ui::colors::POSITIVE()));
        } else if (down) {
            arrow.moveTo(x, arrow_y + g);
            arrow.lineTo(x - g / 2, arrow_y);
            arrow.lineTo(x + g / 2, arrow_y);
            p.fillPath(arrow, QColor(ui::colors::NEGATIVE()));
        } else if (f.slot >= 4) { // flat, where there is room for it
            p.fillRect(QRectF(x - g / 2, arrow_y + g / 2 - 1, g, 2), QColor(ui::colors::TEXT_DIM()));
        }
        if (const Look look = look_of(*mark); !look.glyph.isEmpty()) {
            p.setPen(look.color);
            p.drawText(QRectF(x - g, result_y, 2 * g, g), Qt::AlignCenter, look.glyph);
        }
        p.setOpacity(1.0);
    }
}

void KrxChart::mouseMoveEvent(QMouseEvent* event) {
    const Frame f = frame();
    const double x = event->position().x();
    int index = -1;
    if (f.count > 0 && x >= f.plot.left() && x < f.plot.right())
        index = std::min(f.count - 1, static_cast<int>((x - f.plot.left()) / f.slot));
    if (index != hovered_) {
        hovered_ = index;
        update();
    }
    if (index < 0) {
        QToolTip::hideText();
        return;
    }
    const auto& b = bars_[f.first + index];
    QString text = tr("%1 · 시가 %2 · 고가 %3 · 저가 %4 · 종가 %5 · 거래량 %6")
                       .arg(hhmm(b.start), krx_quant::price_text(b.open), krx_quant::price_text(b.high),
                            krx_quant::price_text(b.low), krx_quant::price_text(b.close),
                            QLocale(QLocale::Korean).toString(b.volume, 'f', 0));
    if (const auto mark = marks_.constFind(b.start); mark != marks_.cend())
        text += "\n" + mark_text(*mark);
    QToolTip::showText(event->globalPosition().toPoint(), text, this);
}

void KrxChart::leaveEvent(QEvent* event) {
    hovered_ = -1;
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}

} // namespace fincept::screens
