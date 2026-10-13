// Unit tests for src/services/krx_quant/Signals.h
//
// The KRX screen raises a desktop notification and a sound when a final consensus is sure
// enough of a move that enough of the active algorithms it pooled agree: the rule of the
// engine's Telegram signals (krx-quant docs/engine-api.md), with the threshold and N set on the
// screen. A move that keeps holding is raised once per 4 minutes, not every minute, and a
// consensus whose 4 minutes are over (read on opening the screen after the close) is not.

#include "services/krx_quant/Signals.h"

#include <QTest>
#include <QTimeZone>

using namespace fincept::krx_quant;

namespace {

QDateTime at(const char* hhmm) {
    return QDateTime(QDate(2026, 10, 12), QTime::fromString(hhmm, "HH:mm"), QTimeZone::fromSecondsAheadOfUtc(9 * 3600));
}

Prediction forecast(const QString& algorithm, const char* bar_close, double up, double flat, double down,
                    const QString& symbol = "005930") {
    Prediction p;
    p.symbol = symbol;
    p.algorithm = algorithm;
    p.status = "ok";
    p.p_up = up;
    p.p_flat = flat;
    p.p_down = down;
    p.bar_close = at(bar_close);
    p.target_at = p.bar_close.addSecs(240);
    return p;
}

/// Three members at ``bar_close``: two up, one down.
QList<Prediction> two_up_one_down(const char* bar_close, const QString& symbol = "005930") {
    return {forecast("alg-a", bar_close, 0.6, 0.3, 0.1, symbol), forecast("alg-b", bar_close, 0.5, 0.2, 0.3, symbol),
            forecast("alg-c", bar_close, 0.1, 0.3, 0.6, symbol)};
}

} // namespace

class TstKrxSignals : public QObject {
    Q_OBJECT

  private slots:
    void a_sure_consensus_with_enough_members_agreeing_is_a_signal() {
        const auto signal =
            judge(forecast("consensus-final", "09:31", 0.64, 0.26, 0.10), two_up_one_down("09:31"), SignalRule{});

        QVERIFY(signal.has_value());
        QCOMPARE(signal->direction, Direction::Up);
        QCOMPARE(signal->probability, 0.64);
        QCOMPARE(signal->agree, 2);
        QCOMPARE(signal->members, 3);
        QCOMPARE(signal->text(),
                 QStringLiteral("005930 상승 64% · 정식 3개 중 2개 같은 방향 · 09:31 봉 마감, 09:35까지"));
    }

    void down_likewise() {
        const QList<Prediction> members = {forecast("alg-a", "09:31", 0.1, 0.2, 0.7),
                                           forecast("alg-b", "09:31", 0.2, 0.2, 0.6)};
        const auto signal = judge(forecast("consensus-final", "09:31", 0.15, 0.2, 0.65), members, SignalRule{});

        QVERIFY(signal.has_value());
        QCOMPARE(signal->direction, Direction::Down);
        QVERIFY(signal->text().contains(QStringLiteral("하락 65%")));
    }

    void below_the_threshold_is_no_signal() {
        const auto consensus = forecast("consensus-final", "09:31", 0.59, 0.31, 0.10);
        QVERIFY(!judge(consensus, two_up_one_down("09:31"), SignalRule{0.6, 2}).has_value());
        QVERIFY(judge(consensus, two_up_one_down("09:31"), SignalRule{0.55, 2}).has_value());
    }

    void too_few_members_agreeing_is_no_signal() {
        const auto consensus = forecast("consensus-final", "09:31", 0.64, 0.26, 0.10);
        QVERIFY(!judge(consensus, two_up_one_down("09:31"), SignalRule{0.6, 3}).has_value());
        QVERIFY(!judge(consensus, QList<Prediction>{}, SignalRule{0.6, 1}).has_value()); // nobody pooled
    }

    void a_member_predicts_its_likeliest_class_a_tie_going_to_flat() {
        const QList<Prediction> members = {forecast("alg-a", "09:31", 0.6, 0.3, 0.1),
                                           forecast("alg-b", "09:31", 0.4, 0.4, 0.2)}; // flat, as the stats count it
        const auto consensus = forecast("consensus-final", "09:31", 0.64, 0.26, 0.10);
        QVERIFY(!judge(consensus, members, SignalRule{0.6, 2}).has_value());
    }

    void a_move_that_keeps_holding_is_raised_again_only_after_its_4_minutes() {
        Alerts alerts;
        const auto raise = [&alerts](const char* bar_close, const char* now) {
            return alerts.consider(forecast("consensus-final", bar_close, 0.64, 0.26, 0.10), two_up_one_down(bar_close),
                                   at(now));
        };

        QVERIFY(raise("09:31", "09:31").has_value());
        QVERIFY(!raise("09:32", "09:32").has_value());
        QVERIFY(!raise("09:34", "09:34").has_value());
        QVERIFY(raise("09:35", "09:35").has_value()); // the 09:31 signal's target time
    }

