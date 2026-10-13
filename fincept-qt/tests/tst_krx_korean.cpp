// Fork addition (codeyoma/krx-quant#132): exercise the Korean catalog compiled
// into the same resource path that LanguageManager loads in the application.

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTest>
#include <QTranslator>
#include <QXmlStreamReader>

namespace {
QStringList placeholders(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("%L?(?:[1-9][0-9]?|n)"));
    QStringList result;
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext())
        result.append(matches.next().captured());
    result.sort();
    return result;
}
} // namespace

class TstKrxKorean : public QObject {
    Q_OBJECT

  private slots:
    void packaged_catalog_loads() {
        QTranslator korean;
        QVERIFY(korean.load(QStringLiteral(":/i18n/fincept_ko_KR.qm")));
        QVERIFY(!korean.isEmpty());
    }

    void settings_and_navigation_resolve_in_their_runtime_contexts() {
        QTranslator korean;
        QVERIFY(korean.load(QStringLiteral(":/i18n/fincept_ko_KR.qm")));
        const QRegularExpression hangul(QStringLiteral("[가-힣]"));
        for (const char* source : {"LANGUAGE", "Interface language", "WINDOW BEHAVIOUR", "Display currency"}) {
            const QString translated = korean.translate("fincept::screens::GeneralSection", source);
            QVERIFY2(hangul.match(translated).hasMatch(), source);
        }
        // Runtime keys use QT_TRANSLATE_NOOP markers so lupdate preserves these translations.
        for (const char* source :
             {"SETTINGS",      "Search settings…", "SHELL",       "General",         "Appearance",
              "Notifications", "Keybindings",      "Voice",       "Logging",         "Developer",
              "PROFILE",       "Profiles",         "Credentials", "Security",        "Data Sources",
              "LLM Config",    "MCP Servers",      "Python Env",  "Storage & Cache", "Cloud Sync"}) {
            const QString translated = korean.translate("fincept::screens::SettingsScreen", source);
            QVERIFY2(hangul.match(translated).hasMatch(), source);
        }
    }

    void exchange_rate_status_resolves_in_runtime_context() {
        QTranslator korean;
        QVERIFY(korean.load(QStringLiteral(":/i18n/fincept_ko_KR.qm")));
        const QRegularExpression hangul(QStringLiteral("[가-힣]"));
        for (const char* source :
             {"Refresh exchange rates", "ECB daily reference rates — %1",
              "Reference rates unavailable. Amounts keep their original currency.", "Refreshing exchange rates…"}) {
            const auto translated = korean.translate("fincept::screens::GeneralSection", source);
            QVERIFY2(hangul.match(translated).hasMatch(), source);
            QCOMPARE(placeholders(translated), placeholders(QString::fromUtf8(source)));
        }
        QCOMPARE(korean.translate("Currency", "(native)"), QStringLiteral("(거래 통화)"));
    }

    void finance_terms_follow_korean_brokerage_usage() {
        // custom/korean-glossary.md: Korean HTS/MTS and DART wording, not literal translations.
        QTranslator korean;
        QVERIFY(korean.load(QStringLiteral(":/i18n/fincept_ko_KR.qm")));
        const struct {
            const char* context;
            const char* source;
            QString expected;
        } terms[] = {
            {"fincept::screens::EquityPeersTab", "P/E", QStringLiteral("PER")},
            {"fincept::screens::ReportsView", "UNREALIZED P&L", QStringLiteral("평가손익")},
            {"fincept::screens::PortfolioBlotter", "AVG COST", QStringLiteral("평균단가")},
            {"fincept::screens::PortfolioBlotter", "COST BASIS", QStringLiteral("매입금액")},
            {"fincept::screens::PortfolioBlotter", "MKT VAL", QStringLiteral("평가금액")},
            {"fincept::screens::EquityTradingScreen", "ACCOUNTS", QStringLiteral("계좌")},
            {"fincept::screens::EquityTradingScreen", "PAPER", QStringLiteral("모의투자")},
            {"fincept::screens::PerformanceRiskView", "SHARPE RATIO", QStringLiteral("샤프지수")},
            {"fincept::screens::crypto::CryptoBottomPanel", "FUNDING RATE", QStringLiteral("펀딩비")},
            {"fincept::ui::ToolBar", "Crypto Trading", QStringLiteral("가상자산 거래")},
        };
        for (const auto& term : terms)
            QCOMPARE(korean.translate(term.context, term.source), term.expected);
    }

    void every_existing_catalog_entry_survives_compilation_with_its_placeholders() {
        QTranslator korean;
        QVERIFY(korean.load(QStringLiteral(":/i18n/fincept_ko_KR.qm")));
        QFile reference(QStringLiteral(FINCEPT_REFERENCE_CATALOG));
        QVERIFY(reference.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&reference);
        QString context;
        int checked = 0;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            if (xml.name() == QLatin1String("name")) {
                context = xml.readElementText();
            } else if (xml.name() == QLatin1String("message")) {
                const bool plural = xml.attributes().value(QLatin1String("numerus")) == QLatin1String("yes");
                QString source;
                QString comment;
                while (xml.readNextStartElement()) {
                    if (xml.name() == QLatin1String("source"))
                        source = xml.readElementText();
                    else if (xml.name() == QLatin1String("comment"))
                        comment = xml.readElementText();
                    else
                        xml.skipCurrentElement();
                }
                const QByteArray context_utf8 = context.toUtf8();
                const QByteArray source_utf8 = source.toUtf8();
                const QByteArray comment_utf8 = comment.toUtf8();
                const QString translated = korean.translate(context_utf8.constData(), source_utf8.constData(),
                                                            comment_utf8.constData(), plural ? 2 : -1);
                const QByteArray description = (context + QStringLiteral(": ") + source).toUtf8();
                QVERIFY2(!translated.isEmpty(), description.constData());
                QVERIFY2(placeholders(translated) == placeholders(source), description.constData());
                ++checked;
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY(checked > 0);
    }

    void plural_counts_translate_and_english_fallback_remains_available() {
        QTranslator korean;
        QVERIFY(korean.load(QStringLiteral(":/i18n/fincept_ko_KR.qm")));
        QVERIFY(QCoreApplication::installTranslator(&korean));
        const QString one = QCoreApplication::translate("ModelChatPanel", "%n action(s)", nullptr, 1);
        const QString many = QCoreApplication::translate("ModelChatPanel", "%n action(s)", nullptr, 12);
        QCoreApplication::removeTranslator(&korean);
        QVERIFY(one.contains(QLatin1Char('1')));
        QVERIFY(many.contains(QStringLiteral("12")));
        QVERIFY(!one.contains(QStringLiteral("action(s)")));
        QVERIFY(!many.contains(QStringLiteral("%n")));
        QCOMPARE(QCoreApplication::translate("fincept::screens::GeneralSection", "Interface language"),
                 QStringLiteral("Interface language"));
    }
};

QTEST_GUILESS_MAIN(TstKrxKorean)
#include "tst_krx_korean.moc"
