// Unit tests for src/services/krx_quant/EngineCalendar.h
//
// With the KRX engine's address set, the dashboard's Economic Calendar reads the engine's
// GET /v1/calendar instead of Fincept's credit-charged endpoint (codeyoma/krx-quant#150). These
// tests pin the request and how the engine's answer becomes the widget's events.

#include "services/krx_quant/EngineCalendar.h"

#include <QJsonObject>
#include <QTest>

using namespace fincept::krx_quant;

namespace {

// The engine's answer, trimmed: built in code, since moc misreads quote characters inside raw
// strings.
QJsonDocument answer() {
    QJsonObject cpi{
        {"id", "2"},
        {"at", "2026-10-14T21:30:00+09:00"},
        {"country", "US"},
        {"title", "Inflation Rate YoY"},
        {"indicator", "Inflation Rate"},
        {"category", "prce"},
        {"period", "Sep"},
        {"actual", QJsonValue()},
        {"forecast", 3.7},
        {"previous", 3.40},
        {"unit", "%"},
        {"currency", "USD"},
        {"importance", "high"},
        {"source", "Bureau of Labour Statistics"},
        {"source_url", "https://www.bls.gov"},
    };
    QJsonObject jobs{
        {"id", "1"},
        {"at", "2026-10-13T08:00:00+09:00"},
        {"country", "KR"},
        {"title", "Unemployment Rate"},
        {"category", QJsonValue()},
        {"period", "Sep"},
        {"actual", 2.6},
        {"forecast", QJsonValue()},
        {"previous", 2.7},
        {"unit", QJsonValue()},
        {"importance", "medium"},
        {"source_url", QJsonValue()},
    };
    QJsonObject trade{
        {"id", "4"},        {"at", "2026-10-13T01:00:00Z"}, // in UTC: still shown in KST
        {"country", "CN"},  {"title", "Balance of Trade"},
        {"actual", 90.45},  {"forecast", -1.2},
        {"previous", -0.0}, {"unit", "CNY"},
        {"scale", "B"},     {"importance", "low"},
    };
    QJsonObject adp{
        {"id", "5"},
        {"at", "2026-10-15T21:15:00+09:00"},
        {"country", "US"},
        {"title", "ADP Employment Change"},
        {"forecast", -12.5},
        {"previous", 23.75},
        {"unit", "$"},
        {"scale", "K"},
        {"importance", "medium"},
    };
    QJsonObject broken{{"id", "9"}, {"at", "not a time"}, {"title", "Broken"}};
    QJsonObject untitled{{"id", "10"}, {"at", "2026-10-13T08:00:00+09:00"}};
    QJsonObject root{
        {"fetched_at", "2026-10-13T09:00:00+09:00"},
        {"source", "tradingview"},
        {"countries", QJsonArray{"US", "KR", "CN"}},
        {"events", QJsonArray{jobs, trade, cpi, adp, broken, untitled}},
    };
    return QJsonDocument(root);
}

} // namespace

class TestKrxEngineCalendar : public QObject {
    Q_OBJECT

  private slots:
    void the_request_asks_the_engine_for_the_widgets_rows() {
        QCOMPARE(calendar_url("http://krx-quant:8080"), QString("http://krx-quant:8080/v1/calendar?limit=25"));
        QCOMPARE(calendar_url(" http://127.0.0.1:8090// ", 5), QString("http://127.0.0.1:8090/v1/calendar?limit=5"));
    }

    void events_become_the_widgets_rows_in_kst() {
        const QJsonArray rows = calendar_widget_events(answer());
        QCOMPARE(rows.size(), 4); // a bad time and a missing title are left out
        const QJsonObject jobs = rows[0].toObject();
        QCOMPARE(jobs["event"].toString(), QString("Unemployment Rate"));
        QCOMPARE(jobs["country"].toString(), QString("KR"));
        QCOMPARE(jobs["date"].toString(), QString("2026-10-13"));
        QCOMPARE(jobs["time"].toString(), QString("08:00"));
        QCOMPARE(jobs["reference_period"].toString(), QString("Sep"));
        QCOMPARE(jobs["actual"].toString(), QString("2.6"));
        QCOMPARE(jobs["forecast"].toString(), QString());
        QCOMPARE(jobs["previous"].toString(), QString("2.7"));
        QCOMPARE(jobs["importance"].toInt(), 2);

        const QJsonObject trade = rows[1].toObject();
        QCOMPARE(trade["time"].toString(), QString("10:00")); // 01:00 UTC
        QCOMPARE(trade["actual"].toString(), QString("90.45B CNY"));
        QCOMPARE(trade["forecast"].toString(), QString("-1.2B CNY"));
        QCOMPARE(trade["previous"].toString(), QString("0B CNY"));
        QCOMPARE(trade["importance"].toInt(), 1);

        const QJsonObject cpi = rows[2].toObject();
        QCOMPARE(cpi["date"].toString(), QString("2026-10-14"));
        QCOMPARE(cpi["time"].toString(), QString("21:30"));
        QCOMPARE(cpi["actual"].toString(), QString()); // not out yet
        QCOMPARE(cpi["forecast"].toString(), QString("3.7%"));
        QCOMPARE(cpi["previous"].toString(), QString("3.4%"));
        QCOMPARE(cpi["consensus"].toString(), QString());
        QCOMPARE(cpi["importance"].toInt(), 3);
        QCOMPARE(cpi["category"].toString(), QString("prce"));
        QCOMPARE(cpi["url"].toString(), QString("https://www.bls.gov"));

        // A currency sign goes before the number, after the minus: thousands of dollars.
        const QJsonObject adp = rows[3].toObject();
        QCOMPARE(adp["previous"].toString(), QString("$23.75K"));
        QCOMPARE(adp["forecast"].toString(), QString("-$12.5K"));
    }

    void the_engine_request_carries_no_fincept_credentials() {
        // Null values: QNetworkRequest::setRawHeader removes a header set to a null value.
        const auto headers = engine_request_headers();
        QCOMPARE(headers.keys(), (QList<QByteArray>{"X-API-Key", "X-Session-Token"}));
        for (const QByteArray& value : headers)
            QVERIFY(value.isNull());
    }

    void an_answer_that_is_not_the_calendar_gives_nothing() {
        QVERIFY(calendar_widget_events(QJsonDocument()).isEmpty());
        QVERIFY(calendar_widget_events(QJsonDocument(QJsonObject{{"error", "x"}})).isEmpty());
        QVERIFY(calendar_widget_events(QJsonDocument(QJsonArray{1, 2})).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestKrxEngineCalendar)
#include "tst_krx_engine_calendar.moc"
