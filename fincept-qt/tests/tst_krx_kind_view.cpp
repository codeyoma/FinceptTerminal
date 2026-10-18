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

// ---- open_to_close (krx-quant #264): made-up days ----

const QTimeZone& kst_zone() {
    static const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    return kst;
}

/// 08:50 of 2026-10-<day> in KST: a daily prediction's time.
QDateTime morning(int day) {
    return QDateTime(QDate(2026, 10, day), QTime(8, 50), kst_zone());
}

KindInfo daily_kind() {
    KindInfo k;
    k.name = "open_to_close";
    k.title = QStringLiteral("시가→종가");
    k.classes = {"up", "flat", "down"};
    k.class_titles = {QStringLiteral("상승"), QStringLiteral("보합"), QStringLiteral("하락")};
    k.action = {"up", "down"};
    k.answer_version = 1;
    k.horizon_minutes = 0;
    k.baselines = {{"otc-flat", std::nullopt}, {"otc-climate", std::nullopt}, {"otc-persist", std::nullopt}};
    DailySchedule day;
    day.predict_at = "08:50";
    day.from = "open";
    day.to = "close";
    day.from_title = QStringLiteral("시가 단일가");
    day.to_title = QStringLiteral("종가 단일가");
    day.answer_from = "20:35";
    day.probation_days = {60, 120, 180};
    day.bench_days = {60, 120, 180};
    day.recent_days = 60;
    k.daily = day;
    return k;
}

/// ``algorithm``'s open-to-close forecast on 2026-10-<day>: up, flat, down; the day's band 0.47%.
KindPrediction daily_forecast(const QString& algorithm, int day, double up, double flat, double down) {
    KindPrediction p;
    p.algorithm = algorithm;
    p.model_version = algorithm + "-1";
    p.status = "ok";
    p.symbol = "005930";
    p.bar_close = morning(day);
    p.target_at = QDateTime(QDate(2026, 10, day), QTime(15, 30), kst_zone());
    p.band = 0.0047;
    p.output = {{"p_up", up}, {"p_flat", flat}, {"p_down", down}};
    return p;
}

/// The day's answer: its class from the KRX opening auction ``open`` to the closing auction ``close``.
KindAnswer daily_answer(const QString& klass, double open, double close, int vi = 0, bool special = false) {
    KindAnswer a;
    a.answer_version = 1;
    a.klass = klass;
    a.special = special;
    OpenToClosePath day;
    day.open = open;
    day.close = close;
    day.change = close / open - 1;
    day.band = 0.0047;
    day.sigma = 0.0105;
    day.scale = 0.4488;
    day.vi = vi;
    a.open_to_close = day;
    return a;
}

/// A day candle of 2026-10-<day> as the day view holds it: from 00:00 KST.
Bar day_candle(int day) {
    return Bar{QDateTime(QDate(2026, 10, day), QTime(0, 0), kst_zone()), 80'000, 81'000, 79'000, 80'500, 1'000'000};
}

} // namespace

class TstKrxKindView : public QObject {
    Q_OBJECT

  private slots:
    void an_older_engine_offers_the_direction_alone() {
        // No GET /v1/kinds (an engine before krx-quant #179): the others greyed, in the screen's order.
        const auto choices = kind_choices(std::nullopt);
        QCOMPARE(choices.size(), 4);
        QCOMPARE(choices[0].name, QStringLiteral("direction"));
        QCOMPARE(choices[1].name, QStringLiteral("barrier"));
        QCOMPARE(choices[2].name, QStringLiteral("range"));
        QCOMPARE(choices[3].name, QStringLiteral("open_to_close")); // krx-quant #264
        QCOMPARE(choices[0].title, QStringLiteral("방향"));
        QCOMPARE(choices[1].title, QStringLiteral("장벽"));
        QCOMPARE(choices[2].title, QStringLiteral("변동폭"));
        QCOMPARE(choices[3].title, QStringLiteral("시가→종가"));
        QVERIFY(choices[0].enabled);
        QVERIFY(!choices[1].enabled);
        QVERIFY(!choices[2].enabled);
        QVERIFY(!choices[3].enabled);
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
        QCOMPARE(kind_choices(QList<KindInfo>{direction_kind(), weather}).size(), 4);
    }

