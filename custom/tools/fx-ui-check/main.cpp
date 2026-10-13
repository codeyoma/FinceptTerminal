#include "screens/dashboard/widgets/BaseWidget.h"
#include "services/markets/MarketDataService.h"
#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QStandardPaths>
#include <cstdlib>
#include <iostream>

using fincept::screens::widgets::BaseWidget;
using fincept::services::MarketDataService;

struct Observations {
    int quote_calls = 0;
    int display_calls = 0;
    int premature_display_calls = 0;
    double last_rendered = 0;
};

class Probe : public BaseWidget {
  public:
    explicit Probe(Observations& observations, bool emit_in_constructor = false)
        : BaseWidget("probe"), observations_(observations) {
        if (emit_in_constructor)
            emit MarketDataService::instance().display_changed();
        constructed_ = true;
    }
    void queue_native_quote(double value) {
        native_quote_ = value;
        schedule_render([this] {
            ++observations_.quote_calls;
            observations_.last_rendered = native_quote_;
        });
    }
    double native_quote() const { return native_quote_; }
  protected:
    void on_display_currency_changed() override {
        ++observations_.display_calls;
        if (!constructed_)
            ++observations_.premature_display_calls;
    }
  private:
    Observations& observations_;
    bool constructed_ = false;
    double native_quote_ = 0;
};

// This class deliberately keeps BaseWidget's default no-op display hook.
class DefaultHookProbe : public BaseWidget {
  public:
    explicit DefaultHookProbe(Observations& observations) : BaseWidget("default hook"), observations_(observations) {}
    void queue_native_quote(double value) {
        native_quote_ = value;
        schedule_render([this] {
            ++observations_.quote_calls;
            observations_.last_rendered = native_quote_;
        });
    }
  private:
    Observations& observations_;
    double native_quote_ = 0;
};

static int checks = 0;
static void check(bool value, const char* description) {
    if (!value) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
    ++checks;
    std::cout << "PASS: " << description << '\n';
}
static void drain_events() {
    for (int turn = 0; turn < 4; ++turn)
        QCoreApplication::processEvents(QEventLoop::AllEvents);
}
static void display_change() { emit MarketDataService::instance().display_changed(); }

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("CodexFxBaseWidgetCheck");
    QCoreApplication::setApplicationName("BaseWidgetEventOrder");
    QStandardPaths::setTestModeEnabled(true);

    {
        Observations observed;
        Probe widget(observed);
        widget.queue_native_quote(123.5);
        display_change();
        check(observed.quote_calls == 0 && observed.display_calls == 0, "quote-first work remains queued");
        drain_events();
        check(observed.quote_calls == 1, "quote-first callback survives display invalidation");
        check(observed.display_calls == 1, "quote-first display callback also executes");
        check(observed.last_rendered == 123.5 && widget.native_quote() == 123.5, "quote-first raw price stays unchanged");
    }
    {
        Observations observed;
        Probe widget(observed);
        display_change();
        widget.queue_native_quote(245.75);
        check(observed.quote_calls == 0 && observed.display_calls == 0, "display-first work remains queued");
        drain_events();
        check(observed.quote_calls == 1, "display-first quote callback executes");
        check(observed.display_calls == 1, "display-first display callback is not replaced by quote work");
        check(observed.last_rendered == 245.75 && widget.native_quote() == 245.75, "display-first raw price stays unchanged");
    }
    {
        Observations observed;
        DefaultHookProbe widget(observed);
        widget.queue_native_quote(300.25);
        display_change();
        drain_events();
        check(observed.quote_calls == 1 && observed.last_rendered == 300.25,
              "default no-op display hook cannot erase queued quote work");
    }
    {
        Observations observed;
        DefaultHookProbe widget(observed);
        display_change();
        widget.queue_native_quote(400.5);
        drain_events();
        check(observed.quote_calls == 1 && observed.last_rendered == 400.5,
              "default no-op hook also preserves display-first quote work");
    }
    {
        Observations observed;
        Probe widget(observed, true);
        check(observed.display_calls == 0, "signal emitted in derived constructor does not dispatch immediately");
        drain_events();
        check(observed.display_calls == 1 && observed.premature_display_calls == 0,
              "constructor signal dispatches only after derived construction completes");
    }
    {
        Observations observed;
        Probe widget(observed);
        widget.queue_native_quote(1.0);
        widget.queue_native_quote(2.0);
        display_change();
        widget.queue_native_quote(3.0);
        display_change();
        drain_events();
        check(observed.quote_calls == 1 && observed.last_rendered == 3.0,
              "quote coalescing still renders newest native value once");
        check(observed.display_calls == 2, "independent display notifications survive interleaved quotes");
    }
    {
        Observations observed;
        auto* widget = new Probe(observed);
        widget->queue_native_quote(999.0);
        display_change();
        delete widget;
        drain_events();
        check(observed.quote_calls == 0 && observed.display_calls == 0,
              "destroyed widgets drop both pending callbacks");
    }
    std::cout << "RESULT: " << checks << " checks passed; production BaseWidget callback and scheduler executed\n";
    return 0;
}
