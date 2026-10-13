// fork addition (codeyoma/krx-quant#142): guard financial comparisons across source units.
#include "screens/equity_research/EquityValuationUnits.h"

#include <QtTest>

#include <limits>

using namespace fincept::screens::equity;

class TestEquityValuationUnits : public QObject {
    Q_OBJECT
  private slots:
    void matching_source_values_keep_the_original_margin() {
        const auto margin = valuation_margin(100.0, "USD", 120.0, "USD");
        QVERIFY(margin.has_value());
        QCOMPARE(*margin, 20.0);
    }

    void mixed_reporting_currency_cannot_produce_a_verdict() {
        // A USD quote and a KRW cash-flow valuation cannot be compared as 100 vs 168,000.
        QVERIFY(!valuation_margin(100.0, "USD", 168000.0, "KRW"));
        QVERIFY(!same_valuation_currency("USD", "KRW"));
        QVERIFY(!same_valuation_currency("KRW", "USD"));
    }

    void missing_metadata_is_not_a_matching_currency() {
        QVERIFY(!valuation_margin(100.0, "", 120.0, ""));
        QVERIFY(!valuation_margin(100.0, "USD", 120.0, ""));
        QVERIFY(!valuation_margin(100.0, "", 120.0, "USD"));
    }

    void pence_and_pounds_are_distinct_units() {
        QVERIFY(!valuation_margin(10000.0, "GBp", 120.0, "GBP"));
        QVERIFY(!same_valuation_currency("GBX", "GBP"));
        const auto pence = valuation_margin(10000.0, "GBp", 12000.0, "GBp");
        QVERIFY(pence.has_value());
        QCOMPARE(*pence, 20.0);
    }

    void pence_quote_does_not_imply_pence_market_cap() {
        QVERIFY(capitalization_currency("GBp").isEmpty());
        QVERIFY(capitalization_currency("GBX").isEmpty());
        QCOMPARE(capitalization_currency("GBP"), QString("GBP"));
        QCOMPARE(capitalization_currency("USD"), QString("USD"));
    }

    void unknown_quote_type_never_enables_conversion() {
        QVERIFY(!monetary_quote_type(resolved_quote_type("UNKNOWN")));
        QVERIFY(!monetary_quote_type(resolved_quote_type("")));
        QVERIFY(!monetary_quote_type("INDEX"));
        QVERIFY(!monetary_quote_type("CURRENCY"));
        QVERIFY(!monetary_quote_type("YIELD"));
        // ES=F and ZB=F report USD/FUTURE but prices are index/bond points.
        QVERIFY(!monetary_quote_type("FUTURE"));
        QVERIFY(monetary_quote_type(resolved_quote_type("UNKNOWN", "EQUITY")));
        QCOMPARE(resolved_quote_type("INDEX", "EQUITY"), QString("INDEX"));
    }

    void invalid_values_never_leave_a_valuation_verdict() {
        QVERIFY(!valuation_margin(0.0, "USD", 120.0, "USD"));
        QVERIFY(!valuation_margin(100.0, "USD", -1.0, "USD"));
        QVERIFY(!valuation_margin(std::numeric_limits<double>::quiet_NaN(), "USD", 120.0, "USD"));
        QVERIFY(!valuation_margin(100.0, "USD", std::numeric_limits<double>::infinity(), "USD"));
    }
};

QTEST_APPLESS_MAIN(TestEquityValuationUnits)
#include "tst_equity_valuation_units.moc"
