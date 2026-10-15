// Unit tests for src/services/krx_quant/KindView.h
//
// The KRX screen shows another kind of prediction than the direction (krx-quant #182): a
// selector at its top, the kind's prediction panel, its hover boxes on the chart and its
// baselines' cards. These tests pin the selector's choices (greyed for an older engine and
// for a kind this screen cannot show), which kind shows for the choice kept, the range's and
// the barrier's (krx-quant #185) panel columns and rows, the barrier's width and barriers on
// the panel's title, their hover lines and windows, and how a kind's predictions are read back
// to the first bar shown. The values are made up and the expected ones worked out by hand.

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

KindInfo barrier_kind() {
    KindInfo k;
    k.name = "barrier";
    k.title = QStringLiteral("장벽");
    k.classes = {"up_first", "timeout", "down_first"};
    k.class_titles = {QStringLiteral("위 먼저"), QStringLiteral("시간 만료"), QStringLiteral("아래 먼저")};
    k.action = {"up_first", "down_first"};
    k.answer_version = 1;
    k.baselines = {{"barrier-climate", std::nullopt}, {"barrier-persist", std::nullopt}};
    return k;
}

/// A barrier forecast of ``algorithm`` at ``bar_close``: up first, timeout, down first; the bar
/// close's width w 6 ticks, as the engine gives every prediction of it (krx-quant #183).
KindPrediction barrier_forecast(const QString& algorithm, const char* bar_close, double up, double timeout,
                                double down) {
    KindPrediction p;
    p.algorithm = algorithm;
    p.model_version = algorithm + "-1";
    p.status = "ok";
    p.symbol = "005930";
    p.bar_close = at(bar_close);
    p.target_at = p.bar_close.addSecs(240);
    p.flat_ticks = 2;
    p.width_ticks = 6;
    p.output = {{"p_up_first", up}, {"p_timeout", timeout}, {"p_down_first", down}};
    return p;
}

