// Unit tests for src/services/krx_quant/BarView.h
//
// A 1-minute bar double-clicked opens a window of its own (krx-quant #255): the bar, every direction
// prediction of its bar close, the range's and the barrier's there, the answer in detail and a mini
// chart of its four minutes with the levels the chart's hover draws (the touch targets, the range's
// highest and lowest mid-points, the barrier's U and L). These tests pin what each section says, the
// mini chart's slots, lines and dots and where they lie, when the window stops reading, and its
// title. The values are made up and the expected ones worked out by hand.

#include "services/krx_quant/BarView.h"

#include <QTest>
#include <QTimeZone>

#include <algorithm>

using namespace fincept::krx_quant;

namespace {

const QTimeZone kKst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);

/// A moment of 2026-10-16 in KST, "HH:mm:ss" or "HH:mm:ss.zzz".
QDateTime at(const char* time, int day = 16) {
    const QString text = QString::fromLatin1(time);
    return QDateTime(QDate(2026, 10, day), QTime::fromString(text, text.contains('.') ? "HH:mm:ss.zzz" : "HH:mm:ss"),
                     kKst);
}

Bar bar(const char* start, double open, double high, double low, double close, double volume, std::optional<int> trades,
        int day = 16) {
    return Bar{at(start, day), open, high, low, close, volume, trades};
}

/// The bars before 10:31: 10:25, the bar 10:26, then 10:27, none at 10:28 (no trades), 10:29, 10:30.
QList<Bar> bars_around() {
    return {
        bar("10:25:00", 80'900, 81'000, 80'900, 81'000, 10, 3), bar("10:26:00", 81'000, 81'400, 81'000, 81'100, 19, 4),
        bar("10:27:00", 81'100, 81'300, 81'000, 81'200, 12, 5), bar("10:29:00", 81'200, 81'400, 81'200, 81'300, 8, 2),
        bar("10:30:00", 81'300, 81'500, 81'300, 81'400, 6, 1)};
}

