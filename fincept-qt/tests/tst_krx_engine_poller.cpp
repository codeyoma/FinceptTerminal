// Tests for how the KRX screens read the engine on a schedule
// (src/services/krx_quant/EnginePoller.h, codeyoma/krx-quant#649): the address, a new one, the symbols, the
// last failure and reading only while shown, against a stand-in engine on this computer. Each screen's own
// tests keep what it reads and when; what every screen shares is tested here once. The settings (the
// engine's address) go to a temporary INI file: AppConfig is defined below, in place of
// core/config/AppConfig.cpp.

#include "core/config/AppConfig.h"
#include "services/krx_quant/EnginePoller.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QEvent>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

QString settings_file; ///< set before the first AppConfig::instance()

} // namespace

namespace fincept {

AppConfig& AppConfig::instance() {
    static AppConfig s;
    return s;
}

AppConfig::AppConfig() : settings_(settings_file, QSettings::IniFormat) {}

QVariant AppConfig::get(const QString& key, const QVariant& default_val) const {
    return settings_.value(key, default_val);
}

void AppConfig::set(const QString& key, const QVariant& value) {
    settings_.setValue(key, value);
}

void AppConfig::remove(const QString& key) {
    settings_.remove(key);
}

} // namespace fincept

using namespace fincept;
using services::EnginePoller;
using Symbols = services::EnginePoller::Symbols;

namespace {

constexpr int kPollMs = 600000; ///< the poll rule here: long enough that the timer never fires in a test
constexpr const char* kTimer = "pollerTimer";

/// The engine as a poller reads it: GET /v1/symbols, the paths asked kept.
class StandIn : public QObject {
  public:
    QString name = QStringLiteral("삼성전자"); ///< the first symbol's: tells two engines apart
    bool fail = false;                         ///< GET /v1/symbols answers 500
    bool hold = false;                         ///< the next GET /v1/symbols is answered only by release()
    QStringList asked;

    StandIn() {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = server_.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        server_.listen(QHostAddress::LocalHost);
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()); }
    /// Whether a read is held (hold).
    bool holding() const { return !held_.isNull(); }
    /// Answer the read held.
    void release() {
        if (held_)
            answer(held_);
        held_.clear();
    }

  private:
    void read(QTcpSocket* socket) {
        QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n"))
            return;
        asked << QString::fromLatin1(request.left(request.indexOf("\r\n")).split(' ').value(1));
        if (hold) {
            hold = false; // the first alone
            held_ = socket;
            return;
        }
        answer(socket);
    }

    void answer(QTcpSocket* socket) {
        const QString path = asked.last();
        int status = 404;
        QByteArray body = R"({"error": "not found"})";
        if (path == "/v1/symbols" && fail) {
            status = 500;
            body = R"({"error": "made-up failure"})";
        } else if (path == "/v1/symbols") {
            status = 200;
            body =
                QStringLiteral(
                    R"({"symbols": [{"symbol": "005930", "name": "%1"}, {"symbol": "000660", "name": "SK하이닉스"}]})")
                    .arg(name)
                    .toUtf8();
        }
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) +
                      "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    QTcpServer server_;
    QPointer<QTcpSocket> held_;
};

QDateTime at_nine() {
    return QDateTime::fromString(QStringLiteral("2026-10-16T09:00:00+09:00"), Qt::ISODate);
}

/// A poller as a screen sets it up: its read counted (and answered at once with ``answer``), its poll rule
/// kPollMs, its clock at_nine.
struct Screen {
    EnginePoller poller;
    int reads = 0;
    bool answer = false;

    explicit Screen(Symbols symbols) : poller(symbols, nullptr, at_nine) {
        poller.set_timer_name(QString::fromLatin1(kTimer));
        poller.set_read([this] {
            ++reads;
            if (answer)
                poller.answered();
        });
        poller.set_poll([] { return kPollMs; });
    }
    /// Whether the next read is on schedule (the poll rule's interval), not at once.
    bool scheduled() const {
        const auto* timer = poller.findChild<QTimer*>(QString::fromLatin1(kTimer));
        return timer && timer->isActive() && timer->interval() == kPollMs;
    }
};

} // namespace

class TstKrxEnginePoller : public QObject {
    Q_OBJECT

