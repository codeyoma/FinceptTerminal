#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/// What the KRX pre-open briefing screen shows (codeyoma/krx-quant#304), as text, from the engine's
/// GET /v1/briefing answer. The screen only draws it.
///
/// Qt Core only (no network, no widgets), unit-tested on its own (tests/tst_krx_briefing.cpp).
namespace fincept::krx_quant::briefing {

/// How often the screen reads the briefing again: every kFastMs from 07:00 to 09:10 on the
/// briefing's day (the pre-market and the opening auction move), else every kSlowMs (to catch
/// the day turning, the evening's answers).
inline constexpr int kFastMs = 30'000;
inline constexpr int kSlowMs = 300'000;

/// The interval to the next read, from what was last read (none yet: the fast one) and now.
int poll_ms(const std::optional<Briefing>& last, const QDateTime& now);

/// "+0.37%", "−0.65%", "0.00%"; "—" without a value.
QString pct_text(const std::optional<double>& pct);
/// A price as KRX shows one: "81,500"; decimals as given for the futures and the US ("393.25").
QString price_text(const std::optional<double>& price, int decimals = 0);
/// Net buys in shares, signed and grouped: "+129,496주"; "—" without a figure.
QString shares_text(const std::optional<double>& shares);

/// The title's line: "2026-10-19(월) 장전 · 국면 위험회피", with "(휴장)" on a closed day and the
/// late open ("수능일 10:00 개장") on a special one.
QString headline(const Briefing& b);

/// A US indicator's line: its name, the last close, its change, and the five days' changes.
struct UsRow {
    QString name;              ///< "SOX", "엔비디아", "마이크론", "VIX"
    QString last;              ///< the last close before the day; "—" without
    QString change;            ///< its change against the close before
    QString days;              ///< the changes of the closes shown, oldest first: "−0.6 · −0.6 · +1.2"
    QString tip;               ///< each close's day and value
    std::optional<double> pct; ///< the last change, for its color
};
QList<UsRow> us_rows(const Briefing& b);

/// The night session as one line and its tooltip.
struct NightLine {
    QString text; ///< "A01612 393.00 (+0.77%) · 고 395.50 저 391.00 · 1,500계약 · 마지막 05:59"
    QString tip;  ///< the window and the contract
    std::optional<double> pct;
};
NightLine night_line(const NightSession& night);

/// A symbol's line of the briefing's symbol table.
struct SymbolRow {
    QString symbol;
    QString previous_close;
    QString after_hours; ///< "81,800 (+0.74%)"; "—"
    QString after_hours_tip;
    QString premarket;     ///< "81,500 (+0.37%)"; "—"
    QString premarket_tip; ///< the window and the trade's time
    QString expected;      ///< "81,600 (+0.49%)"; "—"
    QString expected_tip;  ///< the time, quantity, venue code and whether it is final (or why none), then the note
    QString foreign;
    QString institution;
    QString foreign_sum;
    QString institution_sum;
    QString supply_tip; ///< the day of the figures and how many days the sums cover
    std::optional<double> premarket_pct;
    std::optional<double> expected_pct;
    std::optional<double> after_hours_pct;
};
QList<SymbolRow> symbol_rows(const Briefing& b);

/// The symbol table's KRX expected-open column (codeyoma/krx-quant#314): its title, "KRX 예상 시가(참고)",
/// and the note its tooltip and each cell's tooltip end with. The expected price is a reference: the
/// opening auction's last before the open, which was the open on 2 of 7 days measured
/// (2026-10-12 – 10-20) and usually 1–5 ticks off on the others.
QString expected_open_title();
QString expected_open_note();

/// A daily algorithm's forecast of a symbol (the baselines are left out: their inputs are none).
struct ForecastRow {
    QString symbol;
    QString algorithm; ///< predictor_label
    QString up;        ///< "34.7%"; "—" without a forecast
    QString flat;
    QString down;
    QString band;   ///< the day's flat band: "±0.47%"; "—"
    QString result; ///< the day's answer once in: "상승", "보합", "하락", "채점 불가 (…)"; empty before
    QString status; ///< empty when made; else why not, as status_label says it ("시간 초과", …)
    Direction lean = Direction::None; ///< the likeliest class, a tie going to flat, then up (most_likely)
};
QList<ForecastRow> forecast_rows(const Briefing& b);

/// An input's line, as the model read it.
struct InputRow {
    QString name;  ///< its label ("전일 시가→종가 (σ)"); the feature's name when unknown
    QString key;   ///< the feature's name
    QString value; ///< "−0.81"; "없음" when missing
    QString z;     ///< "−0.85"; "—"
    QString up;    ///< what it added to the up score: "+0.004"; "—" unexplained
    QString down;
    QString lean;      ///< "상승 쪽", "하락 쪽", "보합 쪽" (both scores down), or "—"
    double weight = 0; ///< |up| + |down|, the order
};

/// ``inputs``' lines, the largest parts first when explained, else in the order declared.
QList<InputRow> input_rows(const AlgorithmInputs& inputs);
/// A feature's label in Korean ("us_sox" → "밤사이 SOX 등락"); ``name`` itself when unknown.
QString input_label(const QString& name);
/// The day's inputs of ``symbol`` by its first daily algorithm; none without.
std::optional<AlgorithmInputs> inputs_of(const Briefing& b, const QString& symbol);

/// An economic calendar event's line.
struct EventRow {
    QString time; ///< "21:30"
    QString country;
    QString title;
    QString importance; ///< "높음", "보통", "낮음"
    QString forecast;   ///< "3.7%"; empty when unknown
    QString previous;
    QString actual;
    bool high = false;
};
QList<EventRow> event_rows(const Briefing& b);

/// The day's flags: "선물·옵션 만기일" (or "옵션 만기일"), "월말", "월초", "수능일 10:00 개장", "연초 첫 거래일 10:00
/// 개장".
QStringList flags(const Briefing& b);

} // namespace fincept::krx_quant::briefing
