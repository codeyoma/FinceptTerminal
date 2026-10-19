// Unit tests for src/services/krx_quant/DashboardView.h
//
// The dashboard's KRX widgets (codeyoma/krx-quant#302) show, beside the other market tiles,
// what the KRX screen shows at length: the leaderboard's top predictors with their hit rates
// first, each symbol's final consensus and whether it is a signal, the engine's health, and
// today's open-to-close call. The engine's answers come in as its JSON; the widgets only draw
// the rows these functions make.

#include "services/krx_quant/DashboardView.h"

#include <QJsonDocument>
#include <QTest>
#include <QTimeZone>

using namespace fincept::krx_quant;
using namespace fincept::krx_quant::dashboard;

namespace {

QJsonDocument json(const char* text) {
    return QJsonDocument::fromJson(QByteArray(text));
}

// Two ranked (one with every number, one with little), the consensus unranked with nothing
// scored, and two baselines: "늘 보합" is the one to beat.
const char* kLeaderboard = R"({
  "symbol": "all", "period": "today", "days": ["2026-10-19"], "special": "exclude", "min_sample": 200,
  "ranked": [
    {"rank": 1, "algorithm": "newton", "status": "active", "model_versions": ["newton-2"], "n": 1128,
     "accuracy": {"value": 0.52, "low": 0.49, "high": 0.55, "n": 1128},
     "precision": null, "coverage": null, "brier": null,
     "pnl": {"trades": 96, "total_ticks": 12.4, "per_trade": {"value": 0.13, "low": -0.2, "high": 0.46, "n": 96}},
     "pnl_inverse": {"trades": 80, "total_ticks": 49.6, "per_trade": {"value": 0.62, "low": 0.11, "high": 1.13, "n": 80}},
     "lift": {"low": 0.01, "day_low": -0.01, "vs": {}}},
    {"rank": 2, "algorithm": "gauss", "status": "probation", "model_versions": ["gauss-1"], "n": 900,
     "accuracy": {"value": 0.50, "low": 0.47, "high": 0.53, "n": 900},
     "precision": null, "coverage": null, "brier": null,
     "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null},
     "lift": {"low": -0.03, "vs": {}}}
  ],
  "unranked": [
    {"rank": null, "algorithm": "consensus-final", "status": null, "model_versions": [], "n": 0,
     "accuracy": null, "precision": null, "coverage": null, "brier": null,
     "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}
  ],
  "baselines": [
    {"rank": null, "algorithm": "baseline-persist", "model_versions": ["v1"], "n": 1128,
     "accuracy": {"value": 0.40, "low": 0.37, "high": 0.43, "n": 1128},
     "precision": null, "coverage": null, "brier": null, "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}},
    {"rank": null, "algorithm": "baseline-flat", "model_versions": ["v1"], "n": 1128,
     "accuracy": {"value": 0.51, "low": 0.48, "high": 0.54, "n": 1128},
     "precision": null, "coverage": null, "brier": null, "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}
  ]
})";

Leaderboard leaderboard(const char* text) {
    auto parsed = parse_leaderboard(json(text));
    if (!parsed.is_ok())
        qFatal("bad fixture: %s", parsed.error().c_str());
    return parsed.value();
}

QDateTime at(const char* hhmm) {
    return QDateTime(QDate(2026, 10, 19), QTime::fromString(hhmm, "HH:mm"), QTimeZone::fromSecondsAheadOfUtc(9 * 3600));
}

/// The final consensus at ``bar_close``, its members leaning ``votes``.
Prediction consensus(const char* bar_close, double up, double flat, double down, Votes votes,
                     const QString& symbol = "005930") {
    Prediction p;
    p.symbol = symbol;
    p.algorithm = "consensus-final";
    p.status = "ok";
    p.p_up = up;
    p.p_flat = flat;
    p.p_down = down;
    p.bar_close = at(bar_close);
    p.target_at = p.bar_close.addSecs(240);
    p.votes = votes;
    return p;
}

/// ``p`` scored: the touch label of its bar close says ``happened``.
Prediction scored(Prediction p, Direction happened) {
    Outcome o;
    Touch t;
    t.direction = happened;
    o.touch = t;
    p.outcome = o;
    return p;
}

SymbolQuote samsung() {
    SymbolQuote q;
    q.symbol = "005930";
    q.name = QStringLiteral("삼성전자");
    q.logo_url = "https://static.toss.im/png-icons/securities/icn-sec-fill-005930.png";
    q.mid = 81250.0;
    return q;
}

Latest latest_of(const Prediction& final_consensus) {
    return Latest{final_consensus.symbol, final_consensus.bar_close, {final_consensus}};
}

// A live engine mid-session: two feeds silent, a VI and a sidecar today, last night's model in.
const char* kStatus = R"({
  "mode": "live", "now": "2026-10-19T10:30:05+09:00",
  "commit": "0123456789abcdef0123456789abcdef01234567", "started_at": "2026-10-19T06:02:11+09:00",
  "phase": "continuous", "last_bar_close": "2026-10-19T10:30:00+09:00", "pending_outcomes": 12,
  "special": [
    {"kind": "vi", "symbol": "000660", "start": "2026-10-19T09:40:10+09:00", "end": "2026-10-19T09:42:20+09:00"},
    {"kind": "sidecar", "symbol": null, "start": "2026-10-19T10:01:00+09:00", "end": "2026-10-19T10:06:00+09:00"}
  ],
  "session": {"open": "2026-10-19T09:00:00+09:00", "close": "2026-10-19T15:30:00+09:00"},
  "training": {"day": "2026-10-18", "applied": true, "reason": "applied", "promoted": {"newton": "newton-3"}},
  "kis": {"connected": true, "feeds": [
    {"feed": "H0UNCNT0", "key": "005930", "last_received_at": "2026-10-19T10:30:03+09:00", "seconds_since": 2.0, "gap": null},
    {"feed": "H0UNASP0", "key": "000660", "last_received_at": "2026-10-19T10:14:05+09:00", "seconds_since": 960.0, "gap": "stale"},
    {"feed": "H0IFCNT0", "key": "A01612", "last_received_at": null, "seconds_since": null, "gap": "disconnected: closed"}
  ]},
  "usd_futures": null,
  "cpu": {"budget_seconds": 40.0, "algorithm_seconds": 8.0, "algorithms": {}, "expected_seconds": 18.0,
          "usage": 0.45, "skipped_today": 2}
})";

