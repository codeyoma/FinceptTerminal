#pragma once

#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

/// The KRX screen's panels, in the shape of the Portfolio screen's: a thin header bar with
/// an amber tick, a bold title and the panel's controls on the right, over the panel's
/// body, flush with its edges. Upstream's helper (screens/portfolio/PortfolioPanelHeader.h)
/// is left as it is; it paints once, and these paint again on a theme change (restyle).
namespace fincept::screens::krx_panel {

struct Header {
    QWidget* bar = nullptr;
    QHBoxLayout* row = nullptr; ///< the tick, the title, a stretch, then ``controls``
    QLabel* tick = nullptr;
    QLabel* title = nullptr;
    QHBoxLayout* controls = nullptr; ///< the panel's controls, on the right

    /// Paint in the theme's colors and the user's font size now.
    void restyle() const {
        bar->setStyleSheet(QString("#krxPanelHeader{background:%1;border-bottom:1px solid %2;}"
                                   "#krxPanelHeader QLabel,#krxPanelHeader QCheckBox{background:transparent;color:%3;}")
                               .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), ui::colors::TEXT_SECONDARY()));
        tick->setStyleSheet(QString("background:%1;").arg(ui::colors::AMBER()));
        // The family is the app's (the global style's); the size, three below the user's.
        const int size = std::max(9, ui::ThemeManager::instance().current_font().pixelSize() - 3);
        title->setStyleSheet(QString("color:%1;font-size:%2px;font-weight:700;letter-spacing:1.2px;"
                                     "background:transparent;")
                                 .arg(ui::colors::TEXT_PRIMARY())
                                 .arg(size));
    }
};

/// A header titled ``title``; its tooltip, if any, says what the title alone does not.
inline Header header(const QString& title, const QString& tip = {}) {
    Header h;
    h.bar = new QWidget;
    h.bar->setObjectName("krxPanelHeader");
    h.bar->setAttribute(Qt::WA_StyledBackground);
    h.bar->setMinimumHeight(30);
    h.row = new QHBoxLayout(h.bar);
    h.row->setContentsMargins(12, 2, 10, 2);
    h.row->setSpacing(8);
    h.tick = new QLabel;
    h.tick->setFixedSize(3, 14);
    h.row->addWidget(h.tick);
    h.title = new QLabel(title);
    h.title->setToolTip(tip);
    h.row->addWidget(h.title);
    h.row->addStretch(1);
    h.controls = new QHBoxLayout;
    h.controls->setSpacing(6);
    h.row->addLayout(h.controls);
    h.restyle();
    return h;
}

/// A panel: ``header`` over ``body``, flush.
inline QWidget* panel(const Header& header, QWidget* body) {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header.bar);
    layout->addWidget(body, 1);
    return panel;
}

} // namespace fincept::screens::krx_panel
