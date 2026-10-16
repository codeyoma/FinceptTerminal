#include "screens/krx_quant/KrxSymbolCell.h"

#include "ui/theme/Theme.h"

#include <QApplication>
#include <QFont>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QUrl>

#include <algorithm>

namespace fincept::screens {

namespace {

constexpr int kTimeoutMs = 10000;

/// Pixels between the name and the code: a space, and the margin the style keeps after the
/// name's text (QCommonStyle's viewItemDrawText draws inside PM_FocusFrameHMargin + 1).
int code_gap(const QStyleOptionViewItem& opt, const QStyle* style) {
    return opt.fontMetrics.horizontalAdvance(QLatin1Char(' ')) +
           2 * (style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, opt.widget) + 1);
}

const QStyle* style_of(const QStyleOptionViewItem& opt) {
    return opt.widget ? opt.widget->style() : QApplication::style();
}

QPixmap blank(int pixels, qreal ratio) {
    QPixmap out(pixels, pixels);
    out.setDevicePixelRatio(ratio);
    out.fill(Qt::transparent);
    return out;
}

QPixmap round_logo(const QImage& image, int size, qreal ratio) {
    const int pixels = qRound(size * ratio);
    QPixmap out = blank(pixels, ratio);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, size, size));
    p.setClipPath(circle);
    p.drawImage(QRectF(0, 0, size, size), image);
    return out;
}

QPixmap letter_logo(const QString& name, int size, qreal ratio) {
    QPixmap out = blank(qRound(size * ratio), ratio);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(ui::colors::BG_RAISED()));
    p.drawEllipse(QRectF(0, 0, size, size));
    if (!name.isEmpty()) {
        QFont font = QApplication::font();
        font.setPixelSize(std::max(1, size / 2));
        font.setBold(true);
        p.setFont(font);
        p.setPen(QColor(ui::colors::TEXT_PRIMARY()));
        p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, name.left(1));
    }
    return out;
}

} // namespace

KrxLogos::KrxLogos(QObject* parent) : QObject(parent) {}

QPixmap KrxLogos::logo(const QString& url, const QString& name, int size, qreal ratio) {
    const auto image = images_.constFind(url);
    if (image == images_.cend()) {
        ask(url);
        return letter_logo(name, size, ratio); // the theme's colors now: not kept
    }
    const QString key = QStringLiteral("%1|%2|%3").arg(url).arg(size).arg(ratio);
    auto drawn = drawn_.find(key);
    if (drawn == drawn_.end())
        drawn = drawn_.insert(key, round_logo(*image, size, ratio));
    return *drawn;
}

void KrxLogos::ask(const QString& url) {
    if (url.isEmpty() || asked_.contains(url))
        return;
    asked_.insert(url); // once a run: a logo that cannot be read keeps its stand-in
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply* reply = net_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        QImage image;
        if (reply->error() != QNetworkReply::NoError || !image.loadFromData(reply->readAll()))
            return;
        images_.insert(url, image);
        emit arrived();
    });
}

void KrxSymbolDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QStyledItemDelegate::paint(painter, option, index);
    const QString code = index.data(kCodeRole).toString();
    if (code.isEmpty())
        return;
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QStyle* style = style_of(opt);
    const QRect text = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
    const QRect rest = text.adjusted(opt.fontMetrics.horizontalAdvance(opt.text) + code_gap(opt, style), 0, 0, 0);
    if (rest.width() <= 0)
        return;
    painter->save();
    painter->setFont(opt.font);
    painter->setPen(QColor(ui::colors::TEXT_DIM()));
    painter->drawText(rest, Qt::AlignLeft | Qt::AlignVCenter,
                      opt.fontMetrics.elidedText(code, Qt::ElideRight, rest.width()));
    painter->restore();
}

QSize KrxSymbolDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const QString code = index.data(kCodeRole).toString();
    if (!code.isEmpty()) {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        size.rwidth() += code_gap(opt, style_of(opt)) + opt.fontMetrics.horizontalAdvance(code);
    }
    return size;
}

} // namespace fincept::screens
