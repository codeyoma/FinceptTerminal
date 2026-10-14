// Unit tests for src/services/krx_quant/Board.h
//
// The KRX prediction screen shows each symbol's latest predictions and the final consensus
// on the chart, kept up to date from two sources: the engine's answers (read on connecting,
// reconnecting, and every few seconds without the stream) and its stream. They arrive in any
// order, so these tests pin what wins: a later bar close over an earlier one, a scored
// prediction over the same one unscored. They also pin what the chart marks (the final
// consensus, or the selected models' consensus, krx-quant #159, scored on the bar close's
// truth), who the consensus members are, and what a final consensus pooled (for the signal
// alerts). The expected means and counts are worked out by hand.

#include "services/krx_quant/Board.h"

#include <QTest>
#include <QTimeZone>

#include <algorithm>

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
    p.target_at = p.bar_close.addSecs(240);
    return p;
}

Prediction not_made(const QString& symbol, const QString& algorithm, const char* bar_close, const QString& why) {
    Prediction p;
    p.symbol = symbol;
    p.algorithm = algorithm;
    p.status = why;
    p.bar_close = at(bar_close);
    return p;
}

/// ``p`` scored: Direction::None where a basis could not be scored (its mid-point was missing).
Prediction scored(Prediction p, Direction class_a, Direction class_b, bool special = false) {
    const bool missing = class_a == Direction::None || class_b == Direction::None;
    p.outcome =
        Outcome{class_a,      class_b,      special,      missing ? QStringLiteral("missing mid: display") : QString(),
                std::nullopt, std::nullopt, std::nullopt, std::nullopt};
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
    e.change = {from, to};
    return e;
}

QStringList algorithms_of(const Latest& latest) {
    QStringList out;
    for (const auto& p : latest.predictions)
        out << p.algorithm;
    return out;
}

/// A range forecast (krx-quant #182) of ``algorithm`` at ``bar_close``: small, medium, large.
KindPrediction ranged(const QString& symbol, const QString& algorithm, const char* bar_close, double small,
                      double medium, double large) {
    KindPrediction p;
    p.symbol = symbol;
    p.algorithm = algorithm;
    p.model_version = algorithm + "-1";
    p.status = "ok";
    p.bar_close = at(bar_close);
    p.target_at = p.bar_close.addSecs(240);
    p.output = {{"p_small", small}, {"p_medium", medium}, {"p_large", large}, {"q10", 1}, {"q50", 2}, {"q90", 4}};
    return p;
}

KindAnswer range_answer(const QString& klass) {
    KindAnswer a;
    a.answer_version = 1;
    a.klass = klass;
    return a;
}

