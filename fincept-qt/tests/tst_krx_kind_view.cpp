// Unit tests for src/services/krx_quant/KindView.h
//
// The KRX screen shows another kind of prediction than the direction (krx-quant #182): a
// selector at its top, the kind's prediction panel, its hover boxes on the chart and its
// baselines' cards. These tests pin the selector's choices (greyed for an older engine and
// for a kind this screen cannot show yet), which kind shows for the choice kept, the range's
// panel rows, its hover lines and windows, and how its predictions are read back to the first
// bar shown. The values are made up and the expected ones worked out by hand.

#include "services/krx_quant/KindView.h"

#include <QTest>
#include <QTimeZone>

using namespace fincept::krx_quant;

namespace {

QDateTime at(const char* hhmm) {
    return QDateTime(QDate(2026, 10, 12), QTime::fromString(hhmm, "HH:mm"), QTimeZone::fromSecondsAheadOfUtc(9 * 3600));
}

QDateTime kst(const char* text) {
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
}

KindInfo direction_kind() {
    KindInfo k;
    k.name = "direction";
    k.title = QStringLiteral("방향");
    k.classes = {"up", "flat", "down"};
    k.class_titles = {QStringLiteral("상승"), QStringLiteral("보합"), QStringLiteral("하락")};
    k.action = {"up", "down"};
    k.answer_version = 2;
    return k;
}

KindInfo range_kind() {
    KindInfo k;
    k.name = "range";
    k.title = QStringLiteral("변동폭");
    k.classes = {"small", "medium", "large"};
    k.class_titles = {QStringLiteral("작음"), QStringLiteral("보통"), QStringLiteral("큼")};
    k.action = {"small", "large"};
    k.answer_version = 1;
    k.baselines = {{"range-climate", std::nullopt}, {"range-persist", std::nullopt}};
    return k;
}

/// A range forecast of ``algorithm`` at ``bar_close``: small, medium, large and q10, q50, q90.
KindPrediction forecast(const QString& algorithm, const char* bar_close, double small, double medium, double large,
                        double q10, double q50, double q90) {
    KindPrediction p;
    p.algorithm = algorithm;
    p.model_version = algorithm + "-1";
    p.status = "ok";
    p.symbol = "005930";
    p.bar_close = at(bar_close);
    p.target_at = p.bar_close.addSecs(240);
    p.output = {{"p_small", small}, {"p_medium", medium}, {"p_large", large}, {"q10", q10}, {"q50", q50}, {"q90", q90}};
    return p;
}

KindPrediction not_made(const QString& algorithm, const char* bar_close, const QString& why) {
    KindPrediction p;
    p.algorithm = algorithm;
    p.status = why;
    p.symbol = "005930";
    p.bar_close = at(bar_close);
    return p;
}

/// A range answer: r ticks, its class, at an anchor of 80,000 won with a 100-won tick and k 2;
/// the mid-point up to 80,300 at 09:33:10 and down to 79,900 at 09:31:40.
KindAnswer answer(double r, const QString& klass) {
    KindAnswer a;
    a.answer_version = 1;
    a.klass = klass;
    a.scored_at = at("09:35");
    RangePath path;
    path.r = r;
    path.k = 2;
    path.tick = 100;
    path.anchor = 80'000;
    path.max_mid = 80'300;
    path.max_mid_at = kst("2026-10-12T09:33:10+09:00");
    path.min_mid = 79'900;
    path.min_mid_at = kst("2026-10-12T09:31:40+09:00");
    path.up = 3;
    path.down = 1;
    path.quotes = 120;
    a.range = path;
    return a;
}

KindPrediction with(KindPrediction p, const KindAnswer& a) {
    p.answer = a;
    return p;
}

/// ``p`` with the bar close's k it came with (krx-quant #200).
KindPrediction with_k(KindPrediction p, int k) {
    p.flat_ticks = k;
    return p;
}

} // namespace

class TstKrxKindView : public QObject {
    Q_OBJECT