/// The bar close's touch label: up, the upper target 81,250 (81,050 + 2 ticks of 100) first reached
/// at 10:29:28 with the best bid at 81,300.
Touch touch_up() {
    Touch t;
    t.direction = Direction::Up;
    t.anchor = 81'050;
    t.tick = 100;
    t.k = 2;
    t.upper = 81'250;
    t.lower = 80'850;
    t.upper_reached = Reach{at("10:29:28"), 81'300};
    return t;
}

Prediction direction(const QString& algorithm, double up, double flat, double down, const std::optional<Touch>& touch,
                     const QString& status = "ok") {
    Prediction p;
    p.algorithm = algorithm;
    p.model_version = algorithm + "-20261015";
    p.status = status;
    p.symbol = "005930";
    p.bar_close = at("10:27:00");
    if (status == "ok") {
        p.p_up = up;
        p.p_flat = flat;
        p.p_down = down;
        p.expected_ticks = (up - down) * 2;
        p.target_at = at("10:31:00");
        if (touch) {
            Outcome o;
            o.touch = touch;
            p.outcome = o;
        }
    }
    return p;
}

/// The bar close's direction predictions, scored on ``touch`` (none: not yet).
QList<Prediction> directions(const std::optional<Touch>& touch) {
    Prediction final = direction("consensus-final", 0.5, 0.3, 0.2, touch);
    final.pooled = QStringList{"gauss"};
    return {final, direction("consensus-fast", 0.45, 0.35, 0.2, touch), direction("gauss", 0.5, 0.3, 0.2, touch),
            direction("markov", 0.2, 0.5, 0.3, touch), direction("baseline-flat", 0, 1, 0, touch)};
}

KindInfo range_kind() {
    KindInfo k;
    k.name = "range";
    k.title = QStringLiteral("변동폭");
    k.classes = {"small", "medium", "large"};
    k.class_titles = {QStringLiteral("작음"), QStringLiteral("보통"), QStringLiteral("큼")};
    k.baselines = {{"range-climate", std::nullopt}};
    return k;
}

KindInfo barrier_kind() {
    KindInfo k;
    k.name = "barrier";
    k.title = QStringLiteral("장벽");
    k.classes = {"up_first", "timeout", "down_first"};
    k.class_titles = {QStringLiteral("위 먼저"), QStringLiteral("시간 만료"), QStringLiteral("아래 먼저")};
    k.baselines = {{"barrier-climate", std::nullopt}};
    return k;
}

const std::optional<QList<KindInfo>> kKinds = QList<KindInfo>{range_kind(), barrier_kind()};

/// A prediction of another kind at the bar close, as the engine sends it with its bar close's
/// mid-point 81,050, tick 100, k 2 (krx-quant #200, #210) and, for the barrier, w 3 (#183).
KindPrediction kind_forecast(const QString& algorithm, const QMap<QString, double>& output, bool barrier) {
    KindPrediction p;
    p.algorithm = algorithm;
    p.model_version = algorithm + "-1";
    p.status = "ok";
    p.symbol = "005930";
    p.bar_close = at("10:27:00");
    p.target_at = at("10:31:00");
    p.flat_ticks = 2;
    p.anchor = 81'050;
    p.tick = 100;
    if (barrier)
        p.width_ticks = 3;
    p.output = output;
    return p;
}

/// The range's answer: r 2.5 ticks (81,250 down to 81,000), medium.
KindAnswer range_answer() {
    KindAnswer a;
    a.klass = "medium";
    a.scored_at = at("10:31:00");
    RangePath path;
    path.r = 2.5;
    path.k = 2;
    path.tick = 100;
    path.anchor = 81'050;
    path.max_mid = 81'250;
    path.max_mid_at = at("10:29:28.500");
    path.min_mid = 81'000;
    path.min_mid_at = at("10:27:30");
    a.range = path;
    return a;
}

/// The barrier's answer: U (81,350) first, reached at 10:30:05 with the best bid at 81,400.
KindAnswer barrier_answer() {
    KindAnswer a;
    a.klass = "up_first";
    a.scored_at = at("10:31:00");
    BarrierPath path;
    path.w = 3;
    path.k = 2;
    path.tick = 100;
    path.anchor = 81'050;
    path.upper = 81'350;
    path.lower = 80'750;
    path.up_at = at("10:30:05");
    path.up_bid = 81'400;
    a.barrier = path;
    return a;
}

/// The bar 10:26–10:27 of 005930 as its window reads it: scored (``answered``) or not yet.
BarRead read_of(bool answered) {
    BarRead read;
    read.symbol = "005930";
    read.bar_close = at("10:27:00");
    read.bars = bars_around();
    read.predictions = directions(answered ? std::optional(touch_up()) : std::nullopt);
    read.statuses = {{"gauss", "active"}, {"markov", "probation"}};
    QList<KindPrediction> range{
        kind_forecast("mandelbrot",
                      {{"p_small", 0.2}, {"p_medium", 0.5}, {"p_large", 0.3}, {"q10", 1}, {"q50", 2.5}, {"q90", 5}},
                      false),
        kind_forecast("range-climate",
                      {{"p_small", 0.6}, {"p_medium", 0.3}, {"p_large", 0.1}, {"q10", 0.5}, {"q50", 1.5}, {"q90", 3}},
                      false)};
    QList<KindPrediction> barrier{
        kind_forecast("breiman", {{"p_up_first", 0.5}, {"p_timeout", 0.3}, {"p_down_first", 0.2}}, true),
        kind_forecast("barrier-climate", {{"p_up_first", 0.2}, {"p_timeout", 0.6}, {"p_down_first", 0.2}}, true)};
    if (answered) {
        for (auto& p : range)
            p.answer = range_answer();
        for (auto& p : barrier)
            p.answer = barrier_answer();
    }
    read.kinds = {{"range", range}, {"barrier", barrier}};
    // A VI over its four minutes; the morning's circuit breaker is not over them.
    read.special = {SpecialWindow{"circuit_breaker", {}, at("09:06:00"), at("09:36:00"), QStringLiteral("1단계")},
                    SpecialWindow{"vi", "005930", at("10:28:10"), at("10:30:20"), "dynamic"}};
    return read;
}

} // namespace

class TstKrxBarView : public QObject {
    Q_OBJECT

