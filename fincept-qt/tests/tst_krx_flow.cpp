// Tests for the KRX flow screen's model (codeyoma/krx-quant#306): how it reads GET
// /v1/symbols/{symbol}/flow (src/services/krx_quant/EngineData.h, parse_flow), what it draws and shows
// of it (src/services/krx_quant/FlowView.h) and when it reads it (src/services/krx_quant/KrxFlowLive.h),
// the last against a stand-in engine on this computer. The settings (the engine's address, the symbol
// chosen) go to a temporary INI file: AppConfig is defined below, in place of core/config/AppConfig.cpp.

#include "core/config/AppConfig.h"
#include "services/krx_quant/FlowView.h"
#include "services/krx_quant/KrxFlowLive.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
namespace view = fincept::krx_quant::flow;
using view::Series;

namespace {

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
}

/// A minute as the engine sends it; null values where not given.
QJsonObject minute(const char* at, QJsonValue price, QJsonValue program, QJsonValue member,
                   const char* program_source = "ws", const char* member_source = "ws") {
    return {{"at", QString::fromLatin1(at)},
            {"price", price},
            {"program_net_value", program},
            {"program_buy_value", program.isNull() ? QJsonValue() : QJsonValue(program.toDouble() + 1000)},
            {"program_sell_value", program.isNull() ? QJsonValue() : QJsonValue(1000)},
            {"program_net_qty", program.isNull() ? QJsonValue() : QJsonValue(program.toInt() / 100)},
            {"program_source", program.isNull() ? QJsonValue() : QJsonValue(QString::fromLatin1(program_source))},
            {"member_foreign_net_qty", member},
            {"member_source", member.isNull() ? QJsonValue() : QJsonValue(QString::fromLatin1(member_source))}};
}

QJsonObject flow_json(const QString& symbol = "005930", const QString& day = "2026-10-16") {
    const QJsonArray minutes{
        minute("2026-10-16T09:00:00+09:00", 81000, 700, -30),
        minute("2026-10-16T09:01:00+09:00", 81100, QJsonValue(), -50, "ws", "rest"),
        minute("2026-10-16T09:02:00+09:00", QJsonValue(), 900, QJsonValue(), "rest"),
        minute("2026-10-16T09:03:00+09:00", 81300, 1200, 20),
    };
    const QJsonObject members{
        {"received_at", "2026-10-16T09:03:00+09:00"},
        {"source", "ws"},
        {"sell", QJsonArray{QJsonObject{{"no", "005"}, {"name", "미래에셋"}, {"qty", 5015}, {"foreign", false}},
                            QJsonObject{{"no", "044"}, {"name", ""}, {"qty", QJsonValue()}, {"foreign", true}}}},
        {"buy", QJsonArray{QJsonObject{{"no", "040"}, {"name", "JP모간"}, {"qty", 4435}, {"foreign", true}}}}};
    const QJsonObject totals{{"program_net_value", 1200},
                             {"member_foreign_net_qty", 20},
                             {"estimate_foreign_net_qty", 1500},
                             {"estimate_institution_net_qty", -300}};
    QJsonObject previous = totals;
    previous["day"] = "2026-10-15";
    previous["at"] = "2026-10-15T09:03:00+09:00";
    previous["program_net_value"] = 2000;
    previous["member_foreign_net_qty"] = QJsonValue();
    return {{"symbol", symbol},
            {"day", day},
            {"as_of", "2026-10-16T09:03:30+09:00"},
            {"recorded", true},
            {"open", "2026-10-16T09:00:00+09:00"},
            {"close", "2026-10-16T15:30:00+09:00"},
            {"minutes", minutes},
            {"estimates", QJsonArray{QJsonObject{{"slot", "1"},
                                                 {"received_at", "2026-10-16T09:01:00+09:00"},
                                                 {"foreign_net_qty", 1200},
                                                 {"institution_net_qty", QJsonValue()}},
                                     QJsonObject{{"slot", "2"},
                                                 {"received_at", "2026-10-16T09:02:30+09:00"},
                                                 {"foreign_net_qty", 1500},
                                                 {"institution_net_qty", -300}}}},
            {"members", members},
            {"totals", totals},
            {"previous_day_same_time", previous},
            {"final", QJsonValue()},
            {"special", QJsonArray{QJsonObject{{"kind", "vi"},
                                               {"symbol", "005930"},
                                               {"start", "2026-10-16T09:01:10+09:00"},
                                               {"end", "2026-10-16T09:03:20+09:00"},
                                               {"detail", "dynamic"}}}}};
}

Flow parsed(const QJsonObject& body = flow_json()) {
    auto f = parse_flow(QJsonDocument(body));
    if (f.is_err())
        qFatal("%s", f.error().c_str());
    return f.value();
}