  private slots:
    void an_older_engine_offers_the_direction_alone() {
        // No GET /v1/kinds (an engine before krx-quant #179): the others greyed, in the screen's order.
        const auto choices = kind_choices(std::nullopt);
        QCOMPARE(choices.size(), 3);
        QCOMPARE(choices[0].name, QStringLiteral("direction"));
        QCOMPARE(choices[1].name, QStringLiteral("barrier"));
        QCOMPARE(choices[2].name, QStringLiteral("range"));
        QCOMPARE(choices[0].title, QStringLiteral("방향"));
        QCOMPARE(choices[1].title, QStringLiteral("장벽"));
        QCOMPARE(choices[2].title, QStringLiteral("변동폭"));
        QVERIFY(choices[0].enabled);
        QVERIFY(!choices[1].enabled);
        QVERIFY(!choices[2].enabled);
        QCOMPARE(choices[2].tip, QStringLiteral("엔진이 이 종류를 예측하지 않는다(옛 엔진이거나 아직 없는 종류)."));
    }

    void an_engine_that_makes_the_range_offers_it_and_greys_the_barrier() {
        const auto choices = kind_choices(QList<KindInfo>{direction_kind(), range_kind()});
        QVERIFY(choices[0].enabled);
        QVERIFY(!choices[1].enabled); // the engine makes no barrier yet
        QVERIFY(choices[2].enabled);
        QCOMPARE(choices[0].tip, QStringLiteral("상승·보합·하락의 확률"));
        QVERIFY(choices[2].tip.startsWith(QStringLiteral("작음·보통·큼의 확률\n예측 패널, 순위표, 상세 통계 창이")));
    }

    void a_barrier_the_engine_makes_stays_greyed_until_this_screen_shows_it() {
        KindInfo barrier;
        barrier.name = "barrier";
        barrier.title = QStringLiteral("장벽(엔진)");
        const auto choices = kind_choices(QList<KindInfo>{direction_kind(), range_kind(), barrier});
        QCOMPARE(choices[1].title, QStringLiteral("장벽(엔진)")); // the engine's title
        QVERIFY(!choices[1].enabled);
        QCOMPARE(choices[1].tip, QStringLiteral("이 화면은 아직 이 종류를 보이지 못한다."));
    }

    void the_kind_kept_shows_while_it_can_and_stays_kept_while_it_cannot() {
        const auto now = kind_choices(QList<KindInfo>{direction_kind(), range_kind()});
        const auto older = kind_choices(std::nullopt);
        QCOMPARE(shown_kind("range", now), QStringLiteral("range"));
        QCOMPARE(shown_kind("range", older), QStringLiteral("direction")); // the setting itself is not touched
        QCOMPARE(shown_kind("barrier", now), QStringLiteral("direction"));
        QCOMPARE(shown_kind("direction", now), QStringLiteral("direction"));
        QCOMPARE(shown_kind("", now), QStringLiteral("direction")); // never chosen
        QCOMPARE(shown_kind("weather", now), QStringLiteral("direction"));
    }

    void a_kind_is_found_by_its_name_and_knows_its_baselines() {
        const QList<KindInfo> kinds{direction_kind(), range_kind()};
        QCOMPARE(kind_named(kinds, "range")->title, QStringLiteral("변동폭"));
        QVERIFY(!kind_named(kinds, "barrier").has_value());
        QVERIFY(!kind_named(std::nullopt, "direction").has_value());
        QVERIFY(is_kind_baseline(range_kind(), "range-persist"));
        QVERIFY(!is_kind_baseline(range_kind(), "alg-r"));
    }

    void a_kind_s_baseline_is_described_by_its_card() {
        KindInfo range = range_kind();
        range.baselines[1].card =
            AlgorithmCard{{}, QStringLiteral("기준 예측: 지어낸 성격"), QStringLiteral("지어낸 로직"),
                          {}, QStringLiteral("배우지 않는다"),          {},
                          {}};
        const auto info = kind_baseline_info(QList<KindInfo>{direction_kind(), range}, "range-persist");
        QVERIFY(info.has_value());
        QCOMPARE(info->kind, QStringLiteral("baseline"));
        QCOMPARE(info->prediction_kind, QStringLiteral("range"));
        QCOMPARE(info->card->character, QStringLiteral("기준 예측: 지어낸 성격"));
        QVERIFY(!kind_baseline_info(QList<KindInfo>{range}, "alg-r").has_value());
        QVERIFY(!kind_baseline_info(std::nullopt, "range-persist").has_value());
    }

