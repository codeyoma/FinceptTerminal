#include "screens/krx_quant/KrxChart.h"

#include "ui/theme/Theme.h"

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimeZone>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

namespace {

constexpr int kPriceAxis = 72; ///< px on the right for the prices
constexpr int kTimeAxis = 20;  ///< px at the bottom for the times
constexpr int kMargin = 8;

QString kst_hhmm(const QDateTime& t) {
    static const QTimeZone kst("Asia/Seoul");
    return t.toTimeZone(kst).toString("HH:mm");
}

QString price_text(double price) {
    static const QLocale locale(QLocale::Korean, QLocale::SouthKorea);
    return locale.toString(price, 'f', std::floor(price) == price ? 0 : 1);
}

QString result_text(krx_quant::Mark::Result result) {
    switch (result) {
        case krx_quant::Mark::Result::Hit:
            return QStringLiteral("✓");
        case krx_quant::Mark::Result::Miss:
            return QStringLiteral("✗");
        case krx_quant::Mark::Result::Unscorable:
            return QStringLiteral("?");
        case krx_quant::Mark::Result::Pending:
            break;
    }
    return {};
}

QColor result_color(krx_quant::Mark::Result result) {
    switch (result) {
        case krx_quant::Mark::Result::Hit:
            return QColor(ui::colors::POSITIVE());
        case krx_quant::Mark::Result::Miss:
            return QColor(ui::colors::NEGATIVE());
        default:
            return QColor(ui::colors::TEXT_DIM());
    }
}

/// "09:31 봉 마감 합의(최종) 상승 62% ✓" for a hovered bar's mark.
QString mark_text(const krx_quant::Mark& mark) {
    QString text = QStringLiteral("%1 봉 마감 합의(최종) %2 %3%")
                       .arg(kst_hhmm(mark.bar_close), krx_quant::direction_label(mark.predicted))
                       .arg(std::lround(mark.probability * 100));
    switch (mark.result) {
        case krx_quant::Mark::Result::Hit:
            text += QStringLiteral(" ✓ 맞음");
            break;
        case krx_quant::Mark::Result::Miss:
            text += QStringLiteral(" ✗ 틀림");
            break;
        case krx_quant::Mark::Result::Unscorable:
            text += QStringLiteral(" ? 채점 불가");
            break;
        case krx_quant::Mark::Result::Pending:
            text += QStringLiteral(" (채점 전)");
            break;
    }
    if (mark.special)
        text += QStringLiteral(" · 특수 상황(통계 제외)");
    return text;
}

} // namespace

struct KrxChart::Frame {
    QRectF plot;
    int count = 0;
    double slot = 0;  ///< px per bar
    double glyph = 0; ///< px of a mark
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

QList<krx_quant::Bar> KrxChart::visible() const {
    if (window_ <= 0 || bars_.size() <= window_)
        return bars_;
    return bars_.mid(bars_.size() - window_);
}

KrxChart::Frame KrxChart::frame() const {
    Frame f;
    const QList<krx_quant::Bar> bars = visible();
    f.plot = QRectF(kMargin, kMargin, width() - kMargin - kPriceAxis, height() - kMargin - kTimeAxis);
    f.count = static_cast<int>(bars.size());
    if (f.count == 0)
        return f;
    f.slot = f.plot.width() / f.count;
    f.glyph = std::clamp(f.slot * 0.8, 6.0, 12.0);
    f.pad = 2 * f.glyph + 10;
    f.low = bars[0].low;
    f.high = bars[0].high;
    for (const auto& b : bars) {
        f.low = std::min(f.low, b.low);
        f.high = std::max(f.high, b.high);
    }
    return f;
}

void KrxChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(ui::colors::BG_SURFACE()));
    const QList<krx_quant::Bar> bars = visible();
    const Frame f = frame();
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
                   price_text(price));
    }

    // Times at the bottom, at least 60 px apart.
    const int every = std::max(1, static_cast<int>(std::ceil(60.0 / f.slot)));
    p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
    for (int i = 0; i < f.count; i += every) {
        const double left = std::clamp(f.x_of(i) - 30, 0.0, f.plot.right() - 60);
        p.drawText(QRectF(left, f.plot.bottom() + 2, 60, kTimeAxis - 2), Qt::AlignHCenter | Qt::AlignTop,
                   kst_hhmm(bars[i].start));
    }

    if (hovered_ >= 0 && hovered_ < f.count) {
        p.setPen(QPen(QColor(ui::colors::BORDER_MED()), 1, Qt::DashLine));
        p.drawLine(QPointF(f.x_of(hovered_), f.plot.top()), QPointF(f.x_of(hovered_), f.plot.bottom()));
    }

    // The bars.
    const double body = std::max(1.0, f.slot * 0.6);
    for (int i = 0; i < f.count; ++i) {
        const auto& b = bars[i];
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
        const auto mark = marks_.constFind(bars[i].start);
        if (mark == marks_.cend())
            continue;
        const double x = f.x_of(i), g = f.glyph;
        const bool down = mark->predicted == krx_quant::Direction::Down;
        // Up and flat below the bar, down above it; the result further out.
        const double arrow_y = down ? f.y_of(bars[i].high) - 4 - g : f.y_of(bars[i].low) + 4;
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
        const QString result = result_text(mark->result);
        if (!result.isEmpty()) {
            p.setPen(result_color(mark->result));
            p.drawText(QRectF(x - g, result_y, 2 * g, g), Qt::AlignCenter, result);
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
    const auto b = visible()[index];
    QString text = tr("%1 · 시가 %2 · 고가 %3 · 저가 %4 · 종가 %5 · 거래량 %6")
                       .arg(kst_hhmm(b.start), price_text(b.open), price_text(b.high), price_text(b.low),
                            price_text(b.close), QLocale(QLocale::Korean).toString(b.volume, 'f', 0));
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
