// Tests for the KRX "할 일" screen's model (codeyoma/krx-quant#674, spec #671): how it reads GET /v1/action
// (src/services/krx_quant/EngineData.h, parse_action), what it shows of it and when it alerts
// (src/services/krx_quant/ActionView.h), and how it reads it (src/services/krx_quant/KrxActionLive.h)
// against a stand-in engine on this computer, an engine without the answer (404) included. The settings
// (the engine's address) go to a temporary INI file: AppConfig is defined below, in place of
// core/config/AppConfig.cpp. Every price and time is made up.

#include "core/config/AppConfig.h"
#include "services/krx_quant/ActionView.h"
#include "services/krx_quant/KrxActionLive.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

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
using namespace fincept::krx_quant;
namespace view = fincept::krx_quant::action;

namespace {

/// A buy as the engine answers it: bought at 10:10:02, sold by 10:14 at the latest.
const QByteArray kBuy = R"json({
  "as_of": "2026-10-14T10:10:30+09:00", "mode": "mock", "alive": true,
  "symbols": [{"symbol": "005930", "name": "삼성전자", "action": "buy", "why": "bought",
    "said": "자동 매매가 10:10:02에 70,100원에 샀습니다. 아래 가격에 닿거나 10:14이 되면 팝니다.",
    "since": "2026-10-14T10:10:02.500000+09:00", "until": "2026-10-14T10:11:02.500000+09:00",
    "plan": {"entry_price": 70100.0, "entry_at": "2026-10-14T10:10:02.500000+09:00", "target": 70700.0,
             "stop": 69300.0, "sell_by": "2026-10-14T10:14:00+09:00", "bid": 70000.0,
             "bid_at": "2026-10-14T10:10:30+09:00"},
    "exit": null,
    "record": {"kind": "barrier", "predictor": "breiman", "threshold": 0.6, "days": 20, "signals": 980,
               "bought": 943, "wins": 512, "per_signal_ticks": 0.31, "low_ticks": 0.15, "tick": 100.0,
               "per_signal_won": 31.0, "low_won": 15.0}}]
})json";

/// No cell: what the engine says while no signal beats its costs.
const QByteArray kWait = R"json({
  "as_of": "2026-10-14T10:00:00+09:00", "mode": "mock", "alive": true,
  "symbols": [{"symbol": "005930", "name": "삼성전자", "action": "wait", "why": "no_cell",
    "said": "비용을 빼고도 이익이 난다고 검증된 신호가 아직 없습니다. 엔진이 밤마다 다시 찾습니다.",
    "since": null, "until": null, "plan": null, "exit": null, "record": null}]
})json";

ActionBoard parsed(const QByteArray& json) {
    auto board = parse_action(QJsonDocument::fromJson(json));
    if (board.is_err())
        qFatal("%s", board.error().c_str());
    return board.value();
}

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODateWithMs);
}

/// The engine as the screen reads it: GET /v1/action, or a 404 for an engine before krx-quant #672.
class StandIn : public QObject {
  public:
    QByteArray action = kWait;
    bool served = true;
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

  private:
    void read(QTcpSocket* socket) {
        QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n"))
            return;
        const QString path = QString::fromLatin1(request.left(request.indexOf("\r\n")).split(' ').value(1));
        asked << path;
        QByteArray body = R"({"error": "not found"})";
        int status = 404;
        if (path == "/v1/action" && served) {
            body = action;
            status = 200;
        }
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) +
                      "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    QTcpServer server_;
};

} // namespace

class TstKrxAction : public QObject {
    Q_OBJECT

