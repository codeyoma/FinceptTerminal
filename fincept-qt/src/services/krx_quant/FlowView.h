#pragma once

#include "services/krx_quant/EngineData.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/// What the KRX flow screen shows (codeyoma/krx-quant#306), from the engine's GET
/// /v1/symbols/{symbol}/flow answer: the series its chart draws, the hover box's lines and the side
/// panels' rows. The screen only draws them.
///
/// Qt Core only (no network, no widgets), unit-tested on its own (tests/tst_krx_flow.cpp).
namespace fincept::krx_quant::flow {

/// How often the screen reads the flows again: every kFastMs on today while KRX trades (the open to
/// ten minutes after the close), else every kSlowMs (a past day changes only when the 20:30 final
/// supply comes in).
inline constexpr int kFastMs = 30'000;
inline constexpr int kSlowMs = 300'000;
int poll_ms(const std::optional<Flow>& last, const QDateTime& now);

/// A flow the chart draws, each on its own scale.
enum class Series { Price, Program, Member, EstimateForeign, EstimateInstitution };
inline constexpr int kSeries = 5;
/// "가격", "프로그램 순매수", "외국계 회원사", "추정 외국인", "추정 기관".
QString series_label(Series series);
/// What its values are: "원", "KIS 금액 단위", "주".
QString series_unit(Series series);

/// A point of a series: seconds since the epoch, and the value.
struct Point {
    qint64 at = 0;
    double value = 0;
};

/// A series as lines: each run of minutes with values is one segment; a minute without a value breaks
/// it (a silent feed is not drawn flat). The estimates are steps: each slot's value from when it came
/// in to the next slot's, the last one to the session's close (or now, today).
QList<QList<Point>> segments(const Flow& f, Series series, const QDateTime& now);

/// The hover box at minute ``index`` of f.minutes: its time, then a line a flow ("—" without a value),
/// with "(REST)" where the REST stand-in wrote it, and the estimates known by then.
QStringList hover_lines(const Flow& f, int index);

/// A line of the totals table: the flow, its day total by now (by the close on a past day), and the
/// previous trading day's at the same clock time.
struct TotalRow {
    QString flow;
    QString today;
    QString previous;
    std::optional<double> value; ///< today's, for its color
};
QList<TotalRow> total_rows(const Flow& f);
/// The totals table's previous column's header: "전일(10-16) 10:30까지"; "전일" without one.
QString previous_header(const Flow& f);

/// The final daily supply against the last estimates: "확정 외국인 +1,800주 · 기관 — (추정 마지막: 외국인 +1,500주 ·
/// 기관 −300주)"; "확정 수급은 20:30 뒤에 들어옵니다" before it.
QString final_line(const Flow& f);

/// A member's line: its name, quantity, and whether it is a foreign firm.
struct MemberRow {
    QString name; ///< the name; the number when the name is empty
    QString qty;  ///< "5,015주"
    bool foreign = false;
};
QList<MemberRow> member_rows(const QList<FlowMember>& members);

/// The day's line under the title: "2026-10-12(월) · 마지막 기록 10:30 · REST 대체 10:00–10:10"; "녹화 없음"
/// for a day the server did not record.
QString day_line(const Flow& f);

/// A flow value as the tables show it, signed and grouped: "+5,384,790", "−1,429주"; "—" without one.
QString value_text(const std::optional<double>& value, Series series);

} // namespace fincept::krx_quant::flow