  private slots:
    void a_window_is_named_by_its_symbol_and_bar_close() {
        // The key is the same however the bar close's zone reads.
        QCOMPARE(bar_key("005930", at("10:27:00").toUTC()), QStringLiteral("005930@2026-10-16T10:27:00+09:00"));
        QCOMPARE(bar_key("005930", at("10:27:00")), bar_key("005930", at("10:27:00").toUTC()));
        QVERIFY(bar_key("005930", at("10:27:00")) != bar_key("000660", at("10:27:00")));
        // Named as the chart names its bars, by the close (the hover's "10:27 봉 → 10:31 목표").
        QCOMPARE(bar_title(QStringLiteral("삼성전자"), "005930", at("10:27:00")),
                 QStringLiteral("삼성전자 005930 · 2026-10-16 10:27 봉"));
        QCOMPARE(bar_title({}, "005930", at("10:27:00")), QStringLiteral("005930 · 2026-10-16 10:27 봉"));
        QCOMPARE(bar_scoring_at(read_of(true)), at("10:31:00"));
        BarRead none;
        none.bar_close = at("10:27:00");
        QCOMPARE(bar_scoring_at(none), at("10:31:00")); // no prediction: four minutes on
    }

    void the_bar_s_prices_trades_change_and_special_situations() {
        const QList<BarLine> lines = bar_info_lines(read_of(true), at("10:40:00"));
        const QList<BarLine> expected{
            {"봉 시각", "10:26–10:27"},
            {"시가", "81,000"},
            {"고가", "81,400"},
            {"저가", "81,000"},
            {"종가", "81,100"},
            {"거래량", "19"},
            {"체결 수", "4"},
            // 100 / 81,000 = 0.12%; the bar before by its close.
            {"앞 봉 대비", "+100원 (+0.12%) · 10:26 봉 종가 81,000"},
            {"특수 상황", "동적 VI 10:28:10–10:30:20"},
        };
        QCOMPARE(lines, expected);
    }

    void a_past_day_s_bar_has_no_trades_and_its_bar_before_may_be_the_day_before() {
        BarRead read;
        read.symbol = "005930";
        read.bar_close = at("09:01:00", 15);
        read.bars = {bar("19:59:00", 80'100, 80'100, 80'000, 80'000, 50, std::nullopt, 14),
                     bar("09:00:00", 80'000, 80'900, 79'900, 80'800, 1'234'567, std::nullopt, 15)};
        const QList<BarLine> lines = bar_info_lines(read, at("08:00:00"));
        QCOMPARE(lines.value(5), (BarLine{"거래량", "1,234,567"}));
        QCOMPARE(lines.value(6), (BarLine{"체결 수", "—"}));
        QCOMPARE(lines.value(7), (BarLine{"앞 봉 대비", "+800원 (+1.00%) · 10-14 20:00 봉 종가 80,000"}));
        QCOMPARE(lines.value(8), (BarLine{"특수 상황", "없음"}));
        // A fall, and the first bar the engine has: no bar before.
        read.bars = {bar("09:00:00", 80'000, 80'000, 79'000, 79'200, 10, 1, 15)};
        QCOMPARE(bar_info_lines(read, at("08:00:00")).value(7), (BarLine{"앞 봉 대비", "—"}));
        read.bars = {bar("08:59:00", 80'000, 80'000, 80'000, 80'000, 1, 1, 15),
                     bar("09:00:00", 80'000, 80'000, 79'000, 79'200, 10, 1, 15)};
        QCOMPARE(bar_info_lines(read, at("08:00:00")).value(7),
                 (BarLine{"앞 봉 대비", "-800원 (-1.00%) · 09:00 봉 종가 80,000"}));
    }

    void an_empty_bar_says_so_and_one_being_made_too() {
        BarRead read = read_of(false);
        read.bar_close = at("10:29:00"); // 10:28–10:29: no trades
        const QList<BarLine> empty = bar_info_lines(read, at("10:29:30"));
        QCOMPARE(empty.size(), 3);
        QCOMPARE(empty[0], (BarLine{"봉 시각", "10:28–10:29"}));
        QCOMPARE(empty[1], (BarLine{"가격", "체결 없음"}));
        QCOMPARE(empty[2], (BarLine{"특수 상황", "동적 VI 10:28:10–10:30:20"}));
        read.bar_close = at("10:32:00"); // 10:31–10:32, not over at 10:31:20
        read.special.clear();
        QCOMPARE(bar_info_lines(read, at("10:31:20")).value(1), (BarLine{"가격", "만드는 중"}));
        QCOMPARE(bar_info_lines(read, at("10:31:20")).value(2), (BarLine{"특수 상황", "없음"}));
    }

