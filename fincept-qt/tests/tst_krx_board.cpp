// Unit tests for src/services/krx_quant/Board.h
//
// The KRX prediction screen shows each symbol's latest predictions and the final consensus
// on the chart, kept up to date from two sources: the engine's answers (read on connecting,
// reconnecting, and every few seconds without the stream) and its stream. They arrive in any
// order, so these tests pin what wins: a later bar close over an earlier one, a scored
// prediction over the same one unscored. They also pin what the chart marks, who the
// consensus members are, and what a final consensus pooled (for the signal alerts).

#include "services/krx_quant/Board.h"

#include <QTest>
#include <QTimeZone>

using namespace fincept::krx_quant;

namespace {

QDateTime at(const char* hhmm) {
    return QDateTime(QDate(2026, 10, 12), QTime::fromString(hhmm, "HH:mm"), QTimeZone::fromSecondsAheadOfUtc(9 * 3600));
}

Prediction made(const QString& symbol, const QString& algorithm, const char* bar_close, double up, double flat,
                double down) {
    Prediction p;
    p.symbol = symbol;
    p.algorithm = algorithm;
    p.model_version = algorithm + "-1";
    p.status = "ok";
    p.p_up = up;
    p.p_flat = flat;
    p.p_down = down;
    p.expected_ticks = 0.0;
    p.bar_close = at(bar_close);
    p.displayed_at = p.bar_close;
    p.target_at = p.bar_close.addSecs(240);
    return p;
}

Prediction not_made(const QString& symbol, const QString& algorithm, const char* bar_close, const QString& why) {
    Prediction p;
    p.symbol = symbol;
    p.algorithm = algorithm;
    p.status = why;
    p.bar_close = at(bar_close);
    p.displayed_at = p.bar_close;
    return p;
}

Prediction scored(Prediction p, const QString& class_a, const QString& class_b, bool special = false) {
    p.outcome = Outcome{81000.0, 81000.0, 81200.0, class_a, class_b, special, QString(), p.target_at};
    return p;
}

Latest latest_of(const QString& symbol, const char* bar_close, const QList<Prediction>& predictions) {
    return {symbol, at(bar_close), predictions};
}

Event event_of(Event::Type type, const QList<Prediction>& predictions, const QString& kind = QString()) {
    Event e;
    e.type = type;
    e.kind = kind;
    e.predictions = predictions;
    if (!predictions.isEmpty())
        e.bar_close = predictions.first().bar_close;
    return e;
}

Event lifecycle(const QString& algorithm, const QString& from, const QString& to) {
    Event e;
    e.type = Event::Type::Lifecycle;
    e.algorithm = algorithm;
    e.change = {at("15:40"), from, to, "review"};
    return e;
}

QStringList algorithms_of(const Latest& latest) {
    QStringList out;
    for (const auto& p : latest.predictions)
        out << p.algorithm;
    return out;
}

} // namespace

class TstKrxBoard : public QObject {
    Q_OBJECT

  private slots:
    void the_stream_adds_to_the_latest_bar_close_in_the_screens_order() {
        Board board;
        board.set_latest(latest_of("005930", "09:31", {made("005930", "baseline-flat", "09:31", 0, 1, 0)}));

        board.apply(event_of(Event::Type::Prediction, {made("005930", "alg-a", "09:31", 0.5, 0.3, 0.2)}));
        board.apply(
            event_of(Event::Type::Consensus, {made("005930", "consensus-fast", "09:31", 0.5, 0.3, 0.2)}, "fast"));

        const Latest latest = board.latest("005930");
        QCOMPARE(latest.bar_close, at("09:31"));
        QCOMPARE(algorithms_of(latest), (QStringList{"consensus-fast", "alg-a", "baseline-flat"}));
    }