    QTemporaryDir settings_;

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        settings_file = settings_.filePath("settings.ini");
    }

    void without_an_address_it_says_where_to_set_it_and_reads_nothing() {
        services::KrxQuantService::set_base_url(QString());
        Screen screen(Symbols::Names);
        QSignalSpy changed(&screen.poller, &EnginePoller::changed);
        screen.poller.start();
        QCOMPARE(screen.poller.failure(), QString::fromUtf8(EnginePoller::kNoAddress));
        QVERIFY(screen.poller.failure().contains(QStringLiteral("엔진 주소")));
        QCOMPARE(screen.reads, 0);
        QCOMPARE(changed.count(), 1);
        QVERIFY(screen.scheduled()); // asked again on schedule: the address may be set meanwhile
        QCOMPARE(screen.poller.next_ms(), kPollMs);
        screen.poller.stop();
        QCOMPARE(screen.poller.next_ms(), 0);
    }

    void their_names_are_read_beside_the_screen_s_read() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        Screen screen(Symbols::Names);
        screen.answer = true;
        QSignalSpy symbols(&screen.poller, &EnginePoller::symbols_ready);
        screen.poller.start();
        QCOMPARE(screen.reads, 1); // at once: the names do not hold it back
        QTRY_COMPARE_WITH_TIMEOUT(screen.poller.symbols().size(), 2, 10000);
        QCOMPARE(symbols.count(), 1);
        QCOMPARE(screen.poller.symbols().first().name, QStringLiteral("삼성전자"));
        QCOMPARE(engine.asked, QStringList{"/v1/symbols"});
        QVERIFY(screen.poller.failure().isEmpty());
        QCOMPARE(screen.poller.answered_at(), at_nine());
        QVERIFY(screen.scheduled());

        // Held: read again without asking for them again.
        screen.poller.read_now();
        QCOMPARE(screen.reads, 2);
        QCOMPARE(engine.asked, QStringList{"/v1/symbols"});
        screen.poller.stop();
    }

    void the_names_failing_is_not_the_screen_s_failure() {
        // They are asked for again with the next read; the screen's read goes on.
        StandIn engine;
        engine.fail = true;
        services::KrxQuantService::set_base_url(engine.url());
        Screen screen(Symbols::Names);
        QSignalSpy names_failed(screen.poller.service(), &services::KrxQuantService::symbols_failed);
        screen.poller.start();
        QCOMPARE(screen.reads, 1);
        QTRY_COMPARE_WITH_TIMEOUT(names_failed.count(), 1, 10000);
        QVERIFY(screen.poller.failure().isEmpty());
        engine.fail = false;
        screen.poller.read_now();
        QCOMPARE(screen.reads, 2);
        QTRY_COMPARE_WITH_TIMEOUT(screen.poller.symbols().size(), 2, 10000);
        QCOMPARE(engine.asked.size(), 2);
        screen.poller.stop();
    }

    void a_failure_is_kept_until_an_answer() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        Screen screen(Symbols::None);
        screen.poller.start();
        QCOMPARE(screen.reads, 1);
        QVERIFY(engine.asked.isEmpty()); // no symbols asked for

        // The screen's own read fails: why is kept, and the next read is on schedule.
        screen.poller.failed(QStringLiteral("made-up"));
        QCOMPARE(screen.poller.failure(), QStringLiteral("made-up"));
        QVERIFY(screen.scheduled());
        QVERIFY(!screen.poller.answered_at().isValid());
        screen.poller.read_now();
        QCOMPARE(screen.reads, 2);
        QCOMPARE(screen.poller.failure(), QStringLiteral("made-up")); // not answered yet

        screen.poller.answered();
        QVERIFY(screen.poller.failure().isEmpty());
        QCOMPARE(screen.poller.answered_at(), at_nine());
        screen.poller.stop();
    }

    void a_new_address_drops_what_the_old_engine_was_answering() {
        StandIn first;
        first.hold = true;
        StandIn second;
        second.name = QStringLiteral("다른 엔진");
        second.hold = true;
        services::KrxQuantService::set_base_url(first.url());
        Screen screen(Symbols::Names);
        QSignalSpy engines(&screen.poller, &EnginePoller::engine_changed);
        QSignalSpy symbols(&screen.poller, &EnginePoller::symbols_ready);
        screen.poller.start();
        QCOMPARE(screen.reads, 1);
        QTRY_VERIFY_WITH_TIMEOUT(first.holding(), 10000);
        QCOMPARE(engines.count(), 1); // the first address taken
        screen.poller.answered();
        QCOMPARE(screen.poller.answered_at(), at_nine());

        services::KrxQuantService::set_base_url(second.url());
        screen.poller.read_now();
        QCOMPARE(engines.count(), 2);
        QCOMPARE(screen.reads, 2);
        QVERIFY(!screen.poller.answered_at().isValid());
        QTRY_VERIFY_WITH_TIMEOUT(second.holding(), 10000);
        // The old engine answers after the address changed: its answer is not taken.
        first.release();
        QTRY_VERIFY_WITH_TIMEOUT(!first.holding(), 10000);
        second.release();
        QTRY_VERIFY_WITH_TIMEOUT(!screen.poller.symbols().isEmpty() &&
                                     screen.poller.symbols().first().name == QStringLiteral("다른 엔진"),
                                 10000);
        QCOMPARE(symbols.count(), 1);

        // Another address again: what was read of the old engine goes.
        services::KrxQuantService::set_base_url(first.url());
        screen.poller.read_now();
        QCOMPARE(engines.count(), 3);
        QVERIFY(screen.poller.symbols().isEmpty());
        QVERIFY(!screen.poller.answered_at().isValid());
        screen.poller.stop();
    }

    void it_reads_only_while_shown() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        Screen screen(Symbols::None);
        QObject widget; // stands in for the screen: only its show and hide events are watched
        screen.poller.run_while_shown(&widget);
        QVERIFY(!screen.poller.running());

        QEvent show(QEvent::Show);
        QCoreApplication::sendEvent(&widget, &show);
        QVERIFY(screen.poller.running());
        QCOMPARE(screen.reads, 1); // at once
        screen.poller.schedule();
        QVERIFY(screen.scheduled());

        QEvent hide(QEvent::Hide);
        QCoreApplication::sendEvent(&widget, &hide);
        QVERIFY(!screen.poller.running());
        QVERIFY(!screen.scheduled());
        QCOMPARE(screen.poller.next_ms(), 0);
        screen.poller.read_now(); // the refresh button of a hidden screen: nothing
        QCOMPARE(screen.reads, 1);

        QCoreApplication::sendEvent(&widget, &show);
        QCOMPARE(screen.reads, 2);
        screen.poller.stop();
    }
};

QTEST_GUILESS_MAIN(TstKrxEnginePoller)
#include "tst_krx_engine_poller.moc"
