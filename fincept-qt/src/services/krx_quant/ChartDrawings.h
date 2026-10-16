#pragma once

#include "services/krx_quant/ChartModel.h"

#include <QList>
#include <QString>
#include <QVariant>

#include <array>
#include <optional>

/// The KRX chart's drawings (krx-quant #220, #224), worked out here so that the toolbar and the
/// chart's page only show them: the twelve tools, a drawing as each symbol keeps it, where a
/// moment lies on a view's bars and back, and the magnet. A drawing's points are moments and
/// prices, so it stays at the same place in every view; the page works in bar positions, which
/// these functions turn into moments and back. Qt Core only.
namespace fincept::krx_quant {

/// The drawing tools, in the order of the toolbar's `그리기` menu.
enum class DrawingTool {
    TrendLine,
    Ray,
    Horizontal,
    Vertical,
    Cross,
    Rectangle,
    Ellipse,
    Pitchfork,
    FibRetracement,
    FibChannel,
    FibFan,
    Text,
};

inline constexpr std::array<DrawingTool, 12> kDrawingTools{
    DrawingTool::TrendLine,      DrawingTool::Ray,        DrawingTool::Horizontal, DrawingTool::Vertical,
    DrawingTool::Cross,          DrawingTool::Rectangle,  DrawingTool::Ellipse,    DrawingTool::Pitchfork,
    DrawingTool::FibRetracement, DrawingTool::FibChannel, DrawingTool::FibFan,     DrawingTool::Text,
};

/// The tool's name in the settings and to the page: "trend_line", "ray", "horizontal", "vertical",
/// "cross", "rectangle", "ellipse", "pitchfork", "fib_retracement", "fib_channel", "fib_fan", "text".
QString tool_key(DrawingTool tool);
/// The tool named ``key`` (tool_key); none for any other text.
std::optional<DrawingTool> parse_tool(const QString& key);
/// The tool's name on the menu: 추세선, 반직선, 수평선, 수직선, 크로스, 사각형, 타원, 피치포크,
/// 피보나치 되돌림, 피보나치 채널, 피보나치 속도 저항 팬, 텍스트.
QString tool_title(DrawingTool tool);
/// The points placed to draw one: 1 (수평선, 수직선, 크로스, 텍스트), 3 (피치포크, 피보나치 채널),
/// else 2.
int tool_points(DrawingTool tool);
/// The key that picks the tool in the open `그리기` menu (krx-quant #241), shown at its right: T 추세선,
/// R 반직선, H 수평선, V 수직선, C 크로스, B 사각형, E 타원, P 피치포크, F 피보나치 되돌림,
/// N 피보나치 채널, A 피보나치 속도 저항 팬, X 텍스트.
QChar tool_letter(DrawingTool tool);
/// The key that turns the magnet on or off in the open `그리기` menu (krx-quant #241).
inline constexpr char kMagnetLetter = 'M';
/// The letter of a key pressed, as the `그리기` menu reads it (krx-quant #241): ``key`` (a Qt::Key)
/// A–Z as it is. A letter of a layout that is not Latin (Hangul types ㅅ where T is) is the letter at
/// its place on a US keyboard, by ``native_key`` (QKeyEvent::nativeVirtualKey: macOS's kVK_ANSI_
/// codes, Windows' 'A'–'Z'). None for any other key.
std::optional<QChar> typed_letter(int key, quint32 native_key);

/// How a drawing's lines are drawn.
enum class LineStyle { Solid, Dotted, Dashed };
inline constexpr std::array<LineStyle, 3> kLineStyles{LineStyle::Solid, LineStyle::Dotted, LineStyle::Dashed};
/// "solid", "dotted", "dashed".
QString line_style_key(LineStyle style);
std::optional<LineStyle> parse_line_style(const QString& key);
/// 실선, 점선, 파선.
QString line_style_title(LineStyle style);

/// A point of a drawing: a moment (chart_time, KST read as UTC, to the second) and a price.
struct DrawingPoint {
    qint64 time = 0;
    double price = 0;
    bool operator==(const DrawingPoint& other) const = default;
};

/// A drawing's colour before it is changed (#rrggbb).
inline constexpr auto kDrawingColor = "#60A5FA";
/// The widths a drawing's lines take (a text's: its size, kTextSizes).
inline constexpr int kMinDrawingWidth = 1;
inline constexpr int kMaxDrawingWidth = 4;
/// A text's size in pixels by its width 1–4.
inline constexpr std::array<int, 4> kTextSizes{12, 14, 17, 20};
/// The text a text drawing starts with, chosen to be typed over.
inline constexpr auto kDrawingText = "텍스트";
/// The most characters a text drawing keeps.
inline constexpr int kMaxDrawingText = 200;

/// A drawing on a symbol's chart.
struct Drawing {
    /// Which it is among the symbol's while they are shown: numbered as they are read, not kept.
    int id = 0;
    DrawingTool tool = DrawingTool::TrendLine;
    QList<DrawingPoint> points; ///< tool_points of them, in the order placed
    QString color = QString::fromLatin1(kDrawingColor);
    int width = 2; ///< kMinDrawingWidth–kMaxDrawingWidth
    LineStyle style = LineStyle::Solid;
    QString text; ///< the text tool's; empty for the others
    bool operator==(const Drawing& other) const = default;
};

/// A new drawing of ``tool`` at ``points``, in the tool's first look: kDrawingColor, solid, width
/// 1 for the three Fibonacci tools and 2 for the others.
Drawing new_drawing(DrawingTool tool, const QList<DrawingPoint>& points);
/// The id a new drawing among ``drawings`` takes: one past the largest.
int next_drawing_id(const QList<Drawing>& drawings);

/// Where a symbol keeps its drawings: krx_quant/chart/<symbol>/ + kPrefsDrawings (drawings_value).
inline constexpr auto kPrefsDrawings = "drawings";
/// Whether the magnet is on, for every symbol (a bool).
inline constexpr auto kMagnetKey = "krx_quant/chart_magnet";

/// The drawings as kept: a JSON array, in the order drawn, of {"tool", "points": [{"time" (ISO 8601
/// in KST), "price"}], "color", "width", "style"} and "text" for a text. The ids are not kept.
QString drawings_value(const QList<Drawing>& drawings);
/// The drawings kept (invalid or not such an array: none), numbered 1, 2… in order. A drawing that
/// is not an object, of an unknown tool, without as many points as its tool takes, with a time
/// that is not ISO 8601 or a price that is not a finite number, or a text without text, is dropped;
/// a colour that is not #rrggbb, a width out of 1–4 or an unknown style is the tool's first look;
/// a text is cut to kMaxDrawingText characters.
QList<Drawing> parse_drawings(const QVariant& kept);

/// A place on the chart as the page has it: a bar position (the chart's logical index: the bar
/// ``i`` read is drawn at ``i``, from ``i`` − 0.5 to ``i`` + 0.5) and a price.
struct ChartSpot {
    double logical = 0;
    double price = 0;
    bool operator==(const ChartSpot& other) const = default;
};

/// Where the moment ``time`` (chart_time) lies on ``bars``, the view ``view``'s bars as drawn
/// (oldest first, each starting its slot: a minute view's N minutes on the clock, a day, a week
/// from Monday, a month or a year), as a bar position. Within a bar, as far into it as the moment
/// is into the bar's trading time (08:00–20:00 KST on weekdays; a 1-minute bar's clock, a day
/// candle's 08:00 at its start and 14:00 at its middle). An empty bar (krx-quant #236, ChartSlots'
/// fill_slots: a minute without trades within a day) is a bar like the others: a moment of a halt
/// lies on its slot. Between two bars (the night, or minutes without trades on bars not filled),
/// at the edge between them. Before the first bar or after the last, as many of the
/// view's slots away as the trading time between holds (holidays are not known): an estimate
/// until those bars are read. None without bars.
std::optional<double> logical_at(const QList<ChartBar>& bars, qint64 time, int view);
/// The moment at the bar position ``logical`` on ``bars`` (as logical_at), to the second: the
/// inverse of logical_at but between bars, where the edge is the next bar's trading start. None
/// without bars or for a position that is not a finite number.
std::optional<qint64> time_at(const QList<ChartBar>& bars, double logical, int view);

/// The bar being made (``live``, as the chart draws it) on ``bars``, the bars the page has, oldest
/// first: one with the last bar's start replaces it; a later one is appended; an earlier one is left
/// out. Whether it was appended: then the places past the old last bar move (the slots up to the new
/// bar, a night or a weekend, are that one bar now; a gap without trades within the day is filled
/// with empty bars first in a minute view, ChartSlots' append_live_slots), and the drawings are
/// placed again.
bool append_live(QList<ChartBar>& bars, const ChartBar& live);

/// ``spot`` with the magnet on: over a bar read (its position within it), that bar's middle and
/// whichever of its open, high, low and close is nearest the price (the first of them on a tie).
/// Past the bars (the free space after the last), and over an empty bar (krx-quant #236), which has
/// no prices, as it is.
ChartSpot magnet_spot(const QList<ChartBar>& bars, ChartSpot spot);

} // namespace fincept::krx_quant
