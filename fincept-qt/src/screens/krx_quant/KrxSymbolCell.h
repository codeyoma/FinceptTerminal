#pragma once

#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QStyledItemDelegate>

/// The symbols panel's first column (krx-quant #250), as Toss Securities shows a stock: a round
/// logo, the company's name, and the code after it, dimmed. The engine gives the name and the
/// logo's address (GET /v1/symbols); the logos are read from there while the screen runs and
/// kept in memory only, never on disk (Toss's data is for the user's own use).
namespace fincept::screens {

/// The symbols' logos, read once a run each. Until one arrives, or when it cannot be read (a
/// code Toss has no logo for answers 403), a circle with the name's first letter stands in.
class KrxLogos : public QObject {
    Q_OBJECT
  public:
    explicit KrxLogos(QObject* parent = nullptr);

    /// The logo at ``url``, cropped round, ``size`` points wide at ``ratio`` device pixels a point;
    /// or the stand-in with ``name``'s first letter, asking for the logo if it was not yet.
    QPixmap logo(const QString& url, const QString& name, int size, qreal ratio);

  signals:
    /// A logo arrived: the panel draws again.
    void arrived();

  private:
    void ask(const QString& url);

    QNetworkAccessManager net_;
    QHash<QString, QImage> images_; ///< by address, as read
    QHash<QString, QPixmap> drawn_; ///< by address, size and ratio, cropped round
    QSet<QString> asked_;           ///< asked for this run, read or not
};

/// Paints the name as usual and the code after it in the theme's dim text. The item's text is
/// the name; kCodeRole holds the code, empty when the code is all there is (no name known).
class KrxSymbolDelegate : public QStyledItemDelegate {
  public:
    static constexpr int kCodeRole = Qt::UserRole + 1;
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace fincept::screens