// ---- open_to_close (krx-quant #264) ----

KindInfo open_to_close() {
    KindInfo k;
    k.name = "open_to_close";
    k.title = QStringLiteral("시가→종가");
    k.classes = {"up", "flat", "down"};
    k.class_titles = {QStringLiteral("상승"), QStringLiteral("보합"), QStringLiteral("하락")};
    k.action = {"up", "down"};
    k.horizon_minutes = 0;
    k.baselines = {{"otc-flat", std::nullopt}, {"otc-climate", std::nullopt}, {"otc-persist", std::nullopt}};
    DailySchedule day;
    day.predict_at = "08:50";
    day.from = "open";
    day.to = "close";
    day.answer_from = "20:35";
    k.daily = day;
    return k;
}

/// ``algorithm``'s open-to-close forecast for ``symbol`` on 2026-10-<day> at 08:50; the day's band 0.47%.
KindPrediction daily(const QString& algorithm, const QString& symbol, int day, double up, double flat, double down) {
    KindPrediction p;
    p.algorithm = algorithm;
    p.status = "ok";
    p.symbol = symbol;
    p.bar_close = QDateTime(QDate(2026, 10, day), QTime(8, 50), QTimeZone::fromSecondsAheadOfUtc(9 * 3600));
    p.target_at = QDateTime(QDate(2026, 10, day), QTime(15, 30), QTimeZone::fromSecondsAheadOfUtc(9 * 3600));
    p.band = 0.0047;
    p.output = {{"p_up", up}, {"p_flat", flat}, {"p_down", down}};
    return p;
}

