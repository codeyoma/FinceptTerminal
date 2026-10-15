#pragma once

#include "services/krx_quant/ChartStudies.h"

#include <QFrame>
#include <QHash>

class QGridLayout;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace fincept::screens {

/// The `+ 보조지표` popup above the KRX chart (krx-quant #220, #222), in two columns. On the left
/// the upper and lower indicators' lists: a check circle turns one on or off, its name picks it.
/// On the right the one picked: its name, a `?` with how it is worked out, a line about it, and
/// its settings (the source, its numbers, each line's colour and width; the moving averages a
/// period per line, `기간 추가` and X). It only shows the set the screen gives it and says each
/// change at once; the screen keeps it for the symbol and draws it.
class KrxStudyPanel : public QFrame {
    Q_OBJECT
  public:
    explicit KrxStudyPanel(QWidget* parent = nullptr);

    /// Show ``set`` in the view ``view``: the VWAP is greyed in a candle view ("분봉에서만").
    /// The settings shown are built again only when they differ from those the panel has.
    void show_studies(const krx_quant::StudySet& set, int view);
    /// Show ``study``'s settings on the right.
    void select(krx_quant::Study study);
    krx_quant::Study selected() const { return selected_; }
    /// Open with its top left at ``at`` (global), kept on that screen.
    void open_at(const QPoint& at);
    /// Paint in the theme's colours now.
    void restyle();

  signals:
    /// Any change, at once: the whole set as it is now.
    void studies_changed(const krx_quant::StudySet& set);
    /// The anchored VWAP's `기준 다시 고르기`: the next click on the chart chooses its bar.
    void anchor_requested();

  private:
    /// The lists' check circles and names as the set and the view say.
    void fill_lists();
    /// The right side for the indicator picked.
    void fill_settings();
    /// The set changed here: said, the lists shown again; the settings too with ``rebuild``.
    void changed(bool rebuild = false);
    /// A button showing ``color``; it opens a palette and sets the line's colour.
    QToolButton* color_button(krx_quant::Study study, int line);

    krx_quant::StudySet set_;
    int view_ = 1;
    krx_quant::Study selected_ = krx_quant::Study::MovingAverage;
    QHash<int, QToolButton*> checks_; ///< by study
    QHash<int, QPushButton*> names_;  ///< by study
    QLabel* title_ = nullptr;
    QToolButton* help_ = nullptr;
    QLabel* about_ = nullptr;
    QWidget* form_ = nullptr; ///< the settings, built again for each indicator
    QVBoxLayout* right_ = nullptr;
};

} // namespace fincept::screens
