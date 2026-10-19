#pragma once

#include "screens/krx_quant/KrxPanel.h"
#include "services/krx_quant/EngineData.h"
#include "services/krx_quant/FlowView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

#include <array>
#include <optional>

namespace fincept::services {
class KrxFlowLive;
}

namespace fincept::screens {

class KrxLogos;

/// The flow screen's chart (codeyoma/krx-quant#306): the day's 1-minute price and each flow over the
/// session, each on its own scale (the price's on the left; a flow's zero dashed), the special
/// windows shaded. A run of minutes without a value breaks a line. Hovering shows the minute's values;
/// dragging across zooms to that span, a double-click shows the whole session again.
class KrxFlowChart : public QWidget {
    Q_OBJECT
  public:
    explicit KrxFlowChart(QWidget* parent = nullptr);

    void set_flow(const std::optional<krx_quant::Flow>& flow, const QDateTime& now);
    void set_shown(krx_quant::flow::Series series, bool shown);
    /// The theme's colors now.
    void restyle();
    /// The color a series is drawn in.
    static QString color_of(krx_quant::flow::Series series);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    QRectF plot() const;
    double x_of(qint64 at) const;
    qint64 at_of(double x) const;
    /// The minute under ``x``, as an index of the flow's minutes; -1 outside.
    int minute_at(double x) const;
    /// The end of the span shown when nothing is zoomed: the close, or half an hour past now while the
    /// session goes on.
    qint64 whole_span() const;

    std::optional<krx_quant::Flow> flow_;
    QDateTime now_;
    std::array<bool, krx_quant::flow::kSeries> shown_{true, true, true, true, true};
    qint64 from_ = 0; ///< the span shown, seconds since the epoch
    qint64 to_ = 0;
    int hover_ = -1;
    double drag_from_ = -1;
    double drag_to_ = -1;
};

/// KRX intraday flows (fork addition, codeyoma/krx-quant#306, see custom/README.md): a symbol's
/// foreign and institutional estimates, program trading and foreign member firms through the day, over
/// its 1-minute price; the day's totals against the previous day's at the same time, the final supply
/// against the estimates, and the top five members. For watching, not a signal. It draws what its
/// KrxFlowLive holds, in Fincept's panels, tables, fonts and theme. The text is Korean.
class KrxFlowScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KrxFlowScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void refresh_theme();
    void render();
    void show_dot();
    void fill_symbols();
    void fill_totals();
    void fill_members();
    void move_day(int offset);

    services::KrxFlowLive* live_ = nullptr;
    KrxLogos* logos_ = nullptr;
    QTimer* clock_timer_ = nullptr;

    krx_panel::Header header_;
    QLabel* dot_ = nullptr;
    QComboBox* symbol_box_ = nullptr;
    QLabel* day_line_ = nullptr;
    QPushButton* previous_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* today_ = nullptr;
    QPushButton* reload_ = nullptr;
    QLabel* failure_ = nullptr;

    krx_panel::Header chart_header_;
    KrxFlowChart* chart_ = nullptr;
    QList<QCheckBox*> toggles_;
    krx_panel::Header totals_header_;
    QTableWidget* totals_table_ = nullptr;
    QLabel* final_ = nullptr;
    krx_panel::Header members_header_;
    QTableWidget* sellers_ = nullptr;
    QTableWidget* buyers_ = nullptr;
};

} // namespace fincept::screens