/// The day's answer: ``klass`` from the opening auction at ``open`` to the closing auction at ``close``.
KindPrediction answered(KindPrediction p, const QString& klass, double open, double close) {
    KindAnswer a;
    a.answer_version = 1;
    a.klass = klass;
    OpenToClosePath day;
    day.open = open;
    day.close = close;
    day.change = close / open - 1;
    day.band = 0.0047;
    a.open_to_close = day;
    p.answer = a;
    return p;
}

KindLatest kind_latest(const QString& symbol, const QList<KindPrediction>& predictions) {
    return KindLatest{symbol, "open_to_close", predictions.isEmpty() ? QDateTime() : predictions[0].bar_close,
                      predictions};
}

SymbolQuote quote(const QString& symbol, const QString& name) {
    SymbolQuote q;
    q.symbol = symbol;
    q.name = name;
    return q;
}

EngineStatus status_from(const char* text) {
    auto parsed = parse_status(json(text));
    if (!parsed.is_ok())
        qFatal("bad fixture: %s", parsed.error().c_str());
    return parsed.value();
}

} // namespace

class TstKrxDashboardView : public QObject {
    Q_OBJECT

  private slots:
    void the_leaderboard_lists_the_ranked_then_the_unranked_then_the_flat_baseline() {
        const auto rows = leaderboard_rows(leaderboard(kLeaderboard));

        QCOMPARE(rows.size(), 4);
        const auto& first = rows[0];
        QCOMPARE(first.algorithm, QStringLiteral("newton"));
        QCOMPARE(first.rank, QStringLiteral("1"));
        QCOMPARE(first.name, QStringLiteral("newton"));
        QCOMPARE(first.status, QStringLiteral("정식"));
        QCOMPARE(first.accuracy.text, QStringLiteral("52.0%"));
        QCOMPARE(first.accuracy.tip, QStringLiteral("95% 구간 49.0–55.0% (n 1128)"));
        QCOMPARE(first.day_low, QStringLiteral("-0.010"));
        QCOMPARE(first.n, QStringLiteral("1,128"));
        QCOMPARE(first.pnl, QStringLiteral("+0.13"));
        QCOMPARE(first.pnl_inverse, QStringLiteral("+0.62"));
        QVERIFY(!first.baseline);

        // Little measured: dashes, never 0.
        const auto& second = rows[1];
        QCOMPARE(second.rank, QStringLiteral("2"));
        QCOMPARE(second.status, QStringLiteral("수습"));
        QCOMPARE(second.accuracy.text, QStringLiteral("50.0%"));
        QCOMPARE(second.day_low, QStringLiteral("—"));
        QCOMPARE(second.pnl, QStringLiteral("—"));
        QCOMPARE(second.pnl_inverse, QStringLiteral("—"));

        const auto& unranked = rows[2];
        QCOMPARE(unranked.rank, QStringLiteral("—"));
        QCOMPARE(unranked.name, QStringLiteral("합의 (최종)"));
        QCOMPARE(unranked.status, QString());
        QCOMPARE(unranked.accuracy.text, QStringLiteral("—"));
        QCOMPARE(unranked.n, QStringLiteral("0"));

        // Only "늘 보합" of the direction's baselines: the one every algorithm must beat.
        const auto& baseline = rows[3];
        QCOMPARE(baseline.algorithm, QStringLiteral("baseline-flat"));
        QCOMPARE(baseline.rank, QStringLiteral("—"));
        QCOMPARE(baseline.name, QStringLiteral("기준: 늘 보합"));
        QCOMPARE(baseline.accuracy.text, QStringLiteral("51.0%"));
        QCOMPARE(baseline.day_low, QStringLiteral("—"));
        QVERIFY(baseline.baseline);
    }