    QTemporaryDir settings_;

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        settings_file = settings_.filePath("settings.ini");
    }

    void the_answer_is_read_card_by_card() {
        const ActionBoard board = parsed(kBuy);
        QCOMPARE(board.mode, QStringLiteral("mock"));
        QVERIFY(board.alive);
        QCOMPARE(board.cards.size(), 1);
        const ActionCard& card = board.cards.first();
        QCOMPARE(card.action, QStringLiteral("buy"));
        QCOMPARE(card.why, QStringLiteral("bought"));
        QVERIFY(card.plan.has_value());
        QCOMPARE(card.plan->target, 70700.0);
        QCOMPARE(card.plan->stop, 69300.0);
        QCOMPARE(card.plan->sell_by, kst("2026-10-14T10:14:00+09:00"));
        QCOMPARE(card.plan->bid, std::optional<double>(70000.0));
        QVERIFY(!card.exit.has_value());
        QVERIFY(card.record.has_value());
        QCOMPARE(card.record->wins, std::optional<int>(512));
        QCOMPARE(card.record->low_won, std::optional<double>(15.0));

        const ActionBoard wait = parsed(kWait);
        QVERIFY(!wait.cards.first().plan.has_value());
        QVERIFY(!wait.cards.first().record.has_value());
        QVERIFY(!wait.cards.first().since.isValid());
        QVERIFY(parse_action(QJsonDocument::fromJson(R"({"error": "made-up"})")).is_err());
    }

    void a_buy_shows_its_plan_and_record_in_plain_words() {
        const auto text = view::card_text(parsed(kBuy).cards.first(), kst("2026-10-14T10:10:30+09:00"));
        QCOMPARE(text.word, QStringLiteral("buy"));
        QCOMPARE(text.tone, QStringLiteral("buy"));
        QCOMPARE(text.title, QStringLiteral("삼성전자 005930"));
        QCOMPARE(text.plan, (QStringList{QStringLiteral("이익 실현 70,700원"), QStringLiteral("손절 69,300원"),
                                         QStringLiteral("늦어도 10:14에 매도 (4분 남음)"),
                                         QStringLiteral("지금 70,000원 · 산 값 70,100원보다 1주당 −100원 (비용 전)")}));
        QCOMPARE(text.record,
                 QStringLiteral("지난 20일 이 신호대로 943번 샀다면 512번 이익 · 비용 빼고 1주당 평균 +31원 (나쁘게 봐도 +15원)"));
        QVERIFY(!text.record_tip.isEmpty());
    }

    void a_wait_without_a_cell_shows_its_line_alone() {
        const auto text = view::card_text(parsed(kWait).cards.first(), kst("2026-10-14T10:00:00+09:00"));
        QCOMPARE(text.word, QStringLiteral("wait"));
        QCOMPARE(text.tone, QStringLiteral("wait"));
        QVERIFY(text.line.startsWith(QStringLiteral("비용을 빼고도")));
        QVERIFY(text.plan.isEmpty());
        QVERIFY(text.record.isEmpty());
    }

    void a_sale_shows_what_it_sold_and_bought_at() {
        ActionCard card = parsed(kBuy).cards.first();
        card.action = "sell";
        card.why = "take_profit";
        card.exit = ActionExit{"take_profit", 70700.0, kst("2026-10-14T10:12:40+09:00")};
        const auto text = view::card_text(card, kst("2026-10-14T10:12:50+09:00"));
        QCOMPARE(text.tone, QStringLiteral("sell"));
        QCOMPARE(text.plan, (QStringList{QStringLiteral("판 값 70,700원 · 산 값 70,100원"),
                                         QStringLiteral("1주당 +600원 (비용 전)")}));
    }

    void the_countdown_goes_once_the_time_is_past() {
        ActionCard card = parsed(kBuy).cards.first();
        card.plan->bid.reset();
        QCOMPARE(view::plan_lines(card, kst("2026-10-14T10:13:30+09:00")).value(2),
                 QStringLiteral("늦어도 10:14에 매도 (30초 남음)"));
        QCOMPARE(view::plan_lines(card, kst("2026-10-14T10:14:00+09:00")).value(2), QStringLiteral("늦어도 10:14에 매도"));
        QCOMPARE(view::plan_lines(card, kst("2026-10-14T10:14:00+09:00")).value(3), QStringLiteral("산 값 70,100원"));
    }

    void the_mode_is_said_plainly() {
        ActionBoard board = parsed(kWait);
        QCOMPARE(view::mode_line(board), QStringLiteral("자동 매매: KIS 모의 계좌 · 진짜 돈이 아닙니다"));
        board.mode = "live";
        QCOMPARE(view::mode_line(board), QStringLiteral("자동 매매: 실전 계좌 · 진짜 돈입니다"));
        board.mode = "paper";
        QCOMPARE(view::mode_line(board), QStringLiteral("자동 매매: 모의 · 진짜 돈이 아닙니다"));
        board.alive = false;
        QCOMPARE(view::mode_line(board), QStringLiteral("자동 매매가 돌지 않습니다"));
    }

    void a_trade_s_buy_and_its_sale_alert_once_each_and_never_on_opening() {
        QMap<QString, QString> seen;
        const ActionBoard buy = parsed(kBuy);
        QVERIFY(view::alerts(buy, seen, true).isEmpty()); // the screen opened on a buy: not a new one

        seen.clear();
        QVERIFY(view::alerts(parsed(kWait), seen, true).isEmpty());
        const QStringList raised = view::alerts(buy, seen, false);
        QCOMPARE(raised.size(), 1);
        QVERIFY(raised.first().startsWith(QStringLiteral("buy 삼성전자 · 자동 매매가 10:10:02에")));
        QVERIFY(view::alerts(buy, seen, false).isEmpty()); // the same buy, read again

        ActionBoard selling = buy;
        selling.cards[0].action = "sell";
        selling.cards[0].since = QDateTime(); // a sale begun
        QCOMPARE(view::alerts(selling, seen, false).size(), 1);
        ActionBoard sold = selling;
        sold.cards[0].since = kst("2026-10-14T10:12:40+09:00"); // then done: the same sale
        QVERIFY(view::alerts(sold, seen, false).isEmpty());
        QVERIFY(view::alerts(parsed(kWait), seen, false).isEmpty());
    }

    void it_reads_every_second_while_busy_and_seldom_out_of_the_session() {
        QCOMPARE(view::poll_ms(parsed(kBuy), kst("2026-10-14T20:00:00+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(parsed(kWait), kst("2026-10-14T10:00:00+09:00")), view::kSessionMs);
        QCOMPARE(view::poll_ms(std::nullopt, kst("2026-10-14T08:54:59+09:00")), view::kSlowMs);
        QCOMPARE(view::poll_ms(parsed(kWait), kst("2026-10-17T10:00:00+09:00")), view::kSlowMs); // a Saturday
    }

    void the_screen_reads_the_cards_and_says_when_the_engine_has_none() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxActionLive live(nullptr, [] { return kst("2026-10-14T10:00:00+09:00"); });
        QSignalSpy ready(&live, &services::KrxActionLive::board_ready);
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.board().has_value(), 10000);
        QVERIFY(live.served());
        QVERIFY(engine.asked.contains("/v1/action"));
        QCOMPARE(ready.size(), 1);
        QCOMPARE(live.next_ms(), view::kSessionMs);

        engine.served = false; // an engine before krx-quant #672
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(!live.served(), 10000);
        QVERIFY(!live.board().has_value());
        QVERIFY(live.failure().isEmpty()); // not a failure: said on the screen
        live.stop();
        QCOMPARE(live.next_ms(), 0);
    }
};

QTEST_GUILESS_MAIN(TstKrxAction)
#include "tst_krx_action.moc"