    void another_symbol_or_the_other_way_is_its_own_signal() {
        Alerts alerts;
        QVERIFY(alerts.consider(forecast("consensus-final", "09:31", 0.64, 0.26, 0.10), two_up_one_down("09:31"),
                                at("09:31")));
        QVERIFY(alerts.consider(forecast("consensus-final", "09:31", 0.64, 0.26, 0.10, "000660"),
                                two_up_one_down("09:31", "000660"), at("09:31")));
        const QList<Prediction> down = {forecast("alg-a", "09:32", 0.1, 0.2, 0.7),
                                        forecast("alg-b", "09:32", 0.2, 0.2, 0.6)};
        QVERIFY(alerts.consider(forecast("consensus-final", "09:32", 0.15, 0.2, 0.65), down, at("09:32")));
    }

    void a_consensus_read_twice_is_raised_once() {
        Alerts alerts;
        const auto consensus = forecast("consensus-final", "09:31", 0.64, 0.26, 0.10);
        QVERIFY(alerts.consider(consensus, two_up_one_down("09:31"), at("09:31")));
        // Its bar close again, from an answer read while the stream had already brought it.
        QVERIFY(!alerts.consider(consensus, two_up_one_down("09:31"), at("09:31")));
    }

    void a_consensus_whose_4_minutes_are_over_is_not_raised() {
        Alerts alerts;
        const auto consensus = forecast("consensus-final", "15:16", 0.64, 0.26, 0.10);
        QVERIFY(!alerts.consider(consensus, two_up_one_down("15:16"), at("15:20")));
    }

    void a_new_rule_applies_from_the_next_consensus() {
        Alerts alerts;
        alerts.set_rule({0.7, 2});
        QVERIFY(!alerts.consider(forecast("consensus-final", "09:31", 0.64, 0.26, 0.10), two_up_one_down("09:31"),
                                 at("09:31")));
        alerts.set_rule({0.6, 2});
        QVERIFY(alerts.consider(forecast("consensus-final", "09:32", 0.64, 0.26, 0.10), two_up_one_down("09:32"),
                                at("09:32")));
        QCOMPARE(alerts.rule().min_probability, 0.6);
    }

    void a_consensus_seen_before_its_members_are_known_is_judged_once_they_are() {
        // A final consensus always pools at least one member: none means they are not known yet
        // (being read again after a failure), not that nobody agreed.
        Alerts alerts;
        const auto consensus = forecast("consensus-final", "09:31", 0.64, 0.26, 0.10);
        QVERIFY(!alerts.consider(consensus, {}, at("09:31")));
        QVERIFY(alerts.consider(consensus, two_up_one_down("09:31"), at("09:32")));
    }

    void a_consensus_s_votes_judge_a_signal_as_its_members_would() {
        // As two up, one down (krx-quant #106): the counts the engine stored with it.
        auto consensus = forecast("consensus-final", "09:31", 0.64, 0.26, 0.10);
        const auto signal = judge(consensus, Votes{2, 0, 1}, SignalRule{});
        QVERIFY(signal.has_value());
        QCOMPARE(signal->direction, Direction::Up);
        QCOMPARE(signal->agree, 2);
        QCOMPARE(signal->members, 3);
        QVERIFY(!judge(consensus, Votes{2, 0, 1}, SignalRule{0.6, 3}).has_value());
        QVERIFY(!judge(consensus, Votes{0, 3, 0}, SignalRule{0.6, 1}).has_value()); // nobody up
        consensus.blocked = "special";
        QVERIFY(!judge(consensus, Votes{2, 0, 1}, SignalRule{}).has_value());
    }

    void a_consensus_the_engine_blocked_is_no_signal() {
        auto consensus = forecast("consensus-final", "09:31", 0.7, 0.2, 0.1);
        QVERIFY(judge(consensus, two_up_one_down("09:31"), SignalRule{}));
        for (const char* why : {"special", "no_reference", "expired"}) {
            consensus.blocked = why;
            QVERIFY(!judge(consensus, two_up_one_down("09:31"), SignalRule{}));
            Alerts alerts;
            QVERIFY(!alerts.consider(consensus, two_up_one_down("09:31"), at("09:31")));
        }
    }

    void a_consensus_not_made_is_no_signal() {
        Prediction none;
        none.symbol = "005930";
        none.algorithm = "consensus-final";
        none.status = "none_finished";
        none.bar_close = at("09:31");
        Alerts alerts;
        QVERIFY(!alerts.consider(none, {}, at("09:31")));
    }
};

QTEST_GUILESS_MAIN(TstKrxSignals)
#include "tst_krx_signals.moc"
