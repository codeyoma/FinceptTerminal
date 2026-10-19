// Tests for the KRX pre-open briefing screen's model (codeyoma/krx-quant#304): how it reads GET
// /v1/briefing (src/services/krx_quant/EngineData.h, parse_briefing), what it shows of it
// (src/services/krx_quant/BriefingView.h) and when it reads it (src/services/krx_quant/KrxBriefingLive.h),
// the last against a stand-in engine on this computer. The settings (the engine's address) go to a
// temporary INI file: AppConfig is defined below, in place of core/config/AppConfig.cpp.

#include "core/config/AppConfig.h"
#include "services/krx_quant/BriefingView.h"
#include "services/krx_quant/KrxBriefingLive.h"
#include "services/krx_quant/KrxQuantService.h"

#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

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
namespace view = fincept::krx_quant::briefing;

namespace {

/// A briefing as the engine answers it (the demo engine's morning, trimmed).
const QByteArray kBriefing = R"json({
  "day": "2026-10-19", "as_of": "2026-10-19T08:51:00+09:00", "phase": "opening_auction", "trading_day": true,
  "session": {"open": "2026-10-19T09:00:00+09:00", "close": "2026-10-19T15:30:00+09:00", "special": null},
  "previous_day": "2026-10-16",
  "overnight": {
    "macro": {"day": "2026-10-19", "decided_at": "2026-10-19T07:30:00+09:00", "regime": "risk_off",
              "reasons": ["SOX -1.4% (-1% 이하)"], "readings": [], "failed": {}, "skipped": []},
    "us": [{"indicator": "sox", "closes": [{"day": "2026-10-15", "close": 6120.0, "change_pct": null},
                                           {"day": "2026-10-16", "close": 6080.0, "change_pct": -0.6536}]},
           {"indicator": "vix", "closes": []}]
  },
  "night_futures": {"evening": "2026-10-16", "from": "2026-10-16T18:00:00+09:00", "to": "2026-10-17T06:00:00+09:00",
                    "recorded": true, "contract": "A01612", "last": 393.0, "change_pct": 0.77, "high": 395.5,
                    "low": 391.0, "volume": 1500, "last_trade_at": "2026-10-17T05:59:00+09:00",
                    "minutes": [{"at": "2026-10-16T18:00:00+09:00", "price": 391.0},
                                {"at": "2026-10-17T05:59:00+09:00", "price": 393.0}]},
  "symbols": [
    {"symbol": "005930", "previous_close": 81200,
     "after_hours": {"price": 81800, "at": "2026-10-16T19:59:00+09:00", "change_pct": 0.7389},
     "premarket": {"price": 81500.0, "at": "2026-10-19T08:45:00+09:00", "window": "08:40-08:50", "change_pct": 0.3695},
     "expected_open": {"price": 81600, "qty": 5000, "venue_code": "1", "at": "2026-10-19T08:59:50+09:00",
                       "change_pct": 0.4926, "final": false},
     "supply": {"day": "2026-10-16", "foreign_net": 129496, "institution_net": -138926, "days": 5,
                "foreign_net_sum": -255135.0, "institution_net_sum": null}},
    {"symbol": "000660", "previous_close": null, "after_hours": null, "premarket": null, "expected_open": null,
     "supply": {"day": "2026-10-16", "foreign_net": null, "institution_net": null, "days": 0,
                "foreign_net_sum": null, "institution_net_sum": null}}
  ],
  "open_to_close": {
    "kind": "open_to_close", "day": "2026-10-19", "predicted_at": "2026-10-19T08:50:00+09:00", "previous": false,
    "symbols": [{
      "symbol": "005930",
      "predictions": [
        {"algorithm": "cox", "model_version": "cox-20261016-3fa9c1", "bar_close": "2026-10-19T08:50:00+09:00",
         "displayed_at": "2026-10-19T08:50:00+09:00", "target_at": "2026-10-19T15:30:00+09:00", "status": "ok",
         "flat_ticks": null, "width_ticks": null, "anchor": null, "tick": null, "band": 0.0047,
         "output": {"p_up": 0.4, "p_flat": 0.35, "p_down": 0.25}, "answer": null},
        {"algorithm": "otc-flat", "model_version": "v1", "bar_close": "2026-10-19T08:50:00+09:00",
         "displayed_at": "2026-10-19T08:50:00+09:00", "target_at": "2026-10-19T15:30:00+09:00", "status": "ok",
         "flat_ticks": null, "width_ticks": null, "anchor": null, "tick": null, "band": 0.0047,
         "output": {"p_up": 0.0, "p_flat": 1.0, "p_down": 0.0}, "answer": null}],
      "inputs": [{"algorithm": "cox", "model_version": "cox-20261016-3fa9c1", "bias": {"up": -0.003, "down": -0.025},
                  "inputs": [{"name": "oc_1", "value": -0.81, "z": -0.85, "scores": {"up": 0.004, "down": -0.005}},
                             {"name": "gap_1", "value": -0.35, "z": -1.05, "scores": {"up": 0.022, "down": -0.011}},
                             {"name": "premarket_gap", "value": null, "z": null, "scores": {"up": 0.0, "down": 0.0}},
                             {"name": "weekday", "value": 0.0, "z": null, "scores": {"up": -0.003, "down": 0.005}}]}]
    }]
  },
  "schedule": {"events": [{"id": "x", "at": "2026-10-19T21:30:00+09:00", "country": "US", "title": "Retail Sales MoM",
                           "indicator": null, "category": null, "period": null, "actual": null, "forecast": 0.4,
                           "previous": 0.6, "unit": "%", "scale": null, "currency": null, "importance": "high",
                           "source": null, "source_url": null}],
               "option_expiry": true, "futures_expiry": false, "month_end": false, "month_start": true}
})json";