/// A barrier answer of the bar close 09:31: the mid-point 78,350 won (the best quote 78,300 /
/// 78,400), a 100-won tick, w 6: U 78,950 and L 77,750. ``up``: when the best bid first reached U (it was 79,000),
/// ``down``: when the best ask first reached L (77,700); either may be none.
KindAnswer barrier_answer(const QString& klass, const char* up, const char* down) {
    KindAnswer a;
    a.answer_version = 1;
    a.klass = klass;
    a.scored_at = at("09:35");
    BarrierPath path;
    path.w = 6;
    path.sigma = 6.4;
    path.k = 2;
    path.tick = 100;
    path.anchor = 78'350;
    path.upper = 78'950;
    path.lower = 77'750;
    path.cost = 1.8;
    path.bid_start = 78'300;
    path.ask_start = 78'400;
    if (up) {
        path.up_at = kst(up);
        path.up_bid = 79'000;
        path.up_ask = 79'100;
    }
    if (down) {
        path.down_at = kst(down);
        path.down_bid = 77'600;
        path.down_ask = 77'700;
    }
    path.quotes = 210;
    a.barrier = path;
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

/// ``p`` with the bar close's mid-point and tick it came with (krx-quant #210); without them, as
/// from an engine before #210.
KindPrediction with_anchor(KindPrediction p, double anchor, double tick) {
    p.anchor = anchor;
    p.tick = tick;
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

    void a_barrier_the_engine_makes_can_be_chosen() {
        // krx-quant #185: the screen shows the barrier; an engine since #183 makes it.
        KindInfo barrier = barrier_kind();
        barrier.title = QStringLiteral("장벽(엔진)");
        const auto choices = kind_choices(QList<KindInfo>{direction_kind(), range_kind(), barrier});
        QCOMPARE(choices[1].title, QStringLiteral("장벽(엔진)")); // the engine's title
        QVERIFY(choices[1].enabled);
        QCOMPARE(choices[1].tip,
                 QStringLiteral("위 먼저·시간 만료·아래 먼저의 확률\n예측 패널, 순위표, 상세 통계 창이 이 종류로 "
                                "바뀐다. 차트의 아이콘·선택 합의·신호 알림은 방향 그대로다."));
        QCOMPARE(shown_kind("barrier", choices), QStringLiteral("barrier"));
        // A kind this screen does not know stays out of the selector, whatever the engine makes.
        KindInfo weather;
        weather.name = "weather";
        QCOMPARE(kind_choices(QList<KindInfo>{direction_kind(), weather}).size(), 3);
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

    void the_panel_s_columns_are_the_kind_s_classes_and_the_range_s_quantiles() {
        const auto headers = [](const QList<PanelColumn>& columns) {
            QStringList out;
            for (const auto& c : columns)
                out << c.header;
            return out;
        };
        // The range's, as krx-quant #182 made them.
        const auto range = kind_panel_columns(range_kind(), false);
        QCOMPARE(headers(range), (QStringList{"예측", "구간", "작음", "보통", "큼", "q10", "q50", "q90", "모델"}));
        QVERIFY(range[0].tip.isEmpty());
        QVERIFY(range[1].tip.isEmpty());
        QCOMPARE(range[2].tip, QStringLiteral("4분 동안 중간가 변동폭(최고 − 최저)이 k틱보다 작을 확률"));
        QCOMPARE(range[4].tip, QStringLiteral("2k틱 이상일 확률"));
        QCOMPARE(range[7].tip, QStringLiteral("변동폭의 90% 분위수(틱): 열에 아홉은 이보다 작다"));
        QVERIFY(range[8].tip.isEmpty());
        // The barrier's (krx-quant #185): its three classes, no quantiles; a pinned bar close's result.
        const auto barrier = kind_panel_columns(barrier_kind(), true);
        QCOMPARE(headers(barrier),
                 (QStringList{"예측", "예측 클래스", "위 먼저", "시간 만료", "아래 먼저", "결과", "모델"}));
        QVERIFY(barrier[1].tip.startsWith(QStringLiteral("세 확률 중 가장 큰 것이다")));
        QCOMPARE(
            barrier[2].tip,
            QStringLiteral("4분 안에 매수 1호가가 위 장벽 U(봉 마감 중간가 + w틱)에 아래 장벽보다 먼저 닿을 확률"));
        QCOMPARE(barrier[3].tip, QStringLiteral("4분 동안 어느 장벽에도 닿지 않을 확률"));
        QVERIFY(barrier[5].tip.isEmpty());
        // Without the kind described yet.
        QCOMPARE(headers(kind_panel_columns(std::nullopt, true)), (QStringList{"예측", "구간", "결과", "모델"}));
    }

    void the_barrier_width_is_the_answer_s_once_in_else_the_predictions_bar_close_s() {
        const QList<KindPrediction> pending{barrier_forecast("alg-b", "09:31", 0.3, 0.5, 0.2),
                                            not_made("barrier-persist", "09:31", "no_previous")};
        QCOMPARE(bar_close_width(pending), std::optional<int>(6));
        // Not made, it still has the bar close's w (krx-quant #183); one stored before has none.
        KindPrediction persist = pending[1];
        persist.width_ticks = 6;
        QCOMPARE(bar_close_width({pending[1], persist}), std::optional<int>(6));
        QVERIFY(!bar_close_width({pending[1]}).has_value());
        QVERIFY(!bar_close_width({forecast("alg-r", "09:31", 0.2, 0.3, 0.5, 1, 2, 4)}).has_value()); // the range's
        // Before the answer the engine gives no mid-point of the bar close: w alone.
        QCOMPARE(barrier_text(pending), QStringLiteral("w 6틱"));
        // With it, U and L as the exchange writes prices; its w (made up to differ, to pin which wins).
        KindAnswer answer = barrier_answer("up_first", "2026-10-12T09:34:28+09:00", nullptr);
        answer.barrier->w = 7;
        QCOMPARE(bar_close_width({with(pending[0], answer)}), std::optional<int>(7));
        QCOMPARE(barrier_text({with(pending[0], barrier_answer("timeout", nullptr, nullptr))}),
                 QStringLiteral("w 6틱 · U 78,950 · L 77,750"));
        // The mid-point 78,350 is between ticks (78,300 / 78,400), so are U and L; on a whole tick, U
        // and L are too.
        KindAnswer whole = barrier_answer("timeout", nullptr, nullptr);
        whole.barrier->anchor = 78'400;
        whole.barrier->upper = 79'000;
        whole.barrier->lower = 77'800;
        QCOMPARE(barrier_text({with(pending[0], whole)}), QStringLiteral("w 6틱 · U 79,000 · L 77,800"));
        // A window the engine did not follow (no path): the predictions' w.
        KindAnswer restarted = barrier_answer("", nullptr, nullptr);
        restarted.unscorable = "restarted";
        restarted.barrier.reset();
        QCOMPARE(barrier_text({with(pending[0], restarted)}), QStringLiteral("w 6틱"));
        QVERIFY(barrier_text({not_made("barrier-persist", "09:31", "vi")}).isEmpty());
    }

    void the_barrier_answer_says_which_barrier_was_reached_first_and_when() {
        const KindInfo barrier = barrier_kind();
        QCOMPARE(kind_answer_text(barrier_answer("up_first", "2026-10-12T09:34:28+09:00", nullptr), barrier),
                 QStringLiteral("정답 위 먼저 (09:34:28 닿음)"));
        // Both reached: the first, with its own time.
        QCOMPARE(
            kind_answer_text(barrier_answer("down_first", "2026-10-12T09:33:40+09:00", "2026-10-12T09:32:05.250+09:00"),
                             barrier),
            QStringLiteral("정답 아래 먼저 (09:32:05 닿음)"));
        QCOMPARE(kind_answer_text(barrier_answer("timeout", nullptr, nullptr), barrier),
                 QStringLiteral("정답 시간 만료"));
        KindAnswer same = barrier_answer("", "2026-10-12T09:33:40+09:00", "2026-10-12T09:33:40+09:00");
        same.unscorable = "ambiguous";
        QCOMPARE(kind_answer_text(same, barrier), QStringLiteral("채점 불가 (ambiguous)"));
        KindAnswer special = barrier_answer("timeout", nullptr, nullptr);
        special.special = true;
        QCOMPARE(kind_answer_text(special, barrier), QStringLiteral("정답 시간 만료 · 특수 상황"));
        KindAnswer pathless = barrier_answer("up_first", nullptr, nullptr);
        pathless.barrier.reset();
        QCOMPARE(kind_answer_text(pathless, barrier), QStringLiteral("정답 위 먼저"));
    }

    void the_barrier_panel_lists_three_probabilities_and_scores_on_the_answer() {
        const KindAnswer up = barrier_answer("up_first", "2026-10-12T09:34:28+09:00", nullptr);
        const auto rows = kind_rows({with(barrier_forecast("barrier-persist", "09:31", 1, 0, 0), up),
                                     with(barrier_forecast("barrier-climate", "09:31", 1.0 / 3, 1.0 / 3, 1.0 / 3), up),
                                     with(barrier_forecast("alg-b", "09:31", 0.45, 0.35, 0.2), up)},
                                    barrier_kind());
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].algorithm, QStringLiteral("alg-b"));
        QCOMPARE(rows[1].name, QStringLiteral("기준: 장벽 시간대 기후"));
        QCOMPARE(rows[2].name, QStringLiteral("기준: 직전 4분 장벽"));
        QVERIFY(rows[1].baseline);
        QCOMPARE(rows[0].top, QStringLiteral("위 먼저"));
        QCOMPARE(*rows[0].probabilities[2], 0.2);
        QVERIFY(!rows[0].q50.has_value());
        QCOMPARE(rows[0].result, QStringLiteral("✓ 맞음"));
        // barrier-climate's default, a third each: a tie goes to the middle class, timeout.
        QCOMPARE(rows[1].top, QStringLiteral("시간 만료"));
        QCOMPARE(rows[1].result, QStringLiteral("✗ 틀림"));
        QCOMPARE(rows[2].result, QStringLiteral("✓ 맞음"));
    }

    void the_barrier_hover_has_the_answer_and_w_then_each_predictor_s_three_probabilities() {
        const KindAnswer down = barrier_answer("down_first", "2026-10-12T09:33:40+09:00", "2026-10-12T09:32:05+09:00");
        const QList<KindPrediction> at_close{with(barrier_forecast("barrier-persist", "09:31", 1, 0, 0), down),
                                             with(barrier_forecast("alg-b", "09:31", 0.2, 0.35, 0.45), down),
                                             not_made("barrier-climate", "09:31", "vi")};
        const auto lines = kind_hover_box(at_close, at("09:31"), barrier_kind(), {{"alg-b", "probation"}});
        QCOMPARE(lines.size(), 4);
        QCOMPARE(lines[0], QStringLiteral("09:31 봉 → 09:35 목표 · 정답 아래 먼저 (09:32:05 닿음) · w 6틱"));
        QCOMPARE(lines[1],
                 QStringLiteral("alg-b · 수습 · 아래 먼저 · 위 먼저 20% · 시간 만료 35% · 아래 먼저 45% · ✓"));
        QCOMPARE(lines[2], QStringLiteral("기준: 장벽 시간대 기후 · VI 중"));
        QCOMPARE(lines[3],
                 QStringLiteral("기준: 직전 4분 장벽 · 위 먼저 · 위 먼저 100% · 시간 만료 0% · 아래 먼저 0% · ✗"));
        // Before the answer: w from the predictions, "?" for each.
        const auto pending =
            kind_hover_box({barrier_forecast("alg-b", "09:31", 0.2, 0.35, 0.45)}, at("09:31"), barrier_kind(), {});
        QCOMPARE(pending[0], QStringLiteral("09:31 봉 → 09:35 목표 · 채점 전 · w 6틱"));
        QCOMPARE(pending[1], QStringLiteral("alg-b · 아래 먼저 · 위 먼저 20% · 시간 만료 35% · 아래 먼저 45% · ?"));
    }

    void each_bar_s_hover_has_the_barriers_and_where_each_was_first_reached() {
        const QList<Bar> bars{{at("09:30"), 78'350, 78'350, 78'350, 78'350, 10},
                              {at("09:31"), 78'350, 78'350, 78'350, 78'350, 10},
                              {at("09:32"), 78'350, 78'350, 78'350, 78'350, 10}};
        QMap<QDateTime, QList<KindPrediction>> closes;
        // 09:31: the best ask reached L at 09:32:05, then the best bid U at 09:33:40: both drawn.
        closes.insert(at("09:31"),
                      {with(barrier_forecast("alg-b", "09:31", 0.2, 0.35, 0.45),
                            barrier_answer("down_first", "2026-10-12T09:33:40+09:00", "2026-10-12T09:32:05+09:00"))});
        // 09:32: neither reached.
        closes.insert(at("09:32"), {with(barrier_forecast("alg-b", "09:32", 0.2, 0.35, 0.45),
                                         barrier_answer("timeout", nullptr, nullptr))});
        // 09:33: not answered yet, from an engine before krx-quant #210 (no mid-point with it).
        closes.insert(at("09:33"), {barrier_forecast("alg-b", "09:33", 0.2, 0.35, 0.45)});

        const auto hovers = kind_hovers(bars, closes, barrier_kind(), {});

        QCOMPARE(hovers.size(), 3);
        QCOMPARE(hovers[0].window_from, chart_time(at("09:31")));
        QCOMPARE(hovers[0].window_to, chart_time(at("09:35")));
        QCOMPARE(hovers[0].upper, std::optional<double>(78'950));
        QCOMPARE(hovers[0].lower, std::optional<double>(77'750));
        // Where each was first reached: the best bid at U or above, the best ask at L or below.
        QCOMPARE(hovers[0].upper_reached->time, chart_time(kst("2026-10-12T09:33:40+09:00")));
        QCOMPARE(hovers[0].upper_reached->price, 79'000.0);
        QCOMPARE(hovers[0].lower_reached->time, chart_time(kst("2026-10-12T09:32:05+09:00")));
        QCOMPARE(hovers[0].lower_reached->price, 77'700.0);
        QVERIFY(hovers[0].lines[0].endsWith(QStringLiteral("정답 아래 먼저 (09:32:05 닿음) · w 6틱")));
        // Neither reached: the barriers, no point.
        QCOMPARE(hovers[1].upper, std::optional<double>(78'950));
        QVERIFY(!hovers[1].upper_reached.has_value());
        QVERIFY(!hovers[1].lower_reached.has_value());
        // Not answered, by an engine that gives the bar close's mid-point with the answer only: the
        // window alone.
        QVERIFY(!hovers[2].upper.has_value());
        QVERIFY(!hovers[2].lower.has_value());
        QCOMPARE(hovers[2].window_to - hovers[2].window_from, qint64(240));
    }

    void before_the_answer_the_barriers_come_from_the_predictions_bar_close() {
        // krx-quant #210: the bar close's mid-point 78,350 and tick 100 come with each prediction,
        // w 6: U 78,950 and L 77,750 before the answer.
        const QList<KindPrediction> pending{
            with_anchor(barrier_forecast("alg-b", "09:31", 0.3, 0.5, 0.2), 78'350, 100),
            with_anchor(not_made("barrier-persist", "09:31", "no_previous"), 78'350, 100)};
        const auto barriers = bar_close_barriers(pending);
        QVERIFY(barriers.has_value());
        QCOMPARE(barriers->upper, 78'950.0);
        QCOMPARE(barriers->lower, 77'750.0);
        QCOMPARE(barrier_text(pending), QStringLiteral("w 6틱 · U 78,950 · L 77,750"));
        // A prediction not made has them too (a VI, krx-quant #183): its w with them.
        KindPrediction vi = with_anchor(not_made("barrier-climate", "09:31", "vi"), 78'350, 100);
        vi.width_ticks = 6;
        QCOMPARE(barrier_text({vi}), QStringLiteral("w 6틱 · U 78,950 · L 77,750"));
        // On a mid-point between won (a 1-won tick under 2,000 won), so are U and L.
        KindPrediction cheap = with_anchor(barrier_forecast("alg-b", "09:31", 0.3, 0.5, 0.2), 1'234.5, 1);
        cheap.width_ticks = 3;
        QCOMPARE(barrier_text({cheap}), QStringLiteral("w 3틱 · U 1,237.5 · L 1,231.5"));
        // An engine before krx-quant #210 gives no mid-point with them: w alone, as before.
        QCOMPARE(barrier_text({barrier_forecast("alg-b", "09:31", 0.3, 0.5, 0.2)}), QStringLiteral("w 6틱"));
        QVERIFY(!bar_close_barriers({barrier_forecast("alg-b", "09:31", 0.3, 0.5, 0.2)}).has_value());
        // Nor without the w (another kind: the range's carry the mid-point but no barriers).
        QVERIFY(!bar_close_barriers({with_anchor(forecast("alg-r", "09:31", 0.2, 0.3, 0.5, 1, 2, 4), 78'350, 100)})
                     .has_value());
        // Once the answer is in, its U and L (made up to differ, to pin which wins); a window the
        // engine did not follow has none, as before.
        KindAnswer answer = barrier_answer("timeout", nullptr, nullptr);
        answer.barrier->upper = 79'000;
        answer.barrier->lower = 77'700;
        QCOMPARE(barrier_text({with(pending[0], answer)}), QStringLiteral("w 6틱 · U 79,000 · L 77,700"));
        KindAnswer restarted = barrier_answer("", nullptr, nullptr);
        restarted.unscorable = "restarted";
        restarted.barrier.reset();
        QVERIFY(!bar_close_barriers({with(pending[0], restarted)}).has_value());
        QCOMPARE(barrier_text({with(pending[0], restarted)}), QStringLiteral("w 6틱"));
    }

    void before_the_answer_the_hover_draws_the_barriers_the_predictions_came_with() {
        const QList<Bar> bars{{at("09:30"), 78'350, 78'350, 78'350, 78'350, 10},
                              {at("09:31"), 78'350, 78'350, 78'350, 78'350, 10}};
        QMap<QDateTime, QList<KindPrediction>> closes;
        // 09:31: not answered yet; the predictions came with the bar close's mid-point (krx-quant #210).
        closes.insert(at("09:31"), {with_anchor(barrier_forecast("alg-b", "09:31", 0.2, 0.35, 0.45), 78'350, 100)});
        // 09:32: the range's carry the mid-point too, but the barrier is chosen and they have no w.
        closes.insert(at("09:32"), {with_anchor(forecast("alg-r", "09:32", 0.2, 0.3, 0.5, 1, 2, 4), 78'350, 100)});

        const auto hovers = kind_hovers(bars, closes, barrier_kind(), {});

        QCOMPARE(hovers.size(), 2);
        QCOMPARE(hovers[0].upper, std::optional<double>(78'950));
        QCOMPARE(hovers[0].lower, std::optional<double>(77'750));
        QVERIFY(!hovers[0].upper_reached.has_value()); // nothing reached yet
        QVERIFY(!hovers[0].lower_reached.has_value());
        QCOMPARE(hovers[0].window_to - hovers[0].window_from, qint64(240));
        QVERIFY(hovers[0].lines[0].endsWith(QStringLiteral("채점 전 · w 6틱")));
        QVERIFY(!hovers[1].upper.has_value());
        // While the range is chosen, the barrier's are not drawn.
        const auto range = kind_hovers(bars, closes, range_kind(), {});
        QVERIFY(!range[0].upper.has_value());
        QVERIFY(!range[0].lower.has_value());
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
        QCOMPARE(kind_read(at("09:00"), kst("2026-10-01T09:01:00+09:00"))->limit, kPredictionPage); // at most
        QVERIFY(!kind_read(at("09:01"), at("09:01")).has_value());                                  // reached
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
