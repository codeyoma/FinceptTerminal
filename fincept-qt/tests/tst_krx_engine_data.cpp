// Unit tests for src/services/krx_quant/EngineData.h
//
// The KRX prediction screen shows what the prediction engine's API (v1) answers: its
// status, the symbols with their reference price, and each symbol's latest predictions.
// These tests pin how the answers are read, including what is missing: null values,
// predictions that were not made, a live-only field and failed requests. The fixtures
// hold only the fields the screen reads.

#include "services/krx_quant/EngineData.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QTest>

using namespace fincept::krx_quant;

namespace {

QJsonDocument json(const char* text) {
    return QJsonDocument::fromJson(QByteArray(text));
}

const char* kStatus = R"({
  "mode": "live",
  "now": "2026-10-08T10:30:05+09:00",
  "commit": "0123456789abcdef0123456789abcdef01234567",
  "started_at": "2026-10-08T06:02:11+09:00",
  "phase": "continuous",
  "last_bar_close": "2026-10-08T10:30:00+09:00",
  "pending_outcomes": 96,
  "special": [
    {"kind": "vi", "symbol": "000660", "start": "2026-10-08T09:40:10+09:00", "end": "2026-10-08T09:42:20+09:00"},
    {"kind": "sidecar", "symbol": null, "start": "2026-10-08T10:01:00+09:00", "end": "2026-10-08T10:06:00+09:00"}
  ],
  "session": {"open": "2026-10-08T09:00:00+09:00", "close": "2026-10-08T15:30:00+09:00"},
  "training": {"day": "2026-10-07", "applied": true, "reason": "applied", "promoted": {"alg-b": "alg-b-2"}},
  "kis": {"connected": true, "feeds": [
    {"feed": "H0UNCNT0", "key": "005930", "last_received_at": "2026-10-08T10:30:03+09:00", "seconds_since": 2.0, "gap": null},
    {"feed": "H0UNMBC0", "key": "000660", "last_received_at": "2026-10-08T10:14:05+09:00", "seconds_since": 960.0, "gap": "stale"},
    {"feed": "H0IFCNT0", "key": "A01612", "last_received_at": null, "seconds_since": null, "gap": "disconnected: closed"}
  ]},
  "usd_futures": {"price": 1395.2, "at": "2026-10-08T10:29:58+09:00", "previous_close": 1388.3, "change_pct": 0.497}
})";

const char* kMacro = R"json({
  "day": "2026-10-12", "decided_at": "2026-10-12T07:30:02+09:00", "regime": "risk_off",
  "reasons": ["SOX -1.4% (-1% 이하)"],
  "readings": [
    {"indicator": "sox", "label": "필라델피아 반도체지수", "source": "yahoo", "series": "^SOX",
     "as_of": "2026-10-09", "value": 12620.8, "change_pct": -1.4},
    {"indicator": "usdf", "label": "달러선물", "source": "kis", "series": "175",
     "as_of": "2026-10-09", "value": 1392.2, "change_pct": 0.3},
    {"indicator": "vix", "label": "VIX", "source": "yahoo", "series": "^VIX",
     "as_of": null, "value": null, "change_pct": null}
  ],
  "failed": {"fred": "HTTP 503"},
  "skipped": ["ecos"]
})json";

const char* kLatest = R"({
  "symbol": "005930",
  "bar_close": "2026-10-08T10:30:00+09:00",
  "predictions": [
    {"algorithm": "baseline-flat", "model_version": "v1", "status": "ok",
     "p_up": 0.0, "p_flat": 1.0, "p_down": 0.0, "expected_ticks": 0.0},
    {"algorithm": "alg-a", "model_version": "alg-a-1", "status": "ok",
     "p_up": 0.52, "p_flat": 0.10, "p_down": 0.38, "expected_ticks": 1.42},
    {"algorithm": "consensus-fast", "model_version": "w1", "status": "ok",
     "p_up": 0.47, "p_flat": 0.11, "p_down": 0.42, "expected_ticks": 0.47},
    {"algorithm": "alg-b", "model_version": "alg-b-1", "status": "timed_out",
     "p_up": null, "p_flat": null, "p_down": null, "expected_ticks": null},
    {"algorithm": "consensus-final", "model_version": "w1", "status": "ok",
     "p_up": 0.20, "p_flat": 0.70, "p_down": 0.10, "expected_ticks": 0.1}
  ]
})";

// The consensus scored on both bases, an algorithm scored on basis A only (the mid-point at
// its display time is unknown), and a prediction not made: no target, no outcome.
const char* kHistory = R"({
  "symbol": "005930",
  "predictions": [
    {"algorithm": "consensus-final", "model_version": "w1",
     "bar_close": "2026-10-08T10:26:00+09:00", "displayed_at": "2026-10-08T10:26:00.180000+09:00",
     "target_at": "2026-10-08T10:30:00+09:00", "status": "ok",
     "p_up": 0.62, "p_flat": 0.30, "p_down": 0.08, "expected_ticks": 1.2,
     "outcome": {"mid_close": 81250.0, "mid_display": 81250.0, "mid_target": 81550.0, "flat_ticks": 2,
                 "class_a": "up", "class_b": "up", "special": false, "unscorable": null,
                 "scored_at": "2026-10-08T10:30:00.003000+09:00"}},
    {"algorithm": "alg-a", "model_version": "alg-a-1",
     "bar_close": "2026-10-08T10:25:00+09:00", "displayed_at": "2026-10-08T10:25:00.090000+09:00",
     "target_at": "2026-10-08T10:29:00+09:00", "status": "ok",
     "p_up": 0.2, "p_flat": 0.5, "p_down": 0.3, "expected_ticks": -0.4,
     "outcome": {"mid_close": 81300.0, "mid_display": null, "mid_target": 81100.0, "flat_ticks": 2,
                 "class_a": "down", "class_b": null, "special": true, "unscorable": "missing mid: display",
                 "scored_at": "2026-10-08T10:29:00.002000+09:00"}},
    {"algorithm": "alg-b", "model_version": "alg-b-1",
     "bar_close": "2026-10-08T10:25:00+09:00", "displayed_at": "2026-10-08T10:25:00.001000+09:00",
     "target_at": null, "status": "no_book",
     "p_up": null, "p_flat": null, "p_down": null, "expected_ticks": null, "outcome": null}
  ]
})";

// Ranked: one beating every baseline after the luck correction, one not. Unranked: the
// consensus (no status) with nothing scored. A baseline has no lift and no status.
const char* kLeaderboard = R"({
  "symbol": "all", "period": "5d", "days": ["2026-10-07", "2026-10-08"], "basis": "b",
  "special": "exclude", "min_sample": 200,
  "ranked": [
    {"rank": 1, "algorithm": "alg-a", "status": "active", "model_versions": ["alg-a-2", "alg-a-1"], "n": 1128,
     "accuracy": {"value": 0.52, "low": 0.49, "high": 0.55, "n": 1128},
     "precision": {"value": 0.55, "low": 0.45, "high": 0.65, "n": 96},
     "coverage": {"value": 0.085, "low": 0.07, "high": 0.10, "n": 1128},
     "brier": {"value": 0.58, "low": 0.56, "high": 0.60, "n": 1128},
     "pnl": {"trades": 96, "total_ticks": 12.4, "per_trade": {"value": 0.13, "low": -0.2, "high": 0.46, "n": 96}},
     "lift": {"low": 0.01, "day_low": -0.01, "vs": {
       "baseline-flat": {"n": 1128, "value": 0.04, "low": 0.01, "high": 0.07, "p": 0.001, "q": 0.003, "significant": true,
                         "by_day": {"n": 5, "value": 0.04, "low": -0.01, "high": 0.09, "p": 0.06}},
       "baseline-persist": {"n": 1128, "value": 0.06, "low": 0.03, "high": 0.09, "p": 0.0001, "q": 0.0006, "significant": true,
                            "by_day": null}}}},
    {"rank": 2, "algorithm": "alg-b", "status": "probation", "model_versions": ["alg-b-1"], "n": 900,
     "accuracy": {"value": 0.50, "low": 0.47, "high": 0.53, "n": 900}, "precision": null,
     "coverage": {"value": 0.0, "low": 0.0, "high": 0.004, "n": 900},
     "brier": {"value": 0.61, "low": 0.59, "high": 0.63, "n": 900},
     "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null},
     "lift": {"low": -0.03, "vs": {
       "baseline-flat": {"n": 900, "value": 0.0, "low": -0.03, "high": 0.03, "p": 0.5, "q": 0.6, "significant": false}}}}
  ],
  "unranked": [
    {"rank": null, "algorithm": "consensus-final", "status": null, "model_versions": [], "n": 0,
     "accuracy": null, "precision": null, "coverage": null, "brier": null,
     "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}
  ],
  "baselines": [
    {"rank": null, "algorithm": "baseline-flat", "model_versions": ["v1"], "n": 1128,
     "accuracy": {"value": 0.51, "low": 0.48, "high": 0.54, "n": 1128}, "precision": null,
     "coverage": {"value": 0.0, "low": 0.0, "high": 0.003, "n": 1128},
     "brier": {"value": 0.98, "low": 0.95, "high": 1.01, "n": 1128},
     "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}
  ]
})";

// One algorithm's stats: its leaderboard line with what it left out and the calibration
// curves (two of the ten bins; one with nothing in it; and up on its own), and its
// breakdowns, whose lifts were not tested together and which leave out nothing.
const char* kStats = R"({
  "algorithm": "alg-a", "symbol": "005930", "period": "20d", "days": ["2026-10-08"], "basis": "a",
  "special": "include", "min_sample": 200,
  "overall": {
    "rank": 1, "algorithm": "alg-a", "model_versions": ["alg-a-1"], "n": 300,
    "accuracy": {"value": 0.52, "low": 0.49, "high": 0.55, "n": 300}, "precision": null,
    "coverage": null, "brier": {"value": 0.58, "low": 0.56, "high": 0.60, "n": 300},
    "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null},
    "lift": {"low": 0.01, "vs": {
      "baseline-flat": {"n": 300, "value": 0.04, "low": 0.01, "high": 0.07, "p": 0.001, "q": 0.003, "significant": true}}},
    "calibration": [
      {"low": 0.0, "high": 0.1, "count": 0, "mean_p": null, "observed": null},
      {"low": 0.3, "high": 0.4, "count": 412, "mean_p": 0.347,
       "observed": {"value": 0.31, "low": 0.27, "high": 0.36, "n": 412}}
    ],
    "calibration_by_class": {
      "up": [{"low": 0.3, "high": 0.4, "count": 10, "mean_p": 0.333,
              "observed": {"value": 1.0, "low": 0.72, "high": 1.0, "n": 10}}],
      "flat": [], "down": []
    },
    "excluded": {"special": 2, "unscorable": 1, "status": {"timed_out": 3, "vi": 4}}
  },
  "by_symbol": {"005930": {"rank": null, "algorithm": "alg-a", "model_versions": ["alg-a-1"], "n": 300,
    "accuracy": {"value": 0.52, "low": 0.49, "high": 0.55, "n": 300}, "precision": null, "coverage": null,
    "brier": null, "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}},
  "by_slot": {
    "09:30": {"rank": null, "algorithm": "alg-a", "model_versions": ["alg-a-1"], "n": 120,
      "accuracy": {"value": 0.55, "low": 0.46, "high": 0.64, "n": 120}, "precision": null, "coverage": null,
      "brier": null, "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null},
      "lift": {"low": -0.05, "vs": {
        "baseline-flat": {"n": 120, "value": 0.03, "low": -0.05, "high": 0.11, "p": 0.24, "q": null, "significant": null}}}},
    "09:00": {"rank": null, "algorithm": "alg-a", "model_versions": ["alg-a-1"], "n": 180,
      "accuracy": {"value": 0.50, "low": 0.43, "high": 0.57, "n": 180}, "precision": null, "coverage": null,
      "brier": null, "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}
  },
  "by_model_version": {"alg-a-1": {"rank": null, "algorithm": "alg-a", "model_versions": ["alg-a-1"], "n": 300,
    "accuracy": {"value": 0.52, "low": 0.49, "high": 0.55, "n": 300}, "precision": null, "coverage": null,
    "brier": null, "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null}}}
})";

Prediction forecast(double up, double flat, double down) {
    Prediction p;
    p.algorithm = "alg-c";
    p.model_version = "alg-c-1";
    p.status = "ok";
    p.p_up = up;
    p.p_flat = flat;
    p.p_down = down;
    p.expected_ticks = 0.0;
    return p;
}

// GET /v1/kinds (krx-quant #179, #182): the direction and the range, made-up meanings and cards.
const char* kKinds = R"({"kinds": [
  {"name": "direction", "title": "방향", "classes": ["up", "flat", "down"], "class_titles": ["상승", "보합", "하락"],
   "action": ["up", "down"], "answer_version": 2,
   "fields": {"expected_ticks": {"meaning": "기대값", "unit": "틱", "known": "봉 마감"}},
   "baselines": [{"name": "baseline-flat", "card": {"scientist": null, "character": "늘 보합", "logic": "x",
                  "unique_features": [], "training": "없음"}}]},
  {"name": "range", "title": "변동폭", "classes": ["small", "medium", "large"], "class_titles": ["작음", "보통", "큼"],
   "action": ["small", "large"], "answer_version": 1,
   "fields": {"q10": {"meaning": "10% 분위수", "unit": "틱", "known": "봉 마감"},
              "q50": {"meaning": "중앙", "unit": "틱", "known": "봉 마감"},
              "q90": {"meaning": "90% 분위수", "unit": "틱", "known": "봉 마감"}},
   "baselines": [{"name": "range-climate", "card": {"scientist": null, "character": "시간대 기후", "logic": "y",
                  "unique_features": [], "training": "밤 학습"}},
                 {"name": "range-persist", "card": {"scientist": null, "character": "직전 4분", "logic": "z",
                  "unique_features": [], "training": "없음"}}]}
]})";