    void another_kind_lists_top_at_most_and_each_of_its_baselines() {
        // The range (krx-quant #180) has no P&L and no "늘 보합": its own baselines are the bar.
        Leaderboard board;
        board.kind = "range";
        for (const char* name : {"mandelbrot", "range-lgbm", "range-third"}) {
            Entry e;
            e.algorithm = name;
            board.ranked << e;
        }
        Entry climate, persist;
        climate.algorithm = "range-climate";
        persist.algorithm = "range-persist";
        board.baselines = {climate, persist};

        const auto rows = leaderboard_rows(board, 2);

        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[1].algorithm, QStringLiteral("range-lgbm"));
        QCOMPARE(rows[1].pnl, QStringLiteral("—"));
        QCOMPARE(rows[2].name, QStringLiteral("기준: 변동폭 시간대 기후"));
        QCOMPARE(rows[3].name, QStringLiteral("기준: 직전 4분 변동폭"));
        QVERIFY(rows[2].baseline && rows[3].baseline);
    }

    void the_p_and_l_columns_read_in_ticks_or_basis_points() {
        QCOMPARE(pnl_unit("direction"), QStringLiteral("틱"));
        QCOMPARE(pnl_unit("barrier_30m"), QStringLiteral("틱"));
        QCOMPARE(pnl_unit("open_to_close"), QStringLiteral("bp"));
        QCOMPARE(pnl_unit("range"), QString());
    }

    void a_sure_final_consensus_with_enough_members_agreeing_is_a_live_signal() {
        // The KRX screen's rule (threshold 60%, 2 members), and the latest scored consensus
        // four bar closes back, which was right.
        const auto now = consensus("09:31", 0.64, 0.26, 0.10, Votes{2, 0, 1});
        const QMap<QString, QList<Prediction>> recent = {
            {"005930",
             {now, consensus("09:30", 0.5, 0.3, 0.2, Votes{1, 1, 1}),
              scored(consensus("09:27", 0.2, 0.3, 0.5, Votes{0, 1, 2}), Direction::Down)}}};

        const auto rows = signal_rows({samsung()}, {{"005930", latest_of(now)}}, recent, SignalRule{0.6, 2},
                                      at("09:31").addSecs(5));

        QCOMPARE(rows.size(), 1);
        const auto& row = rows[0];
        QCOMPARE(row.symbol, QStringLiteral("005930"));
        QCOMPARE(row.name, QStringLiteral("삼성전자"));
        QCOMPARE(row.price, QStringLiteral("81,250"));
        QCOMPARE(row.bar_close, QStringLiteral("09:31"));
        QCOMPARE(row.until, QStringLiteral("09:35"));
        QCOMPARE(row.up, QStringLiteral("64%"));
        QCOMPARE(row.flat, QStringLiteral("26%"));
        QCOMPARE(row.down, QStringLiteral("10%"));
        QCOMPARE(row.call, Direction::Up);
        QCOMPARE(row.members, QStringLiteral("정식 3개 중 2개 상승"));
        QCOMPARE(row.signal, QStringLiteral("상승 신호"));
        QCOMPARE(row.signal_direction, Direction::Up);
        QVERIFY(row.live);
        QCOMPARE(row.last, QStringLiteral("09:27 하락 ✓"));
        QCOMPARE(row.last_hit, std::optional<bool>(true));
    }

    void a_blocked_consensus_says_why_and_is_no_signal() {
        auto now = consensus("09:31", 0.70, 0.20, 0.10, Votes{3, 0, 0});
        now.blocked = "special";
        const auto rows =
            signal_rows({samsung()}, {{"005930", latest_of(now)}}, {}, SignalRule{0.6, 2}, at("09:31").addSecs(5));

        QCOMPARE(rows[0].signal, QStringLiteral("막힘: 특수 상황"));
        QCOMPARE(rows[0].signal_direction, Direction::None);
        QCOMPARE(rows[0].up, QStringLiteral("70%")); // the forecast still shows
        QCOMPARE(rows[0].last, QStringLiteral("—")); // nothing scored read yet
    }

    void a_signal_past_its_target_time_shows_but_is_not_live() {
        // After the close, the day's last consensus is still the latest.
        const auto last = consensus("15:19", 0.66, 0.24, 0.10, Votes{2, 1, 0});
        const auto rows =
            signal_rows({samsung()}, {{"005930", latest_of(last)}}, {}, SignalRule{0.6, 2}, at("15:40"));

        QCOMPARE(rows[0].signal, QStringLiteral("상승 신호"));
        QVERIFY(!rows[0].live);
    }

    void a_consensus_without_members_says_so() {
        Prediction empty;
        empty.symbol = "005930";
        empty.algorithm = "consensus-final";
        empty.status = "no_members";
        empty.bar_close = at("09:31");
        const auto rows =
            signal_rows({samsung()}, {{"005930", latest_of(empty)}}, {}, SignalRule{}, at("09:31").addSecs(5));

        QCOMPARE(rows[0].bar_close, QStringLiteral("09:31"));
        QCOMPARE(rows[0].signal, QStringLiteral("합의 대상 없음"));
        QCOMPARE(rows[0].up, QStringLiteral("—"));
        QCOMPARE(rows[0].call, Direction::None);
    }

    void a_symbol_without_predictions_today_shows_dashes() {
        SymbolQuote quote = samsung();
        quote.mid.reset();
        const auto rows = signal_rows({quote}, {}, {}, SignalRule{}, at("08:30"));

        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].price, QStringLiteral("—"));
        QCOMPARE(rows[0].bar_close, QStringLiteral("—"));
        QCOMPARE(rows[0].members, QStringLiteral("—"));
        QCOMPARE(rows[0].signal, QString());
    }

    void the_latest_scored_consensus_that_could_not_be_scored_says_so() {
        const QMap<QString, QList<Prediction>> recent = {
            {"005930",
             {scored(consensus("09:26", 0.6, 0.3, 0.1, Votes{2, 0, 0}), Direction::Up),
              scored(consensus("09:27", 0.2, 0.6, 0.2, Votes{0, 2, 0}), Direction::None)}}};
        const auto rows = signal_rows({samsung()}, {}, recent, SignalRule{}, at("09:31"));

        QCOMPARE(rows[0].last, QStringLiteral("09:27 보합 채점 불가"));
        QVERIFY(!rows[0].last_hit.has_value());
    }

    void the_status_lists_what_the_engine_is_doing_and_heads_with_its_trouble() {
        const auto view = status_view(status_from(kStatus));

        QCOMPARE(view.headline, QStringLiteral("KIS 피드 결측 2개"));
        const QList<StatusLine> expected = {
            {QStringLiteral("장 단계"), QStringLiteral("연속매매"), false},
            {QStringLiteral("마지막 봉 마감"), QStringLiteral("10:30"), false},
            {QStringLiteral("채점 대기"), QStringLiteral("12건"), false},
            {QStringLiteral("KIS"), QStringLiteral("연결됨 · 결측 2개 (H0UNASP0 000660, H0IFCNT0 A01612)"), true},
            {QStringLiteral("오늘 특수 상황"), QStringLiteral("VI 000660 09:40–09:42 · 사이드카 10:01–10:06"), false},
            {QStringLiteral("지난밤 학습"), QStringLiteral("10-18 · 적용됨 · 승격 1개"), false},
            {QStringLiteral("CPU 예산"), QStringLiteral("45% · 오늘 건너뜀 2회"), false},
            {QStringLiteral("엔진"), QStringLiteral("엔진 0123456 · 10-19 06:02 시작"), false},
        };
        QCOMPARE(view.lines.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i) {
            QCOMPARE(view.lines[i].label, expected[i].label);
            QCOMPARE(view.lines[i].value, expected[i].value);
            QCOMPARE(view.lines[i].trouble, expected[i].trouble);
        }
    }

    void a_lost_kis_connection_heads_before_the_other_troubles() {
        auto status = status_from(kStatus);
        status.kis_connected = false;
        status.training->applied = false;
        status.training->reason = "not in the store: newton-3";
        status.cpu_usage = 1.2;
        QCOMPARE(status_view(status).headline, QStringLiteral("KIS 연결 끊김"));

        status.kis_connected = true;
        status.feeds.clear();
        QCOMPARE(status_view(status).headline, QStringLiteral("지난밤 학습 미적용"));

        // Before 08:00 the night's models wait for the swap: no trouble.
        status.training->reason = "waiting for 08:00";
        QCOMPARE(status_view(status).headline, QStringLiteral("CPU 예산 초과 (120%)"));

        status.cpu_usage = 0.5;
        QCOMPARE(status_view(status).headline, QString());
    }

    void today_s_open_to_close_call_is_its_algorithms_mean_and_its_answer_once_in() {
        const QMap<QString, KindLatest> latest = {
            // Before the close: two algorithms averaged (the baseline left out), not scored yet.
            {"005930", kind_latest("005930", {daily("cox", "005930", 19, 0.5, 0.3, 0.2),
                                              daily("neyman", "005930", 19, 0.3, 0.3, 0.4),
                                              daily("otc-flat", "005930", 19, 0.0, 1.0, 0.0)})},
            // Scored: down from the open's 81,950 to the close's 81,250, as called.
            {"000660", kind_latest("000660", {answered(daily("cox", "000660", 19, 0.2, 0.3, 0.5), "down", 81950,
                                                       81250)})},
        };
        const auto rows = open_to_close_rows(
            {quote("005930", QStringLiteral("삼성전자")), quote("000660", QStringLiteral("SK하이닉스")),
             quote("009150", QStringLiteral("삼성전기"))},
            latest, open_to_close(), QDate(2026, 10, 19));

        QCOMPARE(rows.size(), 3);
        const auto& today = rows[0];
        QCOMPARE(today.name, QStringLiteral("삼성전자"));
        QCOMPARE(today.day, QStringLiteral("10-19 08:50"));
        QCOMPARE(today.up, QStringLiteral("40%"));
        QCOMPARE(today.flat, QStringLiteral("30%"));
        QCOMPARE(today.down, QStringLiteral("30%"));
        QCOMPARE(today.call, Direction::Up);
        QCOMPARE(today.basis, QStringLiteral("알고리즘 2개 평균"));
        QCOMPARE(today.band, QStringLiteral("보합 ±0.47%"));
        QCOMPARE(today.answer, QStringLiteral("채점 전 (20:35부터)"));
        QVERIFY(!today.hit.has_value());

        const auto& scored_day = rows[1];
        QCOMPARE(scored_day.call, Direction::Down);
        QCOMPARE(scored_day.answer, QStringLiteral("정답 하락 -0.85% (81,950 → 81,250)"));
        QCOMPARE(scored_day.hit, std::optional<bool>(true));

        const auto& none = rows[2];
        QCOMPARE(none.day, QStringLiteral("예측 없음"));
        QCOMPARE(none.up, QStringLiteral("—"));
        QCOMPARE(none.call, Direction::None);
        QCOMPARE(none.answer, QStringLiteral("—"));
    }

    void before_today_s_prediction_the_latest_is_the_last_trading_day_s() {
        const QMap<QString, KindLatest> latest = {
            {"005930", kind_latest("005930", {answered(daily("cox", "005930", 16, 0.5, 0.3, 0.2), "flat", 81000,
                                                       81100)})}};
        const auto rows = open_to_close_rows({quote("005930", QStringLiteral("삼성전자"))}, latest, open_to_close(),
                                             QDate(2026, 10, 19));

        QCOMPARE(rows[0].day, QStringLiteral("10-16 08:50 (지난 거래일)"));
        QCOMPARE(rows[0].hit, std::optional<bool>(false));
    }
};

QTEST_GUILESS_MAIN(TstKrxDashboardView)
#include "tst_krx_dashboard_view.moc"