    void a_kind_s_class_is_the_likeliest_a_tie_going_to_the_middle_then_the_first() {
        const KindInfo range = range_kind();
        QCOMPARE(likeliest_class(forecast("a", "09:31", 0.2, 0.3, 0.5, 0, 0, 0), range), 2);
        QCOMPARE(likeliest_class(forecast("a", "09:31", 0.4, 0.4, 0.2, 0, 0, 0), range), 1); // tie: medium
        QCOMPARE(likeliest_class(forecast("a", "09:31", 0.4, 0.2, 0.4, 0, 0, 0), range), 0); // tie: small
        QCOMPARE(likeliest_class(not_made("a", "09:31", "vi"), range), -1);
        KindPrediction partial = forecast("a", "09:31", 0.2, 0.3, 0.5, 0, 0, 0);
        partial.output.remove("p_medium");
        QCOMPARE(likeliest_class(partial, range), -1);
    }

    void ticks_read_whole_or_with_one_decimal() {
        QCOMPARE(ticks_text(2), QStringLiteral("2"));
        QCOMPARE(ticks_text(4.5), QStringLiteral("4.5"));
        QCOMPARE(ticks_text(2.35), QStringLiteral("2.4")); // an interpolated quantile
        QCOMPARE(ticks_text(0), QStringLiteral("0"));
    }

    void k_is_the_answer_s_once_in_else_the_predictions_bar_close_s_else_now_s() {
        // The 09:31 predictions came with k 2 (krx-quant #200: the bar close at 86,950 won);
        // the price has since moved to 87,150, where GET /v1/symbols' k is 3. Their small,
        // medium and large were made against 2 and 4: the panel shows k 2, not 3.
        const QList<KindPrediction> pending{with_k(forecast("a", "09:31", 0.2, 0.3, 0.5, 1, 2, 4), 2),
                                            with_k(not_made("range-persist", "09:31", "no_previous"), 2)};
        QCOMPARE(bar_close_k(pending, 3), std::optional<int>(2));
        QCOMPARE(bar_close_k(pending, std::nullopt), std::optional<int>(2)); // a pinned bar close
        QCOMPARE(widths_text(bar_close_k(pending, 3)), QStringLiteral("k 2틱 · 2k 4틱"));
        // A row without it (not made, or stored before #200) does not hide the others'.
        QCOMPARE(bar_close_k({not_made("range-persist", "09:31", "vi"), pending[0]}, 3), std::optional<int>(2));
        // Once the answer is in, its k (made up here to differ from the predictions', to pin which wins).
        QCOMPARE(bar_close_k({with(with_k(pending[0], 5), answer(4, "large"))}, 3), std::optional<int>(2));
        // An engine before #200: no k with the predictions; the symbols' k now, until the answer.
        const QList<KindPrediction> older{forecast("a", "09:31", 0.2, 0.3, 0.5, 1, 2, 4)};
        QCOMPARE(bar_close_k(older, 3), std::optional<int>(3));
        QCOMPARE(bar_close_k({with(older[0], answer(4, "large"))}, 3), std::optional<int>(2));
        QVERIFY(!bar_close_k(older, std::nullopt).has_value());
        QCOMPARE(widths_text(2), QStringLiteral("k 2틱 · 2k 4틱"));
        QCOMPARE(widths_text(std::nullopt), QString());
    }

    void the_answer_reads_r_and_its_class_or_why_there_is_none() {
        const KindInfo range = range_kind();
        QCOMPARE(kind_answer_text(answer(4, "large"), range), QStringLiteral("정답 4틱 · 큼"));
        QCOMPARE(kind_answer_text(answer(0.5, "small"), range), QStringLiteral("정답 0.5틱 · 작음"));
        QCOMPARE(kind_answer_text(std::nullopt, range), QStringLiteral("채점 전"));
        KindAnswer silent = answer(0, "");
        silent.unscorable = "silent quotes";
        silent.range.reset();
        QCOMPARE(kind_answer_text(silent, range), QStringLiteral("채점 불가 (silent quotes)"));
        KindAnswer special = answer(3, "medium");
        special.special = true;
        QCOMPARE(kind_answer_text(special, range), QStringLiteral("정답 3틱 · 보통 · 특수 상황"));
        KindAnswer pathless = answer(3, "medium");
        pathless.range.reset(); // another kind's answer: its class alone
        QCOMPARE(kind_answer_text(pathless, range), QStringLiteral("정답 보통"));
    }