    void the_answer_in_detail_once_scored() {
        const QList<BarLine> expected{
            {"봉 마감 중간가", "81,050"},
            {"호가단위", "100원"},
            {"k (보합 폭)", "2틱"},
            {"위·아래 도달 목표", "위 81,250 · 아래 80,850"},
            {"도달 정답", "정답 상승 (10:29:28 닿음)"},
            {"위 목표에 처음 닿음", "10:29:28 · 매수 1호가 81,300"},
            {"아래 목표에 처음 닿음", "닿지 않음"},
            {"실제 변동폭 r", "2.5틱 · 보통"},
            {"실제 최고 중간가", "81,250 (10:29:28)"},
            {"실제 최저 중간가", "81,000 (10:27:30)"},
            {"장벽 w · U · L", "w 3틱 · U 81,350 · L 80,750"},
            {"먼저 닿은 장벽", "위 먼저 (10:30:05 닿음)"},
            {"U에 처음 닿음", "10:30:05 · 매수 1호가 81,400"},
            {"L에 처음 닿음", "닿지 않음"},
            {"채점 시각", "10:31"},
        };
        QCOMPARE(answer_lines(read_of(true), kKinds), expected);
    }

    void before_its_answer_the_reference_comes_with_the_other_kinds_predictions() {
        // krx-quant #200, #210: the bar close's mid-point, tick and k; the targets and U and L from them.
        const QList<BarLine> expected{
            {"봉 마감 중간가", "81,050"},
            {"호가단위", "100원"},
            {"k (보합 폭)", "2틱"},
            {"위·아래 도달 목표", "위 81,250 · 아래 80,850"},
            {"도달 정답", "채점 전"},
            {"실제 변동폭 r", "채점 전"},
            {"장벽 w · U · L", "w 3틱 · U 81,350 · L 80,750"},
            {"먼저 닿은 장벽", "채점 전"},
            {"채점 시각", "10:31"},
        };
        QCOMPARE(answer_lines(read_of(false), kKinds), expected);
        // An engine that makes the direction alone: the targets are not known before the label.
        BarRead direction_only = read_of(false);
        direction_only.kinds.clear();
        const QList<BarLine> alone = answer_lines(direction_only, std::nullopt);
        QCOMPARE(alone.size(), 6);
        QCOMPARE(alone[0], (BarLine{"봉 마감 중간가", "—"}));
        QCOMPARE(alone[3], (BarLine{"위·아래 도달 목표", "—"}));
        QCOMPARE(alone[5], (BarLine{"채점 시각", "10:31"}));
    }