Event kind_event(Event::Type type, const QString& kind) {
    Event e;
    e.type = type;
    e.prediction_kind = kind;
    return e;
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
        board.apply(event_of(Event::Type::Outcome, {scored(final, Direction::Up, Direction::Up)}));

        board.set_latest(latest_of("005930", "09:31", {final})); // read before it was scored
        board.set_history("005930", {final});

        QVERIFY(board.latest("005930").predictions[0].outcome.has_value());
        QCOMPARE(board.marks("005930")[0].result, Mark::Result::Hit);
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
        board.set_history(
            "005930",
            {
                scored(made("005930", "consensus-final", "09:31", 0.6, 0.3, 0.1), Direction::Up, Direction::Up),
                scored(made("005930", "consensus-final", "09:32", 0.1, 0.3, 0.6), Direction::Up, Direction::Flat),
                scored(made("005930", "consensus-final", "09:33", 0.2, 0.7, 0.1), Direction::None, Direction::None),
                scored(made("005930", "consensus-final", "09:34", 0.2, 0.7, 0.1), Direction::Flat, Direction::Flat,
                       true),
                made("005930", "consensus-final", "09:35", 0.3, 0.3, 0.4),
                not_made("005930", "consensus-final", "09:36", "no_members"),
            });

        const QList<Mark> marks = board.marks("005930");

        QCOMPARE(marks.size(), 5); // none where there was no forecast
        QCOMPARE(marks[0].bar_close, at("09:31"));
        QCOMPARE(marks[0].predicted, Direction::Up);
        QCOMPARE(marks[0].probability, 0.6);
        QCOMPARE(marks[0].result, Mark::Result::Hit);
        QCOMPARE(marks[1].predicted, Direction::Down);
        QCOMPARE(marks[1].result, Mark::Result::Miss);
        QCOMPARE(marks[2].predicted, Direction::Flat);
        QCOMPARE(marks[2].result, Mark::Result::Unscorable);
        QCOMPARE(marks[2].unscorable, QStringLiteral("missing mid: display"));
        QCOMPARE(marks[3].result, Mark::Result::Hit);
        QVERIFY(marks[3].special);
        QCOMPARE(marks[4].result, Mark::Result::Pending);
        QVERIFY(!marks[0].selected);
    }

    void marks_score_on_the_bar_close_s_truth_basis_a() {
        // The chart no longer follows the leaderboard's basis (krx-quant #159): A, the same
        // for every predictor of the bar close.
        Board board;
        board.set_history("005930", {scored(made("005930", "consensus-final", "09:32", 0.1, 0.3, 0.6), Direction::Down,
                                            Direction::Flat)});

        QCOMPARE(board.marks("005930")[0].result, Mark::Result::Hit);
    }

    void marks_score_on_the_touch_label_when_the_engine_gives_one() {
        // krx-quant #170: the touch target decides, whatever the four-minute move (A) was.
        Prediction p = scored(made("005930", "consensus-final", "09:32", 0.1, 0.3, 0.6), Direction::Up, Direction::Up);
        p.outcome->touch = Touch{Direction::Down, {}, 79'000.0, 79'200.0, 78'800.0, std::nullopt, std::nullopt};
        Prediction silent =
            scored(made("005930", "consensus-final", "09:33", 0.1, 0.3, 0.6), Direction::Up, Direction::Up);
        silent.outcome->touch =
            Touch{Direction::None, QStringLiteral("silent quotes"), 79'000.0, std::nullopt, std::nullopt, std::nullopt,
                  std::nullopt};
        Board board;
        board.set_history("005930", {p, silent});

        const auto marks = board.marks("005930");
        QCOMPARE(marks[0].result, Mark::Result::Hit);
        QCOMPARE(marks[1].result, Mark::Result::Unscorable);
        QCOMPARE(truth_of({silent}, at("09:33")).unscorable, QStringLiteral("silent quotes"));
    }

    void the_final_consensus_is_scored_on_any_forecast_of_its_bar_close() {
        // Its own outcome not in yet (read before it was scored), a model's is.
        Board board;
        board.focus("005930");
        board.set_history("005930",
                          {made("005930", "consensus-final", "09:31", 0.6, 0.3, 0.1),
                           scored(made("005930", "gauss", "09:31", 0.2, 0.2, 0.6), Direction::Up, Direction::Up)});

        QCOMPARE(board.marks("005930")[0].result, Mark::Result::Hit);
    }

    void the_stream_scores_the_marks_as_it_goes() {
        Board board;
        const Prediction final = made("005930", "consensus-final", "09:31", 0.6, 0.3, 0.1);
        board.apply(event_of(Event::Type::Consensus, {final}, "final"));
        QCOMPARE(board.marks("005930")[0].result, Mark::Result::Pending);

        board.apply(event_of(Event::Type::Outcome, {scored(final, Direction::Down, Direction::Down)}));

        QCOMPARE(board.marks("005930").size(), 1);
        QCOMPARE(board.marks("005930")[0].result, Mark::Result::Miss);
    }

    void the_chosen_symbol_keeps_every_predictor_s_forecasts_another_only_its_final() {
        Board board;
        board.focus("005930");
        board.apply(event_of(Event::Type::Prediction, {made("005930", "gauss", "09:31", 0.6, 0.3, 0.1),
                                                       made("000660", "gauss", "09:31", 0.6, 0.3, 0.1)}));
        board.apply(event_of(Event::Type::Consensus,
                             {made("005930", "consensus-final", "09:31", 0.6, 0.3, 0.1),
                              made("000660", "consensus-final", "09:31", 0.6, 0.3, 0.1)},
                             "final"));

        QCOMPARE(board.closes("005930").value(at("09:31")).size(), 2);
        QCOMPARE(board.closes("000660").value(at("09:31")).size(), 1); // its final consensus alone
        QCOMPARE(board.closes("000660").value(at("09:31"))[0].algorithm, QStringLiteral("consensus-final"));
        QVERIFY(board.marks("000660", {"gauss"}).isEmpty()); // not the chosen symbol

        board.focus("000660"); // another chosen: the first one's models go
        QCOMPARE(board.closes("005930").value(at("09:31")).size(), 1);
    }

    void the_selected_models_consensus_is_their_mean_and_how_they_leaned() {
        Board board;
        board.focus("005930");
        board.set_history("005930", {made("005930", "gauss", "09:31", 0.6, 0.3, 0.1),
                                     made("005930", "markov", "09:31", 0.2, 0.5, 0.3),
                                     made("005930", "newton", "09:31", 0.7, 0.2, 0.1),
                                     not_made("005930", "turing", "09:31", "timed_out"),
                                     made("005930", "granger", "09:31", 0, 0, 1)}); // not selected

        const QList<Mark> marks = board.marks("005930", {"gauss", "markov", "newton", "turing"});

        // Up (0.6 + 0.2 + 0.7) / 3 = 0.5, flat 1.0 / 3, down 0.5 / 3; two lean up, one flat.
        QCOMPARE(marks.size(), 1);
        QVERIFY(marks[0].selected);
        QCOMPARE(marks[0].predicted, Direction::Up);
        QCOMPARE(marks[0].probability, 0.5);
        QCOMPARE(*marks[0].prediction.p_flat, (0.3 + 0.5 + 0.2) / 3);
        QCOMPARE(marks[0].prediction.votes->up, 2);
        QCOMPARE(marks[0].prediction.votes->flat, 1);
        QCOMPARE(marks[0].prediction.votes->down, 0);
        QCOMPARE(marks[0].made, 3);
        QCOMPARE(marks[0].chosen, 4);
        QCOMPARE(marks[0].prediction.target_at, at("09:35"));
    }

    void a_tie_goes_flat_and_the_mean_does_not_depend_on_the_order_they_came_in() {
        const auto first = made("005930", "gauss", "09:31", 0.5, 0.25, 0.25);
        const auto second = made("005930", "markov", "09:31", 0.25, 0.5, 0.25);
        Board forward, backward;
        forward.focus("005930");
        backward.focus("005930");
        forward.set_history("005930", {first, second});
        backward.set_history("005930", {second, first});

        const Mark a = forward.marks("005930", {"gauss", "markov"})[0];
        const Mark b = backward.marks("005930", {"markov", "gauss"})[0];

        QCOMPARE(a.prediction.p_up, b.prediction.p_up); // the same bits
        QCOMPARE(a.prediction.p_flat, b.prediction.p_flat);
        // Up and flat both 0.375: flat, as the engine scores a tie.
        QCOMPARE(*a.prediction.p_up, *a.prediction.p_flat);
        QCOMPARE(a.predicted, Direction::Flat);
    }

    void baselines_selected_are_left_out_of_the_selected_consensus() {
        // krx-quant #166: a baseline is 100% sure of one class; averaged in, it would swamp the models.
        Board board;
        board.focus("005930");
        board.set_history("005930", {made("005930", "gauss", "09:31", 0.5, 0.25, 0.25),
                                     made("005930", "markov", "09:31", 0.25, 0.5, 0.25),
                                     made("005930", "baseline-persist", "09:31", 1, 0, 0),
                                     made("005930", "baseline-flat", "09:31", 0, 1, 0)});

        const Mark m = board.marks("005930", {"baseline-flat", "baseline-persist", "gauss", "markov"})[0];

        QCOMPARE(*m.prediction.p_up, 0.375); // gauss and markov only
        QCOMPARE(m.prediction.votes->total(), 2);
        QCOMPARE(m.made, 2);
        QCOMPARE(m.chosen, 2);
    }

    void a_baseline_alone_draws_its_own_forecasts_several_draw_none() {
        Board board;
        board.focus("005930");
        board.set_history("005930", {made("005930", "baseline-persist", "09:31", 0, 0, 1),
                                     made("005930", "baseline-flat", "09:31", 0, 1, 0)});

        const QList<Mark> alone = board.marks("005930", {"baseline-persist"});
        QCOMPARE(alone.size(), 1);
        QCOMPARE(alone[0].predicted, Direction::Down);
        QCOMPARE(alone[0].prediction.algorithm, QStringLiteral("baseline-persist"));
        QVERIFY(!alone[0].prediction.votes.has_value()); // never emphasised
        QVERIFY(board.marks("005930", {"baseline-flat", "baseline-persist"}).isEmpty());
    }

    void a_model_arriving_later_moves_the_selected_consensus() {
        Board board;
        board.focus("005930");
        board.apply(event_of(Event::Type::Prediction, {made("005930", "gauss", "09:31", 0.6, 0.3, 0.1)}));
        QCOMPARE(board.marks("005930", {"gauss", "markov"})[0].predicted, Direction::Up);
        QCOMPARE(board.marks("005930", {"gauss", "markov"})[0].made, 1);

        board.apply(event_of(Event::Type::Prediction, {made("005930", "markov", "09:31", 0.0, 0.1, 0.9)}));

        // Up 0.3, flat 0.2, down 0.5.
        QCOMPARE(board.marks("005930", {"gauss", "markov"})[0].predicted, Direction::Down);
        QCOMPARE(board.marks("005930", {"gauss", "markov"})[0].made, 2);
    }

    void no_selected_forecast_no_mark() {
        Board board;
        board.focus("005930");
        board.set_history("005930", {not_made("005930", "turing", "09:31", "timed_out"),
                                     made("005930", "consensus-final", "09:31", 0.6, 0.3, 0.1)});

        QVERIFY(board.marks("005930", {"turing"}).isEmpty());
        QCOMPARE(board.marks("005930").size(), 1); // nothing selected: the final consensus
    }

    void the_selected_consensus_is_scored_and_blocked_as_its_bar_close() {
        Board board;
        board.focus("005930");
        Prediction final = made("005930", "consensus-final", "09:31", 0.6, 0.3, 0.1);
        final.blocked = "special";
        board.set_history("005930", {final, scored(made("005930", "gauss", "09:31", 0.1, 0.3, 0.6), Direction::Down,
                                                   Direction::Up, true)});

        const Mark m = board.marks("005930", {"gauss"})[0];

        QCOMPARE(m.result, Mark::Result::Hit); // down on basis A
        QVERIFY(m.special);
        QCOMPARE(m.prediction.blocked, QStringLiteral("special"));
    }

    void a_final_consensus_with_no_members_does_not_block_the_selected_one() {
        Board board;
        board.focus("005930");
        board.set_history("005930", {not_made("005930", "consensus-final", "09:31", "no_members"),
                                     made("005930", "gauss", "09:31", 0.7, 0.2, 0.1)});

        QVERIFY(board.marks("005930", {"gauss"})[0].prediction.blocked.isEmpty());
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

    void a_failed_read_forgets_the_predictions_not_who_the_members_are() {
        Board board;
        board.set_members({"alg-a", "alg-b"});
        board.set_symbols({{"005930", 81250.0}});
        board.set_latest(latest_of("005930", "09:31", {made("005930", "consensus-final", "09:31", 0.6, 0.3, 0.1)}));

        board.focus("005930");
        board.set_history("005930", {made("005930", "gauss", "09:31", 0.6, 0.3, 0.1)});
        board.forget_predictions();

        QVERIFY(board.symbols().isEmpty());
        QVERIFY(board.closes("005930").isEmpty());
        QVERIFY(board.latest("005930").predictions.isEmpty());
        QVERIFY(board.marks("005930").isEmpty());
        QCOMPARE(board.members(), (QStringList{"alg-a", "alg-b"})); // a signal still counts them
    }

    void an_engine_whose_clock_went_back_takes_the_screen_back_too() {
        // A demo engine started again: its 10:00 comes after the 10:30 already shown.
        Board board;
        board.focus("005930");
        board.set_history("005930", {made("005930", "consensus-final", "09:59", 0.6, 0.3, 0.1),
                                     made("005930", "consensus-final", "10:30", 0.6, 0.3, 0.1),
                                     made("005930", "gauss", "10:30", 0.6, 0.3, 0.1)});
        board.set_latest(latest_of("005930", "10:30", {made("005930", "alg-a", "10:30", 0.5, 0.3, 0.2)}));

        board.rewind_to(at("10:00"));

        QVERIFY(!board.latest("005930").bar_close.isValid());
        QCOMPARE(board.marks("005930").size(), 1);
        QCOMPARE(board.marks("005930")[0].bar_close, at("09:59"));
        QVERIFY(board.marks("005930", {"gauss"}).isEmpty()); // the models' later forecasts go too
        board.set_latest(latest_of("005930", "10:00", {made("005930", "alg-a", "10:00", 0.5, 0.3, 0.2)}));
        QCOMPARE(board.latest("005930").bar_close, at("10:00")); // the earlier bar close shows again
    }

    void a_kind_keeps_each_symbol_s_latest_bar_close_and_the_chosen_one_s_history() {
        KindBoard board;
        board.set_kind("range");
        board.focus("005930");
        board.set_history("005930", {ranged("005930", "alg-r", "09:30", 0.2, 0.3, 0.5),
                                     ranged("005930", "range-climate", "09:31", 0.3, 0.4, 0.3),
                                     ranged("005930", "alg-r", "09:31", 0.2, 0.3, 0.5)});
        board.set_latest({"000660", "range", at("09:31"), {ranged("000660", "alg-r", "09:31", 0.6, 0.3, 0.1)}});

        QCOMPARE(board.latest("005930").bar_close, at("09:31"));
        QCOMPARE(board.latest("005930").predictions.size(), 2);
        QCOMPARE(board.latest("000660").predictions[0].symbol, QStringLiteral("000660"));
        QCOMPARE(board.closes("005930").size(), 2);
        QCOMPARE(board.closes("005930").value(at("09:31")).size(), 2);
        QVERIFY(board.closes("000660").isEmpty()); // not the chosen symbol
        // An earlier bar close does not replace the later one shown.
        board.set_latest({"005930", "range", at("09:29"), {ranged("005930", "alg-r", "09:29", 0.2, 0.3, 0.5)}});
        QCOMPARE(board.latest("005930").bar_close, at("09:31"));
        // Another symbol chosen: the history goes, the latest stays.
        board.focus("000660");
        QVERIFY(board.closes("005930").isEmpty());
        QCOMPARE(board.latest("005930").bar_close, at("09:31"));
    }

    void an_answer_goes_to_every_prediction_of_its_bar_close_and_is_not_undone() {
        KindBoard board;
        board.set_kind("range");
        board.focus("005930");
        board.set_latest({"005930",
                          "range",
                          at("09:31"),
                          {ranged("005930", "alg-r", "09:31", 0.2, 0.3, 0.5),
                           ranged("005930", "range-climate", "09:31", 0.3, 0.4, 0.3)}});
        Event answered = kind_event(Event::Type::KindAnswer, "range");
        answered.kind_answers = {{"005930", at("09:31"), range_answer("large")}};

        QVERIFY(board.apply(answered));

        for (const auto& p : board.latest("005930").predictions)
            QCOMPARE(p.answer->klass, QStringLiteral("large"));
        for (const auto& p : board.closes("005930").value(at("09:31")))
            QCOMPARE(p.answer->klass, QStringLiteral("large"));
        // A read made before the answer, coming after it, keeps the answer.
        board.set_history("005930", {ranged("005930", "alg-r", "09:31", 0.2, 0.3, 0.5)});
        board.set_latest({"005930", "range", at("09:31"), {ranged("005930", "alg-r", "09:31", 0.2, 0.3, 0.5)}});
        QCOMPARE(board.closes("005930").value(at("09:31"))[0].answer->klass, QStringLiteral("large"));
        QCOMPARE(board.latest("005930").predictions[0].answer->klass, QStringLiteral("large"));
        // A prediction of that bar close read for the first time gets it too.
        board.set_history("005930", {ranged("005930", "range-persist", "09:31", 0, 0, 1)});
        const auto at_close = board.closes("005930").value(at("09:31"));
        QVERIFY(std::all_of(at_close.cbegin(), at_close.cend(), [](const KindPrediction& p) { return p.answer; }));
    }

    void the_stream_s_predictions_of_the_kind_shown_come_in_another_s_do_not() {
        KindBoard board;
        board.set_kind("range");
        board.focus("005930");
        Event predicted = kind_event(Event::Type::KindPrediction, "range");
        predicted.kind_predictions = {ranged("005930", "alg-r", "09:32", 0.2, 0.3, 0.5)};
        Event barrier = kind_event(Event::Type::KindPrediction, "barrier");
        barrier.kind_predictions = {ranged("005930", "alg-b", "09:33", 0.2, 0.3, 0.5)};

        QVERIFY(board.apply(predicted));
        QVERIFY(!board.apply(barrier));
        QVERIFY(!board.apply(event_of(Event::Type::Prediction, {made("005930", "alg-a", "09:34", 0.5, 0.3, 0.2)})));

        QCOMPARE(board.latest("005930").bar_close, at("09:32"));
        QCOMPARE(board.closes("005930").size(), 1);
        // The direction shown (no kind): nothing is taken.
        KindBoard direction;
        QVERIFY(!direction.apply(predicted));
        QVERIFY(direction.latest("005930").predictions.isEmpty());
        // A latest answer of another kind is passed over.
        board.set_latest({"005930", "barrier", at("09:40"), {ranged("005930", "alg-b", "09:40", 0.2, 0.3, 0.5)}});
        QCOMPARE(board.latest("005930").bar_close, at("09:32"));
    }

    void another_kind_or_a_rewound_engine_takes_the_kind_s_predictions_back() {
        KindBoard board;
        board.set_kind("range");
        board.focus("005930");
        board.set_history("005930", {ranged("005930", "alg-r", "09:59", 0.2, 0.3, 0.5),
                                     ranged("005930", "alg-r", "10:30", 0.2, 0.3, 0.5)});

        QVERIFY(board.rewind_to(at("10:00")));
        QVERIFY(!board.latest("005930").bar_close.isValid());
        QCOMPARE(board.closes("005930").keys(), (QList<QDateTime>{at("09:59")}));
        QVERIFY(!board.rewind_to(at("10:00"))); // nothing more

        board.set_kind("barrier");
        QVERIFY(board.closes("005930").isEmpty());
        QCOMPARE(board.kind(), QStringLiteral("barrier"));
    }

    void the_direction_s_board_passes_over_another_kind_s_events() {
        Board board;
        board.focus("005930");
        Event predicted = kind_event(Event::Type::KindPrediction, "range");
        predicted.kind_predictions = {ranged("005930", "alg-r", "09:32", 0.2, 0.3, 0.5)};
        QVERIFY(board.apply(predicted).isEmpty());
        QVERIFY(board.latest("005930").predictions.isEmpty());
        QVERIFY(board.closes("005930").isEmpty());
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
