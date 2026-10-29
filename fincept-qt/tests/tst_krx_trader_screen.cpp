// Tests for the KRX auto trader screen itself (src/screens/krx_quant/KrxTraderScreen.h,
// codeyoma/krx-quant#397) against a stand-in engine on this computer: what a poll does to its tables
// (krx_tables::refill: nothing when the rows are the same; the view's top row and the selected row kept
// when they change), and that it has the two pause buttons and no resume button. The values are made
// up. Widgets and Qt Network; it runs on the offscreen platform too. The settings (the engine's address)
// go to a temporary INI file: AppConfig is defined below, in place of core/config/AppConfig.cpp.

#include "core/config/AppConfig.h"
#include "screens/krx_quant/KrxTraderScreen.h"
#include "services/krx_quant/KrxQuantService.h"
#include "services/krx_quant/KrxTraderLive.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

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

namespace {

/// A trade of 14 shares entered on 10-14 at 10:00 plus ``id`` minutes; held if ``held``.
QJsonObject trade_json(int id, bool held) {
    const QString entered = QStringLiteral("2026-10-14T%1:%2:02.500+09:00")
                                .arg(10 + id / 60, 2, 10, QChar('0'))
                                .arg(id % 60, 2, 10, QChar('0'));
    QJsonObject o{{"id", id},
                  {"mode", "paper"},
                  {"symbol", "005930"},
                  {"kind", "barrier"},
                  {"predictor", "breiman"},
                  {"role", "top"},
                  {"prediction_id", id},
                  {"bar_close", entered.left(16) + ":00+09:00"},
                  {"probability", 0.7},
                  {"threshold", 0.6},
                  {"p_star", 0.63},
                  {"precision_low", 0.7},
                  {"kelly", 10.5},
                  {"fraction", 1.0},
                  {"policy", QJsonValue()},
                  {"anchor", 70050.0},
                  {"tick", 100.0},
                  {"width_ticks", 2},
                  {"target", 70250.0},
                  {"stop", 69850.0},
                  {"time_barrier", entered.left(16) + ":00+09:00"},
                  {"decided_at", entered},
                  {"entry", QJsonObject{{"at", entered}, {"price", 70100.0}, {"quantity", 14}}},
                  {"exit", QJsonValue()},
                  {"buy_fee", 147.21},
                  {"sell_fee", QJsonValue()},
                  {"tax", QJsonValue()},
                  {"cost", QJsonValue()},
                  {"gross", QJsonValue()},
                  {"net", QJsonValue()},
                  {"orders", QJsonArray{}}};
    if (!held) {
        o["exit"] = QJsonObject{{"at", entered}, {"price", 70250.0}, {"reason", "take_profit"}};
        o["sell_fee"] = 147.525;
        o["tax"] = 1967.0;
        o["cost"] = 2261.735;
        o["gross"] = 2100.0;
        o["net"] = -161.735;
    }
    return o;
}

/// The engine as the screen reads it: the trader running with trade ``newest`` open, its trades
/// ``newest`` down to 1, newest first, and its days; each request's path kept.
class StandIn : public QObject {
  public:
    int newest = 40;
    double unrealized = -5051.2;
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
    qsizetype count(const QString& path) const {
        return std::count_if(asked.cbegin(), asked.cend(), [&path](const QString& p) { return p.section('?', 0, 0) == path; });
    }

  private:
    QJsonObject state() const {
        return {{"updated_at", "2026-10-14T10:06:00+09:00"},
                {"alive", true},
                {"mode", "paper"},
                {"status", "running"},
                {"started_at", "2026-10-14T07:20:05+09:00"},
                {"capital", 1000000},
                {"cash", 14142.79},
                {"position",
                 QJsonObject{{"trade_id", newest},
                             {"kind", "barrier"},
                             {"predictor", "breiman"},
                             {"quantity", 14},
                             {"entry_price", 70100.0},
                             {"entry_at", "2026-10-14T10:05:02.500+09:00"},
                             {"target", 70250.0},
                             {"stop", 69850.0},
                             {"time_barrier", "2026-10-14T10:09:00+09:00"},
                             {"bid", 69900.0},
                             {"bid_at", "2026-10-14T10:06:00+09:00"},
                             {"unrealized", unrealized},
                             {"selling", false}}},
                {"today",
                 QJsonObject{{"day", "2026-10-14"},
                             {"realized", 0.0},
                             {"unrealized", unrealized},
                             {"net", unrealized},
                             {"trades", newest - 1},
                             {"worst", unrealized}}},
                {"cumulative",
                 QJsonObject{{"since", "2026-10-14T07:20:03+09:00"},
                             {"realized", 0.0},
                             {"unrealized", unrealized},
                             {"net", unrealized},
                             {"worst", unrealized}}},
                {"limits",
                 QJsonObject{{"daily", QJsonObject{{"percent", 10}, {"won", 100000}, {"used", 0.05}, {"reached_at", QJsonValue()}}},
                             {"cumulative",
                              QJsonObject{{"percent", 20}, {"won", 200000}, {"used", 0.02}, {"reached_at", QJsonValue()}}}}},
                {"pause", QJsonValue()},
                {"controls", QJsonArray{}}};
    }

