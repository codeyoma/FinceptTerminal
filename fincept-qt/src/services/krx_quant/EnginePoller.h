#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class QEvent;
class QTimer;

namespace fincept::services {

class KrxQuantService;

/// Reads the KRX prediction engine on a schedule for one screen or dashboard tile (codeyoma/krx-quant#649).
/// The KRX screens' live objects (KrxFlowLive, KrxClosingLive, KrxCostLive, KrxBriefingLive,
/// KrxTraderLive) and the dashboard's KrxEngineTile each own one and give it only what they read
/// (set_read) and when to read again (set_poll); it does the rest.
///
/// - The address: each read takes the KRX screen's setting (`krx_quant/base_url`). A new one drops every
///   read under way (KrxQuantService::rebind), the symbols and the time of the last answer, and says so
///   (engine_changed) for the screen to drop what it read of the old engine. Without one it reads nothing
///   and says where to set it (kNoAddress).
/// - The symbols (GET /v1/symbols), as ``symbols`` says: not read, read beside the screen's read for their
///   names, or read before it until they are in.
/// - The last failure: the address's, the symbols', or what the screen says (failed); an answer
///   (answered) clears it.
/// - The schedule: one read at a time on a single-shot timer, the next started after an answer or a
///   failure by the screen's poll rule; read_now reads at once.
/// - Shown or hidden: started, it reads at once and then on schedule; stopped, it reads nothing.
///   run_while_shown starts it when a widget is shown and stops it when it is hidden.
///
/// An answer of something the screen no longer shows (another symbol or day chosen while it was read)
/// is the screen's to drop: it reads again with read_now.
class EnginePoller : public QObject {
    Q_OBJECT
  public:
    /// What the screen needs of the engine's symbols.
    enum class Symbols {
        None,     ///< nothing: they are not read
        Names,    ///< their names and logos: read beside the screen's read while none are held
        First,    ///< read before the screen's read until they answer; an empty list too
        Required, ///< read before the screen's read until the engine has some; an empty list is a failure
    };

    /// What a read without the engine's address says.
    static constexpr const char* kNoAddress = "엔진 주소가 없습니다. KRX 4분 예측 화면에서 엔진 주소를 넣으세요.";
    /// What an engine without symbols says (Symbols::Required).
    static constexpr const char* kNoSymbols = "엔진이 예측하는 종목이 없습니다.";

    /// ``now`` is the clock answers are stamped with and the poll rule reads (tests give their own).
    explicit EnginePoller(Symbols symbols, QObject* parent = nullptr, std::function<QDateTime()> now = {});

    /// The service the screen reads through: it connects its answers there.
    KrxQuantService* service() const { return service_; }
    /// The screen's read, called once the address (and the symbols, as Symbols says) are in. It ends with
    /// answered(), failed() or read_now(); a read that leaves the schedule to another answer may end without.
    void set_read(std::function<void()> read) { read_ = std::move(read); }
    /// The screen's poll rule: the interval to the next read, ms, asked after each answer or failure.
    void set_poll(std::function<int()> next_ms) { poll_ = std::move(next_ms); }
    /// The schedule's timer's object name (a test fires it).
    void set_timer_name(const QString& name);

    void start(); ///< read at once, then on schedule
    void stop();  ///< read no more until started again
    bool running() const { return running_; }
    /// Start when ``widget`` is shown, stop when it is hidden.
    void run_while_shown(QObject* widget);

    /// Read now, when started.
    void read_now();
    /// The next read by the poll rule, when started.
    void schedule();
    /// The screen's read answered: the failure goes, the time is kept, then changed and the next read.
    void answered();
    /// The screen's read failed: ``reason`` is kept (what was read stays), then changed and the next read.
    void failed(const QString& reason);
    /// The failure goes, without a read (the screen read something that answers it).
    void clear_failure() { failure_.clear(); }

    /// Why the last read failed; empty after an answer.
    const QString& failure() const { return failure_; }
    /// When the engine last answered (``now``'s clock); invalid before, and after another address.
    const QDateTime& answered_at() const { return answered_at_; }
    QDateTime now() const { return now_(); }
    /// The interval to the next read now, ms (0 when stopped).
    int next_ms() const;

    /// The engine's symbols, in its order, with names and logos; empty before they are read.
    const QList<krx_quant::SymbolQuote>& symbols() const { return symbols_; }

  signals:
    /// Something to draw again: the address missing, the symbols, an answer, a failure.
    void changed();
    /// Started: before its first read.
    void started();
    /// Stopped.
    void stopped();
    /// Another address: the reads under way were dropped. What was read of the old engine goes.
    void engine_changed();
    /// The symbols came (symbols()), before changed and before the read they let go on.
    void symbols_ready();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void read();

    Symbols want_;
    std::function<QDateTime()> now_;
    std::function<void()> read_;
    std::function<int()> poll_;
    KrxQuantService* service_ = nullptr;
    QTimer* timer_ = nullptr;
    QList<krx_quant::SymbolQuote> symbols_;
    bool symbols_in_ = false; ///< the symbols answered (Symbols::First, Required)
    QString failure_;
    QDateTime answered_at_;
    bool running_ = false;
};

} // namespace fincept::services