    void a_later_bar_close_starts_afresh() {
        Board board;
        board.set_latest(latest_of("005930", "09:31", {made("005930", "alg-a", "09:31", 0.5, 0.3, 0.2)}));

        board.apply(event_of(Event::Type::Prediction, {made("005930", "baseline-flat", "09:32", 0, 1, 0)}));

        QCOMPARE(board.latest("005930").bar_close, at("09:32"));
        QCOMPARE(algorithms_of(board.latest("005930")), (QStringList{"baseline-flat"}));
    }

    void an_answer_read_before_the_stream_moved_on_does_not_take_the_screen_back() {
        Board board;
        board.apply(event_of(Event::Type::Prediction, {made("005930", "alg-a", "09:32", 0.5, 0.3, 0.2)}));

        board.set_latest(latest_of("005930", "09:31", {made("005930", "alg-a", "09:31", 0.2, 0.3, 0.5)}));

        QCOMPARE(board.latest("005930").bar_close, at("09:32"));
        QCOMPARE(board.latest("005930").predictions[0].p_up, std::optional<double>(0.5));
    }

    void a_prediction_already_scored_stays_scored() {
        Board board;
        const Prediction final = made("005930", "consensus-final", "09:31", 0.6, 0.3, 0.1);
        board.apply(event_of(Event::Type::Outcome, {scored(final, "up", "up")}));

        board.set_latest(latest_of("005930", "09:31", {final})); // read before it was scored
        board.set_history("005930", {final});

        QVERIFY(board.latest("005930").predictions[0].outcome.has_value());
        QCOMPARE(board.marks("005930", "b")[0].result, Mark::Result::Hit);
    }

    void each_symbol_is_kept_on_its_own() {
        Board board;
        board.apply(event_of(Event::Type::Prediction, {made("005930", "alg-a", "09:31", 0.5, 0.3, 0.2),
                                                       made("000660", "alg-a", "09:31", 0.1, 0.3, 0.6)}));

        QCOMPARE(board.latest("005930").predictions[0].p_up, std::optional<double>(0.5));
        QCOMPARE(board.latest("000660").predictions[0].p_down, std::optional<double>(0.6));
        QVERIFY(board.latest("009150").predictions.isEmpty()); // nothing yet
    }

    void the_final_consensus_comes_back_for_the_signals_the_fast_one_does_not() {
        Board board;
        const auto fast = board.apply(
            event_of(Event::Type::Consensus, {made("005930", "consensus-fast", "09:31", 0.7, 0.2, 0.1)}, "fast"));
        const auto final = board.apply(
            event_of(Event::Type::Consensus, {made("005930", "consensus-final", "09:31", 0.7, 0.2, 0.1)}, "final"));
        const auto others = board.apply(event_of(Event::Type::Prediction, {made("005930", "alg-a", "09:31", 1, 0, 0)}));

        QVERIFY(fast.isEmpty());
        QVERIFY(others.isEmpty());
        QCOMPARE(final.size(), 1);
        QCOMPARE(final[0].algorithm, QStringLiteral("consensus-final"));
    }

    void marks_say_what_the_final_consensus_predicted_and_whether_it_was_right() {
        Board board;
        board.set_history("005930",
                          {
                              scored(made("005930", "consensus-final", "09:31", 0.6, 0.3, 0.1), "up", "up"),
                              scored(made("005930", "consensus-final", "09:32", 0.1, 0.3, 0.6), "up", "flat"),
                              scored(made("005930", "consensus-final", "09:33", 0.2, 0.7, 0.1), "flat", ""),
                              scored(made("005930", "consensus-final", "09:34", 0.2, 0.7, 0.1), "flat", "flat", true),
                              made("005930", "consensus-final", "09:35", 0.3, 0.3, 0.4),
                              not_made("005930", "consensus-final", "09:36", "no_members"),
                          });

        const QList<Mark> marks = board.marks("005930", "b");

        QCOMPARE(marks.size(), 5); // none where there was no forecast
        QCOMPARE(marks[0].bar_close, at("09:31"));
        QCOMPARE(marks[0].predicted, Direction::Up);
        QCOMPARE(marks[0].probability, 0.6);
        QCOMPARE(marks[0].result, Mark::Result::Hit);
        QCOMPARE(marks[1].predicted, Direction::Down);
        QCOMPARE(marks[1].result, Mark::Result::Miss);
        QCOMPARE(marks[2].predicted, Direction::Flat);
        QCOMPARE(marks[2].result, Mark::Result::Unscorable); // not scored on basis B
        QCOMPARE(marks[3].result, Mark::Result::Hit);
        QVERIFY(marks[3].special);
        QCOMPARE(marks[4].result, Mark::Result::Pending);
    }

