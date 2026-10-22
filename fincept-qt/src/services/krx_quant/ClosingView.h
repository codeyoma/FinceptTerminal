#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/// What the KRX closing flow screen shows (codeyoma/krx-quant#305), from the engine's GET
/// /v1/symbols/{symbol}/closing-flow and …/closing-flow/days answers: today's demand card, the product
/// table, the past days' rows and the summary's cells, and when to read again. The screen only draws
/// them.
///
/// Qt Core only (no network, no widgets), unit-tested on its own (tests/tst_krx_closing.cpp).
namespace fincept::krx_quant::closing {

/// How often the screen reads again: every kFastMs while the demand is the minute's (continuous
/// trading) and after a failed read; every kSlowMs in the evening of a trading day until the days are
/// computed (the engine computes them once after the 20:30 backfill, from kEveningCheck on).
inline constexpr int kFastMs = 60'000;
inline constexpr int kSlowMs = 600'000;
/// A summary cell over fewer days than this is dimmed: its rate may be chance.
inline constexpr int kThin = 20;
/// What the KRX Open API's terms ask a screen showing its data to say.
inline constexpr const char* kKrxLabel = "한국거래소 통계정보";

/// When to read again (ms from ``now``): every kFastMs while the demand is the minute's, after a
/// failure and before the first answer; before the open, at 09:00 (then every kFastMs until it opens,
/// on a late-open day); on a trading day after 15:20, at 20:35 and then every kSlowMs until the days
/// are computed after 20:30; else when the day changes (just after midnight, KST).
int poll_ms(const std::optional<ClosingFlow>& flow, const std::optional<ClosingDays>& days, bool failed,
            const QDateTime& now);

/// Whether the next read asks for the days too (they change once a day): none yet, another symbol's,
/// read on another day than ``now``'s (KST), or read before they were computed on a trading day's
/// evening (from 20:30).
bool days_due(const std::optional<ClosingDays>& days, const QString& symbol, const QDateTime& read_at,
              const std::optional<ClosingFlow>& flow, const QDateTime& now);

/// Whether a net assets' source is KRX's: any but KIS's ("kis"). KRX's Open API is the only other
/// source the engine reads them from (krx-quant docs/design.md 4.1), and its name there is not fixed.
bool krx_source(const QString& source);
/// Whether anything shown came from KRX's Open API (a product's net assets, a day's): the screen then
/// says kKrxLabel, as KRX's terms ask.
bool shows_krx(const std::optional<ClosingFlow>& flow, const std::optional<ClosingDays>& days);
/// A net assets' source as the screen names it: "KIS", kKrxLabel; empty for none.
QString source_label(const QString& source);

/// Buy (+), sell (−), or neither (0 or none): the amount's color.
enum class Side { Buy, Sell, None };
Side side_of(const std::optional<double>& value);

/// An amount in won, in 억원: "+1,154.6억원", "−2,378.7억원", unsigned "764.9억원", "0원"; "—" without one.
QString won_text(const std::optional<double>& won, bool sign = true);
/// "매수", "매도"; empty for 0 or none.
QString side_text(const std::optional<double>& won);
/// A ratio in times: "−1.51배"; with ``upper``, "−1.51배 (상한)"; "—" without one.
QString ratio_text(const std::optional<double>& ratio, bool upper = false);
/// Basis points with a sign: "+12.3bp"; "—" without one.
QString bp_text(const std::optional<double>& bp);
/// "현물+선물형", "선물형"; an unknown one as it is.
QString structure_label(const QString& structure);
/// Why today's demand is none, in Korean (the answer's ``reason``); empty for none.
QString reason_text(const QString& reason);
/// Why the denominator is none, in Korean (``denominator_reason``); empty for none.
QString denominator_reason_text(const QString& reason);

/// The line under the title: "2026-10-12(월) · 10:30 지금 가격으로 계산", "… · 15:20 저장값", "… · 장 전".
QString day_line(const ClosingFlow& f);
/// The demand card's headline: "매도 −2,378.7억원"; the reason when there is no demand.
QString headline(const ClosingFlow& f);

/// A line of the demand card: its label, value and tooltip, the value's side and whether it is dim.
struct CardLine {
    QString label;
    QString value;
    QString tip;
    Side side = Side::None;
    bool dim = false;
};
/// The card's lines: each structure's sum, the closing auction ratio (an upper bound) and the total's,
/// the denominator with its days or reason, r, and when the demand was computed. A symbol without
/// products (009150) has the denominator, r and the time only.
QList<CardLine> card_lines(const ClosingFlow& f);
/// The notes under the card: the formula's value, not an execution; the Hong Kong products and the
/// ETNs left out; the products without net assets, out of the sums.
QStringList notes(const ClosingFlow& f);

/// A row of the product table.
struct ProductRow {
    QString code;
    QString name;
    QString leverage;        ///< "2배", "−2배"
    QString structure;       ///< structure_label's
    QString coefficient;     ///< L² − L: "2", "6"
    QString net_assets;      ///< "2,154.6억원"; "없음"
    QString net_assets_date; ///< "10-08"; "—"
    QString source;          ///< source_label's; "—"
    QString demand;          ///< "−106.1억원"; "없음 (합에서 뺌)"
    QString tip;             ///< the row's: what its coefficient and its missing net assets mean
    Side side = Side::None;
    bool missing = false; ///< no net assets: out of the sums
    bool inverse = false; ///< L < 0: a coefficient of 6
};
QList<ProductRow> product_rows(const ClosingFlow& f);

/// A row of the past days' table, newest first.
struct DayRow {
    QString date; ///< "10-08(목)"
    QString period;
    QString demand;
    QString demand_tip;
    Side side = Side::None;
    QString ratio; ///< the closing auction ratio, an upper bound; the reason without one
    QString ratio_tip;
    bool ratio_missing = false;
    QString auction; ///< "+24.9bp (1틱)"
    QString auction_tip;
    Side auction_side = Side::None;
    QString agree; ///< "같음", "반대", "—"; "(흐름)" where compared with the day's flow
    QString agree_tip;
    std::optional<bool> agreed;
    QString next_gap;
    QString next_close;
    QString special; ///< "VI · 서킷브레이커"; empty
    QString source;  ///< how the demand was had: "15:20 저장", "1분봉 계산"; empty
};
/// ``has_products``: whether the symbol has products (a day without a demand is then one without net
/// assets, else a symbol without products such as 009150).
QList<DayRow> day_rows(const ClosingDays& d, bool has_products);

/// A summary cell: its text, tooltip and whether its sample is thin (under kThin days).
struct StatsCell {
    QString text;
    QString tip;
    bool thin = false;
};
/// A summary row: a period or a ratio tertile, with or without the special situations' days.
struct SummaryRow {
    QString group;
    QString group_tip;
    bool without_special = false;
    QList<StatsCell> cells; ///< summary_headers' columns after the group's
};
/// The summary table's columns after the group's: days, the median |closing auction move|, how often
/// it went the demand's way, how often the next day's gap and close went against it.
QStringList summary_headers();
/// Each period, then each tertile of |ratio|, each as a row with every day and a row without the
/// special situations' days.
QList<SummaryRow> summary_rows(const ClosingDays& d);

/// "2026-10-21 20:41에 계산 · 2025-10-23 – 2026-10-22"; "아직 계산하지 않았습니다" before the first.
QString days_line(const ClosingDays& d);

} // namespace fincept::krx_quant::closing