    void read(QTcpSocket* socket) {
        QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n"))
            return;
        const QString path = QString::fromLatin1(request.left(request.indexOf("\r\n")).split(' ').value(1));
        asked << path;
        QJsonObject body{{"error", "not found"}};
        int status = 404;
        const QString route = path.section('?', 0, 0);
        if (route == "/v1/trader") {
            status = 200;
            body = state();
        } else if (route == "/v1/trader/trades") {
            status = 200;
            QJsonArray trades;
            for (int id = newest; id >= 1; --id)
                trades.append(trade_json(id, id == newest));
            body = {{"trades", trades}};
        } else if (route == "/v1/trader/days") {
            status = 200;
            body = {{"from", "2025-10-15"}, {"to", "2026-10-14"}, {"days", QJsonArray{}}};
        }
        const QByteArray bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                      "\r\nConnection: close\r\n\r\n" + bytes);
        socket->disconnectFromHost();
    }

    QTcpServer server_;
};

QString key_at(const QTableWidget* table, int row) {
    const QTableWidgetItem* item = row >= 0 ? table->item(row, 0) : nullptr;
    return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
}

} // namespace

class TstKrxTraderScreen : public QObject {
    Q_OBJECT

    QTemporaryDir settings_;

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        settings_file = settings_.filePath("settings.ini");
    }

    void it_has_the_two_pause_buttons_and_no_resume() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        screens::KrxTraderScreen screen;
        screen.resize(1600, 1000);
        screen.show();
        auto* entries = screen.findChild<QPushButton*>(screens::KrxTraderScreen::kPauseEntriesName);
        auto* sell = screen.findChild<QPushButton*>(screens::KrxTraderScreen::kPauseSellName);
        QVERIFY(entries && sell);
        QTRY_VERIFY_WITH_TIMEOUT(entries->isEnabled() && sell->isEnabled(), 10000); // once the state is read
        const auto buttons = screen.findChildren<QPushButton*>();
        QVERIFY(std::none_of(buttons.cbegin(), buttons.cend(), [](const QPushButton* b) {
            return b->text().contains(QStringLiteral("재개")) || b->text().contains(QStringLiteral("다시 켜기"));
        }));
    }

    void a_poll_keeps_the_trades_table_where_the_reader_is() {
        // Codex review on codeyoma/FinceptTerminal#108: every poll emptied and refilled the table, which
        // threw a reader scrolled to older trades back to the newest, every 2 s with a position open.
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        screens::KrxTraderScreen screen;
        screen.resize(1600, 1000);
        screen.show();
        auto* table = screen.findChild<QTableWidget*>(screens::KrxTraderScreen::kTradesName);
        auto* live = screen.findChild<services::KrxTraderLive*>();
        QVERIFY(table && live);
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 40, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live->reading(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(table->verticalScrollBar()->maximum() > 0, 10000); // more rows than shown

        // The same rows: the table is left as it is, its items the same.
        const QTableWidgetItem* first = table->item(0, 0);
        const qsizetype trades = engine.count("/v1/trader/trades");
        live->read_now();
        QTRY_VERIFY_WITH_TIMEOUT(engine.count("/v1/trader/trades") > trades && !live->reading(), 10000);
        QCOMPARE(table->item(0, 0), first);

        // Scrolled down to older trades with one chosen; then the open trade's P&L moves (its row
        // changes) and a new trade comes in at the top.
        table->verticalScrollBar()->setValue(table->verticalScrollBar()->maximum());
        const QString top = key_at(table, table->rowAt(0));
        QVERIFY(!top.isEmpty() && top != key_at(table, 0));
        table->selectRow(table->rowCount() - 3);
        const QString chosen = key_at(table, table->rowCount() - 3);
        engine.unrealized = 1234.0;
        engine.newest = 41;
        live->read_now();
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 41, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live->reading(), 10000);
        QCOMPARE(key_at(table, table->rowAt(0)), top); // the same trade at the top of the view
        const QModelIndexList selected = table->selectionModel()->selectedRows();
        QCOMPARE(selected.size(), 1);
        QCOMPARE(key_at(table, selected.first().row()), chosen); // the same trade chosen
        QVERIFY(table->verticalScrollBar()->value() > 0);

        // Not scrolled: new trades come in at the top, in view.
        table->verticalScrollBar()->setValue(0);
        engine.newest = 42;
        live->read_now();
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 42, 10000);
        QCOMPARE(table->verticalScrollBar()->value(), 0);
        QCOMPARE(key_at(table, table->rowAt(0)), QStringLiteral("42"));
    }
};

QTEST_MAIN(TstKrxTraderScreen)
#include "tst_krx_trader_screen.moc"
