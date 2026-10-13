// fork addition (codeyoma/krx-quant#142): reference-rate conversion, without network or settings.
#include "core/currency/fx/FxRates.h"

#include <QJsonDocument>
#include <QTest>

#include <limits>
using namespace fincept::currency;

class TstFxRates : public QObject {
    Q_OBJECT
    const QDate today{2026, 10, 13};
    QByteArray xml(const QByteArray& date = "2026-10-12") {
        return "<Envelope><Cube><Cube time='" + date +
               "'><Cube currency='USD' rate='1.2'/>"
               "<Cube currency='KRW' rate='1500'/><Cube currency='GBP' rate='0.8'/>"
               "<Cube currency='JPY' rate='160'/></Cube></Cube></Envelope>";
    }
  private slots:
    void quote_units_require_monetary_provenance() {
        QVERIFY(monetary_quote_type("EQUITY"));
        QVERIFY(monetary_quote_type("ETF"));
        QVERIFY(monetary_quote_type("CRYPTOCURRENCY"));
        for (const QString& type : {QString("INDEX"), QString("YIELD"), QString("CURRENCY"), QString("FUTURE"),
                                    QString("UNKNOWN"), QString()})
            QVERIFY2(!monetary_quote_type(type), qPrintable(type));
    }
    void cross_rates_and_signs() {
        const auto r = FxRates::from_ecb_xml(xml(), today);
        QVERIFY(r.has_value());
        const auto won = r->convert(100, "USD", "KRW", today);
        QVERIFY(won.available);
        QVERIFY(won.converted);
        QCOMPARE(won.value, 125000.0);
        QCOMPARE(won.currency, QString("KRW"));
        QCOMPARE(won.rate_date, QDate(2026, 10, 12));
        QCOMPARE(r->convert(-100, "USD", "KRW", today).value, -125000.0);
        QCOMPARE(r->convert(125000, "KRW", "USD", today).value, 100.0);
        QCOMPARE(r->convert(100, "EUR", "KRW", today).value, 150000.0);
        QCOMPARE(r->convert(0, "USD", "KRW", today).value, 0.0);
        // Display refresh always starts with the original amount.
        QCOMPARE(r->convert(100, "USD", "JPY", today).value, 100 * 160.0 / 1.2);
        QCOMPARE(r->convert(100, "USD", "KRW", today).value, won.value);
    }
    void subunits_are_not_pounds() {
        const auto r = FxRates::from_ecb_xml(xml(), today).value();
        QCOMPARE(r.convert(100, "GBp", "GBP", today).value, 1.0);
        QCOMPARE(r.convert(100, "GBX", "KRW", today).value, 1875.0);
        QCOMPARE(r.convert(100, "GBP", "KRW", today).value, 187500.0);
    }
    void unavailable_keeps_source() {
        const FxRates empty;
        auto same = empty.convert(100, "KRW", "KRW", today);
        QVERIFY(same.available);
        QVERIFY(!same.converted);
        QCOMPARE(same.value, 100.0);
        auto missing = empty.convert(100, "USD", "KRW", today);
        QVERIFY(!missing.available);
        QCOMPARE(missing.currency, QString("USD"));
        QCOMPARE(missing.value, 100.0);
        const auto r = FxRates::from_ecb_xml(xml(), today).value();
        for (const QString& source : {QString("USDT"), QString("USDC"), QString(), QString("XYZ")}) {
            auto v = r.convert(100, source, "KRW", today);
            QVERIFY(!v.available);
            QCOMPARE(v.value, 100.0);
            QCOMPARE(v.currency, source);
        }
        QVERIFY(!r.convert(std::numeric_limits<double>::infinity(), "USD", "KRW", today).available);
        QVERIFY(!r.convert(std::numeric_limits<double>::max(), "USD", "KRW", today).available);
    }
    void freshness_and_invalid_xml() {
        auto r = FxRates::from_ecb_xml(xml(), today).value();
        QVERIFY(r.usable(QDate(2026, 10, 19)));
        QVERIFY(!r.usable(QDate(2026, 10, 20)));
        QVERIFY(!r.convert(100, "USD", "KRW", QDate(2026, 10, 20)).available);
        QVERIFY(!FxRates::from_ecb_xml(xml("2026-10-14"), today));
        QVERIFY(!FxRates::from_ecb_xml(xml("2026-09-01"), today));
        for (const QByteArray& bad :
             {QByteArray("0"), QByteArray("-1"), QByteArray("nan"), QByteArray("inf"), QByteArray("no")}) {
            auto data = xml();
            data.replace("rate='1.2'", "rate='" + bad + "'");
            QVERIFY(!FxRates::from_ecb_xml(data, today));
        }
        auto duplicate = xml();
        duplicate.replace("currency='JPY'", "currency='USD'");
        QVERIFY(!FxRates::from_ecb_xml(duplicate, today));
        QVERIFY(!FxRates::from_ecb_xml(xml().chopped(7), today));
        QVERIFY(!FxRates::from_ecb_xml("<html>Service unavailable</html>", today));
        QVERIFY(!FxRates::from_ecb_xml(QByteArray(300000, 'x'), today));
    }
    void cache_round_trip_and_validation() {
        const auto r = FxRates::from_ecb_xml(xml(), today).value();
        auto cache = r.to_json();
        auto restored = FxRates::from_json(cache, today);
        QVERIFY(restored);
        QCOMPARE(restored->convert(100, "USD", "KRW", today).value, 125000.0);
        auto bad = cache;
        bad["version"] = 2;
        QVERIFY(!FxRates::from_json(bad, today));
        bad = cache;
        bad["source"] = "unknown";
        QVERIFY(!FxRates::from_json(bad, today));
        bad = cache;
        bad["date"] = "tomorrow";
        QVERIFY(!FxRates::from_json(bad, today));
        bad = cache;
        bad["rates"] = QJsonObject{{"EUR", 1}, {"KRW", 0}};
        QVERIFY(!FxRates::from_json(bad, today));
        QVERIFY(!FxRates::from_json(cache, QDate(2026, 10, 20)));
    }
    void formatting_uses_actual_currency() {
        auto r = FxRates::from_ecb_xml(xml(), today).value();
        QCOMPARE(format_display_amount(r.convert(100, "USD", "KRW", today)), QString::fromUtf8("₩125,000 KRW"));
        QCOMPARE(format_display_amount(r.convert(-100, "USD", "KRW", today)), QString::fromUtf8("-₩125,000 KRW"));
        QCOMPARE(format_display_amount(FxRates{}.convert(100, "USD", "KRW", today)), QString("$100.00 USD"));
        QCOMPARE(format_display_amount(r.convert(100, "USDT", "KRW", today)), QString("100.00 USDT"));
        QCOMPARE(format_display_amount(r.convert(100, "", "KRW", today)), QString("100.00 (native)"));
        QCOMPARE(format_display_amount(r.convert(100, "USD", "KRW", today), true), QString::fromUtf8("₩125K KRW"));
        QVERIFY(format_display_amount(r.convert(0.00001, "USD", "KRW", today)).contains("0.0125"));
    }
};
QTEST_GUILESS_MAIN(TstFxRates)
#include "tst_fx_rates.moc"