    void marks_follow_the_chosen_basis() {
        Board board;
        board.set_history("005930",
                          {scored(made("005930", "consensus-final", "09:32", 0.1, 0.3, 0.6), "down", "flat")});

        QCOMPARE(board.marks("005930", "a")[0].result, Mark::Result::Hit);
        QCOMPARE(board.marks("005930", "b")[0].result, Mark::Result::Miss);
    }

    void the_stream_scores_the_marks_as_it_goes() {
        Board board;
        const Prediction final = made("005930", "consensus-final", "09:31", 0.6, 0.3, 0.1);
        board.apply(event_of(Event::Type::Consensus, {final}, "final"));
        QCOMPARE(board.marks("005930", "b")[0].result, Mark::Result::Pending);

        board.apply(event_of(Event::Type::Outcome, {scored(final, "down", "down")}));

        QCOMPARE(board.marks("005930", "b").size(), 1);
        QCOMPARE(board.marks("005930", "b")[0].result, Mark::Result::Miss);
    }

    void the_members_follow_their_status_changes() {
        Board board;
        board.set_members({"alg-a", "alg-b"});

        board.apply(lifecycle("alg-b", "active", "bench"));
        board.apply(lifecycle("alg-c", "probation", "active"));
        board.apply(lifecycle("alg-d", "probation", "clone"));

        QCOMPARE(board.members(), (QStringList{"alg-a", "alg-c"}));
    }

    void a_final_consensus_pooled_the_active_members_forecasts_of_its_bar_close() {
        Board board;
        board.set_members({"alg-a", "alg-b", "alg-c"});
        board.set_latest(latest_of(
            "005930", "09:31",
            {made("005930", "consensus-final", "09:31", 0.7, 0.2, 0.1), made("005930", "alg-a", "09:31", 0.7, 0.2, 0.1),
             not_made("005930", "alg-b", "09:31", "timed_out"), made("005930", "alg-c", "09:31", 0.1, 0.2, 0.7),
             made("005930", "alg-d", "09:31", 0.7, 0.2, 0.1), // on probation
             made("005930", "baseline-flat", "09:31", 0, 1, 0)}));

        QStringList pooled;
        for (const auto& p : board.pooled("005930", at("09:31")))
            pooled << p.algorithm;

        QCOMPARE(pooled, (QStringList{"alg-a", "alg-c"}));
        QVERIFY(board.pooled("005930", at("09:30")).isEmpty()); // not the bar close shown
    }

    void a_bar_close_is_scored_four_minutes_later_unless_nothing_was_predicted() {
        const Latest latest =
            latest_of("005930", "09:31",
                      {not_made("005930", "alg-b", "09:31", "vi"), made("005930", "alg-a", "09:31", 0.5, 0.3, 0.2)});
        QCOMPARE(scoring_at(latest), at("09:35"));
        QVERIFY(!scoring_at(latest_of("005930", "09:31", {not_made("005930", "alg-b", "09:31", "vi")})).isValid());
    }

    void symbols_are_kept_as_given() {
        Board board;
        board.set_symbols({{"005930", 81250.0}, {"000660", std::nullopt}});
        QCOMPARE(board.symbols().size(), 2);
        QCOMPARE(board.symbols()[1].symbol, QStringLiteral("000660"));
    }
};

QTEST_GUILESS_MAIN(TstKrxBoard)
#include "tst_krx_board.moc"