/// The engine as the flow screen reads it: GET /v1/symbols and …/flow, the paths asked kept.
class StandIn : public QObject {
  public:
    bool fail = false;
    bool no_symbols = false; ///< GET /v1/symbols answers an empty list
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
        int status = 404;
        QJsonObject body{{"error", "not found"}};
        if (path == "/v1/symbols" && no_symbols) {
            status = 200;
            body = {{"symbols", QJsonArray{}}};
        } else if (path == "/v1/symbols") {
            status = 200;
            body = {{"symbols", QJsonArray{QJsonObject{{"symbol", "005930"}, {"name", "삼성전자"}},
                                           QJsonObject{{"symbol", "000660"}, {"name", "SK하이닉스"}}}}};
        } else if (path.contains("/flow") && fail) {
            status = 500;
            body = {{"error", "made-up failure"}};
        } else if (path.contains("/flow")) {
            status = 200;
            const QString symbol = path.section('/', 3, 3);
            const QString day = path.contains("day=") ? path.section("day=", 1) : QStringLiteral("2026-10-16");
            body = flow_json(symbol, day);
        }
        const QByteArray bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                      "\r\nConnection: close\r\n\r\n" + bytes);
        socket->disconnectFromHost();
    }

    QTcpServer server_;
};

} // namespace

class TstKrxFlow : public QObject {
    Q_OBJECT