Briefing parsed() {
    auto b = parse_briefing(QJsonDocument::fromJson(kBriefing));
    if (b.is_err())
        qFatal("%s", b.error().c_str());
    return b.value();
}

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
}

/// The engine as the briefing reads it: GET /v1/briefing and /v1/symbols, the paths asked kept.
class StandIn : public QObject {
  public:
    QByteArray briefing = kBriefing;
    bool fail = false;
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
        if (path.startsWith("/v1/briefing") && !fail) {
            body = briefing;
            status = 200;
        } else if (path.startsWith("/v1/briefing")) {
            status = 500;
            body = R"({"error": "made-up failure"})";
        } else if (path == "/v1/symbols") {
            body = R"({"symbols": [{"symbol": "005930", "mid": 81200, "name": "삼성전자", "logo_url": ""}]})";
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

class TstKrxBriefing : public QObject {
    Q_OBJECT

    QTemporaryDir settings_;

  private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        settings_file = settings_.filePath("settings.ini");
    }

    void the_briefing_is_read_part_by_part() {
        const Briefing b = parsed();
        QCOMPARE(b.day, QDate(2026, 10, 19));
        QCOMPARE(b.phase, QStringLiteral("opening_auction"));
        QVERIFY(b.trading_day && b.special.isEmpty());
        QCOMPARE(b.previous_day, QDate(2026, 10, 16));
        QCOMPARE(b.macro.regime, QStringLiteral("risk_off"));
        QCOMPARE(b.us.size(), 2);
        QCOMPARE(b.us[0].closes.size(), 2);
        QVERIFY(!b.us[0].closes[0].change_pct);
        QVERIFY(b.night.recorded);
        QCOMPARE(b.night.contract, QStringLiteral("A01612"));
        QCOMPARE(*b.night.high, 395.5);
        QCOMPARE(b.night.minutes.size(), 2);
        const auto& samsung = b.symbols[0];
        QCOMPARE(*samsung.previous_close, 81200.0);
        QCOMPARE(samsung.premarket->window, QStringLiteral("08:40-08:50"));
        QCOMPARE(samsung.expected_open->price, 81600.0);
        QVERIFY(!samsung.expected_open->final);
        QVERIFY(!samsung.supply.institution_net_sum);
        const auto& empty = b.symbols[1];
        QVERIFY(!empty.previous_close && !empty.premarket && !empty.expected_open && !empty.after_hours);
        QVERIFY(b.open_to_close.has_value());
        const auto& day = b.open_to_close->symbols[0];
        QCOMPARE(day.predictions.size(), 2);
        QCOMPARE(day.predictions[0].symbol, QStringLiteral("005930"));
        QCOMPARE(*day.predictions[0].band, 0.0047);
        QCOMPARE(day.inputs[0].bias.value("down"), -0.025);
        QVERIFY(!day.inputs[0].inputs[2].value);
        QCOMPARE(b.events.size(), 1);
        QVERIFY(b.option_expiry && !b.futures_expiry && b.month_start);
    }

    void an_engine_error_is_a_failure() {
        QVERIFY(parse_briefing(QJsonDocument::fromJson(R"({"error": "day must be a date"})")).is_err());
    }

    void the_screen_s_lines() {
        const Briefing b = parsed();
        QCOMPARE(view::headline(b), QStringLiteral("2026-10-19(월) 장전 · 국면 위험회피"));
        const auto us = view::us_rows(b);
        QCOMPARE(us[0].name, QStringLiteral("SOX"));
        QCOMPARE(us[0].change, QStringLiteral("−0.65%"));
        QCOMPARE(us[0].days, QStringLiteral("— · −0.65"));
        QCOMPARE(us[1].last, QStringLiteral("—"));
        QVERIFY(view::night_line(b.night).text.startsWith(QStringLiteral("A01612 393.00 (+0.77%)")));
        NightSession silent;
        QCOMPARE(view::night_line(silent).text, QStringLiteral("녹화 없음"));

        const auto rows = view::symbol_rows(b);
        QCOMPARE(rows[0].premarket, QStringLiteral("81,500 (+0.37%)"));
        QCOMPARE(rows[0].expected, QStringLiteral("81,600 (+0.49%)"));
        QCOMPARE(rows[0].after_hours, QStringLiteral("81,800 (+0.74%)"));
        QCOMPARE(rows[0].foreign, QStringLiteral("+129,496주"));
        QCOMPARE(rows[0].institution, QStringLiteral("−138,926주"));
        QCOMPARE(rows[0].institution_sum, QStringLiteral("—"));
        QVERIFY(rows[0].premarket_tip.contains(QStringLiteral("08:40-08:50")));
        QCOMPARE(rows[1].premarket, QStringLiteral("—"));

        const auto forecasts = view::forecast_rows(b);
        QCOMPARE(forecasts.size(), 1); // the baselines are left out
        QCOMPARE(forecasts[0].up, QStringLiteral("40.0%"));
        QCOMPARE(forecasts[0].band, QStringLiteral("±0.47%"));
        QCOMPARE(forecasts[0].lean, Direction::Up);
        QVERIFY(forecasts[0].result.isEmpty());

        // A tie goes to flat, then up, as the engine scores it (most_likely); a forecast not made says why
        // in Korean.
        Briefing tied = b;
        auto& cox = tied.open_to_close->symbols[0].predictions[0];
        cox.output = {{"p_up", 0.4}, {"p_flat", 0.4}, {"p_down", 0.2}};
        QCOMPARE(view::forecast_rows(tied)[0].lean, Direction::Flat);
        cox.output = {{"p_up", 0.4}, {"p_flat", 0.2}, {"p_down", 0.4}};
        QCOMPARE(view::forecast_rows(tied)[0].lean, Direction::Up);
        cox.status = "timed_out";
        cox.output.clear();
        QCOMPARE(view::forecast_rows(tied)[0].status, status_label("timed_out"));
        QVERIFY(view::forecast_rows(tied)[0].status != QStringLiteral("timed_out"));

        const auto inputs = view::input_rows(*view::inputs_of(b, "005930"));
        QCOMPARE(inputs[0].key, QStringLiteral("gap_1")); // the largest part first
        QCOMPARE(inputs[0].lean, QStringLiteral("상승 쪽"));
        QCOMPARE(inputs[0].up, QStringLiteral("+0.022"));
        QCOMPARE(inputs[0].down, QStringLiteral("−0.011"));
        QCOMPARE(inputs.last().key, QStringLiteral("premarket_gap")); // no part: last
        QCOMPARE(inputs.last().value, QStringLiteral("없음"));
        QCOMPARE(inputs.last().lean, QStringLiteral("—"));
        QVERIFY(!view::inputs_of(b, "000660"));

        const auto events = view::event_rows(b);
        QCOMPARE(events[0].time, QStringLiteral("21:30"));
        QCOMPARE(events[0].forecast, QStringLiteral("0.4%"));
        QVERIFY(events[0].high);
        QCOMPARE(view::flags(b), (QStringList{QStringLiteral("옵션 만기일"), QStringLiteral("월초")}));
    }

    void a_closed_or_late_day_says_so() {
        Briefing b = parsed();
        b.trading_day = false;
        QVERIFY(view::headline(b).contains(QStringLiteral("(휴장)")));
        b = parsed();
        b.special = "exam_day";
        b.open = kst("2026-10-19T10:00:00+09:00");
        QVERIFY(view::headline(b).contains(QStringLiteral("수능일 10:00 개장")));
        QCOMPARE(view::flags(b).first(), QStringLiteral("수능일 10:00 개장"));
    }

    void it_reads_often_before_the_open_and_seldom_after() {
        const Briefing b = parsed(); // 2026-10-19, a trading day
        QCOMPARE(view::poll_ms(std::nullopt, kst("2026-10-19T12:00:00+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(b, kst("2026-10-19T06:59:00+09:00")), view::kSlowMs);
        QCOMPARE(view::poll_ms(b, kst("2026-10-19T07:00:00+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(b, kst("2026-10-19T09:09:59+09:00")), view::kFastMs);
        QCOMPARE(view::poll_ms(b, kst("2026-10-19T09:10:00+09:00")), view::kSlowMs);
        QCOMPARE(view::poll_ms(b, kst("2026-10-18T08:00:00+09:00")), view::kSlowMs); // another day shown
        Briefing closed = b;
        closed.trading_day = false;
        QCOMPARE(view::poll_ms(closed, kst("2026-10-19T08:00:00+09:00")), view::kSlowMs);
    }

    void the_screen_reads_the_day_and_the_names_and_again_on_its_schedule() {
        StandIn engine;
        services::KrxQuantService::set_base_url(engine.url());
        services::KrxBriefingLive live(nullptr, [] { return kst("2026-10-19T08:51:00+09:00"); });
        QSignalSpy changed(&live, &services::KrxBriefingLive::changed);
        live.start();
        QTRY_VERIFY_WITH_TIMEOUT(live.briefing().has_value(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!live.symbols().isEmpty(), 10000);
        QVERIFY(engine.asked.contains("/v1/briefing"));
        QVERIFY(engine.asked.contains("/v1/symbols"));
        QCOMPARE(live.symbols().value("005930").name, QStringLiteral("삼성전자"));
        QVERIFY(live.failure().isEmpty());
        QCOMPARE(live.next_ms(), view::kFastMs); // 08:51 on the day shown

        live.set_day(QDate(2026, 10, 16));
        QVERIFY(!live.briefing().has_value()); // the old day's is gone at once
        QTRY_VERIFY_WITH_TIMEOUT(engine.asked.contains("/v1/briefing?day=2026-10-16"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(live.briefing().has_value(), 10000);

        engine.fail = true;
        live.read_now();
        QTRY_VERIFY_WITH_TIMEOUT(!live.failure().isEmpty(), 10000);
        QVERIFY(live.briefing().has_value()); // the last answer stays on screen
        live.stop();
        QCOMPARE(live.next_ms(), 0);
    }

    void without_an_address_it_says_where_to_set_it() {
        services::KrxQuantService::set_base_url(QString());
        services::KrxBriefingLive live;
        live.start();
        QVERIFY(live.failure().contains(QStringLiteral("엔진 주소")));
        QVERIFY(!live.reading());
        live.stop();
    }
};

QTEST_GUILESS_MAIN(TstKrxBriefing)
#include "tst_krx_briefing.moc"
