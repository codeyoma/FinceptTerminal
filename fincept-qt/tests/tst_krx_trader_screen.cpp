// Tests for the KRX auto trader screen itself (src/screens/krx_quant/KrxTraderScreen.h,
// codeyoma/krx-quant#397, #401) against a stand-in engine on this computer: what a poll does to its tables
// (krx_tables::refill: nothing when the rows are the same; the view's top row and the selected row kept
// when they change), that it has the two pause buttons and no resume button, the policy versions table
// with no challenger and with three, the live gate passed and not, and the trades of a policy chosen.
// The values are made up. Widgets and Qt Network; it runs on the offscreen platform too. The settings (the engine's address)
// go to a temporary INI file: AppConfig is defined below, in place of core/config/AppConfig.cpp.

#include "core/config/AppConfig.h"
#include "screens/krx_quant/KrxTraderScreen.h"
#include "services/krx_quant/KrxQuantService.h"
#include "services/krx_quant/KrxTraderLive.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
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

QJsonObject paper_json(int days, int trades, double net, QJsonValue low) {
    return {{"since", days > 0 ? QJsonValue("2026-10-08") : QJsonValue()},
            {"last", days > 0 ? QJsonValue("2026-10-14") : QJsonValue()},
            {"days", days},
            {"trades", trades},
            {"net", net},
            {"mean", days > 0 ? QJsonValue(net / days) : QJsonValue()},
            {"low", low}};
}

/// Policy ``version`` with ``status``, one cell, admitted unless it waits or is the first champion.
QJsonObject policy_json(int version, const char* status, QJsonObject paper) {
    const QString created = QStringLiteral("2026-10-%1T22:31:04+09:00").arg(6 + version, 2, 10, QChar('0'));
    const bool admitted = version > 1 && QLatin1String(status) != QLatin1String("waiting");
    return {{"version", version},
            {"status", status},
            {"created_at", created},
            {"reason", QStringLiteral("정책 %1의 밤 맞추기(지어낸 값)").arg(version)},
            {"cells", QJsonArray{QJsonObject{{"kind", "barrier"},
                                             {"role", "top"},
                                             {"predictor", "breiman"},
                                             {"threshold", 0.6},
                                             {"kelly_fraction", 0.5},
                                             {"fitted", QJsonObject{}}}}},
            {"left_out", QJsonArray{}},
            {"fit", QJsonObject{}},
            {"admitted_at", admitted ? QJsonValue(created) : QJsonValue()},
            {"dropped_at", QLatin1String(status) == QLatin1String("dropped") ? QJsonValue("2026-10-14T22:30:40+09:00")
                                                                             : QJsonValue()},
            {"paper", paper}};
}

/// A challenger's standing against champion 1: won (``passed``) or short of days.
QJsonObject promotion_json(int days, bool passed, int challengers) {
    return {{"champion", 1},
            {"days", days},
            {"required", 10},
            {"until", 20},
            {"mean", 500.0},
            {"low", passed ? QJsonValue(63.0) : QJsonValue()},
            {"level", 1 - 0.05 / challengers},
            {"challengers", challengers},
            {"passed", passed},
            {"why", passed ? QJsonValue() : QJsonValue("few_days")}};
}

/// The engine as the screen reads it: the trader running with trade ``newest`` open, its trades
/// ``newest`` down to 1, newest first, and its days; policy 1 the champion with ``challengers`` (0 or 3:
/// versions 2–4) and one waiting and one dropped with them, its gate passed or not; policy 2's own trades
/// for ``policy=2``; each request's path kept.
class StandIn : public QObject {
  public:
    int newest = 40;
    double unrealized = -5051.2;
    int challengers = 0;
    bool gate_passed = false;
    bool practising = false; ///< the mock practice active (codeyoma/krx-quant#427)
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
    QJsonObject gate() const {
        return gate_passed ? QJsonObject{{"policy", 1},     {"days", 21},     {"required", 20}, {"mean", 9300.0},
                                         {"low", 3930.0}, {"level", 0.95}, {"passed", true}}
                           : QJsonObject{{"policy", 1},      {"days", 12},     {"required", 20}, {"mean", 800.0},
                                         {"low", -3000.0}, {"level", 0.95}, {"passed", false}};
    }

    QJsonArray running() const {
        QJsonArray out{QJsonObject{{"version", 1}, {"status", "champion"}, {"paper", paper_json(12, 24, 9600.0, -3000.0)},
                                   {"promotion", QJsonValue()}}};
        for (int version = 2; version < 2 + challengers; ++version)
            out.append(QJsonObject{{"version", version},
                                   {"status", "challenger"},
                                   {"paper", paper_json(version + 9, version * 3, 1000.0 * version, 63.0)},
                                   {"promotion", promotion_json(version == 2 ? 11 : version, version == 2, challengers)}});
        return out;
    }