    void the_panel_lists_the_algorithms_then_the_baselines_with_classes_and_quantiles() {
        const auto rows = kind_rows({forecast("range-persist", "09:31", 0, 0, 1, 5, 5, 5),
                                     forecast("alg-r", "09:31", 0.25, 0.45, 0.30, 1, 2.5, 6),
                                     forecast("range-climate", "09:31", 0.3, 0.5, 0.2, 0.5, 2, 4.5),
                                     forecast("alg-a", "09:31", 0.6, 0.3, 0.1, 0, 1, 2)},
                                    range_kind());
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[0].algorithm, QStringLiteral("alg-a"));
        QCOMPARE(rows[1].algorithm, QStringLiteral("alg-r"));
        QCOMPARE(rows[2].name, QStringLiteral("기준: 변동폭 시간대 기후"));
        QCOMPARE(rows[3].name, QStringLiteral("기준: 직전 4분 변동폭"));
        QVERIFY(!rows[1].baseline);
        QVERIFY(rows[2].baseline);
        QCOMPARE(rows[1].probabilities.size(), 3);
        QCOMPARE(*rows[1].probabilities[1], 0.45);
        QCOMPARE(rows[1].top, QStringLiteral("보통"));
        QCOMPARE(rows[0].top, QStringLiteral("작음"));
        QCOMPARE(rows[1].q50, std::optional<double>(2.5));
        QCOMPARE(rows[1].model_version, QStringLiteral("alg-r-1"));
        QCOMPARE(rows[1].result, QStringLiteral("채점 전"));
    }

    void the_panel_scores_each_on_its_bar_close_s_answer_and_says_why_one_is_missing() {
        const KindAnswer large = answer(4, "large");
        const auto rows =
            kind_rows({with(forecast("alg-r", "09:31", 0.25, 0.45, 0.30, 1, 2.5, 6), large),
                       with(forecast("range-persist", "09:31", 0, 0, 1, 5, 5, 5), large),
                       not_made("range-climate", "09:31", "no_previous"), not_made("alg-z", "09:31", "timed_out")},
                      range_kind());
        QCOMPARE(rows[0].result, QStringLiteral("✗ 틀림")); // medium, but large
        QCOMPARE(rows[1].algorithm, QStringLiteral("alg-z"));
        QCOMPARE(rows[1].top, QStringLiteral("시간 초과"));
        QVERIFY(rows[1].probabilities.isEmpty());
        QVERIFY(rows[1].result.isEmpty());
        QCOMPARE(rows[2].top, QStringLiteral("직전 창 없음"));
        QCOMPARE(rows[3].result, QStringLiteral("✓ 맞음"));
        KindAnswer silent = answer(0, "");
        silent.unscorable = "silent quotes";
        QCOMPARE(kind_rows({with(forecast("alg-r", "09:31", 0.2, 0.3, 0.5, 1, 2, 4), silent)}, range_kind())[0].result,
                 QStringLiteral("채점 불가"));
    }

    void the_hover_has_the_answer_then_a_line_per_predictor() {
        const KindAnswer large = answer(4, "large");
        const QList<KindPrediction> at_close{
            with(forecast("range-climate", "09:31", 0.3, 0.5, 0.2, 0.5, 2, 4.5), large),
            with(forecast("alg-r", "09:31", 0.25, 0.30, 0.45, 1, 2.5, 6), large),
            not_made("range-persist", "09:31", "no_previous")};
        const auto lines = kind_hover_box(at_close, at("09:31"), range_kind(), {{"alg-r", "probation"}});
        QCOMPARE(lines.size(), 4);
        QCOMPARE(lines[0], QStringLiteral("09:31 봉 → 09:35 목표 · 정답 4틱 · 큼"));
        QCOMPARE(lines[1], QStringLiteral("alg-r · 수습 · 큼 45% · q10~q90 1~6틱 · ✓"));
        QCOMPARE(lines[2], QStringLiteral("기준: 변동폭 시간대 기후 · 보통 50% · q10~q90 0.5~4.5틱 · ✗"));
        QCOMPARE(lines[3], QStringLiteral("기준: 직전 4분 변동폭 · 직전 창 없음"));
        // Before the answer: "?" for each, "채점 전" on the first line.
        const auto pending =
            kind_hover_box({forecast("alg-r", "09:31", 0.25, 0.30, 0.45, 1, 2.5, 6)}, at("09:31"), range_kind(), {});
        QCOMPARE(pending[0], QStringLiteral("09:31 봉 → 09:35 목표 · 채점 전"));
        QCOMPARE(pending[1], QStringLiteral("alg-r · 큼 45% · q10~q90 1~6틱 · ?"));
        QCOMPARE(kind_hover_box({}, at("09:31"), range_kind(), {}),
                 (QStringList{QStringLiteral("09:31 봉 → 09:35 목표"), QStringLiteral("예측 없음")}));
    }

    void each_bar_s_hover_has_its_window_and_the_range_s_highest_and_lowest_mid_points() {
        const QList<Bar> bars{{at("09:30"), 80'000, 80'000, 80'000, 80'000, 10},
                              {at("09:31"), 80'000, 80'000, 80'000, 80'000, 10}};
        QMap<QDateTime, QList<KindPrediction>> closes;
        closes.insert(at("09:31"), {with(forecast("alg-r", "09:31", 0.25, 0.30, 0.45, 1, 2.5, 6), answer(4, "large"))});
        closes.insert(at("09:32"), {forecast("alg-r", "09:32", 0.25, 0.30, 0.45, 1, 2.5, 6)});

        const auto hovers = kind_hovers(bars, closes, range_kind(), {});

        QCOMPARE(hovers.size(), 2);
        // The bar 09:30–09:31 closed at 09:31: its window runs 09:31–09:35.
        QCOMPARE(hovers[0].time, chart_time(at("09:30")));
        QCOMPARE(hovers[0].window_from, chart_time(at("09:31")));
        QCOMPARE(hovers[0].window_to, chart_time(at("09:35")));
        QCOMPARE(hovers[0].upper, std::optional<double>(80'300));
        QCOMPARE(hovers[0].lower, std::optional<double>(79'900));
        QCOMPARE(hovers[0].upper_reached->time, chart_time(kst("2026-10-12T09:33:10+09:00")));
        QCOMPARE(hovers[0].upper_reached->price, 80'300.0);
        QCOMPARE(hovers[0].lower_reached->time, chart_time(kst("2026-10-12T09:31:40+09:00")));
        QVERIFY(hovers[0].lines[0].endsWith(QStringLiteral("정답 4틱 · 큼")));
        // Its answer not in yet: the window alone.
        QVERIFY(!hovers[1].upper.has_value());
        QVERIFY(!hovers[1].lower_reached.has_value());
        QCOMPARE(hovers[1].window_to - hovers[1].window_from, qint64(240));
    }

    void the_kind_s_predictions_are_read_back_to_the_first_bar_shown() {
        // No bar shown: nothing to read for.
        QVERIFY(!kind_read({}, {}).has_value());
        // None read yet: the latest page.
        const auto first = kind_read({}, at("09:01"));
        QVERIFY(first.has_value());
        QVERIFY(!first->before.isValid());
        QCOMPARE(first->limit, kKindPage);
        // Read back to 10:00, bars from 09:01: the 60 bar closes before 10:00 down to 09:01 (and 09:01 itself).
        const auto back = kind_read(at("10:00"), at("09:01"));
        QCOMPARE(back->before, at("10:00"));
        QCOMPARE(back->limit, 60);
        QCOMPARE(kind_read(at("09:00"), kst("2026-10-01T09:01:00+09:00"))->limit, kMaxOlderBars); // at most
        QVERIFY(!kind_read(at("09:01"), at("09:01")).has_value());                                // reached
        // A page moves it to its earliest bar close; an empty one to the first bar shown.
        const QList<KindPrediction> page{forecast("a", "09:40", 0.3, 0.3, 0.4, 0, 0, 0),
                                         forecast("a", "09:20", 0.3, 0.3, 0.4, 0, 0, 0)};
        QCOMPARE(kind_from_after({}, page, at("09:01")), at("09:20"));
        QCOMPARE(kind_from_after(at("09:10"), page, at("09:01")), at("09:10"));
        QCOMPARE(kind_from_after(at("09:20"), {}, at("09:01")), at("09:01"));
    }
};

QTEST_GUILESS_MAIN(TstKrxKindView)
#include "tst_krx_kind_view.moc"