    void one_that_could_not_be_scored_or_a_special_situation_touched_says_so() {
        Touch silent;
        silent.direction = Direction::None;
        silent.unscorable = "silent quotes";
        silent.anchor = 81'050;
        silent.tick = 100;
        silent.k = 2;
        BarRead read = read_of(false);
        read.predictions = directions(silent);
        read.kinds.clear();
        const QList<BarLine> lines = answer_lines(read, std::nullopt);
        QCOMPARE(lines.size(), 6);
        QCOMPARE(lines[3], (BarLine{"위·아래 도달 목표", "—"})); // the label has no targets: none guessed
        QCOMPARE(lines[4], (BarLine{"도달 정답", "채점 불가 (silent quotes)"}));

        Touch down = touch_up();
        down.direction = Direction::Down;
        down.upper_reached.reset();
        down.lower_reached = Reach{at("10:28:01"), 80'800};
        read.predictions = directions(down);
        for (auto& p : read.predictions)
            if (p.outcome)
                p.outcome->special = true;
        KindAnswer unscorable = range_answer();
        unscorable.klass.clear();
        unscorable.unscorable = "rest quotes";
        unscorable.range.reset();
        QList<KindPrediction> range{
            kind_forecast("mandelbrot", {{"p_small", 1}, {"p_medium", 0}, {"p_large", 0}}, false)};
        range[0].answer = unscorable;
        read.kinds = {{"range", range}};
        const QList<BarLine> special = answer_lines(read, QList<KindInfo>{range_kind()});
        QCOMPARE(special[4], (BarLine{"도달 정답", "정답 하락 (10:28:01 닿음) · 특수 상황"}));
        QCOMPARE(special[5], (BarLine{"위 목표에 처음 닿음", "닿지 않음"}));
        QCOMPARE(special[6], (BarLine{"아래 목표에 처음 닿음", "10:28:01 · 매도 1호가 80,800"}));
        QCOMPARE(special[7], (BarLine{"실제 변동폭 r", "채점 불가 (rest quotes)"}));
        QCOMPARE(special[8], (BarLine{"채점 시각", "10:31"})); // no barrier: the engine makes none
    }

    void the_range_and_the_barrier_as_their_panels_with_the_results() {
        const auto sections = kind_sections(read_of(true), kKinds);
        QCOMPARE(sections.size(), 2);
        const KindSection& range = sections[0];
        QCOMPARE(range.kind, QStringLiteral("range"));
        QCOMPARE(range.title, QStringLiteral("변동폭 예측"));
        QCOMPARE(range.note, QStringLiteral("정답 2.5틱 · 보통 · k 2틱 · 2k 4틱"));
        QStringList headers;
        for (const auto& c : range.columns)
            headers << c.header;
        QCOMPARE(headers, (QStringList{"예측", "구간", "작음", "보통", "큼", "q10", "q50", "q90", "결과", "모델"}));
        QCOMPARE(range.rows.size(), 2);
        QCOMPARE(range.rows[0].name, QStringLiteral("mandelbrot")); // the algorithms, then the baselines
        QCOMPARE(range.rows[0].top, QStringLiteral("보통"));
        QCOMPARE(range.rows[0].result, QStringLiteral("✓ 맞음"));
        QVERIFY(range.rows[1].baseline);
        QCOMPARE(range.rows[1].result, QStringLiteral("✗ 틀림"));

        const KindSection& barrier = sections[1];
        QCOMPARE(barrier.title, QStringLiteral("장벽 예측"));
        QCOMPARE(barrier.note, QStringLiteral("정답 위 먼저 (10:30:05 닿음) · w 3틱 · U 81,350 · L 80,750"));
        headers.clear();
        for (const auto& c : barrier.columns)
            headers << c.header;
        QCOMPARE(headers, (QStringList{"예측", "예측 클래스", "위 먼저", "시간 만료", "아래 먼저", "결과", "모델"}));
        QCOMPARE(barrier.rows[0].name, QStringLiteral("breiman"));
        QCOMPARE(barrier.rows[0].result, QStringLiteral("✓ 맞음"));
        QCOMPARE(barrier.rows[1].result, QStringLiteral("✗ 틀림")); // "시간 만료" predicted

        // Before the answer: the widths and barriers still, the results "채점 전".
        const auto before = kind_sections(read_of(false), kKinds);
        QCOMPARE(before[0].note, QStringLiteral("채점 전 · k 2틱 · 2k 4틱"));
        QCOMPARE(before[1].note, QStringLiteral("채점 전 · w 3틱 · U 81,350 · L 80,750"));
        QCOMPARE(before[0].rows[0].result, QStringLiteral("채점 전"));
    }

    void a_kind_the_engine_does_not_make_or_could_not_be_read_says_so() {
        BarRead read = read_of(true);
        const auto none = kind_sections(read, std::nullopt);
        QCOMPARE(none.size(), 2);
        QCOMPARE(none[0].title, QStringLiteral("변동폭 예측"));
        QCOMPARE(none[0].note, QStringLiteral("엔진이 이 종류를 예측하지 않는다"));
        QVERIFY(none[0].rows.isEmpty() && none[0].columns.isEmpty());
        QCOMPARE(none[1].title, QStringLiteral("장벽 예측"));

        read.kind_failures.insert("barrier", QStringLiteral("엔진 오류(500)"));
        read.kinds.remove("barrier");
        read.kinds["range"].clear();
        const auto sections = kind_sections(read, kKinds);
        QCOMPARE(sections[0].note, QStringLiteral("그 봉 마감의 예측 없음"));
        QVERIFY(sections[0].rows.isEmpty());
        QCOMPARE(sections[1].note, QStringLiteral("읽지 못함: 엔진 오류(500)"));
    }

    void the_direction_rows_carry_their_expected_ticks_and_models() {
        const auto rows = close_rows(read_of(true).predictions, read_of(true).statuses, {"gauss", "markov"});
        QCOMPARE(rows.size(), 6); // the selected consensus first (krx-quant #159)
        QCOMPARE(rows[0].name, QStringLiteral("선택 합의"));
        QVERIFY(!rows[0].expected_ticks.has_value());
        QVERIFY(rows[0].model_version.isEmpty());
        const auto gauss =
            std::find_if(rows.cbegin(), rows.cend(), [](const CloseRow& r) { return r.algorithm == "gauss"; });
        QVERIFY(gauss != rows.cend());
        QVERIFY(gauss->expected_ticks && qFuzzyCompare(*gauss->expected_ticks, 0.6)); // (0.5 − 0.2) × 2
        QCOMPARE(gauss->model_version, QStringLiteral("gauss-20261015"));
        QCOMPARE(gauss->status, QStringLiteral("정식"));
        QCOMPARE(gauss->in_consensus, QStringLiteral("포함"));
        QCOMPARE(gauss->result, QStringLiteral("✓ 맞음"));
    }

    void the_mini_chart_once_scored() {
        const MiniChart m = mini_chart(read_of(true), at("10:40:00"));
        QCOMPARE(m.from, chart_time(at("10:26:00")));
        QCOMPARE(m.window_from, chart_time(at("10:27:00")));
        QCOMPARE(m.window_to, chart_time(at("10:31:00")));
        QVERIFY(!m.open);
        QCOMPARE(m.now, chart_time(at("10:40:00")));
        // The bar and the four after it, a minute each; 10:28 had no trades.
        QCOMPARE(m.candles.size(), 5);
        QCOMPARE(m.candles[0].time, chart_time(at("10:26:00")));
        QCOMPARE(m.candles[0].close, 81'100.0);
        QVERIFY(m.candles[2].empty);
        QCOMPARE(m.candles[2].time, chart_time(at("10:28:00")));
        QCOMPARE(m.candles[4].close, 81'400.0);
        const QList<MiniLevel> levels{
            {"위 목표", 81'250, true, false},     {"아래 목표", 80'850, false, false},
            {"최고 중간가", 81'250, true, false}, {"최저 중간가", 81'000, false, false},
            {"U", 81'350, true, false},           {"L", 80'750, false, false},
        };
        QCOMPARE(m.levels, levels);
        QCOMPARE(m.points.size(), 4);
        QCOMPARE(m.points[0], (MiniPoint{chart_time(at("10:29:28")), 81'300, true,
                                         QStringLiteral("위 목표에 닿음 10:29:28 · 매수 1호가 81,300")}));
        QCOMPARE(m.points[1].time, chart_time(at("10:29:28"))); // the range's highest, to the second
        QCOMPARE(m.points[2], (MiniPoint{chart_time(at("10:27:30")), 81'000, false,
                                         QStringLiteral("최저 중간가 10:27:30 · 81,000")}));
        QCOMPARE(m.points[3], (MiniPoint{chart_time(at("10:30:05")), 81'400, true,
                                         QStringLiteral("U에 닿음 10:30:05 · 매수 1호가 81,400")}));
        // From the lowest drawn (L 80,750) to the highest (the 10:30 bar's 81,500), 8% more each way.
        QCOMPARE(m.low, 80'750 - 60.0);
        QCOMPARE(m.high, 81'500 + 60.0);
    }

    void before_its_answers_the_mini_chart_s_lines_are_dashed() {
        BarRead read = read_of(false);
        read.bars = bars_around().mid(0, 3); // up to the 10:27 bar, at 10:29
        const MiniChart m = mini_chart(read, at("10:29:00"));
        QVERIFY(m.open);
        QCOMPARE(m.candles.size(), 5);
        QVERIFY(!m.candles[1].empty);
        QVERIFY(m.candles[2].empty && m.candles[3].empty && m.candles[4].empty);
        // The targets from the bar close's mid-point ± k ticks, U and L ± w ticks; no range lines yet.
        const QList<MiniLevel> levels{
            {"위 목표", 81'250, true, true},
            {"아래 목표", 80'850, false, true},
            {"U", 81'350, true, true},
            {"L", 80'750, false, true},
        };
        QCOMPARE(m.levels, levels);
        QVERIFY(m.points.isEmpty());
        // An engine that gives no reference before the answer: the window and the bars alone.
        read.kinds.clear();
        const MiniChart bare = mini_chart(read, at("10:29:00"));
        QVERIFY(bare.levels.isEmpty());
        QCOMPARE(bare.low, 81'000 - 0.08 * 400);
        QCOMPARE(bare.high, 81'400 + 0.08 * 400);
        // Nothing at all: no prices to show.
        BarRead nothing;
        nothing.bar_close = at("10:27:00");
        const MiniChart empty = mini_chart(nothing, at("10:29:00"));
        QCOMPARE(empty.candles.size(), 5);
        QCOMPARE(empty.low, empty.high);
    }

    void the_mini_chart_places_moments_and_prices_and_spreads_its_labels() {
        MiniChart m;
        m.from = 1000;
        m.window_from = 1060;
        m.window_to = 1300;
        m.low = 0;
        m.high = 100;
        QCOMPARE(mini_x(m, 1000, 10, 300), 10.0);
        QCOMPARE(mini_x(m, 1150, 10, 300), 160.0);
        QCOMPARE(mini_x(m, 1300, 10, 300), 310.0);
        QCOMPARE(mini_y(m, 100, 5, 200), 5.0);
        QCOMPARE(mini_y(m, 25, 0, 200), 150.0);
        m.high = m.low; // nothing to show: the middle
        QCOMPARE(mini_y(m, 40, 0, 200), 100.0);

        QCOMPARE(spread_labels({50, 52, 100}, 10, 0, 200), (QList<double>{50, 60, 100}));
        QCOMPARE(spread_labels({100, 50}, 10, 0, 200), (QList<double>{100, 50})); // in their own order
        QCOMPARE(spread_labels({195, 198}, 10, 0, 200), (QList<double>{190, 200}));
        QCOMPARE(spread_labels({-5, 3}, 10, 0, 200), (QList<double>{0, 10}));
        QVERIFY(spread_labels({}, 10, 0, 200).isEmpty());
    }

    void the_window_reads_until_nothing_more_is_to_come() {
        // Before its four minutes are over: read again.
        QVERIFY(!bar_settled(read_of(false), at("10:29:00")));
        QCOMPARE(bar_state_text(read_of(false), at("10:29:00")), QStringLiteral("채점 전 · 갱신 중"));
        // Over, the outcomes not in yet: still.
        QVERIFY(!bar_settled(read_of(false), at("10:31:05")));
        // The direction scored, the range's answer not yet: still.
        BarRead half = read_of(true);
        for (auto& p : half.kinds["range"])
            p.answer.reset();
        QVERIFY(!bar_settled(half, at("10:31:05")));
        // Everything in: settled.
        QVERIFY(bar_settled(read_of(true), at("10:31:05")));
        QCOMPARE(bar_state_text(read_of(true), at("10:31:05")), QStringLiteral("채점됨"));
        // Ten minutes past its scoring time, whatever came.
        QVERIFY(!bar_settled(read_of(false), at("10:40:59")));
        QVERIFY(bar_settled(read_of(false), at("10:41:00")));
        QCOMPARE(bar_state_text(read_of(false), at("10:41:00")), QStringLiteral("채점되지 않음"));
        // No forecast made (a VI): nothing to score once its minutes are over.
        BarRead vi = read_of(false);
        vi.predictions = {direction("gauss", 0, 0, 0, std::nullopt, "vi")};
        vi.kinds.clear();
        QVERIFY(!bar_settled(vi, at("10:30:00")));
        QVERIFY(bar_settled(vi, at("10:31:00")));
        // An earlier day's bar: read once.
        QVERIFY(bar_settled(read_of(false), at("08:00:00", 17)));
        // A kind that could not be read is read again: today's until ten minutes past its scoring
        // time, an earlier day's until it is read.
        BarRead failed = read_of(true);
        failed.kinds.remove("barrier");
        failed.kind_failures.insert("barrier", QStringLiteral("엔진 오류(500)"));
        QVERIFY(!bar_settled(failed, at("10:31:05")));
        QCOMPARE(bar_state_text(failed, at("10:31:05")), QStringLiteral("채점 전 · 갱신 중"));
        QVERIFY(bar_settled(failed, at("10:41:00")));
        QVERIFY(!bar_settled(failed, at("08:00:00", 17)));
        // One that could not be scored.
        Touch silent;
        silent.direction = Direction::None;
        silent.unscorable = "silent quotes";
        BarRead unscorable = read_of(true);
        unscorable.predictions = directions(silent);
        QCOMPARE(bar_state_text(unscorable, at("10:32:00")), QStringLiteral("채점 불가"));
    }
};

QTEST_GUILESS_MAIN(TstKrxBarView)
#include "tst_krx_bar_view.moc"