    void the_barrier_s_horizon_ladder_follows_the_barrier_when_the_engine_makes_it() {
        // krx-quant #263: barrier_15m, barrier_30m, barrier_60m after the barrier, in the engine's
        // order; none when the engine does not list them (no greyed rung).
        QList<KindInfo> kinds{direction_kind(), range_kind(), barrier_kind()};
        for (const int minutes : {15, 30, 60}) {
            KindInfo rung = barrier_kind();
            rung.name = QStringLiteral("barrier_%1m").arg(minutes);
            rung.title = QStringLiteral("장벽 %1분").arg(minutes);
            rung.horizon_minutes = minutes;
            kinds << rung;
        }
        const auto choices = kind_choices(kinds);
        QStringList names;
        for (const auto& c : choices)
            names << c.name;
        QCOMPARE(names, (QStringList{"direction", "barrier", "barrier_15m", "barrier_30m", "barrier_60m", "range",
                                     "open_to_close"}));
        QCOMPARE(choices[2].title, QStringLiteral("장벽 15분"));
        QVERIFY(choices[2].enabled && choices[4].enabled);
        QCOMPARE(choices[3].tip,
                 QStringLiteral("위 먼저·시간 만료·아래 먼저의 확률\n봉 마감부터 30분 동안의 장벽이다.\n"
                                "예측 패널, 순위표, 상세 통계 창이 이 종류로 바뀐다. 차트의 아이콘·선택 "
                                "합의·신호 알림은 방향 그대로다."));
        QCOMPARE(shown_kind("barrier_30m", choices), QStringLiteral("barrier_30m"));
        QCOMPARE(shown_kind("barrier_30m", kind_choices(QList<KindInfo>{direction_kind(), barrier_kind()})),
                 QStringLiteral("direction")); // an engine without the ladder
        QCOMPARE(kind_choices(QList<KindInfo>{direction_kind(), barrier_kind()}).size(), 4);

        // Its panel and hover say its minutes; without a target time it is scored H minutes on.
        const KindInfo& thirty = kinds[4];
        QVERIFY(is_barrier_kind(thirty.name) && is_barrier_kind("barrier") && !is_barrier_kind("barrier_x"));
        const auto columns = kind_panel_columns(thirty, false);
        QVERIFY(columns[2].tip.startsWith(QStringLiteral("30분 안에 매수 1호가가 위 장벽 U")));
        KindPrediction p = barrier_forecast("breiman-30m", "09:31", 0.5, 0.3, 0.2);
        p.target_at = QDateTime();
        QVERIFY(kind_hover_box({p}, at("09:31"), thirty, {})[0].startsWith(QStringLiteral("09:31 봉 → 10:01 목표")));
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

    // ---- A kind predicted once a trading day (krx-quant #264) ----

    void open_to_close_is_chosen_when_the_engine_makes_it() {
        KindInfo otc = daily_kind();
        const auto choices = kind_choices(QList<KindInfo>{direction_kind(), range_kind(), barrier_kind(), otc});
        QCOMPARE(choices.last().name, QStringLiteral("open_to_close"));
        QCOMPARE(choices.last().title, QStringLiteral("시가→종가")); // the engine's title
        QVERIFY(choices.last().enabled);
        QCOMPARE(choices.last().tip,
                 QStringLiteral("상승·보합·하락의 확률\n하루 한 번, 거래일 08:50에 그날 시가 단일가 → 종가 단일가의 "
                                "방향을 예측한다. 정답은 그날 KRX 일봉이 저장된 뒤(20:35부터)다.\n예측 패널, 순위표, "
                                "상세 통계 창이 이 종류로 바뀌고, 일 보기 차트에 날마다의 예측과 결과가 붙는다. 분 "
                                "보기의 아이콘·호버·선택 합의와 신호 알림은 방향 그대로다."));
        QCOMPARE(shown_kind("open_to_close", choices), QStringLiteral("open_to_close"));
        // An engine without it (before krx-quant #293): greyed, the direction shown, the setting kept.
        const auto older = kind_choices(QList<KindInfo>{direction_kind(), range_kind(), barrier_kind()});
        QVERIFY(!older.last().enabled);
        QCOMPARE(older.last().name, QStringLiteral("open_to_close"));
        QCOMPARE(shown_kind("open_to_close", older), QStringLiteral("direction"));
        QVERIFY(is_daily(otc) && !is_daily(range_kind()));
    }

    void a_daily_prediction_is_of_its_day_and_any_moment_of_the_day_finds_it() {
        QCOMPARE(daily_day(morning(8)), QDate(2026, 10, 8));
        // A minute bar's close, a late moment, the day's start, or the same moment in UTC: that day's 08:50.
        const KindInfo otc = daily_kind();
        QCOMPARE(daily_close_on(QDateTime(QDate(2026, 10, 8), QTime(10, 31), kst_zone()), otc), morning(8));
        QCOMPARE(daily_close_on(QDateTime(QDate(2026, 10, 8), QTime(23, 59), kst_zone()), otc), morning(8));
        QCOMPARE(daily_close_on(QDateTime(QDate(2026, 10, 8), QTime(0, 0), kst_zone()), otc), morning(8));
        QCOMPARE(daily_close_on(QDateTime::fromString("2026-10-07T16:30:00Z", Qt::ISODate), otc), morning(8));
        QVERIFY(!daily_close_on(morning(8), range_kind()).isValid()); // a bar-close kind has none
        QVERIFY(!daily_close_on({}, otc).isValid());
    }

    void the_daily_panel_has_its_class_three_probabilities_its_result_and_the_model() {
        const auto columns = kind_panel_columns(daily_kind(), false);
        QStringList headers;
        for (const auto& c : columns)
            headers << c.header;
        // The result always: the day is answered that night, not only once pinned.
        QCOMPARE(headers, (QStringList{"예측", "예측 클래스", "상승", "보합", "하락", "결과", "모델"}));
        QCOMPARE(kind_panel_columns(daily_kind(), true).size(), columns.size());
        QCOMPARE(columns[1].tip,
                 QStringLiteral("세 확률 중 가장 큰 것이다(같으면 보합, 그다음 상승). 채점은 이것으로 한다."));
        QCOMPARE(columns[2].tip,
                 QStringLiteral("그날 시가 단일가 → 종가 단일가의 변화(종가 단일가 ÷ 시가 단일가 − 1)가 "
                                "+보합 폭보다 클 확률. 보합 폭은 제목에 있다."));
        QCOMPARE(columns[3].tip, QStringLiteral("그 변화가 ±보합 폭 안일 확률"));
        QCOMPARE(columns[5].tip,
                 QStringLiteral("그날 KRX 일봉의 시가 단일가·종가 단일가로 채점한다. 일봉은 장이 끝난 뒤 "
                                "저장되어 20:35부터 나온다."));
    }

    void the_daily_rows_score_on_the_day_s_answer_and_say_why_one_is_missing() {
        const KindInfo otc = daily_kind();
        const KindAnswer down = daily_answer("down", 81'950, 81'250);
        KindPrediction persist;
        persist.algorithm = "otc-persist";
        persist.status = "no_previous";
        persist.symbol = "005930";
        persist.bar_close = morning(8);
        const QList<KindPrediction> day{with(daily_forecast("otc-flat", 8, 0, 1, 0), down), persist,
                                        with(daily_forecast("cox", 8, 0.30, 0.30, 0.40), down)};
        const auto rows = kind_rows(day, otc);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].algorithm, QStringLiteral("cox")); // the algorithms, then the baselines by name
        QCOMPARE(rows[0].top, QStringLiteral("하락"));
        QCOMPARE(rows[0].result, QStringLiteral("✓ 맞음"));
        QCOMPARE(rows[1].name, QStringLiteral("기준: 시가→종가 늘 보합"));
        QVERIFY(rows[1].baseline);
        QCOMPARE(rows[1].result, QStringLiteral("✗ 틀림"));
        QCOMPARE(rows[2].top, QStringLiteral("전 거래일 정답 없음")); // a daily baseline's no_previous
        QCOMPARE(kind_rows({daily_forecast("cox", 12, 0.4, 0.3, 0.3)}, otc)[0].result, QStringLiteral("채점 전"));
    }

    void the_daily_answer_reads_the_class_the_change_and_the_auction_prices() {
        const KindInfo otc = daily_kind();
        QCOMPARE(daily_answer_text(daily_answer("down", 81'950, 81'250), otc),
                 QStringLiteral("정답 하락 -0.85% (81,950 → 81,250)"));
        QCOMPARE(daily_answer_text(daily_answer("up", 80'000, 80'800), otc),
                 QStringLiteral("정답 상승 +1.00% (80,000 → 80,800)"));
        QCOMPARE(daily_answer_text(daily_answer("flat", 80'000, 80'000), otc),
                 QStringLiteral("정답 보합 0.00% (80,000 → 80,000)"));
        QCOMPARE(daily_answer_text(std::nullopt, otc), QStringLiteral("채점 전 (20:35부터)"));
        // A circuit breaker or sidecar that day: special; VIs counted in, not special.
        QCOMPARE(daily_answer_text(daily_answer("up", 80'000, 80'800, 2, true), otc),
                 QStringLiteral("정답 상승 +1.00% (80,000 → 80,800) · 특수 상황 · VI 2회"));
        KindAnswer no_band;
        no_band.unscorable = "no band";
        QCOMPARE(daily_answer_text(no_band, otc), QStringLiteral("채점 불가 (보합 폭 없음)"));
        KindAnswer no_candle;
        no_candle.unscorable = "no candle";
        QCOMPARE(kind_answer_text(no_candle, otc), QStringLiteral("채점 불가 (일봉 없음)")); // through kind_answer_text
        KindAnswer other;
        other.unscorable = "something else";
        QCOMPARE(daily_answer_text(other, otc), QStringLiteral("채점 불가 (something else)"));
    }

    void the_daily_title_says_the_day_the_band_and_the_answer() {
        const KindInfo otc = daily_kind();
        const QList<KindPrediction> today{daily_forecast("cox", 12, 0.35, 0.33, 0.32)};
        QCOMPARE(daily_title("005930", otc, morning(12), today, false),
                 QStringLiteral("005930 · 10-12 시가→종가 (08:50 예측) · 보합 ±0.47% · 채점 전 (20:35부터)"));
        const QList<KindPrediction> answered{
            with(daily_forecast("cox", 8, 0.3, 0.3, 0.4), daily_answer("down", 81'950, 81'250))};
        QCOMPARE(daily_title("005930", otc, morning(8), answered, false),
                 QStringLiteral("005930 · 10-08 시가→종가 (08:50 예측) · 보합 ±0.47% · 정답 하락 -0.85% "
                                "(81,950 → 81,250)"));
        // Before today's 08:50 (or a day without one): the last trading day's.
        QCOMPARE(daily_title("005930", otc, morning(8), answered, true),
                 QStringLiteral("005930 · 10-08 시가→종가 (08:50 예측, 지난 거래일) · 보합 ±0.47% · 정답 하락 -0.85% "
                                "(81,950 → 81,250)"));
        QCOMPARE(daily_title("005930", otc, {}, {}, false), QStringLiteral("005930 · 시가→종가 · 예측 없음"));
        QCOMPARE(daily_title("005930", otc, morning(9), {}, false),
                 QStringLiteral("005930 · 10-09 시가→종가 · 예측 없음"));
        // Without days enough to set the band: no band on the title.
        KindPrediction unbanded = daily_forecast("cox", 12, 0.35, 0.33, 0.32);
        unbanded.band.reset();
        QCOMPARE(daily_title("005930", otc, morning(12), {unbanded}, false),
                 QStringLiteral("005930 · 10-12 시가→종가 (08:50 예측) · 채점 전 (20:35부터)"));
        QVERIFY(
            daily_tip(otc).startsWith(QStringLiteral("하루 한 번 예측이다. 거래일 08:50에, 그날 시가 단일가부터 종가 "
                                                     "단일가까지의 방향을 상승·보합·하락 셋 중 하나로 예측한다.")));
        QVERIFY(daily_tip(otc).contains(QStringLiteral("정답이 있는 날 60·120·180일째에 판정한다(최근 60일)")));
        QVERIFY(daily_tip(range_kind()).isEmpty());
    }

    void the_band_is_the_answer_s_once_in_else_the_predictions() {
        KindPrediction p = daily_forecast("cox", 8, 0.3, 0.3, 0.4);
        QCOMPARE(daily_band({p}), std::optional<double>(0.0047));
        KindAnswer a = daily_answer("down", 81'950, 81'250);
        a.open_to_close->band = 0.0051;
        QCOMPARE(daily_band({with(p, a)}), std::optional<double>(0.0051));
        p.band.reset();
        QVERIFY(!daily_band({p}).has_value());
        QCOMPARE(band_text(0.004732), QStringLiteral("보합 ±0.47%"));
    }

    void the_day_s_call_is_the_mean_of_its_algorithms_without_the_baselines() {
        const KindInfo otc = daily_kind();
        // One algorithm: its own forecast.
        auto call =
            daily_call({daily_forecast("cox", 8, 0.30, 0.30, 0.40), daily_forecast("otc-flat", 8, 0, 1, 0)}, otc);
        QVERIFY(call.has_value());
        QCOMPARE(call->klass, 2);
        QCOMPARE(call->algorithms, (QStringList{"cox"}));
        // Two: each weighing the same; a tie goes to flat, then up.
        call =
            daily_call({daily_forecast("zeta", 8, 0.50, 0.00, 0.50), daily_forecast("cox", 8, 0.25, 0.50, 0.25)}, otc);
        QCOMPARE(call->algorithms, (QStringList{"cox", "zeta"}));
        QCOMPARE(call->probabilities[0], 0.375);
        QCOMPARE(call->probabilities[1], 0.25);
        QCOMPARE(call->probabilities[2], 0.375);
        QCOMPARE(call->klass, 0); // up and down tie above flat: up
        QCOMPARE(daily_call({daily_forecast("cox", 8, 0.4, 0.4, 0.2)}, otc)->klass, 1); // a tie with flat: flat
        // Baselines alone, or an algorithm without a forecast: no call.
        KindPrediction timed_out = daily_forecast("cox", 8, 0, 0, 0);
        timed_out.status = "timed_out";
        QVERIFY(!daily_call({daily_forecast("otc-climate", 8, 0.3, 0.3, 0.4), timed_out}, otc).has_value());
    }

    void the_day_view_marks_each_day_s_call_and_its_result_on_its_candle() {
        const KindInfo otc = daily_kind();
        QMap<QDateTime, QList<KindPrediction>> closes;
        closes[morning(6)] = {with(daily_forecast("cox", 6, 0.5, 0.3, 0.2), daily_answer("up", 80'000, 80'800))};
        closes[morning(7)] = {
            with(daily_forecast("cox", 7, 0.5, 0.3, 0.2), daily_answer("down", 80'000, 79'000, 0, true))};
        closes[morning(8)] = {with(daily_forecast("otc-flat", 8, 0, 1, 0), daily_answer("flat", 80'000, 80'100))};
        closes[morning(12)] = {daily_forecast("cox", 12, 0.2, 0.5, 0.3)};
        closes[morning(13)] = {daily_forecast("cox", 13, 0.2, 0.3, 0.5)}; // no candle for it yet
        const QList<Bar> days{day_candle(5), day_candle(6), day_candle(7), day_candle(8), day_candle(12)};
        const auto icons = daily_icons(days, closes, otc);
        // 10-05 has no prediction, 10-08 no algorithm's, 10-13 no candle: three icons.
        QCOMPARE(icons.size(), 3);
        QCOMPARE(icons[0].time, chart_time(day_candle(6).start));
        QCOMPARE(icons[0].bar_close, morning(6)); // clicked: the day's prediction time
        QCOMPARE(icons[0].direction, Direction::Up);
        QCOMPARE(icons[0].arrow, QStringLiteral("▲"));
        QCOMPARE(icons[0].result, QStringLiteral("✓"));
        QVERIFY(!icons[0].faint && !icons[0].signal);
        QCOMPARE(icons[1].result, QStringLiteral("✗"));
        QVERIFY(icons[1].faint); // a circuit breaker or sidecar that day
        QCOMPARE(icons[2].direction, Direction::Flat);
        QCOMPARE(icons[2].arrow, QStringLiteral("–"));
        QCOMPARE(icons[2].result, QStringLiteral("?")); // today's, answered tonight
    }

    void the_day_view_s_box_has_the_day_its_answer_then_each_predictor() {
        const KindInfo otc = daily_kind();
        const KindAnswer down = daily_answer("down", 81'950, 81'250);
        QMap<QDateTime, QList<KindPrediction>> closes;
        closes[morning(8)] = {with(daily_forecast("otc-climate", 8, 0.28, 0.33, 0.39), down),
                              with(daily_forecast("zeta", 8, 0.40, 0.20, 0.40), down),
                              with(daily_forecast("cox", 8, 0.34, 0.32, 0.34), down)};
        closes[morning(12)] = {daily_forecast("cox", 12, 0.35, 0.33, 0.32)};
        const auto hovers =
            daily_hovers({day_candle(7), day_candle(8), day_candle(12)}, closes, otc, {{"cox", "probation"}});
        QCOMPARE(hovers.size(), 2); // none for a day without predictions
        const BarHover& h = hovers[0];
        QCOMPARE(h.time, chart_time(day_candle(8).start));
        QCOMPARE(h.window_from, 0); // no window: a day is no bar close
        QCOMPARE(h.window_to, 0);
        QVERIFY(!h.upper.has_value() && !h.lower.has_value());
        QCOMPARE(h.lines,
                 (QStringList{
                     QStringLiteral("10-08 시가→종가 (08:50 예측) · 정답 하락 -0.85% (81,950 → 81,250) · 보합 ±0.47%"),
                     QStringLiteral("알고리즘 평균 (2) · 상승 · 상승 37% · 보합 26% · 하락 37% · ✗"),
                     QStringLiteral("cox · 수습 · 상승 · 상승 34% · 보합 32% · 하락 34% · ✗"),
                     QStringLiteral("zeta · 상승 · 상승 40% · 보합 20% · 하락 40% · ✗"),
                     QStringLiteral("기준: 시가→종가 기후 · 하락 · 상승 28% · 보합 33% · 하락 39% · ✓"),
                 }));
        // Today's, one algorithm: no mean line, its own is the icon's.
        QCOMPARE(hovers[1].lines,
                 (QStringList{
                     QStringLiteral("10-12 시가→종가 (08:50 예측) · 채점 전 (20:35부터) · 보합 ±0.47%"),
                     QStringLiteral("cox · 수습 · 상승 · 상승 35% · 보합 33% · 하락 32% · ?"),
                 }));
    }

    void the_day_view_scores_the_calls_of_the_days_in_view() {
        const KindInfo otc = daily_kind();
        QMap<QDateTime, QList<KindPrediction>> closes;
        closes[morning(1)] = {with(daily_forecast("cox", 1, 0.5, 0.3, 0.2), daily_answer("up", 80'000, 80'800))};
        closes[morning(2)] = {with(daily_forecast("cox", 2, 0.5, 0.3, 0.2), daily_answer("flat", 80'000, 80'100))};
        closes[morning(5)] = {with(daily_forecast("cox", 5, 0.2, 0.3, 0.5), daily_answer("down", 80'000, 79'000))};
        closes[morning(6)] = {
            with(daily_forecast("cox", 6, 0.2, 0.3, 0.5), daily_answer("up", 80'000, 81'000, 0, true))};
        closes[morning(7)] = {with(daily_forecast("cox", 7, 0.2, 0.5, 0.3), KindAnswer{})}; // unscorable
        closes[morning(8)] = {daily_forecast("cox", 8, 0.2, 0.5, 0.3)};                     // not answered yet
        closes[morning(12)] = {with(daily_forecast("cox", 12, 0.5, 0.3, 0.2), daily_answer("up", 80'000, 81'000))};
        // 10-01 to 10-08 in view: 10-12 out of it.
        const auto daily = daily_score(closes, otc, chart_time(day_candle(1).start), chart_time(day_candle(8).start));
        QCOMPARE(daily.algorithms, (QStringList{"cox"}));
        QCOMPARE(daily.score.n, 3);
        QCOMPARE(daily.score.hits, 2);
        QCOMPARE(daily.score.flat_hits, 1);
        QCOMPARE(daily.score.up_n, 2);
        QCOMPARE(daily.score.up_hits, 1);
        QCOMPARE(daily.score.down_n, 1);
        QCOMPARE(daily.score.down_hits, 1);
        QCOMPARE(daily.score.days, 3);
        QCOMPARE(daily.score.special, 1);
        QCOMPARE(
            daily_score_text(daily),
            QStringLiteral("cox · 맞음 67% · 늘 보합 33% · ▲ 맞음 50% (2) · ▼ 맞음 100% (1) · 3일 · 특수 1일 제외"));
        // Only today's in view, before its answer: none scored yet.
        const auto pending = daily_score(closes, otc, chart_time(day_candle(8).start), chart_time(day_candle(8).start));
        QCOMPARE(daily_score_text(pending), QStringLiteral("cox · 채점된 날 없음"));
        // Several algorithms; and no call in view at all: nothing to say.
        closes[morning(1)] << with(daily_forecast("zeta", 1, 0.5, 0.3, 0.2), daily_answer("up", 80'000, 80'800));
        QVERIFY(daily_score_text(daily_score(closes, otc, 0, chart_time(day_candle(1).start)))
                    .startsWith(QStringLiteral("알고리즘 평균 (2) · 맞음 100%")));
        QVERIFY(daily_score_text(daily_score(closes, otc, 0, 1)).isEmpty());
    }

    void a_daily_kind_s_predictions_are_read_by_days_back_to_the_first_day_held() {
        // None read yet: the latest year's, with or without days held.
        auto read = daily_read({}, {}, false);
        QVERIFY(read.has_value());
        QVERIFY(!read->before.isValid());
        QCOMPARE(read->limit, kDailyPage);
        QCOMPARE(daily_read({}, QDate(2026, 1, 2), true)->limit, kDailyPage);
        // Read back to 10-01; on a chart read, the latest few days again for their answers.
        read = daily_read(morning(1), QDate(2026, 9, 1), true);
        QVERIFY(!read->before.isValid());
        QCOMPARE(read->limit, kDailyRefresh);
        // Then the days before 10-01 back to 09-01 (30 days and 09-01 itself), the engine's most at once.
        read = daily_read(morning(1), QDate(2026, 9, 1), false);
        QCOMPARE(read->before, morning(1));
        QCOMPARE(read->limit, 31);
        QCOMPARE(daily_read(morning(1), QDate(2020, 1, 2), false)->limit, kPredictionPage);
        QVERIFY(!daily_read(morning(1), QDate(2026, 10, 1), false).has_value());      // reached
        QVERIFY(!daily_read(morning(1), {}, false).has_value());                      // nothing held
        QVERIFY(!daily_read(from_the_start(), QDate(2020, 1, 2), false).has_value()); // the engine has none before
        // A page moves it to its earliest day; an empty one: none before, all read.
        const QList<KindPrediction> page{daily_forecast("cox", 8, 0.3, 0.3, 0.4),
                                         daily_forecast("cox", 6, 0.3, 0.3, 0.4)};
        QCOMPARE(daily_from_after({}, page), morning(6));
        QCOMPARE(daily_from_after(morning(2), page), morning(2));
        QCOMPARE(daily_from_after(morning(6), {}), from_the_start());
        QCOMPARE(daily_from_after(from_the_start(), page), from_the_start()); // a refresh after all were read
    }
};

QTEST_GUILESS_MAIN(TstKrxKindView)
#include "tst_krx_kind_view.moc"