    QTemporaryDir settings_;

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        settings_file = settings_.filePath("settings.ini");
    }

    void the_flow_is_read_part_by_part() {
        const Flow f = parsed();
        QCOMPARE(f.symbol, QStringLiteral("005930"));
        QCOMPARE(f.day, QDate(2026, 10, 16));
        QVERIFY(f.recorded);
        QCOMPARE(f.minutes.size(), 4);
        QCOMPARE(*f.minutes[0].program_net_value, 700.0);
        QVERIFY(!f.minutes[1].program_net_value);
        QCOMPARE(f.minutes[1].member_source, QStringLiteral("rest"));
        QCOMPARE(f.minutes[2].program_source, QStringLiteral("rest"));
        QCOMPARE(f.estimates.size(), 2);
        QVERIFY(!f.estimates[0].institution_net_qty);
        QCOMPARE(f.members->sell.size(), 2);
        QVERIFY(!f.members->sell[1].qty);
        QCOMPARE(*f.totals.estimate_institution_net_qty, -300.0);
        QCOMPARE(f.previous_day_same_time->day, QDate(2026, 10, 15));
        QVERIFY(!f.previous_day_same_time->member_foreign_net_qty);
        QVERIFY(!f.final_known);
        QCOMPARE(f.special.size(), 1);
        QVERIFY(parse_flow(QJsonDocument::fromJson(R"({"error": "2026-10-17 is not a trading day"})")).is_err());
    }

    void a_minute_without_a_value_breaks_the_line_and_the_estimates_are_steps() {
        const Flow f = parsed();
        const auto program = view::segments(f, Series::Program, QDateTime());
        QCOMPARE(program.size(), 2); // 09:00, then 09:02–09:03 after the empty 09:01
        QCOMPARE(program[0].size(), 1);
        QCOMPARE(program[1].size(), 2);
        QCOMPARE(view::segments(f, Series::Price, QDateTime()).size(), 2); // no price at 09:02
        QCOMPARE(view::segments(f, Series::Member, QDateTime()).size(), 2);
        const auto foreign = view::segments(f, Series::EstimateForeign, kst("2026-10-16T09:10:00+09:00"));
        QCOMPARE(foreign.size(), 1);
        // 1,200 from 09:01, held to 09:02:30, then 1,500 to now (09:10).
        QCOMPARE(foreign[0].size(), 4);
        QCOMPARE(foreign[0][1].value, 1200.0);
        QCOMPARE(foreign[0][2].value, 1500.0);
        QCOMPARE(foreign[0].last().at, kst("2026-10-16T09:10:00+09:00").toSecsSinceEpoch());
        const auto institution = view::segments(f, Series::EstimateInstitution, QDateTime()); // a past day: the close
        QCOMPARE(institution[0].size(), 2);
        QCOMPARE(institution[0].last().at, f.close.toSecsSinceEpoch());
    }

    void the_hover_box_and_the_panels() {
        const Flow f = parsed();
        const QStringList at_0901 = view::hover_lines(f, 1);
        QCOMPARE(at_0901[0], QStringLiteral("09:01"));
        QCOMPARE(at_0901[2], QStringLiteral("프로그램 순매수 —"));
        QCOMPARE(at_0901[3], QStringLiteral("외국계 회원사 −50주 (REST)"));
        QCOMPARE(at_0901[4], QStringLiteral("추정 외국인 +1,200주 (09:01 입력)"));
        QCOMPARE(at_0901[5], QStringLiteral("추정 기관 —"));
        QCOMPARE(view::hover_lines(f, 2)[2], QStringLiteral("프로그램 순매수 +900 (REST)"));
        QVERIFY(view::hover_lines(f, 9).isEmpty());

        const auto totals = view::total_rows(f);
        QCOMPARE(totals[0].today, QStringLiteral("+1,200"));
        QCOMPARE(totals[0].previous, QStringLiteral("+2,000"));
        QCOMPARE(totals[1].previous, QStringLiteral("—"));
        QCOMPARE(totals[3].today, QStringLiteral("−300주"));
        QCOMPARE(view::previous_header(f), QStringLiteral("전일(10-15) 09:03까지"));
        QVERIFY(view::final_line(f).startsWith(QStringLiteral("확정 수급은 20:30 뒤에")));
        Flow settled = f;
        settled.final_known = true;
        settled.final_foreign_net = 1800;
        QVERIFY(view::final_line(settled).startsWith(QStringLiteral("확정 외국인 +1,800주 · 기관 —")));

        const auto sellers = view::member_rows(f.members->sell);
        QCOMPARE(sellers[0].qty, QStringLiteral("5,015주"));
        QCOMPARE(sellers[1].name, QStringLiteral("044")); // no name: the number
        QCOMPARE(sellers[1].qty, QStringLiteral("—"));
        QVERIFY(sellers[1].foreign);
        QCOMPARE(view::day_line(f), QStringLiteral("2026-10-16(금) · 마지막 기록 09:03 · REST 대체 09:01–09:03"));
        Flow silent = f;
        silent.recorded = false;
        QCOMPARE(view::day_line(silent), QStringLiteral("2026-10-16(금) · 녹화 없음"));
    }

    void it_reads_often_while_krx_trades_today_and_seldom_otherwise() {
        const Flow f = parsed();
        QCOMPARE(view::poll_ms(std::nullopt, kst("2026-10-16T20:00:00+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(f, kst("2026-10-16T08:59:00+09:00")), view::kSlowMs);
        QCOMPARE(view::poll_ms(f, kst("2026-10-16T09:00:00+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(f, kst("2026-10-16T15:39:59+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(f, kst("2026-10-16T15:40:00+09:00")), view::kSlowMs);
        QCOMPARE(view::poll_ms(f, kst("2026-10-19T10:00:00+09:00")), view::kSlowMs); // a past day shown
    }

    void the_screen_reads_the_symbols_then_the_chosen_one_s_flow() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        AppConfig::instance().remove(services::KrxFlowLive::kSymbolKey);
        services::KrxFlowLive live(nullptr, [] { return kst("2026-10-16T09:03:30+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.flow().has_value(), 10000);
        QCOMPARE(engine.asked.first(), QStringLiteral("/v1/symbols")); // which symbol, and the names
        QCOMPARE(live.symbol(), QStringLiteral("005930"));             // none chosen: the engine's first
        QVERIFY(engine.asked.contains("/v1/symbols/005930/flow"));
        QCOMPARE(live.symbols().size(), 2);
        QCOMPARE(live.next_ms(), view::kFastMs);

        live.set_symbol("000660");
        QVERIFY(!live.flow().has_value());
        QTRY_VERIFY_WITH_TIMEOUT(live.flow().has_value() && live.flow()->symbol == "000660", 10000);
        QCOMPARE(AppConfig::instance().get(services::KrxFlowLive::kSymbolKey).toString(), QStringLiteral("000660"));

        live.set_day(QDate(2026, 10, 15));
        QTRY_VERIFY_WITH_TIMEOUT(engine.asked.contains("/v1/symbols/000660/flow?day=2026-10-15"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(live.flow().has_value() && live.flow()->day == QDate(2026, 10, 15), 10000);

        engine.fail = true;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(!live.failure().isEmpty(), 10000);
        QVERIFY(live.flow().has_value()); // the last answer stays
        live.stop();
        QCOMPARE(live.next_ms(), 0);

        // The symbol kept is read first next time.
        services::KrxFlowLive again(nullptr, [] { return kst("2026-10-16T09:03:30+09:00"); });
        QCOMPARE(again.symbol(), QStringLiteral("000660"));
    }

    void an_engine_without_symbols_is_asked_again_on_schedule_not_at_once() {
        // An empty list answered at once must not be asked for again at once, in a loop.
        StandIn engine;
        engine.no_symbols = true;
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxFlowLive live(nullptr, [] { return kst("2026-10-16T09:03:30+09:00"); });
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(!live.failure().isEmpty(), 10000);
        QVERIFY(live.failure().contains(QStringLiteral("종목이 없습니다")));
        QTest::qWait(500);
        QCOMPARE(engine.asked, QStringList{"/v1/symbols"});
        QVERIFY(!live.flow().has_value());
        live.stop();
    }

    void without_an_address_it_says_where_to_set_it() {
        services::KrxQuantService::set_base_url(QString());
        services::KrxFlowLive live;
        live.start();
        QVERIFY(live.failure().contains(QStringLiteral("엔진 주소")));
        live.stop();
    }
};

QTEST_GUILESS_MAIN(TstKrxFlow)
#include "tst_krx_flow.moc"