    QJsonObject policies() const {
        QJsonArray versions;
        if (challengers > 0) {
            versions.append(policy_json(2 + challengers, "waiting", paper_json(0, 0, 0.0, QJsonValue())));
            for (int version = 2 + challengers - 1; version >= 2; --version)
                versions.append(policy_json(version, "challenger", paper_json(version + 9, version * 3, 1000.0 * version, 63.0)));
        }
        versions.append(policy_json(1, "champion", paper_json(12, 24, 9600.0, -3000.0)));
        if (challengers > 0)
            versions.append(policy_json(2 + challengers + 1, "dropped", paper_json(20, 30, -9000.0, -1500.0)));
        return {{"champion", 1},
                {"policies", versions},
                {"champions", QJsonArray{QJsonObject{{"at", "2026-10-07T22:31:04+09:00"},
                                                     {"version", 1},
                                                     {"reason", "처음 맞춘 정책(#399)"},
                                                     {"detail", QJsonValue()}}}}};
    }

    QJsonObject state() const {
        QJsonObject o{{"updated_at", "2026-10-14T10:06:00+09:00"},
                {"champion", 1},
                {"gate", gate()},
                {"policies", running()},
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
        if (practising) { // codeyoma/krx-quant#427: the mock book practises, no champion cell to trade
            o["mode"] = "mock";
            o["practice"] = QJsonObject{{"enabled", true},
                                        {"active", true},
                                        {"cell", QJsonObject{{"kind", "barrier"},
                                                             {"role", "top"},
                                                             {"predictor", "breiman"},
                                                             {"p_star", 0.63},
                                                             {"low", 0.60},
                                                             {"quantity", 1}}}};
        }
        return o;
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
        } else if (route == "/v1/trader/trades" && path.contains("policy=2")) {
            status = 200;
            QJsonArray trades;
            for (int id = 103; id >= 101; --id) {
                QJsonObject t = trade_json(id, false);
                t["policy"] = 2;
                t["champion"] = false;
                trades.append(t);
            }
            body = {{"trades", trades}};
        } else if (route == "/v1/trader/trades") {
            status = 200;
            QJsonArray trades;
            for (int id = newest; id >= 1; --id)
                trades.append(trade_json(id, id == newest));
            body = {{"trades", trades}};
        } else if (route == "/v1/trader/policies") {
            status = 200;
            body = policies();
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

    void the_policy_table_with_no_challenger_and_with_three() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        screens::KrxTraderScreen screen;
        screen.resize(1600, 1000);
        screen.show();
        auto* table = screen.findChild<QTableWidget*>(screens::KrxTraderScreen::kPoliciesName);
        auto* history = screen.findChild<QTableWidget*>(screens::KrxTraderScreen::kHistoryName);
        auto* choice = screen.findChild<QComboBox*>(screens::KrxTraderScreen::kTradesPolicyName);
        auto* live = screen.findChild<services::KrxTraderLive*>();
        QVERIFY(table && history && choice && live);
        // No challenger: the champion alone, its first change, and the trades of it or the champion's.
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live->reading(), 10000);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("정책 1"));
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("챔피언"));
        QCOMPARE(table->item(0, 11)->text(), QStringLiteral("관문 12/20일 · 못 넘음"));
        QCOMPARE(history->rowCount(), 1);
        QCOMPARE(history->item(0, 2)->text(), QStringLiteral("처음 챔피언"));
        QCOMPARE(choice->count(), 2);
        QCOMPARE(choice->currentText(), QStringLiteral("챔피언의 매매"));

        // Three challengers, one waiting and one dropped: in the table's order, each challenger's standing.
        engine.challengers = 3;
        live->read_now();
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 6, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live->reading(), 10000);
        QStringList rows;
        for (int r = 0; r < table->rowCount(); ++r)
            rows << QStringLiteral("%1 %2").arg(table->item(r, 0)->text(), table->item(r, 1)->text());
        QCOMPARE(rows, (QStringList{"정책 1 챔피언", "정책 4 도전", "정책 3 도전", "정책 2 도전", "정책 5 대기", "정책 6 내려짐"}));
        QCOMPARE(table->item(3, 11)->text(), QStringLiteral("이김 · 함께 11/10일 · 차이 하한 +63원"));
        QCOMPARE(table->item(1, 11)->text(), QStringLiteral("함께 4/10일 · 날 수 부족"));
        QVERIFY(table->item(3, 11)->toolTip().contains(QStringLiteral("98.33%"))); // over three challengers
        QCOMPARE(history->rowCount(), 1 + 4 + 1); // the first champion, four slots given, one dropped
        QCOMPARE(choice->count(), 1 + 6);
        const auto labels = screen.findChildren<QLabel*>();
        QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel* l) {
            return l->text() == QStringLiteral("챔피언 정책 1 · 도전 3개 · 대기 1개 · 전 챔피언 0개 · 내려짐 1개");
        }));
    }

    void the_gate_passed_and_not_in_the_header_and_over_the_table() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        screens::KrxTraderScreen screen;
        screen.resize(1600, 1000);
        screen.show();
        auto* gate = screen.findChild<QLabel*>(screens::KrxTraderScreen::kGateName);
        auto* line = screen.findChild<QLabel*>(screens::KrxTraderScreen::kGateLineName);
        auto* live = screen.findChild<services::KrxTraderLive*>();
        QVERIFY(gate && line && live);
        // Not passed.
        QTRY_COMPARE_WITH_TIMEOUT(gate->text(), QStringLiteral("챔피언 정책 1 · 관문 모의 12/20일 · 하한 -0.3%"), 10000);
        QVERIFY(gate->isVisible());
        QVERIFY(!gate->styleSheet().contains(ui::colors::POSITIVE()));
        QVERIFY(line->text().startsWith(QStringLiteral("실전 관문 · 챔피언 정책 1의 모의 기록 12/20일")));
        QVERIFY(line->text().endsWith(QStringLiteral("못 넘음: 8일 더, 하한 ≤ 0")));
        // Passed: said so, in green.
        engine.gate_passed = true;
        live->read_now();
        QTRY_COMPARE_WITH_TIMEOUT(gate->text(), QStringLiteral("챔피언 정책 1 · 관문 모의 21/20일 · 하한 0.4% · 관문 넘음"), 10000);
        QVERIFY(gate->styleSheet().contains(ui::colors::POSITIVE()));
        QVERIFY(line->text().endsWith(QStringLiteral("넘음: 실전 모드는 서버에서 켭니다")));
        QVERIFY(line->styleSheet().contains(ui::colors::POSITIVE()));
        auto* table = screen.findChild<QTableWidget*>(screens::KrxTraderScreen::kPoliciesName);
        QTRY_COMPARE_WITH_TIMEOUT(table->item(0, 11)->text(), QStringLiteral("관문 넘음"), 10000);
    }

    void practising_shows_practice_beside_the_mode() {
        // codeyoma/krx-quant#427: "연습" beside the mode badge while the mock book practises, hidden else.
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        screens::KrxTraderScreen screen;
        screen.resize(1600, 1000);
        screen.show();
        auto* practice = screen.findChild<QLabel*>(screens::KrxTraderScreen::kPracticeName);
        auto* gate = screen.findChild<QLabel*>(screens::KrxTraderScreen::kGateName);
        auto* live = screen.findChild<services::KrxTraderLive*>();
        QVERIFY(practice && gate && live);
        QTRY_VERIFY_WITH_TIMEOUT(!gate->text().isEmpty(), 10000); // the state read
        QVERIFY(practice->isHidden());
        engine.practising = true;
        live->read_now();
        QTRY_VERIFY_WITH_TIMEOUT(practice->isVisible(), 10000);
        QCOMPARE(practice->text(), QStringLiteral("연습"));
        QVERIFY(practice->toolTip().contains(QStringLiteral("연습 칸 장벽 4분 · breiman (1위)을 1주씩")));
        engine.practising = false;
        live->read_now();
        QTRY_VERIFY_WITH_TIMEOUT(practice->isHidden(), 10000);
    }

    void choosing_a_policy_shows_its_trades() {
        StandIn engine;
        engine.challengers = 3;
        services::KrxQuantService::set_base_url(engine.url());
        screens::KrxTraderScreen screen;
        screen.resize(1600, 1000);
        screen.show();
        auto* trades = screen.findChild<QTableWidget*>(screens::KrxTraderScreen::kTradesName);
        auto* choice = screen.findChild<QComboBox*>(screens::KrxTraderScreen::kTradesPolicyName);
        auto* live = screen.findChild<services::KrxTraderLive*>();
        QVERIFY(trades && choice && live);
        QTRY_COMPARE_WITH_TIMEOUT(trades->rowCount(), 40, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(choice->count(), 7, 10000);
        const int two = choice->findText(QStringLiteral("정책 2 · 도전"));
        QVERIFY(two > 0);
        choice->setCurrentIndex(two);
        emit choice->activated(two); // as a click on it
        QTRY_COMPARE_WITH_TIMEOUT(trades->rowCount(), 3, 10000);
        QVERIFY(engine.asked.contains(QStringLiteral("/v1/trader/trades?limit=100&policy=2")));
        QCOMPARE(trades->item(0, 11)->text(), QStringLiteral("정책 2 · 도전"));
        QCOMPARE(choice->currentText(), QStringLiteral("정책 2 · 도전"));
        // A poll keeps the choice, and leaves the list as it is (an open one is not closed under the reader).
        QTRY_VERIFY_WITH_TIMEOUT(!live->reading(), 10000);
        const QSignalSpy removed(choice->model(), &QAbstractItemModel::rowsRemoved);
        const qsizetype polled = engine.count("/v1/trader/policies");
        live->read_now();
        QTRY_VERIFY_WITH_TIMEOUT(engine.count("/v1/trader/policies") > polled && !live->reading(), 10000);
        QCOMPARE(trades->rowCount(), 3);
        QCOMPARE(choice->currentText(), QStringLiteral("정책 2 · 도전"));
        QCOMPARE(removed.count(), 0);
        // Back to the champion's.
        emit choice->activated(0);
        QTRY_COMPARE_WITH_TIMEOUT(trades->rowCount(), 40, 10000);
        QCOMPARE(choice->currentText(), QStringLiteral("챔피언의 매매"));
    }
};

QTEST_MAIN(TstKrxTraderScreen)
#include "tst_krx_trader_screen.moc"
