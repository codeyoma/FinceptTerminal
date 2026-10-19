#pragma once

#include "services/krx_quant/EngineData.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/// What the KRX horizon and cost screen shows (codeyoma/krx-quant#307), from the engine's GET
/// /v1/cost-map answer: the cost cards' lines, the horizon table's cells and colors, and the base
/// rate table's rows. The screen only draws them.
///
/// Qt Core only (no network, no widgets), unit-tested on its own (tests/tst_krx_cost.cpp).
namespace fincept::krx_quant::cost {

/// How often the screen reads the map again: every kFastMs while a symbol's price is live
/// (continuous trading), else every kSlowMs (the map moves with the day's predictions only, and a
/// screen left open still sees the market open).
inline constexpr int kFastMs = 60'000;
inline constexpr int kSlowMs = 600'000;
int poll_ms(const std::optional<CostMap>& last);

/// Which round trip the table holds its rows to.
enum class Cost { Limit, Market };
/// "지정가", "시장가".
QString cost_label(Cost cost);

/// A predictor's verdict at a cost: its hit rate's lower bound above p*, not, too few calls, or no
/// p* to judge by.
enum class Verdict { Pass, Fail, Few, None };
Verdict verdict(const CostPredictor& predictor, Cost cost);
/// "넘음", "못 넘음", "표본 부족", "해당 없음".
QString verdict_text(Verdict verdict);

/// A cost card's title: "SK하이닉스 000660"; the code alone without a name.
QString card_title(const CostSymbol& symbol);

/// A cost card's line: its label, value and tooltip; ``warn`` where the value says the horizon cannot
/// pay (k under the market round trip).
struct CardLine {
    QString label;
    QString value;
    QString tip;
    bool warn = false;
};
/// The card's lines: the price, the tick, each cost, both round trips, k, the inverse 2X round trip
/// ("거래 수단 없음" without one), and the tick-band edges below and above.
QList<CardLine> card_lines(const CostSymbol& symbol);

/// The row's horizon: "방향 4분", "장벽 15분", "시가→종가 (하루)".
QString horizon_text(const CostRow& row);
/// The width W: "2틱", "12틱", "51.4bp"; "—" without one.
QString width_text(const CostRow& row);
/// Both costs C: "지정가 1.76 · 시장가 2.76틱"; a daily kind's "23.0bp (단일가)"; "—" without them.
QString cost_text(const CostRow& row);
/// The breakeven at ``cost``: "p* 93.9% · C/W 0.88", "(불가능)" past 100%; "해당 없음" without one.
QString breakeven_text(const CostRow& row, Cost cost);
/// What the breakeven is, on its cell's tooltip: the formulas, the bar close W and C were read at
/// (an approximation), or why there is none.
QString breakeven_tip(const CostRow& row);
/// The row header's tooltip: the kind's answer rule and the days it covers.
QString horizon_tip(const CostRow& row);

/// The three predictors of a row, the table's columns in order.
enum class Role { Consensus, Top, Baseline };
inline constexpr int kRoles = 3;
/// "최종 합의", "순위 1위", "가장 센 기준 예측".
QString role_label(Role role);
const std::optional<CostPredictor>& predictor(const CostRow& row, Role role);

/// A predictor's cell at ``cost``: its name, hit rate with the lower bound, edge and calls, and its
/// research P&L; the reason when there is none. Colored by ``verdict``.
struct PredictorCell {
    QString text;
    QString tip;
    Verdict verdict = Verdict::None;
};
PredictorCell predictor_cell(const CostRow& row, Role role, Cost cost, int min_sample);

/// A base rate table's line, as its cells show it.
struct RegimeLine {
    QString symbol;
    QString period;
    QString days;
    QString windows;
    QString up;
    QString flat; ///< always-flat's hit rate
    QString down;
    QString k;
    QString persist;
};
QList<RegimeLine> regime_lines(const BaseRates& rates);
/// The base rate table's title line: "계산 10-11 20:30 · 1분봉 2025-10-13 – 2026-10-08"; the reason
/// they are missing.
QString regimes_line(const CostMap& map);

/// The answer's line under the title: "2026-10-12 10:30 기준 · 최소 표본 200 · 스프레드 1틱 가정".
QString as_of_line(const CostMap& map);

} // namespace fincept::krx_quant::cost