// GET /v1/symbols/005930/kinds/range/latest: one scored with its path, one not made, one pending.
const char* kRangeLatest = R"({
  "symbol": "005930", "kind": "range", "bar_close": "2026-10-12T09:31:00+09:00",
  "predictions": [
    {"algorithm": "range-climate", "model_version": "range-climate-x", "bar_close": "2026-10-12T09:31:00+09:00",
     "displayed_at": "2026-10-12T09:31:00.004+09:00", "target_at": "2026-10-12T09:35:00+09:00", "status": "ok",
     "flat_ticks": 2, "output": {"p_small": 0.2, "p_medium": 0.5, "p_large": 0.3, "q10": 1.0, "q50": 2.5, "q90": 5.0},
     "answer": {"answer_version": 1, "class": "medium", "unscorable": null, "special": false,
                "scored_at": "2026-10-12T09:35:00.003+09:00",
                "path": {"r": 3.0, "k": 2, "tick": 100, "anchor": 81250.0,
                         "max_mid": 81450.0, "max_mid_at": "2026-10-12T09:32:10.201+09:00",
                         "min_mid": 81150.0, "min_mid_at": "2026-10-12T09:33:30.512+09:00",
                         "up": 2.0, "down": 1.0, "quotes": 1043, "silent": false, "max_bid": 81400.0, "min_ask": 81200.0}}},
    {"algorithm": "range-persist", "model_version": "v1", "bar_close": "2026-10-12T09:31:00+09:00",
     "displayed_at": "2026-10-12T09:31:00.004+09:00", "target_at": null, "status": "no_previous",
     "flat_ticks": 2, "output": null, "answer": null},
    {"algorithm": "alg-r", "model_version": "alg-r-1", "bar_close": "2026-10-12T09:31:00+09:00",
     "displayed_at": "2026-10-12T09:31:01+09:00", "target_at": "2026-10-12T09:35:00+09:00", "status": "ok",
     "flat_ticks": 2, "output": {"p_small": 0.1, "p_medium": 0.3, "p_large": 0.6, "q10": 2.0, "q50": 4.0, "q90": 8.5},
     "answer": null}
  ]
})";

// GET /v1/leaderboard?kind=range: an entry with its quantile stats (extra), a baseline without vs_climate.
const char* kRangeLeaderboard = R"({
  "kind": "range", "symbol": "all", "period": "5d", "days": ["2026-10-12", "2026-10-13"], "special": "exclude",
  "min_sample": 200,
  "ranked": [],
  "unranked": [
    {"rank": null, "algorithm": "alg-r", "status": "probation", "model_versions": ["alg-r-1"], "n": 240,
     "accuracy": {"value": 0.5, "low": 0.44, "high": 0.56, "n": 240}, "precision": null, "coverage": null,
     "brier": {"value": 0.6, "low": 0.58, "high": 0.62, "n": 240},
     "extra": {"n": 240,
               "pinball": {"q10": {"mean": {"value": 0.4, "low": 0.35, "high": 0.45, "n": 240}, "by_day": null},
                           "q50": {"mean": {"value": 1.2, "low": 1.0, "high": 1.4, "n": 240},
                                   "by_day": {"value": 1.25, "low": 0.9, "high": 1.6, "n": 2}},
                           "q90": {"mean": null, "by_day": null}},
               "interval_coverage": {"value": 0.78, "low": 0.72, "high": 0.83, "n": 240},
               "q50_abs_error": {"value": 2.4, "low": 2.1, "high": 2.7, "n": 240},
               "vs_climate": {"q10": {"mean": {"value": -0.1, "low": -0.2, "high": 0.0, "n": 240}, "by_day": null},
                              "q50": {"mean": null, "by_day": null}, "q90": {"mean": null, "by_day": null}}},
     "lift": {"low": null, "day_low": null, "vs": {}},
     "excluded": {"special": 0, "unscorable": 1, "status": {}}}
  ],
  "baselines": [
    {"rank": null, "algorithm": "range-climate", "model_versions": ["range-climate-x"], "n": 250,
     "accuracy": null, "precision": null, "coverage": null, "brier": null,
     "extra": {"n": 250, "pinball": {"q10": {"mean": null, "by_day": null}, "q50": {"mean": null, "by_day": null},
                                     "q90": {"mean": null, "by_day": null}},
               "interval_coverage": null, "q50_abs_error": null, "vs_climate": null},
     "excluded": {"special": 0, "unscorable": 0, "status": {}}}
  ]
})";

// GET /v1/symbols/005930/kinds/barrier/latest (krx-quant #183, #185): one scored "up first" with its
// path (the demo engine's 10:26 bar close), one not made with the bar close's w, mid-point and tick
// (krx-quant #210) all the same.
const char* kBarrierLatest = R"({
  "symbol": "005930", "kind": "barrier", "bar_close": "2026-10-12T10:26:00+09:00",
  "predictions": [
    {"algorithm": "barrier-persist", "model_version": "v1", "bar_close": "2026-10-12T10:26:00+09:00",
     "displayed_at": "2026-10-12T10:26:00+09:00", "target_at": "2026-10-12T10:30:00+09:00", "status": "ok",
     "flat_ticks": 2, "width_ticks": 6, "anchor": 78350.0, "tick": 100,
     "output": {"p_up_first": 0.0, "p_timeout": 1.0, "p_down_first": 0.0},
     "answer": {"answer_version": 1, "class": "up_first", "unscorable": null, "special": false,
                "scored_at": "2026-10-12T10:30:00+09:00",
                "path": {"w": 6, "sigma": 6.405, "k": 2, "tick": 100, "anchor": 78350.0, "upper": 78950.0,
                         "lower": 77750.0, "cost": 1.80205, "bid_start": 78300.0, "ask_start": 78400.0,
                         "up_at": "2026-10-12T10:29:28+09:00", "up_bid": 79000, "up_ask": 79100,
                         "down_at": null, "down_bid": null, "down_ask": null, "bid_end": 79200, "ask_end": 79300,
                         "quotes": 21, "silent": false}}},
    {"algorithm": "barrier-climate", "model_version": "barrier-climate-default",
     "bar_close": "2026-10-12T10:26:00+09:00", "displayed_at": "2026-10-12T10:26:00+09:00", "target_at": null,
     "status": "vi", "flat_ticks": 2, "width_ticks": 6, "anchor": 78350.0, "tick": 100, "output": null,
     "answer": null}
  ]
})";

// GET /v1/leaderboard?kind=barrier: barrier-persist's barrier P&L (its extra), barrier-climate's
// default calling every bar close "timeout": no trade.
const char* kBarrierLeaderboard = R"({
  "kind": "barrier", "symbol": "all", "period": "today", "days": ["2026-10-12"], "special": "exclude",
  "min_sample": 200, "ranked": [], "unranked": [],
  "baselines": [
    {"rank": null, "algorithm": "barrier-climate", "model_versions": ["barrier-climate-default"], "n": 252,
     "accuracy": {"value": 0.504, "low": 0.443, "high": 0.565, "n": 252}, "precision": null,
     "coverage": {"value": 0.0, "low": 0.0, "high": 0.015, "n": 252}, "brier": null,
     "extra": {"n": 252, "pnl": {"trades": 0, "total_ticks": 0, "per_trade": null},
               "long": {"trades": 0, "total_ticks": 0, "per_trade": null},
               "short": {"trades": 0, "total_ticks": 0, "per_trade": null}},
     "excluded": {"special": 4, "unscorable": 0, "status": {"vi": 2}}},
    {"rank": null, "algorithm": "barrier-persist", "model_versions": ["v1"], "n": 240,
     "accuracy": {"value": 0.4125, "low": 0.352, "high": 0.476, "n": 240},
     "precision": {"value": 0.281, "low": 0.209, "high": 0.367, "n": 121},
     "coverage": {"value": 0.504, "low": 0.441, "high": 0.567, "n": 240}, "brier": null,
     "extra": {"n": 240,
               "pnl": {"trades": 121, "total_ticks": -378.38, "per_trade": {"value": -3.127, "low": -3.855, "high": -2.399, "n": 121}},
               "long": {"trades": 60, "total_ticks": -214.87, "per_trade": {"value": -3.581, "low": -4.564, "high": -2.599, "n": 60}},
               "short": {"trades": 61, "total_ticks": -163.51, "per_trade": {"value": -2.681, "low": -3.75, "high": -1.611, "n": 61}}},
     "excluded": {"special": 0, "unscorable": 0, "status": {}}}
  ]
})";
} // namespace

class TstKrxEngineData : public QObject {
    Q_OBJECT

  private slots:
    void status_is_read_with_its_special_situations_and_training() {
        const auto status = parse_status(json(kStatus));
        QVERIFY(status.is_ok());
        const auto& s = status.value();
        QCOMPARE(s.phase, QStringLiteral("continuous"));
        QCOMPARE(s.last_bar_close, QDateTime::fromString("2026-10-08T10:30:00+09:00", Qt::ISODate));
        QCOMPARE(s.pending_outcomes, 96);
        QCOMPARE(s.special.size(), 2);
        QCOMPARE(s.special[0].symbol, QStringLiteral("000660"));
        QVERIFY(s.special[1].symbol.isEmpty()); // the whole market
        QVERIFY(s.training.has_value());
        QCOMPARE(s.training->day, QStringLiteral("2026-10-07"));
        QVERIFY(s.training->applied);
        QCOMPARE(s.training->promoted.value("alg-b"), QStringLiteral("alg-b-2"));
        QCOMPARE(s.kis_connected, std::optional<bool>(true));
    }

    void a_symbols_special_situations_over_days_are_read() {
        // GET /v1/symbols/{symbol}/special (krx-quant #236): its VIs and the market's circuit breakers
        // and sidecars, a detail where the engine gives one; kept in start order.
        const auto read = parse_special(json(R"json({"symbol": "005930", "from": "2026-10-13", "to": "2026-10-15",
          "special": [
            {"kind": "vi", "symbol": "005930", "start": "2026-10-15T13:45:10+09:00",
             "end": "2026-10-15T13:47:20+09:00", "detail": null},
            {"kind": "circuit_breaker", "symbol": null, "start": "2026-10-15T13:10:00+09:00",
             "end": "2026-10-15T13:30:00+09:00", "detail": "1단계"},
            {"kind": "sidecar", "symbol": null, "start": "2026-10-14T09:05:00+09:00",
             "end": "2026-10-14T09:10:00+09:00"}
          ]})json"));
        QVERIFY(read.is_ok());
        const auto& windows = read.value();
        QCOMPARE(windows.size(), 3);
        QCOMPARE(windows[0].kind, QStringLiteral("sidecar"));
        QVERIFY(windows[0].detail.isEmpty()); // none given
        QCOMPARE(windows[1].kind, QStringLiteral("circuit_breaker"));
        QVERIFY(windows[1].symbol.isEmpty()); // the whole market
        QCOMPARE(windows[1].detail, QStringLiteral("1단계"));
        QCOMPARE(windows[1].start, QDateTime::fromString("2026-10-15T13:10:00+09:00", Qt::ISODate));
        QCOMPARE(windows[1].end, QDateTime::fromString("2026-10-15T13:30:00+09:00", Qt::ISODate));
        QCOMPARE(windows[2].symbol, QStringLiteral("005930"));
        QVERIFY(windows[2].detail.isEmpty()); // null
        // None, and the engine's error.
        QVERIFY(parse_special(json(R"({"symbol": "005930", "special": []})")).value().isEmpty());
        QVERIFY(parse_special(json(R"({"error": "unknown symbol"})")).is_err());
        // The status's carry a detail too.
        const auto status = parse_status(json(R"({"special": [{"kind": "circuit_breaker", "symbol": null,
          "start": "2026-10-15T13:10:00+09:00", "end": "2026-10-15T13:30:00+09:00", "detail": "1단계"}]})"));
        QCOMPARE(status.value().special.value(0).detail, QStringLiteral("1단계"));
    }

    void status_has_the_usd_futures_now() {
        const auto status = parse_status(json(kStatus));
        QVERIFY(status.is_ok());
        const auto& usd = status.value().usd_futures;
        QVERIFY(usd.has_value());
        QCOMPARE(usd->price, 1395.2);
        QCOMPARE(usd->at, QDateTime::fromString("2026-10-08T10:29:58+09:00", Qt::ISODate));
        QCOMPARE(usd->change_pct, std::optional<double>(0.497));
        const auto unknown = parse_status(json(R"({"mode": "live", "usd_futures": {"price": 1390.0,
            "at": "2026-10-08T09:00:05+09:00", "previous_close": null, "change_pct": null}})"));
        QVERIFY(unknown.is_ok());
        QVERIFY(unknown.value().usd_futures.has_value());
        QVERIFY(!unknown.value().usd_futures->change_pct.has_value());
    }

    void the_usd_futures_show_live_or_as_read_in_the_morning() {
        UsdFutures live{1395.2, QDateTime::fromString("2026-10-08T10:29:58+09:00", Qt::ISODate), 0.497};
        const QList<Reading> morning = {
            {"sox", "필라델피아 반도체지수", "2026-10-07", 12620.8, -1.4},
            {"usdf", "달러선물 최근월물", "2026-10-07", 1388.3, 0.3},
        };
        // Named as the morning's reading, so the row reads the same before and after the first trade.
        QCOMPARE(usd_futures_text(live, morning), QStringLiteral("달러선물 최근월물 1,395.2 (+0.50%, 10:29)"));
        live.change_pct.reset();                                                          // no close to compare with
        QCOMPARE(usd_futures_text(live, {}), QStringLiteral("달러선물 1,395.2 (10:29)")); // and no reading
        // Before today's first trade: the morning's reading.
        QCOMPARE(usd_futures_text(std::nullopt, morning), QStringLiteral("달러선물 최근월물 1,388.3 (+0.30%)"));
        QCOMPARE(usd_futures_text(std::nullopt, {}), QString());
    }

    void status_without_a_training_kis_or_bar_close_yet() {
        const auto status = parse_status(json(R"({"mode": "demo", "now": "2026-10-08T08:10:00+09:00",
            "phase": "pre_open", "last_bar_close": null, "pending_outcomes": 0, "special": [], "training": null})"));
        QVERIFY(status.is_ok());
        QVERIFY(!status.value().last_bar_close.isValid());
        QVERIFY(!status.value().training.has_value());
        QVERIFY(!status.value().kis_connected.has_value()); // only a live engine reports KIS
        QVERIFY(!status.value().usd_futures.has_value());   // no trade today yet (or an older engine)
    }

    void an_answer_that_is_not_an_object_is_an_error_in_korean() {
        const auto status = parse_status(json("[1, 2]"));
        QVERIFY(status.is_err());
        QCOMPARE(QString::fromStdString(status.error()), QStringLiteral("엔진 응답이 JSON 객체가 아닙니다"));
        QVERIFY(parse_latest(QJsonDocument()).is_err());
    }

    void a_failed_request_says_why_in_korean() {
        // HttpClient: a transport failure carries no text; a body that is not JSON carries
        // Qt's parse error; an HTTP error carries the engine's {"error": ...}.
        QCOMPARE(failure_text(0, QString()),
                 QStringLiteral("엔진에 연결할 수 없습니다. 엔진 주소와 네트워크를 확인하세요."));
        QCOMPARE(failure_text(0, QStringLiteral("illegal value")),
                 QStringLiteral("엔진 응답을 읽을 수 없습니다: illegal value"));
        QCOMPARE(failure_text(404, QStringLiteral("unknown symbol 999999")),
                 QStringLiteral("엔진 오류 (HTTP 404): unknown symbol 999999"));
        QCOMPARE(failure_text(502, QString()), QStringLiteral("엔진 오류 (HTTP 502)"));
    }

    void symbols_keep_an_unknown_mid_point_unknown() {
        const auto symbols = parse_symbols(json(R"({"symbols": [
            {"symbol": "005930", "mid": 81250.0}, {"symbol": "000660", "mid": null}]})"));
        QVERIFY(symbols.is_ok());
        QCOMPARE(symbols.value().size(), 2);
        QCOMPARE(symbols.value()[0].mid, std::optional<double>(81250.0));
        QVERIFY(!symbols.value()[1].mid.has_value());
    }

    void latest_predictions_come_consensus_first_then_algorithms_then_baselines() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        QCOMPARE(latest.value().bar_close, QDateTime::fromString("2026-10-08T10:30:00+09:00", Qt::ISODate));
        QStringList order;
        for (const auto& p : latest.value().predictions)
            order << p.algorithm;
        QCOMPARE(order, (QStringList{"consensus-final", "consensus-fast", "alg-a", "alg-b", "baseline-flat"}));
    }

    void the_algorithms_keep_one_order_whatever_order_they_finished_in() {
        // The stream brings each algorithm when it finishes: the rows must not move about.
        Prediction late = forecast(0.3, 0.4, 0.3), early = forecast(0.3, 0.4, 0.3);
        late.algorithm = "alg-z";
        early.algorithm = "alg-a";
        Prediction persist = late, flat = late;
        persist.algorithm = "baseline-persist";
        flat.algorithm = "baseline-flat";
        QStringList order;
        for (const auto& p : in_screen_order({persist, late, flat, early}))
            order << p.algorithm;
        QCOMPARE(order, (QStringList{"alg-a", "alg-z", "baseline-flat", "baseline-persist"}));
    }

    void a_prediction_not_made_has_no_probabilities() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        const auto& made = latest.value().predictions;
        const auto& timed_out = made[3];
        QCOMPARE(timed_out.status, QStringLiteral("timed_out"));
        QVERIFY(!timed_out.p_up.has_value());
        QVERIFY(!timed_out.expected_ticks.has_value());
        QCOMPARE(shown_direction(timed_out), Direction::None);
        QCOMPARE(made[2].p_up, std::optional<double>(0.52));
    }

    void the_direction_shown_is_the_likeliest_class() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        const auto& made = latest.value().predictions;
        QCOMPARE(shown_direction(made[0]), Direction::Flat); // consensus-final: 0.70 flat
        QCOMPARE(shown_direction(made[1]), Direction::Up);   // consensus-fast: 0.47 up
        QCOMPARE(shown_direction(forecast(0.30, 0.10, 0.60)), Direction::Down);
        QCOMPARE(direction_label(Direction::Up), QStringLiteral("상승"));
        QCOMPARE(direction_label(Direction::None), QString());
    }

    void up_and_down_shown_as_the_same_percent_lean_neither_way() {
        // Shown as 36% / 28% / 36%: no direction, whatever the rounding underneath.
        QCOMPARE(shown_direction(forecast(0.3600000001, 0.28, 0.3599999999)), Direction::None);
        QCOMPARE(shown_direction(forecast(0.474, 0.06, 0.466)), Direction::None); // both 47%
        QCOMPARE(shown_direction(forecast(0.30, 0.40, 0.30)), Direction::Flat);
    }

    void names_and_codes_read_in_korean() {
        QCOMPARE(predictor_label("consensus-final"), QStringLiteral("합의 (최종)"));
        QCOMPARE(predictor_label("consensus-fast"), QStringLiteral("합의 (빠른)"));
        QCOMPARE(predictor_label("baseline-flat"), QStringLiteral("기준: 늘 보합"));
        QCOMPARE(predictor_label("baseline-persist"), QStringLiteral("기준: 직전 4분 방향"));
        QCOMPARE(predictor_label("baseline-random"), QStringLiteral("기준: 비율대로 무작위"));
        // The direction's climate (krx-quant #169): its name in Korean too (krx-quant #210).
        QCOMPARE(predictor_label("baseline-climate"), QStringLiteral("기준: 시간대 기후"));
        QCOMPARE(predictor_label("alg-a"), QStringLiteral("alg-a")); // an algorithm keeps its name
        QVERIFY(is_consensus("consensus-fast"));
        QVERIFY(!is_consensus("alg-a"));
        QCOMPARE(phase_label("continuous"), QStringLiteral("연속매매"));
        QCOMPARE(phase_label("closed"), QStringLiteral("휴장"));
        QCOMPARE(status_label("vi"), QStringLiteral("VI 중"));
        QCOMPARE(status_label("timed_out"), QStringLiteral("시간 초과"));
        QCOMPARE(special_label("sidecar"), QStringLiteral("사이드카"));
    }

    void the_status_names_the_engine_s_code_and_when_it_started() {
        const auto status = parse_status(json(kStatus));
        QVERIFY(status.is_ok());
        QCOMPARE(engine_build_parts(status.value()), (QStringList{"엔진 0123456", "10-08 06:02 시작"}));
        EngineStatus by_hand = status.value();
        by_hand.commit = "unknown";
        QCOMPARE(engine_build_parts(by_hand), (QStringList{"엔진 커밋 모름", "10-08 06:02 시작"}));
        EngineStatus older; // an engine from before krx-quant #85 says neither
        QVERIFY(engine_build_parts(older).isEmpty());
    }

    void the_engine_dot_is_green_only_when_everything_was_read() {
        const QDateTime refreshed = QDateTime::fromString("2026-10-13T09:31:05+09:00", Qt::ISODate);
        const QDateTime engine = QDateTime::fromString("2026-10-13T09:31:04+09:00", Qt::ISODate);
        const QString clocks = QStringLiteral("갱신 09:31:05 · 엔진 시계 09:31:04");

        auto dot = engine_dot(false, {}, {}, refreshed, engine);
        QCOMPARE(dot.state, EngineDot::State::Reading);
        QCOMPARE(dot.tip, QStringLiteral("엔진을 읽는 중\n") + clocks);

        dot = engine_dot(true, {}, {}, refreshed, engine);
        QCOMPARE(dot.state, EngineDot::State::Ok);
        QCOMPARE(dot.tip, clocks);

        dot = engine_dot(true, {}, {"000660 예측: 시간 초과", "합의 구성원: HTTP 503"}, refreshed, engine);
        QCOMPARE(dot.state, EngineDot::State::Failed);
        QCOMPARE(dot.tip, QStringLiteral("읽지 못한 것: 000660 예측: 시간 초과; 합의 구성원: HTTP 503\n") + clocks);

        dot = engine_dot(false, QStringLiteral("연결 거부"), {}, refreshed, engine);
        QCOMPARE(dot.state, EngineDot::State::Failed);
        QCOMPARE(dot.tip, QStringLiteral("엔진을 읽지 못함: 연결 거부\n") + clocks);
    }

    void a_consensus_says_how_its_members_leaned() {
        const auto read = parse_predictions(json(R"({"symbol": "005930", "predictions": [
          {"algorithm": "consensus-final", "model_version": "condorcet-equal",
           "bar_close": "2026-10-08T09:31:00+09:00", "displayed_at": "2026-10-08T09:31:03+09:00",
           "target_at": "2026-10-08T09:35:00+09:00", "status": "ok", "blocked": null,
           "votes": {"up": 2, "flat": 1, "down": 0},
           "p_up": 0.7, "p_flat": 0.2, "p_down": 0.1, "expected_ticks": 1.0, "outcome": null},
          {"algorithm": "gauss", "model_version": "gauss-default",
           "bar_close": "2026-10-08T09:31:00+09:00", "displayed_at": "2026-10-08T09:31:03+09:00",
           "target_at": "2026-10-08T09:35:00+09:00", "status": "ok", "blocked": null, "votes": null,
           "p_up": 0.7, "p_flat": 0.2, "p_down": 0.1, "expected_ticks": 1.0, "outcome": null}]})"));
        QVERIFY(read.is_ok());
        QCOMPARE(read.value().size(), 2);
        QVERIFY(read.value()[0].votes.has_value());
        QCOMPARE(read.value()[0].votes->up, 2);
        QCOMPARE(read.value()[0].votes->flat, 1);
        QCOMPARE(read.value()[0].votes->total(), 3);
        QVERIFY(!read.value()[1].votes.has_value());
    }

    void a_consensus_names_whom_it_pooled() {
        const auto read = parse_predictions(json(R"({"symbol": "005930", "predictions": [
          {"algorithm": "consensus-final", "model_version": "condorcet-equal",
           "bar_close": "2026-10-08T09:31:00+09:00", "displayed_at": "2026-10-08T09:31:03+09:00",
           "target_at": "2026-10-08T09:35:00+09:00", "status": "ok", "blocked": null, "votes": null,
           "pooled": ["gauss", "markov"],
           "p_up": 0.7, "p_flat": 0.2, "p_down": 0.1, "expected_ticks": 1.0, "outcome": null},
          {"algorithm": "consensus-fast", "model_version": "condorcet-equal",
           "bar_close": "2026-10-08T09:31:00+09:00", "displayed_at": "2026-10-08T09:31:03+09:00",
           "target_at": "2026-10-08T09:35:00+09:00", "status": "ok", "blocked": null, "votes": null,
           "p_up": 0.7, "p_flat": 0.2, "p_down": 0.1, "expected_ticks": 1.0, "outcome": null}]})"));
        QVERIFY(read.is_ok());
        QCOMPARE(read.value()[0].pooled, std::optional<QStringList>(QStringList{"gauss", "markov"}));
        QVERIFY(!read.value()[1].pooled.has_value()); // an older engine's: not there
    }

    void the_algorithms_say_their_statuses() {
        const auto read = parse_statuses(json(R"({"algorithms": [
          {"name": "newton", "model": {"version": "newton-default"}},
          {"name": "gauss", "model": {"version": "gauss-default"},
           "status": {"status": "probation", "since": "2026-10-12T08:00:00+09:00", "history": []}}]})"));
        QVERIFY(read.is_ok());
        QCOMPARE(read.value().value("gauss"), QStringLiteral("probation"));
        QVERIFY(!read.value().contains("newton")); // none known
    }

    void an_outcome_carries_its_bar_close_s_touch_label() {
        // krx-quant #167: recorded beside the outcome; none from an engine before it.
        const auto read = parse_predictions(json(R"({"symbol": "005930", "predictions": [
          {"algorithm": "gauss", "model_version": "g-1", "bar_close": "2026-10-12T09:31:00+09:00",
           "displayed_at": "2026-10-12T09:31:00.5+09:00", "target_at": "2026-10-12T09:35:00+09:00",
           "status": "ok", "p_up": 0.5, "p_flat": 0.3, "p_down": 0.2, "expected_ticks": 1.0,
           "outcome": {"mid_close": 81250, "mid_display": 81250, "mid_target": 81350, "flat_ticks": 2,
                       "class_a": "flat", "class_b": "flat", "special": false, "unscorable": null,
                       "scored_at": "2026-10-12T09:35:00+09:00",
                       "touch": {"target_version": 2, "class": "up", "unscorable": null, "anchor": 81250,
                                 "upper": 81450, "lower": 81050, "upper_at": "2026-10-12T09:32:30+09:00",
                                 "upper_price": 81500, "lower_at": null, "lower_price": null}}},
          {"algorithm": "markov", "model_version": "m-1", "bar_close": "2026-10-12T09:31:00+09:00",
           "displayed_at": "2026-10-12T09:31:00.5+09:00", "target_at": "2026-10-12T09:35:00+09:00",
           "status": "ok", "p_up": 0.5, "p_flat": 0.3, "p_down": 0.2, "expected_ticks": 1.0,
           "outcome": {"mid_close": 81250, "mid_display": 81250, "mid_target": 81350, "flat_ticks": 2,
                       "class_a": "flat", "class_b": "flat", "special": false, "unscorable": null,
                       "scored_at": "2026-10-12T09:35:00+09:00"}}]})"));
        QVERIFY(read.is_ok());
        const auto& touch = read.value()[0].outcome->touch;
        QVERIFY(touch.has_value());
        QCOMPARE(touch->direction, Direction::Up);
        QCOMPARE(touch->upper, std::optional<double>(81450.0));
        QCOMPARE(touch->upper_reached->price, 81500.0);
        QCOMPARE(touch->upper_reached->at, QDateTime::fromString("2026-10-12T09:32:30+09:00", Qt::ISODate));
        QVERIFY(!touch->lower_reached.has_value());
        QVERIFY(!read.value()[1].outcome->touch.has_value()); // an older engine's
    }

    void the_algorithms_say_what_they_are_and_how_they_learn() {
        // Made-up text: the fork holds none of the engine's own (krx-quant #163).
        const auto read = parse_algorithms(json(R"({"algorithms": [
          {"name": "alpha",
           "card": {"scientist": "A. Person", "character": "테스트 성격", "logic": "로직 글",
                    "unique_features": ["f1"], "training": "학습 글"},
           "features": ["f1", "f2"], "training_days": 20,
           "model": {"version": "alpha-1", "params": {"b": 0.25, "a": 3, "mode": "x", "list": [1, 2]},
                     "trained": "2026-09-01..2026-10-01, 100 samples, log loss 0.7", "trained_until": "2026-10-01"},
           "status": {"status": "probation", "since": "2026-10-10T15:09:52+09:00",
                      "history": [{"at": "2026-10-10T15:09:52+09:00", "from": null, "to": "probation",
                                   "reason": "registered"}]}},
          {"name": "beta", "model": {"version": "beta-default", "params": {}, "trained": null, "trained_until": null}}]})"));
        QVERIFY(read.is_ok());
        const AlgorithmInfo alpha = read.value().value("alpha");
        QCOMPARE(alpha.card->character, QStringLiteral("테스트 성격"));
        QCOMPARE(alpha.card->unique_features, QStringList{"f1"});
        QCOMPARE(alpha.features, (QStringList{"f1", "f2"}));
        QCOMPARE(alpha.training_days, std::optional<int>(20));
        QCOMPARE(alpha.model_version, QStringLiteral("alpha-1"));
        // By name; a number with up to six significant digits, anything else as compact JSON.
        QCOMPARE(alpha.params,
                 (QList<QPair<QString, QString>>{{"a", "3"}, {"b", "0.25"}, {"list", "[1,2]"}, {"mode", "x"}}));
        QCOMPARE(alpha.trained_until, QStringLiteral("2026-10-01"));
        QCOMPARE(alpha.status, QStringLiteral("probation"));
        QCOMPARE(alpha.history.size(), 1);
        QCOMPARE(alpha.history[0].from, QString()); // first registered
        QCOMPARE(alpha.history[0].reason, QStringLiteral("registered"));
        const AlgorithmInfo beta = read.value().value("beta");
        QVERIFY(!beta.card.has_value()); // none given
        QVERIFY(beta.trained.isEmpty());
        QVERIFY(!beta.training_days.has_value());
        QVERIFY(!beta.feature_meanings.has_value()); // no such field: an older engine
        QVERIFY(!beta.param_meanings.has_value());
    }

    void the_cards_say_how_each_predictor_forecasts_and_what_its_names_mean() {
        // Made-up text: the fork holds none of the engine's own (krx-quant #164).
        const auto read = parse_algorithms(json(R"({"algorithms": [
          {"name": "alpha",
           "card": {"scientist": "A. Person", "character": "테스트 성격", "logic": "로직 글", "unique_features": [],
                    "training": "학습 글", "forecasting": "확률 글", "failure": "실패 글"},
           "features": ["f1"], "feature_meanings": {"f1": {"meaning": "뜻 1", "unit": "틱", "known": "봉 마감"}},
           "model": {"version": "alpha-1", "params": {"a": 3}, "trained": null, "trained_until": null},
           "param_meanings": {"a": {"meaning": "뜻 a", "unit": "", "known": "밤 학습"}}},
          {"name": "gamma", "features": [], "feature_meanings": {},
           "model": {"version": "gamma-1", "params": {}, "trained": null, "trained_until": null},
           "param_meanings": {}}],
         "baselines": [{"name": "baseline-x", "card": {"scientist": null, "character": "기준 성격", "logic": "기준 로직",
                        "unique_features": [], "training": "배우지 않음", "forecasting": "기준 확률", "failure": "기준 실패"}}],
         "consensus": [{"name": "consensus-y", "card": {"scientist": "C. Person", "character": "합의 성격", "logic": "합의 로직",
                        "unique_features": [], "training": "가중치 글", "forecasting": "합의 확률", "failure": "합의 실패"}}]})"));
        QVERIFY(read.is_ok());
        const AlgorithmInfo alpha = read.value().value("alpha");
        QCOMPARE(alpha.kind, QStringLiteral("algorithm"));
        QCOMPARE(alpha.card->forecasting, QStringLiteral("확률 글"));
        QCOMPARE(alpha.card->failure, QStringLiteral("실패 글"));
        QCOMPARE(alpha.feature_meanings->value("f1").meaning, QStringLiteral("뜻 1"));
        QCOMPARE(alpha.feature_meanings->value("f1").unit, QStringLiteral("틱"));
        QCOMPARE(alpha.param_meanings->value("a").known, QStringLiteral("밤 학습"));
        const AlgorithmInfo gamma = read.value().value("gamma"); // the fields, though empty
        QVERIFY(gamma.feature_meanings.has_value() && gamma.feature_meanings->isEmpty());
        QVERIFY(gamma.param_meanings.has_value());
        const AlgorithmInfo baseline = read.value().value("baseline-x");
        QCOMPARE(baseline.kind, QStringLiteral("baseline"));
        QCOMPARE(baseline.card->scientist, QString()); // a baseline has none
        QCOMPARE(baseline.card->forecasting, QStringLiteral("기준 확률"));
        QVERIFY(baseline.model_version.isEmpty());
        const AlgorithmInfo consensus = read.value().value("consensus-y");
        QCOMPARE(consensus.kind, QStringLiteral("consensus"));
        QCOMPARE(consensus.card->training, QStringLiteral("가중치 글"));
    }

    void an_algorithm_says_what_it_learned_from_and_how_last_night_went() {
        // Made-up values (krx-quant #165).
        const auto read = parse_algorithms(json(R"({"algorithms": [
          {"name": "alpha",
           "model": {"version": "alpha-2", "params": {}, "trained": "t", "trained_until": "2026-10-07",
                     "training": {"first_day": "2026-09-08", "last_day": "2026-10-07", "samples": 1200, "loss": 0.81,
                                  "days": {"recorded": 3, "bars": 17}}},
           "validation": {"day": "2026-10-13", "validation_days": ["2026-10-12", "2026-10-13"], "samples": 400,
                          "challenger": {"version": "alpha-2", "loss": 0.82},
                          "champion": {"version": "alpha-1", "loss": null, "trained_on_validation": true},
                          "promoted": true, "reason": "이유 글"},
           "stage": {"status": "probation", "began": "2026-10-12", "counted_from": "2026-10-12", "days": 2,
                     "review_days": [10, 20], "next_review": 10, "next_review_on": "2026-10-23", "lift_days": 2,
                     "level": 0.98, "weakest": {"baseline": "base-x", "n": 2, "value": -0.01, "low": -0.4, "high": 0.3},
                     "enough": true}},
          {"name": "beta", "model": {"version": "beta-default", "params": {}, "trained": null, "trained_until": null,
                                     "training": null},
           "validation": null, "stage": null},
          {"name": "gamma", "model": {"version": "gamma-1", "params": {}, "trained": "t", "trained_until": null,
                                      "training": {"first_day": "2026-09-08", "last_day": "2026-10-07", "samples": 9,
                                                   "loss": 0.8, "days": {"recorded": 2.5, "bars": 3}}}},
          {"name": "delta", "model": {"version": "delta-default", "params": {}, "trained": null, "trained_until": null},
           "stage": {"status": "probation", "began": "2026-10-23", "counted_from": null, "days": 0, "review_days": [10],
                     "next_review": 10, "next_review_on": null, "lift_days": 0, "level": 0.98, "weakest": null,
                     "enough": false}},
          {"name": "epsilon", "model": {"version": "epsilon-default", "params": {}, "trained": null, "trained_until": null},
           "stage": {"status": "probation", "began": "2026-10-12", "days": 3, "review_days": [10], "next_review": 10}}]})"));
        QVERIFY(read.is_ok());
        const AlgorithmInfo alpha = read.value().value("alpha");
        QCOMPARE(alpha.training->samples, 1200);
        QCOMPARE(alpha.training->recorded_days, std::optional<int>(3));
        QCOMPARE(alpha.training->bar_days, std::optional<int>(17));
        QCOMPARE(alpha.validation->validation_days, (QStringList{"2026-10-12", "2026-10-13"}));
        QCOMPARE(alpha.validation->challenger->loss, std::optional<double>(0.82));
        QVERIFY(!alpha.validation->champion->loss.has_value());
        QCOMPARE(alpha.validation->champion->trained_on_validation, std::optional<bool>(true));
        QVERIFY(alpha.validation->promoted);
        QCOMPARE(alpha.stage->next_review, std::optional<int>(10));
        QCOMPARE(alpha.stage->review_days, (QList<int>{10, 20}));
        QCOMPARE(alpha.stage->weakest->baseline, QStringLiteral("base-x"));
        QCOMPARE(alpha.stage->weakest->low, std::optional<double>(-0.4));
        const AlgorithmInfo beta = read.value().value("beta");
        QVERIFY(!beta.training && !beta.validation && !beta.stage);
        const AlgorithmInfo gamma = read.value().value("gamma"); // a day count not whole: not taken
        QVERIFY(!gamma.training->recorded_days.has_value());
        QCOMPARE(gamma.training->bar_days, std::optional<int>(3));
        // Waiting for a model on the touch target only when counted_from is given as null (krx-quant #170).
        QVERIFY(!alpha.stage->waiting);
        QVERIFY(read.value().value("delta").stage->waiting);
        QVERIFY(!read.value().value("epsilon").stage->waiting); // left out: an older engine
    }

    void an_outcome_keeps_its_mid_points() {
        const auto history = parse_predictions(json(kHistory));
        QVERIFY(history.is_ok());
        QCOMPARE(history.value().size(), 3);
        const auto& first = *history.value()[0].outcome;
        QCOMPARE(first.mid_close, std::optional<double>(81250.0));
        QCOMPARE(first.mid_target, std::optional<double>(81550.0));
        QVERIFY(!history.value()[1].outcome->mid_display.has_value()); // null in the answer
    }

    void a_final_consensus_says_why_it_is_not_to_be_acted_on() {
        const auto read = parse_predictions(json(R"({"symbol": "005930", "predictions": [
          {"algorithm": "consensus-final", "model_version": "condorcet-equal",
           "bar_close": "2026-10-08T09:31:00+09:00", "displayed_at": "2026-10-08T09:31:03+09:00",
           "target_at": "2026-10-08T09:35:00+09:00", "status": "ok", "blocked": "special",
           "p_up": 0.7, "p_flat": 0.2, "p_down": 0.1, "expected_ticks": 1.0, "outcome": null},
          {"algorithm": "consensus-final", "model_version": "condorcet-equal",
           "bar_close": "2026-10-08T09:30:00+09:00", "displayed_at": "2026-10-08T09:30:03+09:00",
           "target_at": "2026-10-08T09:34:00+09:00", "status": "ok", "blocked": null,
           "p_up": 0.7, "p_flat": 0.2, "p_down": 0.1, "expected_ticks": 1.0, "outcome": null}]})"));
        QVERIFY(read.is_ok());
        QCOMPARE(read.value().size(), 2);
        QCOMPARE(read.value()[0].blocked, QStringLiteral("special"));
        QVERIFY(read.value()[1].blocked.isEmpty());
    }

    void a_history_keeps_each_prediction_with_its_symbol_times_and_outcome() {
        const auto history = parse_predictions(json(kHistory));
        QVERIFY(history.is_ok());
        const auto& made = history.value();
        QCOMPARE(made.size(), 3);
        QCOMPARE(made[0].symbol, QStringLiteral("005930")); // from the answer, for every item
        QCOMPARE(made[0].bar_close, QDateTime::fromString("2026-10-08T10:26:00+09:00", Qt::ISODate));
        QCOMPARE(made[0].target_at, QDateTime::fromString("2026-10-08T10:30:00+09:00", Qt::ISODate));
        QVERIFY(made[0].outcome.has_value());
        QCOMPARE(made[0].outcome->class_a, Direction::Up);
        QCOMPARE(made[0].outcome->class_b, Direction::Up);
        QVERIFY(!made[0].outcome->special);
    }

    void an_outcome_unscorable_on_one_basis_has_no_class_there() {
        const auto history = parse_predictions(json(kHistory));
        QVERIFY(history.is_ok());
        const auto& outcome = history.value()[1].outcome;
        QVERIFY(outcome.has_value());
        QCOMPARE(outcome->class_a, Direction::Down);
        QCOMPARE(outcome->class_b, Direction::None);
        QCOMPARE(outcome->unscorable, QStringLiteral("missing mid: display"));
        QVERIFY(outcome->special);
    }

    void a_prediction_not_made_has_no_target_and_no_outcome() {
        const auto history = parse_predictions(json(kHistory));
        QVERIFY(history.is_ok());
        const auto& not_made = history.value()[2];
        QCOMPARE(not_made.status, QStringLiteral("no_book"));
        QVERIFY(!not_made.target_at.isValid());
        QVERIFY(!not_made.outcome.has_value());
        QCOMPARE(not_made.bar_close, QDateTime::fromString("2026-10-08T10:25:00+09:00", Qt::ISODate));
    }

    void the_latest_predictions_carry_their_symbol_and_bar_close_too() {
        const auto latest = parse_latest(json(kLatest));
        QVERIFY(latest.is_ok());
        for (const auto& p : latest.value().predictions) {
            QCOMPARE(p.symbol, QStringLiteral("005930"));
            QCOMPARE(p.bar_close, latest.value().bar_close); // the answer's, where an item has none
        }
    }

    void bars_are_read_oldest_first() {
        const auto bars = parse_bars(json(R"({"symbol": "005930", "bars": [
            {"start": "2026-10-08T09:30:00+09:00", "open": 81000, "high": 81400, "low": 80900,
             "close": 81100, "volume": 19, "trades": 4},
            {"start": "2026-10-08T09:31:00+09:00", "open": 81100, "high": 81200, "low": 81000,
             "close": 81200, "volume": 7, "trades": 2}]})"));
        QVERIFY(bars.is_ok());
        QCOMPARE(bars.value().size(), 2);
        const auto& first = bars.value()[0];
        QCOMPARE(first.start, QDateTime::fromString("2026-10-08T09:30:00+09:00", Qt::ISODate));
        QCOMPARE(first.open, 81000.0);
        QCOMPARE(first.high, 81400.0);
        QCOMPARE(first.low, 80900.0);
        QCOMPARE(first.close, 81100.0);
        QCOMPARE(first.volume, 19.0);
        QVERIFY(parse_bars(json(R"({"error": "unknown symbol 999999"})")).is_err());
    }

    void day_candles_start_at_midnight_in_kst_with_their_market() {
        const auto read = parse_candles(json(R"({"symbol": "005930", "period": "day", "venue": "KRX", "candles": [
            {"date": "2026-10-07", "open": 80000, "high": 81000, "low": 79500, "close": 80900, "volume": 1200},
            {"date": "2026-10-08", "open": 80900, "high": 81700, "low": 80500, "close": 81250, "volume": 900}]})"));
        QVERIFY(read.is_ok());
        QCOMPARE(read.value().venue, QStringLiteral("KRX"));
        QCOMPARE(read.value().candles.size(), 2);
        const auto& last = read.value().candles[1];
        QCOMPARE(last.start, QDateTime::fromString("2026-10-08T00:00:00+09:00", Qt::ISODate));
        QCOMPARE((QList<double>{last.open, last.high, last.low, last.close, last.volume}),
                 (QList<double>{80900, 81700, 80500, 81250, 900}));

        const auto none = parse_candles(json(R"({"symbol": "005930", "period": "day", "venue": null, "candles": []})"));
        QVERIFY(none.is_ok());
        QVERIFY(none.value().venue.isEmpty() && none.value().candles.isEmpty());
        QVERIFY(parse_candles(json(R"({"error": "period must be one of day, got 'week'"})")).is_err());
    }

    void the_leaderboard_keeps_its_query_and_three_lists() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        const auto& b = board.value();
        QCOMPARE(b.symbol, QStringLiteral("all"));
        QCOMPARE(b.period, QStringLiteral("5d"));
        QCOMPARE(b.basis, QStringLiteral("b"));
        QCOMPARE(b.special, QStringLiteral("exclude"));
        QCOMPARE(b.days, (QStringList{"2026-10-07", "2026-10-08"}));
        QCOMPARE(b.min_sample, 200);
        QCOMPARE(b.ranked.size(), 2);
        QCOMPARE(b.unranked.size(), 1);
        QCOMPARE(b.baselines.size(), 1);
    }

    void a_ranked_entry_has_its_rank_status_models_and_stats() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        const auto& first = board.value().ranked[0];
        QCOMPARE(first.rank, std::optional<int>(1));
        QCOMPARE(first.algorithm, QStringLiteral("alg-a"));
        QCOMPARE(first.status, QStringLiteral("active"));
        QCOMPARE(first.model_versions, (QStringList{"alg-a-2", "alg-a-1"}));
        QCOMPARE(first.n, 1128);
        QVERIFY(first.accuracy.has_value());
        QCOMPARE(first.accuracy->value, 0.52);
        QCOMPARE(first.accuracy->low, std::optional<double>(0.49));
        QCOMPARE(first.brier->value, 0.58);
        QCOMPARE(first.trades, 96);
        QCOMPARE(first.total_ticks, 12.4);
        QCOMPARE(first.per_trade->value, 0.13);
        QCOMPARE(first.lift_low, std::optional<double>(0.01));
    }

    void significant_means_every_baseline_beaten_after_the_luck_correction() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        QVERIFY(board.value().ranked[0].significant);
        QVERIFY(!board.value().ranked[1].significant); // beats none of them for sure
        QVERIFY(!board.value().baselines[0].significant);
    }

    void each_baselines_lift_has_its_interval_and_test() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        const auto& lifts = board.value().ranked[0].lifts;
        QCOMPARE(lifts.size(), 2);
        QCOMPARE(lifts[0].baseline, QStringLiteral("baseline-flat"));
        QCOMPARE(lifts[0].gain->value, 0.04);
        QCOMPARE(lifts[0].gain->low, std::optional<double>(0.01));
        QCOMPARE(lifts[0].gain->high, std::optional<double>(0.07));
        QCOMPARE(lifts[0].gain->n, 1128);
        QCOMPARE(lifts[0].p, std::optional<double>(0.001));
        QCOMPARE(lifts[0].q, std::optional<double>(0.003));
        QCOMPARE(lifts[0].significant, std::optional<bool>(true));
        QCOMPARE(lifts[1].baseline, QStringLiteral("baseline-persist"));
        QVERIFY(board.value().unranked[0].lifts.isEmpty()); // nothing to compare with
    }

    void a_lift_taken_by_trading_day_has_its_days_and_interval() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        const auto& lifts = board.value().ranked[0].lifts;
        QCOMPARE(lifts[0].by_day->n, 5); // days, not pairs
        QCOMPARE(lifts[0].by_day->value, 0.04);
        QCOMPARE(lifts[0].by_day->low, std::optional<double>(-0.01));
        QCOMPARE(lifts[0].by_day->high, std::optional<double>(0.09));
        QVERIFY(!lifts[1].by_day); // fewer than two days
        // An engine from before the lifts by trading day has none.
        QVERIFY(!board.value().ranked[1].lifts[0].by_day);
    }

    void an_algorithms_stats_keep_their_query_and_its_leaderboard_line() {
        const auto stats = parse_stats(json(kStats));
        QVERIFY(stats.is_ok());
        const auto& s = stats.value();
        QCOMPARE(s.algorithm, QStringLiteral("alg-a"));
        QCOMPARE(s.symbol, QStringLiteral("005930"));
        QCOMPARE(s.period, QStringLiteral("20d"));
        QCOMPARE(s.basis, QStringLiteral("a"));
        QCOMPARE(s.special, QStringLiteral("include"));
        QCOMPARE(s.days, QStringList{"2026-10-08"});
        QCOMPARE(s.min_sample, 200);
        QCOMPARE(s.overall.rank, std::optional<int>(1));
        QCOMPARE(s.overall.n, 300);
        QCOMPARE(s.overall.lifts.size(), 1);
        QVERIFY(s.overall.significant);
    }

    void the_calibration_curve_keeps_its_empty_bins_unknown() {
        const auto stats = parse_stats(json(kStats));
        QVERIFY(stats.is_ok());
        const auto& bins = stats.value().calibration;
        QCOMPARE(bins.size(), 2);
        QCOMPARE(bins[0].count, 0);
        QVERIFY(!bins[0].mean_p.has_value());
        QVERIFY(!bins[0].observed.has_value());
        QCOMPARE(bins[1].low, 0.3);
        QCOMPARE(bins[1].high, 0.4);
        QCOMPARE(bins[1].count, 412);
        QCOMPARE(bins[1].mean_p, std::optional<double>(0.347));
        QCOMPARE(bins[1].observed->value, 0.31);
        QCOMPARE(bins[1].observed->low, std::optional<double>(0.27));
    }

    void the_stats_carry_each_class_calibration_and_what_they_left_out() {
        const auto stats = parse_stats(json(kStats));
        QVERIFY(stats.is_ok());
        const auto& s = stats.value();
        QCOMPARE(s.calibration_by_class.keys(), (QStringList{"down", "flat", "up"}));
        QCOMPARE(s.calibration_by_class.value("up").size(), 1);
        QCOMPARE(s.calibration_by_class.value("up")[0].observed->value, 1.0);
        QVERIFY(s.calibration_by_class.value("down").isEmpty());
        QVERIFY(s.overall.excluded.has_value());
        QCOMPARE(s.overall.excluded->special, 2);
        QCOMPARE(s.overall.excluded->unscorable, 1);
        QCOMPARE(s.overall.excluded->statuses, (QMap<QString, int>{{"timed_out", 3}, {"vi", 4}}));
        QVERIFY(!s.by_symbol.value("005930").excluded.has_value());
    }

    void the_breakdowns_are_by_symbol_slot_and_model_version_in_order() {
        const auto stats = parse_stats(json(kStats));
        QVERIFY(stats.is_ok());
        const auto& s = stats.value();
        QCOMPARE(s.by_symbol.keys(), QStringList{"005930"});
        QCOMPARE(s.by_slot.keys(), (QStringList{"09:00", "09:30"}));
        const Entry slot = s.by_slot.value("09:30");
        QCOMPARE(slot.n, 120);
        QCOMPARE(s.by_model_version.keys(), QStringList{"alg-a-1"});
        // A breakdown's lift was not tested with the others: no q, no verdict.
        const Lift& lift = slot.lifts.first();
        QCOMPARE(lift.p, std::optional<double>(0.24));
        QVERIFY(!lift.q.has_value());
        QVERIFY(!lift.significant.has_value());
    }

    void stats_with_nothing_scored_are_empty_not_zero() {
        // A consensus over a period it did not predict in: the baselines are still there to
        // compare with, but no bar close pairs with them.
        const auto stats = parse_stats(json(R"({
          "algorithm": "consensus-final", "symbol": "all", "period": "today", "days": [], "basis": "b",
          "special": "exclude", "min_sample": 200,
          "overall": {"rank": null, "algorithm": "consensus-final", "model_versions": [], "n": 0,
            "accuracy": null, "precision": null, "coverage": null, "brier": null,
            "pnl": {"trades": 0, "total_ticks": 0.0, "per_trade": null},
            "lift": {"low": null, "vs": {
              "baseline-flat": {"n": 0, "value": null, "low": null, "high": null, "p": null, "q": null, "significant": false}}},
            "calibration": [{"low": 0.0, "high": 0.1, "count": 0, "mean_p": null, "observed": null}]},
          "by_symbol": {}, "by_slot": {}, "by_model_version": {}
        })"));
        QVERIFY(stats.is_ok());
        const auto& s = stats.value();
        QCOMPARE(s.overall.n, 0);
        QVERIFY(!s.overall.lift_low.has_value());
        QVERIFY(!s.overall.significant);
        QCOMPARE(s.overall.lifts.size(), 1);
        QVERIFY(!s.overall.lifts[0].gain.has_value()); // no pairs: unknown, not a gain of 0
        QVERIFY(!s.overall.lifts[0].p.has_value());
        QVERIFY(s.by_symbol.isEmpty() && s.by_slot.isEmpty() && s.by_model_version.isEmpty());
        QVERIFY(s.days.isEmpty());
    }

    void stats_of_an_unknown_algorithm_are_an_error() {
        QVERIFY(parse_stats(json(R"({"error": "unknown algorithm alg-z"})")).is_err());
    }

    void what_was_not_measured_stays_unknown() {
        const auto board = parse_leaderboard(json(kLeaderboard));
        QVERIFY(board.is_ok());
        const auto& consensus = board.value().unranked[0];
        QVERIFY(!consensus.rank.has_value());
        QVERIFY(consensus.status.isEmpty()); // the consensus has no status
        QVERIFY(!consensus.accuracy.has_value());
        QVERIFY(!consensus.per_trade.has_value());
        QVERIFY(!consensus.lift_low.has_value());
        QVERIFY(!board.value().ranked[1].precision.has_value());
        const auto& baseline = board.value().baselines[0];
        QVERIFY(baseline.status.isEmpty());
        QVERIFY(!baseline.lift_low.has_value());
    }

    void status_has_each_feeds_silence_and_gap() {
        const auto status = parse_status(json(kStatus));
        QVERIFY(status.is_ok());
        const auto& s = status.value();
        QCOMPARE(s.feeds.size(), 3);
        QCOMPARE(s.feeds[0].feed, QStringLiteral("H0UNCNT0"));
        QCOMPARE(s.feeds[0].key, QStringLiteral("005930"));
        QCOMPARE(s.feeds[0].seconds_since, std::optional<double>(2.0));
        QVERIFY(s.feeds[0].gap.isEmpty());
        QCOMPARE(s.feeds[1].gap, QStringLiteral("stale"));
        QVERIFY(!s.feeds[2].seconds_since.has_value()); // never received
    }

    void a_disconnected_engine_lists_no_feeds() {
        const auto status = parse_status(json(R"({"mode": "live", "now": "2026-10-09T10:00:00+09:00",
            "phase": "closed", "session": null, "last_bar_close": null, "pending_outcomes": 0, "special": [],
            "training": null, "kis": {"connected": false, "feeds": []}})"));
        QVERIFY(status.is_ok());
        QCOMPARE(status.value().kis_connected, std::optional<bool>(false));
        QVERIFY(status.value().feeds.isEmpty());
    }

    void the_macro_row_has_the_regime_its_reasons_and_readings() {
        const auto macro = parse_macro(json(kMacro));
        QVERIFY(macro.is_ok());
        const auto& m = macro.value();
        QCOMPARE(m.day, QStringLiteral("2026-10-12"));
        QCOMPARE(m.regime, QStringLiteral("risk_off"));
        QCOMPARE(m.reasons, (QStringList{QStringLiteral("SOX -1.4% (-1% 이하)")}));
        QCOMPARE(m.readings.size(), 3);
        QCOMPARE(m.readings[1].indicator, QStringLiteral("usdf"));
        QCOMPARE(m.readings[1].label, QStringLiteral("달러선물"));
        QCOMPARE(m.readings[1].value, std::optional<double>(1392.2));
        QCOMPARE(m.readings[1].change_pct, std::optional<double>(0.3));
        QVERIFY(!m.readings[2].value.has_value()); // missing, not 0
        QCOMPARE(m.failed.value("fred"), QStringLiteral("HTTP 503"));
        QCOMPARE(m.skipped, (QStringList{"ecos"}));
    }

    void no_macro_decided_yet_is_empty_not_an_error() {
        const auto macro = parse_macro(json(R"({"day": null, "decided_at": null, "regime": null,
            "reasons": [], "readings": [], "failed": {}, "skipped": []})"));
        QVERIFY(macro.is_ok());
        QVERIFY(macro.value().day.isEmpty());
        QVERIFY(macro.value().regime.isEmpty());
        QVERIFY(macro.value().readings.isEmpty());
    }

    void the_consensus_members_are_the_active_algorithms() {
        const auto members = parse_members(json(R"({"names": ["consensus-fast", "consensus-final"],
            "version": "w-20261012", "computed_at": "2026-10-12T15:40:00+09:00",
            "members": ["alg-a", "alg-b"], "fast_seconds": 2.0, "weights": {}})"));
        QVERIFY(members.is_ok());
        QCOMPARE(members.value(), (QStringList{"alg-a", "alg-b"}));
    }

    void a_prediction_event_brings_a_bar_closes_predictions_with_their_symbols() {
        const auto event =
            parse_event(QStringLiteral(R"({"type": "prediction", "at": "2026-10-12T09:31:00.090000+09:00",
            "bar_close": "2026-10-12T09:31:00+09:00", "predictions": [
              {"symbol": "000660", "algorithm": "alg-a", "model_version": "alg-a-1",
               "bar_close": "2026-10-12T09:31:00+09:00", "displayed_at": "2026-10-12T09:31:00.090000+09:00",
               "target_at": "2026-10-12T09:35:00+09:00", "status": "ok",
               "p_up": 0.5, "p_flat": 0.3, "p_down": 0.2, "expected_ticks": 0.6, "outcome": null}]})"));
        QVERIFY(event.is_ok());
        const auto& e = event.value();
        QCOMPARE(e.type, Event::Type::Prediction);
        QCOMPARE(e.at, QDateTime::fromString("2026-10-12T09:31:00.090+09:00", Qt::ISODateWithMs));
        QCOMPARE(e.bar_close, QDateTime::fromString("2026-10-12T09:31:00+09:00", Qt::ISODate));
        QCOMPARE(e.predictions.size(), 1);
        QCOMPARE(e.predictions[0].symbol, QStringLiteral("000660"));
        QCOMPARE(e.predictions[0].p_up, std::optional<double>(0.5));
        QVERIFY(!e.predictions[0].outcome.has_value());
    }

    void a_consensus_event_says_which_one() {
        const auto event = parse_event(QStringLiteral(R"({"type": "consensus", "at": "2026-10-12T09:31:02+09:00",
            "kind": "final", "bar_close": "2026-10-12T09:31:00+09:00", "predictions": [
              {"symbol": "005930", "algorithm": "consensus-final", "model_version": "w1",
               "bar_close": "2026-10-12T09:31:00+09:00", "displayed_at": "2026-10-12T09:31:02+09:00",
               "target_at": "2026-10-12T09:35:00+09:00", "status": "ok",
               "p_up": 0.64, "p_flat": 0.26, "p_down": 0.1, "expected_ticks": 1.1, "outcome": null}]})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Consensus);
        QCOMPARE(event.value().kind, QStringLiteral("final"));
        QCOMPARE(event.value().predictions[0].algorithm, QStringLiteral("consensus-final"));
    }

    void an_outcome_event_brings_the_scored_predictions() {
        const auto event = parse_event(QStringLiteral(R"({"type": "outcome", "at": "2026-10-12T09:35:00+09:00",
            "outcomes": [
              {"symbol": "005930", "algorithm": "consensus-final", "model_version": "w1",
               "bar_close": "2026-10-12T09:31:00+09:00", "displayed_at": "2026-10-12T09:31:02+09:00",
               "target_at": "2026-10-12T09:35:00+09:00", "status": "ok",
               "p_up": 0.64, "p_flat": 0.26, "p_down": 0.1, "expected_ticks": 1.1,
               "outcome": {"mid_close": 81250.0, "mid_display": 81250.0, "mid_target": 81050.0, "flat_ticks": 2,
                           "class_a": "down", "class_b": "down", "special": false, "unscorable": null,
                           "scored_at": "2026-10-12T09:35:00+09:00"}}]})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Outcome);
        QCOMPARE(event.value().predictions.size(), 1);
        QCOMPARE(event.value().predictions[0].outcome->class_b, Direction::Down);
    }

    void a_health_event_brings_the_kis_status() {
        const auto event = parse_event(QStringLiteral(R"({"type": "health", "at": "2026-10-12T09:40:00+09:00",
            "kis": {"connected": false, "feeds": [{"feed": "H0UNCNT0", "key": "005930",
              "last_received_at": "2026-10-12T09:39:00+09:00", "seconds_since": 60.0,
              "gap": "disconnected: closed"}]}})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Health);
        QVERIFY(!event.value().kis_connected);
        QCOMPARE(event.value().feeds.size(), 1);
        QCOMPARE(event.value().feeds[0].gap, QStringLiteral("disconnected: closed"));
    }

    void a_lifecycle_event_brings_an_algorithms_status_change() {
        const auto event = parse_event(QStringLiteral(R"({"type": "lifecycle", "at": "2026-10-12T15:40:00+09:00",
            "algorithm": "alg-b", "change": {"at": "2026-10-12T15:40:00+09:00", "from": "probation",
            "to": "active", "reason": "lift 0.02 > 0"}})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Lifecycle);
        QCOMPARE(event.value().algorithm, QStringLiteral("alg-b"));
        QCOMPARE(event.value().change.from, QStringLiteral("probation"));
        QCOMPARE(event.value().change.to, QStringLiteral("active"));
    }

    void a_bar_event_brings_the_bar_being_made() {
        const auto event = parse_event(QStringLiteral(R"({"type": "bar", "at": "2026-10-12T09:30:04+09:00",
            "symbol": "005930", "bar": {"start": "2026-10-12T09:30:00+09:00", "open": 81200, "high": 81400,
            "low": 81200, "close": 81300, "volume": 16, "trades": 3}})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Bar);
        QCOMPARE(event.value().symbol, QStringLiteral("005930"));
        QCOMPARE(event.value().bar.start, QDateTime::fromString("2026-10-12T09:30:00+09:00", Qt::ISODate));
        QCOMPARE(event.value().bar.high, 81400.0);
        QCOMPARE(event.value().bar.close, 81300.0);
        QCOMPARE(event.value().bar.volume, 16.0);
    }

    void an_event_of_a_newer_engine_is_passed_over_not_an_error() {
        const auto event = parse_event(QStringLiteral(R"({"type": "weather", "at": "2026-10-12T09:00:00+09:00"})"));
        QVERIFY(event.is_ok());
        QCOMPARE(event.value().type, Event::Type::Other);
        const auto broken = parse_event(QStringLiteral("not json"));
        QVERIFY(broken.is_err());
        QCOMPARE(QString::fromStdString(broken.error()), QStringLiteral("엔진 스트림 메시지를 읽을 수 없습니다"));
    }

    void the_stream_is_at_the_engines_address_over_websocket() {
        QCOMPARE(stream_url("http://engine.local:8080"), QStringLiteral("ws://engine.local:8080/v1/stream"));
        QCOMPARE(stream_url("https://engine.example/"), QStringLiteral("wss://engine.example/v1/stream"));
        QCOMPARE(stream_url("http://127.0.0.1:8090"), QStringLiteral("ws://127.0.0.1:8090/v1/stream"));
        QVERIFY(stream_url("").isEmpty());
        QVERIFY(stream_url("engine.local:8080").isEmpty()); // no http(s): nothing to connect to
    }

    void the_predicted_class_is_the_likeliest_a_tie_going_to_flat_then_up() {
        // As the engine scores: unlike the direction shown, a near tie still has a class.
        QCOMPARE(most_likely(forecast(0.474, 0.06, 0.466)), Direction::Up);
        QCOMPARE(most_likely(forecast(0.40, 0.40, 0.20)), Direction::Flat);
        QCOMPARE(most_likely(forecast(0.40, 0.20, 0.40)), Direction::Up);
        QCOMPARE(most_likely(forecast(0.30, 0.20, 0.50)), Direction::Down);
        Prediction not_made;
        not_made.status = "timed_out";
        QCOMPARE(most_likely(not_made), Direction::None);
    }

    void why_last_nights_training_is_not_running_reads_in_korean() {
        QCOMPARE(training_reason_label("waiting for 08:00"), QStringLiteral("08:00 교체 대기"));
        QCOMPARE(training_reason_label("final at 10-10 07:41, after 07:30: yesterday's models run on"),
                 QStringLiteral("07:30 뒤에 확정되어 어제 모델로 돈다"));
        QCOMPARE(training_reason_label("not in the store: alg-a-20261010-abc123; yesterday's models run on"),
                 QStringLiteral("모델 파일이 저장소에 없어 어제 모델로 돈다"));
        QCOMPARE(training_reason_label("applied, then replaced: alg-a runs its defaults"),
                 QStringLiteral("적용 뒤 바뀜: alg-a runs its defaults"));
        QCOMPARE(training_reason_label("something new"), QStringLiteral("something new")); // as it is
    }

    void times_read_in_kst_and_prices_as_on_the_exchange() {
        const QDateTime utc = QDateTime::fromString("2026-10-12T00:31:05Z", Qt::ISODate);
        QCOMPARE(kst_text(utc, "HH:mm"), QStringLiteral("09:31"));
        QCOMPARE(kst_text(QDateTime(), "HH:mm"), QStringLiteral("—"));
        QCOMPARE(price_text(81250.0), QStringLiteral("81,250"));
        QCOMPARE(price_text(1392.2), QStringLiteral("1,392.2")); // a mid-point between ticks keeps a decimal
    }

    void the_consensus_and_the_baselines_are_told_by_their_names() {
        QVERIFY(is_consensus("consensus-final"));
        QVERIFY(is_baseline("baseline-random"));
        QVERIFY(!is_baseline("newton"));
        QVERIFY(!is_consensus("newton"));
    }

    void more_names_read_in_korean() {
        QCOMPARE(status_label("no_book"), QStringLiteral("호가 없음"));
        QCOMPARE(regime_label("risk_off"), QStringLiteral("위험회피"));
        QCOMPARE(regime_label("shock"), QStringLiteral("충격"));
        QCOMPARE(standing_label("probation"), QStringLiteral("수습"));
        QCOMPARE(standing_label("active"), QStringLiteral("정식"));
        QCOMPARE(standing_label("clone"), QStringLiteral("복제"));
        QCOMPARE(standing_label(""), QString()); // the consensus and the baselines have none
    }
    void the_kinds_say_their_classes_fields_and_baselines() {
        const auto kinds = parse_kinds(json(kKinds));
        QVERIFY(kinds.is_ok());
        QCOMPARE(kinds.value().size(), 2);
        const KindInfo& range = kinds.value()[1];
        QCOMPARE(range.name, QStringLiteral("range"));
        QCOMPARE(range.title, QStringLiteral("변동폭"));
        QCOMPARE(range.classes, (QStringList{"small", "medium", "large"}));
        QCOMPARE(range.class_titles, (QStringList{"작음", "보통", "큼"}));
        QCOMPARE(range.action, (QStringList{"small", "large"}));
        QCOMPARE(range.answer_version, 1);
        QCOMPARE(range.fields.keys(), (QList<QString>{"q10", "q50", "q90"}));
        QCOMPARE(range.fields.value("q90").meaning, QStringLiteral("90% 분위수"));
        QCOMPARE(range.baselines.size(), 2);
        QCOMPARE(range.baselines[1].name, QStringLiteral("range-persist"));
        QCOMPARE(range.baselines[1].card->character, QStringLiteral("직전 4분"));

        // Read again from the same engine: the same description. An upgrade that changes a
        // baseline's card is another description, which the screen rebuilds on (review on #48).
        QCOMPARE(parse_kinds(json(kKinds)).value()[1], range);
        KindInfo upgraded = range;
        upgraded.baselines[1].card->character = QStringLiteral("직전 4분 (새)");
        QVERIFY(upgraded != range);
    }

    void an_engine_before_the_kinds_has_none_to_give() {
        // Its 404, or an answer without the list: the screen knows the direction alone then.
        QVERIFY(parse_kinds(json(R"({"error": "not found"})")).is_err());
        QVERIFY(parse_kinds(json(R"({"symbols": []})")).is_err());
    }

    void a_kind_s_latest_predictions_keep_their_output_and_answer() {
        const auto latest = parse_kind_latest(json(kRangeLatest));
        QVERIFY(latest.is_ok());
        const KindLatest& l = latest.value();
        QCOMPARE(l.kind, QStringLiteral("range"));
        QCOMPARE(l.bar_close, QDateTime::fromString("2026-10-12T09:31:00+09:00", Qt::ISODate));
        QCOMPARE(l.predictions.size(), 3);
        const KindPrediction& climate = l.predictions[0];
        QCOMPARE(climate.symbol, QStringLiteral("005930"));
        QCOMPARE(climate.value("p_medium"), std::optional<double>(0.5));
        QCOMPARE(climate.value("q90"), std::optional<double>(5.0));
        QVERIFY(!climate.value("p_up").has_value());
        QCOMPARE(climate.target_at, QDateTime::fromString("2026-10-12T09:35:00+09:00", Qt::ISODate));
        QCOMPARE(climate.answer->klass, QStringLiteral("medium"));
        QCOMPARE(climate.answer->answer_version, 1);
        const RangePath& path = *climate.answer->range;
        QCOMPARE(path.r, std::optional<double>(3.0));
        QCOMPARE(path.k, std::optional<int>(2));
        QCOMPARE(path.max_mid, std::optional<double>(81450.0));
        QCOMPARE(path.max_mid_at, QDateTime::fromString("2026-10-12T09:32:10.201+09:00", Qt::ISODateWithMs));
        QCOMPARE(path.min_mid, std::optional<double>(81150.0));
        QCOMPARE(path.down, std::optional<double>(1.0));
        QCOMPARE(path.quotes, 1043);
        QVERIFY(!path.silent);
        // Not made: no output, no target, no answer; made but its four minutes not over: no answer.
        QVERIFY(l.predictions[1].output.isEmpty());
        QVERIFY(!l.predictions[1].target_at.isValid());
        QVERIFY(!l.predictions[1].answer.has_value());
        QCOMPARE(l.predictions[1].status, QStringLiteral("no_previous"));
        QVERIFY(!l.predictions[2].answer.has_value());
        // The bar close's k came with each, made or not (krx-quant #200).
        for (const auto& p : l.predictions)
            QCOMPARE(p.flat_ticks, std::optional<int>(2));
    }

    void a_kind_answer_not_followed_or_not_scored_has_no_path_or_no_class() {
        const auto read = parse_kind_predictions(json(R"({"symbol": "005930", "kind": "range", "predictions": [
            {"algorithm": "alg-r", "model_version": "alg-r-1", "bar_close": "2026-10-12T09:31:00+09:00",
             "displayed_at": "2026-10-12T09:31:00+09:00", "target_at": "2026-10-12T09:35:00+09:00", "status": "ok",
             "output": {"p_small": 0.3, "p_medium": 0.3, "p_large": 0.4, "q10": 0, "q50": 1, "q90": 2},
             "answer": {"answer_version": 1, "class": null, "unscorable": "restarted", "special": false,
                        "scored_at": "2026-10-12T09:35:00+09:00", "path": {}}}]})"));
        QVERIFY(read.is_ok());
        const KindAnswer& a = *read.value()[0].answer;
        QVERIFY(a.klass.isEmpty());
        QCOMPARE(a.unscorable, QStringLiteral("restarted"));
        QVERIFY(!a.range.has_value()); // a window not followed: an empty path
        QCOMPARE(read.value()[0].symbol, QStringLiteral("005930"));
        QVERIFY(!read.value()[0].flat_ticks.has_value()); // an engine before krx-quant #200 gives no k

        // One stored before #200, or at a bar close without a mid-point: null, none.
        const auto older = parse_kind_predictions(json(R"({"symbol": "005930", "kind": "range", "predictions": [
            {"algorithm": "range-persist", "model_version": "v1", "bar_close": "2026-10-09T15:00:00+09:00",
             "displayed_at": "2026-10-09T15:00:00+09:00", "target_at": null, "status": "no_previous",
             "flat_ticks": null, "output": null, "answer": null}]})"));
        QVERIFY(older.is_ok());
        QVERIFY(!older.value()[0].flat_ticks.has_value());
    }

    void kind_events_bring_their_kind_s_predictions_and_answers() {
        const auto predicted = parse_event(R"({"type": "kind_prediction", "at": "2026-10-12T09:31:00.2+09:00",
            "prediction_kind": "range", "bar_close": "2026-10-12T09:31:00+09:00", "predictions": [
              {"symbol": "000660", "algorithm": "range-persist", "model_version": "v1",
               "bar_close": "2026-10-12T09:31:00+09:00", "displayed_at": "2026-10-12T09:31:00+09:00",
               "target_at": "2026-10-12T09:35:00+09:00", "status": "ok", "flat_ticks": 1,
               "output": {"p_small": 0, "p_medium": 0, "p_large": 1, "q10": 6, "q50": 6, "q90": 6}, "answer": null}]})");
        QVERIFY(predicted.is_ok());
        QCOMPARE(predicted.value().type, Event::Type::KindPrediction);
        QCOMPARE(predicted.value().prediction_kind, QStringLiteral("range"));
        QVERIFY(predicted.value().predictions.isEmpty()); // not the direction's
        QCOMPARE(predicted.value().kind_predictions.size(), 1);
        QCOMPARE(predicted.value().kind_predictions[0].symbol, QStringLiteral("000660"));
        QCOMPARE(predicted.value().kind_predictions[0].value("q50"), std::optional<double>(6));
        QCOMPARE(predicted.value().kind_predictions[0].flat_ticks, std::optional<int>(1));

        const auto answered = parse_event(R"({"type": "kind_answer", "at": "2026-10-12T09:35:00.1+09:00",
            "prediction_kind": "range", "answers": [
              {"symbol": "000660", "bar_close": "2026-10-12T09:31:00+09:00", "answer_version": 1, "class": "large",
               "unscorable": null, "special": true, "scored_at": "2026-10-12T09:35:00+09:00",
               "path": {"r": 7.5, "k": 1, "tick": 500, "anchor": 209000.0, "max_mid": 210250.0,
                        "max_mid_at": "2026-10-12T09:34:00+09:00", "min_mid": 206500.0,
                        "min_mid_at": "2026-10-12T09:31:00+09:00", "up": 2.5, "down": 5.0, "quotes": 80,
                        "silent": false, "max_bid": 210000.0, "min_ask": 206750.0}}]})");
        QVERIFY(answered.is_ok());
        QCOMPARE(answered.value().type, Event::Type::KindAnswer);
        QCOMPARE(answered.value().kind_answers.size(), 1);
        const KindAnswerAt& a = answered.value().kind_answers[0];
        QCOMPARE(a.symbol, QStringLiteral("000660"));
        QCOMPARE(a.bar_close, QDateTime::fromString("2026-10-12T09:31:00+09:00", Qt::ISODate));
        QCOMPARE(a.answer.klass, QStringLiteral("large"));
        QVERIFY(a.answer.special);
        QCOMPARE(a.answer.range->r, std::optional<double>(7.5));
        // The direction's consensus event keeps its own "kind" (fast, final).
        QCOMPARE(parse_event(R"({"type": "consensus", "kind": "final", "predictions": []})").value().prediction_kind,
                 QString());
    }

    void a_kind_s_leaderboard_keeps_its_kind_and_quantile_stats() {
        const auto board = parse_leaderboard(json(kRangeLeaderboard));
        QVERIFY(board.is_ok());
        QCOMPARE(board.value().kind, QStringLiteral("range"));
        const Entry& e = board.value().unranked[0];
        QCOMPARE(e.trades, 0); // no touch P&L: the direction's own
        QVERIFY(e.range.has_value());
        const RangeStats& r = *e.range;
        QCOMPARE(r.n, 240);
        QCOMPARE(r.pinball.value("q10").mean->value, 0.4);
        QVERIFY(!r.pinball.value("q10").by_day.has_value());
        QCOMPARE(r.pinball.value("q50").by_day->n, 2);
        QVERIFY(!r.pinball.value("q90").mean.has_value());
        QCOMPARE(r.interval_coverage->value, 0.78);
        QCOMPARE(r.q50_abs_error->high, std::optional<double>(2.7));
        QCOMPARE(r.vs_climate->value("q10").mean->value, -0.1);
        // range-climate itself: no comparison with itself, and nothing measured yet.
        const Entry& climate = board.value().baselines[0];
        QVERIFY(!climate.range->vs_climate.has_value());
        QVERIFY(!climate.range->interval_coverage.has_value());
        // The direction's: as before, no kind in the answer.
        const auto direction = parse_leaderboard(json(kLeaderboard));
        QCOMPARE(direction.value().kind, QStringLiteral("direction"));
        QVERIFY(!direction.value().ranked[0].range.has_value());
    }

    void a_kind_predictor_s_stats_say_its_kind() {
        const auto stats = parse_stats(json(R"({"kind": "range", "algorithm": "alg-r", "symbol": "all",
            "period": "today", "days": [], "special": "exclude", "min_sample": 200,
            "overall": {"rank": null, "algorithm": "alg-r", "model_versions": [], "n": 0, "accuracy": null,
                        "precision": null, "coverage": null, "brier": null,
                        "extra": {"n": 0, "pinball": {}, "interval_coverage": null, "q50_abs_error": null,
                                  "vs_climate": null},
                        "calibration": [], "calibration_by_class": {"small": [], "medium": [], "large": []},
                        "excluded": {"special": 0, "unscorable": 0, "status": {}}},
            "by_symbol": {}, "by_slot": {}, "by_model_version": {}})"));
        QVERIFY(stats.is_ok());
        QCOMPARE(stats.value().kind, QStringLiteral("range"));
        QCOMPARE(stats.value().calibration_by_class.keys(), (QList<QString>{"large", "medium", "small"}));
        QCOMPARE(stats.value().overall.range->n, 0);
        QCOMPARE(parse_stats(json(kStats)).value().kind, QStringLiteral("direction")); // an older engine's
    }

    void the_symbols_say_their_tick_and_k_now() {
        const auto symbols = parse_symbols(json(R"({"symbols": [
            {"symbol": "005930", "mid": 81250.0, "tick": 100, "flat_ticks": 2},
            {"symbol": "000660", "mid": null, "tick": null, "flat_ticks": null}]})"));
        QCOMPARE(symbols.value()[0].tick, std::optional<double>(100));
        QCOMPARE(symbols.value()[0].flat_ticks, std::optional<int>(2));
        QVERIFY(!symbols.value()[1].flat_ticks.has_value());
    }

    void an_algorithm_says_which_kind_it_predicts() {
        const auto read = parse_algorithms(json(R"({"algorithms": [
            {"name": "alg-r", "kind": "range", "card": {}, "features": [], "model": {}, "status": {}},
            {"name": "alg-d", "card": {}, "features": [], "model": {}, "status": {}}],
            "baselines": [], "consensus": []})"));
        QVERIFY(read.is_ok());
        QCOMPARE(read.value().value("alg-r").prediction_kind, QStringLiteral("range"));
        QCOMPARE(read.value().value("alg-r").kind, QStringLiteral("algorithm"));
        QCOMPARE(read.value().value("alg-d").prediction_kind, QStringLiteral("direction")); // an older engine's
    }

    void the_range_s_baselines_read_in_korean() {
        QCOMPARE(predictor_label("range-climate"), QStringLiteral("기준: 변동폭 시간대 기후"));
        QCOMPARE(predictor_label("range-persist"), QStringLiteral("기준: 직전 4분 변동폭"));
    }

    void a_barrier_prediction_keeps_its_width_and_its_answer_s_barriers() {
        // krx-quant #183, #185.
        const auto latest = parse_kind_latest(json(kBarrierLatest));
        QVERIFY(latest.is_ok());
        const KindLatest& l = latest.value();
        QCOMPARE(l.kind, QStringLiteral("barrier"));
        const KindPrediction& persist = l.predictions[0];
        QCOMPARE(persist.width_ticks, std::optional<int>(6));
        QCOMPARE(persist.anchor, std::optional<double>(78350.0)); // the bar close's mid-point (krx-quant #210)
        QCOMPARE(persist.tick, std::optional<double>(100));
        QCOMPARE(persist.value("p_timeout"), std::optional<double>(1.0));
        QCOMPARE(persist.answer->klass, QStringLiteral("up_first"));
        QVERIFY(!persist.answer->range.has_value()); // not a range's path
        const BarrierPath& path = *persist.answer->barrier;
        QCOMPARE(path.w, std::optional<int>(6));
        QCOMPARE(path.sigma, std::optional<double>(6.405));
        QCOMPARE(path.k, std::optional<int>(2));
        QCOMPARE(path.anchor, std::optional<double>(78350.0));
        QCOMPARE(path.upper, std::optional<double>(78950.0));
        QCOMPARE(path.lower, std::optional<double>(77750.0));
        QCOMPARE(path.cost, std::optional<double>(1.80205));
        QCOMPARE(path.up_at, QDateTime::fromString("2026-10-12T10:29:28+09:00", Qt::ISODate));
        QCOMPARE(path.up_bid, std::optional<double>(79000));
        QVERIFY(!path.down_at.isValid()); // L not reached
        QVERIFY(!path.down_ask.has_value());
        QCOMPARE(path.ask_end, std::optional<double>(79300));
        QCOMPARE(path.quotes, 21);
        // Not made (a VI): no output, still the bar close's w, mid-point and tick.
        QCOMPARE(l.predictions[1].width_ticks, std::optional<int>(6));
        QCOMPARE(l.predictions[1].anchor, std::optional<double>(78350.0));
        QCOMPARE(l.predictions[1].tick, std::optional<double>(100));
        QVERIFY(l.predictions[1].output.isEmpty());
        // The range's: no barrier path, no w; from an engine before krx-quant #210, no mid-point or tick.
        const auto range = parse_kind_latest(json(kRangeLatest));
        QVERIFY(!range.value().predictions[0].answer->barrier.has_value());
        QVERIFY(!range.value().predictions[0].width_ticks.has_value());
        QVERIFY(!range.value().predictions[0].anchor.has_value());
        QVERIFY(!range.value().predictions[0].tick.has_value());
    }

    void a_barrier_answer_not_followed_or_without_a_mid_point_has_no_barriers() {
        const auto read = parse_kind_predictions(json(R"({"symbol": "005930", "kind": "barrier", "predictions": [
            {"algorithm": "barrier-persist", "model_version": "v1", "bar_close": "2026-10-12T09:31:00+09:00",
             "displayed_at": "2026-10-12T09:31:00+09:00", "target_at": "2026-10-12T09:35:00+09:00", "status": "ok",
             "flat_ticks": null, "width_ticks": null, "anchor": null, "tick": null,
             "output": {"p_up_first": 1, "p_timeout": 0, "p_down_first": 0},
             "answer": {"answer_version": 1, "class": null, "unscorable": "restarted", "special": false,
                        "scored_at": "2026-10-12T09:35:00+09:00", "path": {}}},
            {"algorithm": "barrier-persist", "model_version": "v1", "bar_close": "2026-10-12T09:32:00+09:00",
             "displayed_at": "2026-10-12T09:32:00+09:00", "target_at": "2026-10-12T09:36:00+09:00", "status": "ok",
             "flat_ticks": null, "width_ticks": null,
             "output": {"p_up_first": 1, "p_timeout": 0, "p_down_first": 0},
             "answer": {"answer_version": 1, "class": null, "unscorable": "missing mid", "special": false,
                        "scored_at": "2026-10-12T09:36:00+09:00",
                        "path": {"w": null, "sigma": null, "k": null, "tick": null, "anchor": null, "upper": null,
                                 "lower": null, "cost": null, "bid_start": null, "ask_start": null, "up_at": null,
                                 "up_bid": null, "up_ask": null, "down_at": null, "down_bid": null,
                                 "down_ask": null, "bid_end": null, "ask_end": null, "quotes": 0,
                                 "silent": false}}}]})"));
        QVERIFY(read.is_ok());
        QVERIFY(!read.value()[0].answer->barrier.has_value()); // an empty path
        QVERIFY(!read.value()[0].width_ticks.has_value());
        QVERIFY(!read.value()[0].anchor.has_value()); // null: no mid-point at the bar close
        QVERIFY(!read.value()[0].tick.has_value());
        const BarrierPath& none = *read.value()[1].answer->barrier; // without a mid-point: all null
        QVERIFY(!none.w.has_value());
        QVERIFY(!none.upper.has_value());
        QVERIFY(!none.up_at.isValid());
    }

    void a_barrier_answer_comes_by_the_stream_with_its_path() {
        const auto predicted = parse_event(R"({"type": "kind_prediction", "at": "2026-10-12T09:31:00.2+09:00",
            "prediction_kind": "barrier", "bar_close": "2026-10-12T09:31:00+09:00", "predictions": [
              {"symbol": "000660", "algorithm": "barrier-climate", "model_version": "barrier-climate-default",
               "bar_close": "2026-10-12T09:31:00+09:00", "displayed_at": "2026-10-12T09:31:00+09:00",
               "target_at": "2026-10-12T09:35:00+09:00", "status": "ok", "flat_ticks": 1, "width_ticks": 3,
               "anchor": 209000.0, "tick": 500,
               "output": {"p_up_first": 0.25, "p_timeout": 0.5, "p_down_first": 0.25}, "answer": null}]})");
        QVERIFY(predicted.is_ok());
        QCOMPARE(predicted.value().kind_predictions[0].width_ticks, std::optional<int>(3));
        QCOMPARE(predicted.value().kind_predictions[0].anchor, std::optional<double>(209000.0)); // krx-quant #210
        QCOMPARE(predicted.value().kind_predictions[0].tick, std::optional<double>(500));
        const auto answered = parse_event(R"({"type": "kind_answer", "at": "2026-10-12T09:35:00.1+09:00",
            "prediction_kind": "barrier", "answers": [
              {"symbol": "000660", "bar_close": "2026-10-12T09:31:00+09:00", "answer_version": 1,
               "class": "down_first", "unscorable": null, "special": false, "scored_at": "2026-10-12T09:35:00+09:00",
               "path": {"w": 3, "sigma": 2.6, "k": 1, "tick": 500, "anchor": 209000.0, "upper": 210500.0,
                        "lower": 207500.0, "cost": 0.96, "bid_start": 208500.0, "ask_start": 209500.0,
                        "up_at": "2026-10-12T09:34:10.5+09:00", "up_bid": 210500.0, "up_ask": 211000.0,
                        "down_at": "2026-10-12T09:32:01.25+09:00", "down_bid": 207000.0, "down_ask": 207500.0,
                        "bid_end": 210000.0, "ask_end": 210500.0, "quotes": 64, "silent": false}}]})");
        QVERIFY(answered.is_ok());
        const KindAnswerAt& a = answered.value().kind_answers[0];
        QCOMPARE(a.answer.klass, QStringLiteral("down_first"));
        QCOMPARE(a.answer.barrier->down_at, QDateTime::fromString("2026-10-12T09:32:01.25+09:00", Qt::ISODateWithMs));
        QCOMPARE(a.answer.barrier->down_ask, std::optional<double>(207500.0));
        QCOMPARE(a.answer.barrier->up_at, QDateTime::fromString("2026-10-12T09:34:10.5+09:00", Qt::ISODateWithMs));
        QCOMPARE(a.answer.barrier->lower, std::optional<double>(207500.0));
    }

    void a_barrier_leaderboard_keeps_its_barrier_p_and_l() {
        const auto board = parse_leaderboard(json(kBarrierLeaderboard));
        QVERIFY(board.is_ok());
        QCOMPARE(board.value().kind, QStringLiteral("barrier"));
        const Entry& persist = board.value().baselines[1];
        QVERIFY(!persist.range.has_value());
        QCOMPARE(persist.trades, 0); // no touch P&L: the direction's own
        const BarrierStats& b = *persist.barrier;
        QCOMPARE(b.n, 240);
        QCOMPARE(b.pnl.trades, 121);
        QCOMPARE(b.pnl.total_ticks, -378.38);
        QCOMPARE(b.pnl.per_trade->low, std::optional<double>(-3.855));
        QCOMPARE(b.longs.trades, 60);
        QCOMPARE(b.shorts.per_trade->value, -2.681);
        // No trade: no mean.
        const BarrierStats& climate = *board.value().baselines[0].barrier;
        QCOMPARE(climate.n, 252);
        QCOMPARE(climate.pnl.trades, 0);
        QVERIFY(!climate.pnl.per_trade.has_value());
        // The range's and the direction's: no barrier P&L.
        QVERIFY(!parse_leaderboard(json(kRangeLeaderboard)).value().unranked[0].barrier.has_value());
        QVERIFY(!parse_leaderboard(json(kLeaderboard)).value().ranked[0].barrier.has_value());
    }

    void the_barrier_s_baselines_read_in_korean() {
        QCOMPARE(predictor_label("barrier-climate"), QStringLiteral("기준: 장벽 시간대 기후"));
        QCOMPARE(predictor_label("barrier-persist"), QStringLiteral("기준: 직전 4분 장벽"));
    }
};

QTEST_GUILESS_MAIN(TstKrxEngineData)
#include "tst_krx_engine_data.moc"
