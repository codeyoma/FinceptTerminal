#include "services/krx_quant/EngineData.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fincept::krx_quant {

namespace {

std::optional<double> number(const QJsonValue& v) {
    if (!v.isDouble())
        return std::nullopt;
    return v.toDouble();
}

QDateTime time_of(const QJsonValue& v) {
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}

/// The object of an answer, or the engine's error ({"error": "..."}).
Result<QJsonObject> object_of(const QJsonDocument& doc) {
    if (!doc.isObject())
        return Result<QJsonObject>::err(QStringLiteral("엔진 응답이 JSON 객체가 아닙니다").toStdString());
    const QJsonObject obj = doc.object();
    if (obj.contains("error"))
        return Result<QJsonObject>::err(obj.value("error").toString().toStdString());
    return Result<QJsonObject>::ok(obj);
}

/// 0: consensus, 1: algorithm, 2: baseline.
int group_of(const QString& algorithm) {
    if (is_consensus(algorithm))
        return 0;
    if (is_baseline(algorithm))
        return 2;
    return 1;
}

/// A class as the engine writes it ("up", "flat", "down"); None for null (not scored).
Direction class_of(const QJsonValue& v) {
    static const QMap<QString, Direction> classes = {
        {"up", Direction::Up}, {"flat", Direction::Flat}, {"down", Direction::Down}};
    return classes.value(v.toString(), Direction::None);
}

/// A touch target's first reach in a touch label, if it was reached.
std::optional<Reach> reach_of(const QJsonObject& touch, const QString& side) {
    const QDateTime at = time_of(touch.value(side + "_at"));
    const auto price = number(touch.value(side + "_price"));
    if (!at.isValid() || !price)
        return std::nullopt;
    return Reach{at, *price};
}

std::optional<Touch> touch_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject t = v.toObject();
    const auto k = number(t.value("k"));
    return Touch{class_of(t.value("class")),
                 t.value("unscorable").toString(),
                 number(t.value("anchor")),
                 number(t.value("upper")),
                 number(t.value("lower")),
                 reach_of(t, "upper"),
                 reach_of(t, "lower"),
                 number(t.value("tick")),
                 k ? std::optional<int>(static_cast<int>(std::lround(*k))) : std::nullopt};
}

std::optional<Outcome> outcome_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return Outcome{class_of(o.value("class_a")),     class_of(o.value("class_b")), o.value("special").toBool(),
                   o.value("unscorable").toString(), number(o.value("mid_close")), number(o.value("mid_display")),
                   number(o.value("mid_target")),    touch_of(o.value("touch"))};
}

/// The feeds of a KIS status (``/v1/status``'s ``kis``, a health event's).
QList<Feed> feeds_of(const QJsonObject& kis) {
    QList<Feed> out;
    for (const auto& v : kis.value("feeds").toArray()) {
        const QJsonObject f = v.toObject();
        out.push_back({f.value("feed").toString(), f.value("key").toString(), number(f.value("seconds_since")),
                       f.value("gap").toString()});
    }
    return out;
}

std::optional<Interval> interval_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return Interval{o.value("value").toDouble(), number(o.value("low")), number(o.value("high")), o.value("n").toInt()};
}

QStringList strings_of(const QJsonValue& v) {
    QStringList out;
    for (const auto& s : v.toArray())
        out << s.toString();
    return out;
}

QList<CalibrationBin> bins_of(const QJsonValue& value) {
    QList<CalibrationBin> bins;
    for (const auto& v : value.toArray()) {
        const QJsonObject b = v.toObject();
        bins.push_back({b.value("low").toDouble(), b.value("high").toDouble(), b.value("count").toInt(),
                        number(b.value("mean_p")), interval_of(b.value("observed"))});
    }
    return bins;
}

std::optional<Excluded> excluded_of(const QJsonValue& value) {
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject o = value.toObject();
    Excluded excluded{o.value("special").toInt(), o.value("unscorable").toInt(), {}};
    const QJsonObject statuses = o.value("status").toObject();
    for (auto it = statuses.begin(); it != statuses.end(); ++it)
        excluded.statuses.insert(it.key(), it.value().toInt());
    return excluded;
}

std::optional<RangeMean> range_mean_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return RangeMean{interval_of(o.value("mean")), interval_of(o.value("by_day"))};
}

/// A mean for each quantile; none unless an object.
std::optional<QMap<QString, RangeMean>> quantile_means_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    QMap<QString, RangeMean> out;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
        if (const auto mean = range_mean_of(it.value()))
            out.insert(it.key(), *mean);
    return out;
}

/// A range entry's extra (krx-quant #180): its quantile stats; none for another kind's (a
/// direction entry has none, another kind's extra has other fields).
std::optional<RangeStats> range_stats_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    if (!o.contains("pinball") || !o.contains("interval_coverage"))
        return std::nullopt;
    RangeStats stats;
    stats.n = o.value("n").toInt();
    stats.pinball = quantile_means_of(o.value("pinball")).value_or(QMap<QString, RangeMean>{});
    stats.interval_coverage = interval_of(o.value("interval_coverage"));
    stats.q50_abs_error = interval_of(o.value("q50_abs_error"));
    stats.vs_climate = quantile_means_of(o.value("vs_climate"));
    return stats;
}

BarrierPnl barrier_pnl_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    return BarrierPnl{o.value("trades").toInt(), o.value("total_ticks").toDouble(), interval_of(o.value("per_trade"))};
}

/// A barrier entry's extra (krx-quant #183): its barrier P&L; none for another kind's (an
/// open_to_close entry's has the same sides in basis points, krx-quant #264).
std::optional<BarrierStats> barrier_stats_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    if (!o.contains("pnl") || !o.contains("long") || !o.contains("short") ||
        !o.value("pnl").toObject().contains("total_ticks"))
        return std::nullopt;
    BarrierStats stats{o.value("n").toInt(), barrier_pnl_of(o.value("pnl")), barrier_pnl_of(o.value("long")),
                       barrier_pnl_of(o.value("short"))};
    // krx-quant #267; a null one is absent, not a measured zero.
    if (o.value("pnl_inverse").isObject())
        stats.pnl_inverse = barrier_pnl_of(o.value("pnl_inverse"));
    if (o.value("short_inverse").isObject())
        stats.shorts_inverse = barrier_pnl_of(o.value("short_inverse"));
    return stats;
}

DailyPnl daily_pnl_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    return DailyPnl{o.value("trades").toInt(), o.value("total_bp").toDouble(), interval_of(o.value("per_trade"))};
}

/// An open_to_close entry's extra (krx-quant #264): its open-to-close P&L, basis points; none for
/// another kind's.
std::optional<OpenToCloseStats> open_to_close_stats_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    if (!o.contains("pnl") || !o.contains("long") || !o.contains("short") ||
        !o.value("pnl").toObject().contains("total_bp"))
        return std::nullopt;
    OpenToCloseStats stats;
    stats.n = o.value("n").toInt();
    stats.pnl = daily_pnl_of(o.value("pnl"));
    stats.longs = daily_pnl_of(o.value("long"));
    stats.shorts = daily_pnl_of(o.value("short"));
    if (o.value("pnl_inverse").isObject())
        stats.pnl_inverse = daily_pnl_of(o.value("pnl_inverse"));
    if (o.value("short_inverse").isObject())
        stats.shorts_inverse = daily_pnl_of(o.value("short_inverse"));
    return stats;
}

Entry entry_of(const QJsonObject& e) {
    Entry entry;
    if (e.value("rank").isDouble())
        entry.rank = e.value("rank").toInt();
    entry.algorithm = e.value("algorithm").toString();
    entry.status = e.value("status").toString();
    entry.model_versions = strings_of(e.value("model_versions"));
    entry.n = e.value("n").toInt();
    entry.accuracy = interval_of(e.value("accuracy"));
    entry.precision = interval_of(e.value("precision"));
    entry.coverage = interval_of(e.value("coverage"));
    entry.brier = interval_of(e.value("brier"));
    const QJsonObject pnl = e.value("pnl").toObject();
    entry.trades = pnl.value("trades").toInt();
    entry.total_ticks = pnl.value("total_ticks").toDouble();
    entry.per_trade = interval_of(pnl.value("per_trade"));
    if (e.value("pnl_inverse").isObject()) // krx-quant #267
        entry.inverse = barrier_pnl_of(e.value("pnl_inverse"));
    entry.excluded = excluded_of(e.value("excluded"));
    entry.range = range_stats_of(e.value("extra"));
    entry.barrier = barrier_stats_of(e.value("extra"));
    entry.open_to_close = open_to_close_stats_of(e.value("extra")); // open_to_close_5d's too (krx-quant #310)
    // Its windows' overlap (krx-quant #310): 1 for every kind but one of several sessions.
    entry.overlap_sessions = std::max(1, e.value("overlap_sessions").toInt(1));
    if (e.value("independent_days").isDouble())
        entry.independent_days = e.value("independent_days").toInt();
    if (e.value("lift").isObject()) {
        const QJsonObject lift = e.value("lift").toObject();
        entry.lift_low = number(lift.value("low"));
        entry.day_low = number(lift.value("day_low")); // codeyoma/krx-quant#302
        const QJsonObject vs = lift.value("vs").toObject();
        entry.significant = !vs.isEmpty() && std::all_of(vs.begin(), vs.end(), [](const auto& v) {
            return v.toObject().value("significant").toBool();
        });
        for (auto it = vs.begin(); it != vs.end(); ++it) {
            const QJsonObject l = it.value().toObject();
            const QJsonValue significant = l.value("significant");
            const auto gain = l.value("value").isDouble() ? interval_of(l) : std::nullopt;
            entry.lifts.push_back({it.key(), gain, number(l.value("p")), number(l.value("q")),
                                   significant.isBool() ? std::optional<bool>(significant.toBool()) : std::nullopt,
                                   interval_of(l.value("by_day"))});
        }
    }
    return entry;
}

QList<Entry> entries_of(const QJsonValue& v) {
    QList<Entry> out;
    for (const auto& e : v.toArray())
        out.push_back(entry_of(e.toObject()));
    return out;
}

QMap<QString, Entry> entries_by_key(const QJsonValue& v) {
    QMap<QString, Entry> out;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
        out.insert(it.key(), entry_of(it.value().toObject()));
    return out;
}

/// A direction detail's skill (krx-quant #262); none when it is null or missing.
std::optional<Skill> skill_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    const QJsonObject independence = o.value("independence").toObject();
    Skill s;
    s.n = o.value("n").toInt();
    s.accuracy = o.value("accuracy").toDouble();
    s.expected_accuracy = o.value("expected_accuracy").toDouble();
    s.kappa = number(o.value("kappa"));
    s.gerrity = number(o.value("gerrity"));
    s.balanced_accuracy = o.value("balanced_accuracy").toDouble();
    s.chi2 = number(independence.value("chi2"));
    s.df = independence.value("df").toInt();
    s.p = number(independence.value("p"));
    s.kappa_by_day = interval_of(o.value("by_day"));
    if (s.kappa_by_day)
        s.kappa_by_day_p = number(o.value("by_day").toObject().value("p"));
    return s;
}

/// A prediction as the API shows it. ``symbol`` and ``bar_close`` are the answer's, for
/// items that do not carry their own.
Prediction prediction_of(const QJsonObject& p, const QString& symbol, const QDateTime& bar_close) {
    Prediction made{p.value("algorithm").toString(),
                    p.value("model_version").toString(),
                    p.value("status").toString(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    p.contains("symbol") ? p.value("symbol").toString() : symbol,
                    p.contains("bar_close") ? time_of(p.value("bar_close")) : bar_close,
                    time_of(p.value("target_at")),
                    outcome_of(p.value("outcome")),
                    p.value("blocked").toString(),
                    std::nullopt,
                    std::nullopt};
    if (const QJsonValue v = p.value("pooled"); v.isArray()) {
        QStringList names;
        for (const auto& name : v.toArray())
            names << name.toString();
        made.pooled = names;
    }
    if (const QJsonValue v = p.value("votes"); v.isObject()) {
        const QJsonObject o = v.toObject();
        made.votes = Votes{o.value("up").toInt(), o.value("flat").toInt(), o.value("down").toInt()};
    }
    if (made.status == "ok") {
        made.p_up = number(p.value("p_up"));
        made.p_flat = number(p.value("p_flat"));
        made.p_down = number(p.value("p_down"));
        made.expected_ticks = number(p.value("expected_ticks"));
    }
    return made;
}

/// A special situation's window: the status's, or a symbol's over days (krx-quant #236).
SpecialWindow special_of(const QJsonObject& w) {
    return {w.value("kind").toString(), w.value("symbol").toString(), time_of(w.value("start")),
            time_of(w.value("end")), w.value("detail").toString()};
}

} // namespace

Result<EngineStatus> parse_status(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<EngineStatus>::err(obj.error());
    const QJsonObject& o = obj.value();
    EngineStatus s;
    s.mode = o.value("mode").toString();
    s.phase = o.value("phase").toString();
    s.now = time_of(o.value("now"));
    if (const QJsonObject session = o.value("session").toObject(); !session.isEmpty()) {
        s.session_open = time_of(session.value("open"));
        s.session_close = time_of(session.value("close"));
    }
    s.commit = o.value("commit").toString();
    s.started_at = time_of(o.value("started_at"));
    s.last_bar_close = time_of(o.value("last_bar_close"));
    s.pending_outcomes = o.value("pending_outcomes").toInt();
    for (const auto& v : o.value("special").toArray()) {
        const QJsonObject w = v.toObject();
        s.special.push_back(special_of(w));
    }
    if (o.value("training").isObject()) {
        const QJsonObject t = o.value("training").toObject();
        Training training{t.value("day").toString(), t.value("applied").toBool(), t.value("reason").toString(), {}};
        const QJsonObject promoted = t.value("promoted").toObject();
        for (auto it = promoted.begin(); it != promoted.end(); ++it)
            training.promoted.insert(it.key(), it.value().toString());
        s.training = training;
    }
    if (o.value("kis").isObject()) {
        const QJsonObject kis = o.value("kis").toObject();
        s.kis_connected = kis.value("connected").toBool();
        s.feeds = feeds_of(kis);
    }
    if (o.value("usd_futures").isObject()) {
        const QJsonObject u = o.value("usd_futures").toObject();
        s.usd_futures = UsdFutures{u.value("price").toDouble(), time_of(u.value("at")), number(u.value("change_pct"))};
    }
    if (o.value("cpu").isObject()) { // codeyoma/krx-quant#302
        const QJsonObject cpu = o.value("cpu").toObject();
        s.cpu_usage = number(cpu.value("usage"));
        s.cpu_skipped_today = cpu.value("skipped_today").toInt();
    }
    return Result<EngineStatus>::ok(s);
}

Result<QList<SpecialWindow>> parse_special(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<SpecialWindow>>::err(obj.error());
    QList<SpecialWindow> windows;
    for (const auto& v : obj.value().value("special").toArray())
        windows.push_back(special_of(v.toObject()));
    // In start order, as the engine sends them; kept so if one does not.
    std::stable_sort(windows.begin(), windows.end(),
                     [](const SpecialWindow& a, const SpecialWindow& b) { return a.start < b.start; });
    return Result<QList<SpecialWindow>>::ok(windows);
}

Result<QList<TraderMark>> parse_trader_marks(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<TraderMark>>::err(obj.error());
    QList<TraderMark> marks;
    for (const auto& v : obj.value().value("marks").toArray()) {
        const QJsonObject m = v.toObject();
        const QString side = m.value("side").toString();
        TraderMark mark;
        mark.trade_id = m.value("trade_id").toInteger(-1);
        mark.buy = side == QLatin1String("buy");
        mark.at = time_of(m.value("at"));
        mark.price = m.value("price").toDouble();
        mark.quantity = m.value("quantity").toInt();
        mark.mode = m.value("mode").toString();
        mark.kind = m.value("kind").toString();
        mark.predictor = m.value("predictor").toString();
        mark.exit_reason = m.value("exit_reason").toString();
        mark.net = number(m.value("net"));
        if (mark.trade_id < 0 || !mark.at.isValid() || (side != QLatin1String("buy") && side != QLatin1String("sell")))
            continue;
        marks.push_back(mark);
    }
    return Result<QList<TraderMark>>::ok(marks);
}

QString supply_field_key(SupplyField field) {
    switch (field) {
        case SupplyField::ShortVolume:
            return QStringLiteral("short_volume");
        case SupplyField::IndividualNet:
            return QStringLiteral("individual_net");
        case SupplyField::InstitutionNet:
            return QStringLiteral("institution_net");
        case SupplyField::ForeignNet:
            return QStringLiteral("foreign_net");
        case SupplyField::ForeignHolding:
            return QStringLiteral("foreign_holding");
        case SupplyField::ForeignRatio:
            return QStringLiteral("foreign_ratio");
        case SupplyField::LendingBalance:
            return QStringLiteral("lending_balance");
        case SupplyField::CreditBalance:
            return QStringLiteral("credit_balance");
    }
    return {};
}

Result<QList<SupplyDay>> parse_supply(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<SupplyDay>>::err(obj.error());
    QList<SupplyDay> days;
    for (const auto& v : obj.value().value("days").toArray()) {
        const QJsonObject d = v.toObject();
        SupplyDay day;
        day.date = QDate::fromString(d.value("date").toString(), Qt::ISODate);
        if (!day.date.isValid())
            continue;
        for (std::size_t i = 0; i < kSupplyFields; ++i)
            day.values[i] = number(d.value(supply_field_key(static_cast<SupplyField>(i))));
        days.push_back(day);
    }
    // Oldest first, as the engine sends them; kept so if it does not.
    std::stable_sort(days.begin(), days.end(), [](const SupplyDay& a, const SupplyDay& b) { return a.date < b.date; });
    return Result<QList<SupplyDay>>::ok(days);
}

Result<QList<SymbolQuote>> parse_symbols(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<SymbolQuote>>::err(obj.error());
    QList<SymbolQuote> out;
    for (const auto& v : obj.value().value("symbols").toArray()) {
        const QJsonObject s = v.toObject();
        SymbolQuote quote{s.value("symbol").toString(), number(s.value("mid"))};
        quote.name = s.value("name").toString().trimmed(); // null until the engine has read it
        quote.logo_url = s.value("logo_url").toString();
        quote.tick = number(s.value("tick"));
        if (s.value("flat_ticks").isDouble())
            quote.flat_ticks = s.value("flat_ticks").toInt();
        out.push_back(quote);
    }
    return Result<QList<SymbolQuote>>::ok(out);
}

Result<Latest> parse_latest(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<Latest>::err(obj.error());
    const QJsonObject& o = obj.value();
    Latest latest{o.value("symbol").toString(), time_of(o.value("bar_close")), {}};
    for (const auto& v : o.value("predictions").toArray())
        latest.predictions.push_back(prediction_of(v.toObject(), latest.symbol, latest.bar_close));
    latest.predictions = in_screen_order(latest.predictions);
    return Result<Latest>::ok(latest);
}

Result<QList<Prediction>> parse_predictions(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<Prediction>>::err(obj.error());
    const QString symbol = obj.value().value("symbol").toString();
    QList<Prediction> out;
    for (const auto& v : obj.value().value("predictions").toArray())
        out.push_back(prediction_of(v.toObject(), symbol, {}));
    return Result<QList<Prediction>>::ok(out);
}

Result<QList<Bar>> parse_bars(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<Bar>>::err(obj.error());
    QList<Bar> out;
    for (const auto& v : obj.value().value("bars").toArray()) {
        const QJsonObject b = v.toObject();
        const auto trades = number(b.value("trades")); // null for a past day's (krx-quant #255)
        out.push_back({time_of(b.value("start")), b.value("open").toDouble(), b.value("high").toDouble(),
                       b.value("low").toDouble(), b.value("close").toDouble(), b.value("volume").toDouble(),
                       trades ? std::optional<int>(static_cast<int>(std::lround(*trades))) : std::nullopt});
    }
    return Result<QList<Bar>>::ok(out);
}

Result<Candles> parse_candles(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<Candles>::err(obj.error());
    const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    Candles out;
    out.venue = obj.value().value("venue").toString();
    for (const auto& v : obj.value().value("candles").toArray()) {
        const QJsonObject c = v.toObject();
        const QDate day = QDate::fromString(c.value("date").toString(), Qt::ISODate);
        if (!day.isValid())
            continue;
        out.candles.push_back({QDateTime(day, QTime(0, 0), kst), c.value("open").toDouble(), c.value("high").toDouble(),
                               c.value("low").toDouble(), c.value("close").toDouble(), c.value("volume").toDouble()});
    }
    return Result<Candles>::ok(out);
}

Result<Leaderboard> parse_leaderboard(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<Leaderboard>::err(obj.error());
    const QJsonObject& o = obj.value();
    Leaderboard board;
    if (const QString kind = o.value("kind").toString(); !kind.isEmpty())
        board.kind = kind;
    board.symbol = o.value("symbol").toString();
    board.period = o.value("period").toString();
    board.basis = o.value("basis").toString();
    board.special = o.value("special").toString();
    board.days = strings_of(o.value("days"));
    board.min_sample = o.value("min_sample").toInt();
    board.ranked = entries_of(o.value("ranked"));
    board.unranked = entries_of(o.value("unranked"));
    board.baselines = entries_of(o.value("baselines"));
    return Result<Leaderboard>::ok(board);
}

Result<AlgorithmStats> parse_stats(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<AlgorithmStats>::err(obj.error());
    const QJsonObject& o = obj.value();
    AlgorithmStats stats;
    if (const QString kind = o.value("kind").toString(); !kind.isEmpty())
        stats.kind = kind;
    stats.algorithm = o.value("algorithm").toString();
    stats.symbol = o.value("symbol").toString();
    stats.period = o.value("period").toString();
    stats.basis = o.value("basis").toString();
    stats.special = o.value("special").toString();
    stats.days = strings_of(o.value("days"));
    stats.min_sample = o.value("min_sample").toInt();
    const QJsonObject overall = o.value("overall").toObject();
    stats.overall = entry_of(overall);
    stats.calibration = bins_of(overall.value("calibration"));
    const QJsonObject by_class = overall.value("calibration_by_class").toObject();
    for (auto it = by_class.begin(); it != by_class.end(); ++it)
        stats.calibration_by_class.insert(it.key(), bins_of(it.value()));
    stats.by_symbol = entries_by_key(o.value("by_symbol"));
    stats.by_slot = entries_by_key(o.value("by_slot"));
    stats.by_model_version = entries_by_key(o.value("by_model_version"));
    stats.skill = skill_of(overall.value("skill"));
    stats.by_volatility = entries_by_key(o.value("by_volatility"));
    const QJsonObject cuts = o.value("volatility_cuts").toObject();
    for (auto it = cuts.begin(); it != cuts.end(); ++it) {
        QList<double> bounds;
        for (const QJsonValue& c : it.value().toArray())
            bounds << c.toDouble();
        stats.volatility_cuts.insert(it.key(), bounds);
    }
    return Result<AlgorithmStats>::ok(stats);
}

Result<QMap<QString, QString>> parse_statuses(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QMap<QString, QString>>::err(obj.error());
    const QJsonValue algorithms = obj.value().value("algorithms");
    QMap<QString, QString> out;
    for (const auto& v : algorithms.toArray()) {
        const QJsonObject a = v.toObject();
        if (const QString status = a.value("status").toObject().value("status").toString(); !status.isEmpty())
            out.insert(a.value("name").toString(), status);
    }
    return Result<QMap<QString, QString>>::ok(out);
}

namespace {

/// A model parameter's value as text: a number with up to six significant digits, a text
/// as it is, anything else as compact JSON.
QString param_text(const QJsonValue& v) {
    if (v.isDouble())
        return QString::number(v.toDouble(), 'g', 6);
    if (v.isString())
        return v.toString();
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isNull() || v.isUndefined())
        return QStringLiteral("—");
    const QJsonDocument doc = v.isArray() ? QJsonDocument(v.toArray()) : QJsonDocument(v.toObject());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

} // namespace

namespace {

std::optional<AlgorithmCard> card_of(const QJsonValue& v) {
    const QJsonObject card = v.toObject();
    if (card.isEmpty())
        return std::nullopt;
    return AlgorithmCard{card.value("scientist").toString(), card.value("character").toString(),
                         card.value("logic").toString(),     strings_of(card.value("unique_features")),
                         card.value("training").toString(),  card.value("forecasting").toString(),
                         card.value("failure").toString()};
}

/// The meanings by name, or none when the engine gave no such field (an older engine).
std::optional<QMap<QString, Meaning>> meanings_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    QMap<QString, Meaning> out;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonObject m = it.value().toObject();
        out.insert(it.key(),
                   Meaning{m.value("meaning").toString(), m.value("unit").toString(), m.value("known").toString()});
    }
    return out;
}

/// A whole number, or none (absent, null, or not whole: not truncated).
std::optional<int> integer_of(const QJsonValue& v) {
    if (!v.isDouble())
        return std::nullopt;
    const double d = v.toDouble();
    if (std::floor(d) != d || d < std::numeric_limits<int>::min() || d > std::numeric_limits<int>::max())
        return std::nullopt;
    return static_cast<int>(d);
}

std::optional<TrainingInfo> training_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    TrainingInfo t{o.value("first_day").toString(),
                   o.value("last_day").toString(),
                   o.value("samples").toInt(),
                   o.value("loss").toDouble(),
                   std::nullopt,
                   std::nullopt};
    if (const QJsonValue days = o.value("days"); days.isObject()) {
        t.recorded_days = integer_of(days.toObject().value("recorded"));
        t.bar_days = integer_of(days.toObject().value("bars"));
    }
    return t;
}

std::optional<ValidatedModel> validated_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    const QJsonValue trained = o.value("trained_on_validation");
    return ValidatedModel{o.value("version").toString(), number(o.value("loss")),
                          trained.isBool() ? std::optional<bool>(trained.toBool()) : std::nullopt};
}

std::optional<NightValidation> validation_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return NightValidation{o.value("day").toString(),         strings_of(o.value("validation_days")),
                           integer_of(o.value("samples")),    validated_of(o.value("challenger")),
                           validated_of(o.value("champion")), o.value("promoted").toBool(),
                           o.value("reason").toString()};
}

std::optional<Stage> stage_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    Stage stage;
    stage.status = o.value("status").toString();
    stage.began = o.value("began").toString();
    stage.counted_from = o.value("counted_from").toString();
    stage.waiting = o.value("counted_from").isNull(); // given as null, not left out (an older engine)
    stage.days = o.value("days").toInt();
    for (const auto& d : o.value("review_days").toArray())
        stage.review_days << d.toInt();
    stage.next_review = integer_of(o.value("next_review"));
    stage.next_review_on = o.value("next_review_on").toString();
    stage.lift_days = o.value("lift_days").toInt();
    stage.level = number(o.value("level"));
    if (const QJsonValue w = o.value("weakest"); w.isObject()) {
        const QJsonObject weakest = w.toObject();
        stage.weakest =
            WeakestLift{weakest.value("baseline").toString(), weakest.value("n").toInt(),
                        number(weakest.value("value")), number(weakest.value("low")), number(weakest.value("high"))};
    }
    stage.enough = o.value("enough").toBool();
    stage.simulated = o.value("simulated").toInt(); // 0 when left out (an older engine)
    return stage;
}

std::optional<Simulation> simulation_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    Simulation simulation;
    simulation.cadence = o.value("cadence").toString();
    simulation.days = o.value("days").toInt();
    simulation.first_day = o.value("first_day").toString();
    simulation.last_day = o.value("last_day").toString();
    simulation.why = o.value("why").toString();
    if (const QJsonValue r = o.value("review"); r.isObject()) {
        const QJsonObject review = r.toObject();
        simulation.review_day = integer_of(review.value("review_day"));
        simulation.status = review.value("status").toString();
    }
    return simulation;
}

} // namespace

Result<QMap<QString, AlgorithmInfo>> parse_algorithms(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QMap<QString, AlgorithmInfo>>::err(obj.error());
    QMap<QString, AlgorithmInfo> out;
    for (const auto& v : obj.value().value("algorithms").toArray()) {
        const QJsonObject a = v.toObject();
        AlgorithmInfo info;
        info.name = a.value("name").toString();
        if (info.name.isEmpty())
            continue;
        if (const QString kind = a.value("kind").toString(); !kind.isEmpty())
            info.prediction_kind = kind;
        info.card = card_of(a.value("card"));
        info.features = strings_of(a.value("features"));
        info.feature_meanings = meanings_of(a.value("feature_meanings"));
        info.param_meanings = meanings_of(a.value("param_meanings"));
        if (a.value("training_days").isDouble())
            info.training_days = a.value("training_days").toInt();
        const QJsonObject model = a.value("model").toObject();
        info.model_version = model.value("version").toString();
        const QJsonObject params = model.value("params").toObject();
        QStringList names = params.keys();
        names.sort(); // by name, whatever order the object keeps
        for (const auto& name : names)
            info.params << QPair<QString, QString>(name, param_text(params.value(name)));
        info.trained = model.value("trained").toString();
        info.training = training_of(model.value("training"));
        info.validation = validation_of(a.value("validation"));
        info.stage = stage_of(a.value("stage"));
        info.simulation = simulation_of(a.value("simulation"));
        info.trained_until = model.value("trained_until").toString();
        const QJsonObject status = a.value("status").toObject();
        info.status = status.value("status").toString();
        info.since = QDateTime::fromString(status.value("since").toString(), Qt::ISODate);
        for (const auto& h : status.value("history").toArray()) {
            const QJsonObject record = h.toObject();
            info.history << StatusRecord{QDateTime::fromString(record.value("at").toString(), Qt::ISODate),
                                         record.value("from").toString(), record.value("to").toString(),
                                         record.value("reason").toString(), record.value("simulated").toBool()};
        }
        out.insert(info.name, info);
    }
    // The baselines and the consensus: what they are, nothing more (krx-quant #164).
    for (const auto& [key, kind] : {std::pair{"baselines", "baseline"}, std::pair{"consensus", "consensus"}}) {
        for (const auto& v : obj.value().value(QLatin1String(key)).toArray()) {
            const QJsonObject p = v.toObject();
            AlgorithmInfo info;
            info.name = p.value("name").toString();
            if (info.name.isEmpty() || out.contains(info.name))
                continue;
            info.kind = QLatin1String(kind);
            info.card = card_of(p.value("card"));
            out.insert(info.name, info);
        }
    }
    return Result<QMap<QString, AlgorithmInfo>>::ok(out);
}

namespace {

/// GET /v1/macro's answer, also the briefing's ``overnight.macro`` (krx-quant #304).
Macro macro_of(const QJsonObject& o) {
    Macro macro;
    macro.day = o.value("day").toString();
    macro.decided_at = time_of(o.value("decided_at"));
    macro.regime = o.value("regime").toString();
    macro.reasons = strings_of(o.value("reasons"));
    for (const auto& v : o.value("readings").toArray()) {
        const QJsonObject r = v.toObject();
        macro.readings.push_back({r.value("indicator").toString(), r.value("label").toString(),
                                  r.value("as_of").toString(), number(r.value("value")),
                                  number(r.value("change_pct"))});
    }
    const QJsonObject failed = o.value("failed").toObject();
    for (auto it = failed.begin(); it != failed.end(); ++it)
        macro.failed.insert(it.key(), it.value().toString());
    macro.skipped = strings_of(o.value("skipped"));
    return macro;
}

} // namespace

Result<Macro> parse_macro(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<Macro>::err(obj.error());
    return Result<Macro>::ok(macro_of(obj.value()));
}

Result<QStringList> parse_members(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QStringList>::err(obj.error());
    return Result<QStringList>::ok(strings_of(obj.value().value("members")));
}

bool is_barrier_kind(const QString& name) {
    return name == QLatin1String("barrier") ||
           (name.startsWith(QLatin1String("barrier_")) && name.endsWith(QLatin1Char('m')));
}

Result<QList<KindInfo>> parse_kinds(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<KindInfo>>::err(obj.error());
    const QJsonValue kinds = obj.value().value("kinds");
    if (!kinds.isArray())
        return Result<QList<KindInfo>>::err(QStringLiteral("엔진이 예측 종류를 주지 않았습니다").toStdString());
    QList<KindInfo> out;
    for (const auto& v : kinds.toArray()) {
        const QJsonObject k = v.toObject();
        KindInfo kind;
        kind.name = k.value("name").toString();
        if (kind.name.isEmpty())
            continue;
        kind.title = k.value("title").toString();
        kind.classes = strings_of(k.value("classes"));
        kind.class_titles = strings_of(k.value("class_titles"));
        kind.action = strings_of(k.value("action"));
        kind.answer_version = k.value("answer_version").toInt();
        // A kind predicted once a day (krx-quant #264): its day, and no bar close (horizon null).
        if (const QJsonObject d = k.value("daily").toObject(); !d.isEmpty()) {
            DailySchedule day;
            day.predict_at = d.value("predict_at").toString();
            day.from = d.value("from").toString();
            day.to = d.value("to").toString();
            day.from_title = d.value("from_title").toString();
            day.to_title = d.value("to_title").toString();
            // Over how many sessions (krx-quant #310); the day from an engine before it.
            day.sessions = std::max(1, d.value("sessions").toInt(1));
            day.answer_from = d.value("answer_from").toString();
            for (const auto& n : d.value("probation_days").toArray())
                day.probation_days << n.toInt();
            for (const auto& n : d.value("bench_days").toArray())
                day.bench_days << n.toInt();
            day.recent_days = d.value("recent_days").toInt();
            day.awaiting = d.value("awaiting").toString(); // null: it runs (krx-quant #310)
            day.review_hold = d.value("review_hold").toString();
            kind.daily = day;
        }
        kind.horizon_minutes = k.value("horizon_minutes").isDouble() ? k.value("horizon_minutes").toInt()
                               : kind.daily                          ? 0
                                                                     : 4;
        kind.fields = meanings_of(k.value("fields")).value_or(QMap<QString, Meaning>{});
        for (const auto& b : k.value("baselines").toArray()) {
            const QJsonObject baseline = b.toObject();
            if (const QString name = baseline.value("name").toString(); !name.isEmpty())
                kind.baselines.push_back({name, card_of(baseline.value("card"))});
        }
        out.push_back(kind);
    }
    return Result<QList<KindInfo>>::ok(out);
}

namespace {

/// A range answer's path (krx-quant #180); none for another kind's (no ``r``) or a window not
/// followed (an empty path).
std::optional<RangePath> range_path_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    if (!o.contains("r") || !o.contains("max_mid"))
        return std::nullopt;
    RangePath path;
    path.r = number(o.value("r"));
    path.k = integer_of(o.value("k"));
    path.tick = number(o.value("tick"));
    path.anchor = number(o.value("anchor"));
    path.max_mid = number(o.value("max_mid"));
    path.max_mid_at = time_of(o.value("max_mid_at"));
    path.min_mid = number(o.value("min_mid"));
    path.min_mid_at = time_of(o.value("min_mid_at"));
    path.up = number(o.value("up"));
    path.down = number(o.value("down"));
    path.quotes = o.value("quotes").toInt();
    path.silent = o.value("silent").toBool();
    return path;
}

/// A barrier answer's path (krx-quant #183); none for another kind's (no ``w`` and ``upper``) or a
/// window not followed (an empty path).
std::optional<BarrierPath> barrier_path_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    if (!o.contains("w") || !o.contains("upper"))
        return std::nullopt;
    BarrierPath path;
    path.w = integer_of(o.value("w"));
    path.sigma = number(o.value("sigma"));
    path.k = integer_of(o.value("k"));
    path.tick = number(o.value("tick"));
    path.anchor = number(o.value("anchor"));
    path.upper = number(o.value("upper"));
    path.lower = number(o.value("lower"));
    path.cost = number(o.value("cost"));
    path.bid_start = number(o.value("bid_start"));
    path.ask_start = number(o.value("ask_start"));
    path.up_at = time_of(o.value("up_at"));
    path.up_bid = number(o.value("up_bid"));
    path.up_ask = number(o.value("up_ask"));
    path.down_at = time_of(o.value("down_at"));
    path.down_bid = number(o.value("down_bid"));
    path.down_ask = number(o.value("down_ask"));
    path.bid_end = number(o.value("bid_end"));
    path.ask_end = number(o.value("ask_end"));
    path.quotes = o.value("quotes").toInt();
    path.silent = o.value("silent").toBool();
    return path;
}

/// An open_to_close answer's day (krx-quant #264); none for another kind's (no ``change``) or a
/// day whose KRX day candle never came (an empty path).
std::optional<OpenToClosePath> open_to_close_path_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    if (!o.contains("change") || !o.contains("open") || !o.contains("close"))
        return std::nullopt;
    OpenToClosePath path;
    path.open = o.value("open").toDouble();
    path.close = o.value("close").toDouble();
    path.change = o.value("change").toDouble();
    path.band = number(o.value("band"));
    path.sigma = number(o.value("sigma"));
    path.scale = number(o.value("scale"));
    path.vi = o.value("vi").toInt();
    // A window of several sessions (krx-quant #310, open_to_close_5d): its last session, each
    // session's close (null: a session the symbol has none of), whether an ex-dividend date fell
    // after the first session (null: not known).
    path.end_day = QDate::fromString(o.value("end_day").toString(), Qt::ISODate);
    path.sessions = std::max(1, o.value("sessions").toInt(1));
    for (const auto& c : o.value("closes").toArray())
        path.closes << number(c);
    if (o.value("ex_dividend").isBool())
        path.ex_dividend = o.value("ex_dividend").toBool();
    return path;
}

std::optional<KindAnswer> kind_answer_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    KindAnswer answer;
    answer.answer_version = o.value("answer_version").toInt();
    answer.klass = o.value("class").toString();
    answer.unscorable = o.value("unscorable").toString();
    answer.special = o.value("special").toBool();
    answer.scored_at = time_of(o.value("scored_at"));
    answer.range = range_path_of(o.value("path"));
    answer.barrier = barrier_path_of(o.value("path"));
    answer.open_to_close = open_to_close_path_of(o.value("path"));
    return answer;
}

/// A prediction of a kind; ``symbol`` and ``bar_close`` are the answer's, for items without their own.
KindPrediction kind_prediction_of(const QJsonObject& p, const QString& symbol, const QDateTime& bar_close) {
    KindPrediction made;
    made.algorithm = p.value("algorithm").toString();
    made.model_version = p.value("model_version").toString();
    made.status = p.value("status").toString();
    made.symbol = p.contains("symbol") ? p.value("symbol").toString() : symbol;
    made.bar_close = p.contains("bar_close") ? time_of(p.value("bar_close")) : bar_close;
    made.target_at = time_of(p.value("target_at"));
    made.flat_ticks = integer_of(p.value("flat_ticks"));
    made.width_ticks = integer_of(p.value("width_ticks"));
    made.anchor = number(p.value("anchor"));
    made.tick = number(p.value("tick"));
    made.band = number(p.value("band")); // a daily kind's (krx-quant #264)
    if (made.status == "ok") {
        const QJsonObject output = p.value("output").toObject();
        for (auto it = output.begin(); it != output.end(); ++it)
            if (it.value().isDouble())
                made.output.insert(it.key(), it.value().toDouble());
    }
    made.answer = kind_answer_of(p.value("answer"));
    return made;
}

} // namespace

Result<KindLatest> parse_kind_latest(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<KindLatest>::err(obj.error());
    const QJsonObject& o = obj.value();
    KindLatest latest{o.value("symbol").toString(), o.value("kind").toString(), time_of(o.value("bar_close")), {}};
    for (const auto& v : o.value("predictions").toArray())
        latest.predictions.push_back(kind_prediction_of(v.toObject(), latest.symbol, latest.bar_close));
    return Result<KindLatest>::ok(latest);
}

Result<QList<KindPrediction>> parse_kind_predictions(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<KindPrediction>>::err(obj.error());
    const QString symbol = obj.value().value("symbol").toString();
    QList<KindPrediction> out;
    for (const auto& v : obj.value().value("predictions").toArray())
        out.push_back(kind_prediction_of(v.toObject(), symbol, {}));
    return Result<QList<KindPrediction>>::ok(out);
}

Result<Event> parse_event(const QString& message) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return Result<Event>::err(QStringLiteral("엔진 스트림 메시지를 읽을 수 없습니다").toStdString());
    const QJsonObject o = doc.object();
    static const QMap<QString, Event::Type> types = {
        {"prediction", Event::Type::Prediction},
        {"consensus", Event::Type::Consensus},
        {"outcome", Event::Type::Outcome},
        {"health", Event::Type::Health},
        {"lifecycle", Event::Type::Lifecycle},
        {"bar", Event::Type::Bar},
        {"kind_prediction", Event::Type::KindPrediction},
        {"kind_answer", Event::Type::KindAnswer},
    };
    Event event;
    event.type = types.value(o.value("type").toString(), Event::Type::Other);
    event.at = time_of(o.value("at"));
    event.kind = o.value("kind").toString();
    event.bar_close = time_of(o.value("bar_close"));
    event.prediction_kind = o.value("prediction_kind").toString();
    if (event.type == Event::Type::KindPrediction) {
        // Another kind's predictions (krx-quant #179): kept apart from the direction's.
        for (const auto& v : o.value("predictions").toArray())
            event.kind_predictions.push_back(kind_prediction_of(v.toObject(), {}, event.bar_close));
        return Result<Event>::ok(event);
    }
    if (event.type == Event::Type::KindAnswer) {
        for (const auto& v : o.value("answers").toArray()) {
            const QJsonObject a = v.toObject();
            if (const auto answer = kind_answer_of(a))
                event.kind_answers.push_back({a.value("symbol").toString(), time_of(a.value("bar_close")), *answer});
        }
        return Result<Event>::ok(event);
    }
    const QJsonArray items = o.value(event.type == Event::Type::Outcome ? "outcomes" : "predictions").toArray();
    for (const auto& v : items)
        event.predictions.push_back(prediction_of(v.toObject(), {}, event.bar_close));
    if (o.value("kis").isObject()) {
        const QJsonObject kis = o.value("kis").toObject();
        event.kis_connected = kis.value("connected").toBool();
        event.feeds = feeds_of(kis);
    }
    event.algorithm = o.value("algorithm").toString();
    const QJsonObject change = o.value("change").toObject();
    event.change = {change.value("from").toString(), change.value("to").toString()};
    event.symbol = o.value("symbol").toString();
    if (const QJsonObject b = o.value("bar").toObject(); !b.isEmpty())
        event.bar = {time_of(b.value("start")), b.value("open").toDouble(),  b.value("high").toDouble(),
                     b.value("low").toDouble(), b.value("close").toDouble(), b.value("volume").toDouble()};
    return Result<Event>::ok(event);
}

QString stream_url(const QString& base_url) {
    QUrl url(base_url.trimmed());
    const QString scheme = url.scheme().toLower();
    if (url.host().isEmpty() || (scheme != "http" && scheme != "https"))
        return {};
    url.setScheme(scheme == "https" ? "wss" : "ws");
    QString path = url.path();
    while (path.endsWith('/'))
        path.chop(1);
    url.setPath(path + "/v1/stream");
    return url.toString();
}

QList<Prediction> in_screen_order(QList<Prediction> predictions) {
    std::stable_sort(predictions.begin(), predictions.end(), [](const Prediction& a, const Prediction& b) {
        const int ga = group_of(a.algorithm), gb = group_of(b.algorithm);
        if (ga != gb)
            return ga < gb;
        // The final consensus before the fast one: it is the one to read once both are there.
        if (ga == 0)
            return a.algorithm > b.algorithm;
        return a.algorithm < b.algorithm;
    });
    return predictions;
}

Direction shown_direction(const Prediction& p) {
    if (!p.p_up || !p.p_flat || !p.p_down)
        return Direction::None;
    const double up = *p.p_up, flat = *p.p_flat, down = *p.p_down;
    // The screen shows whole percents: up and down shown alike lean neither way.
    if (std::lround(up * 100) == std::lround(down * 100) && std::max(up, down) >= flat)
        return Direction::None;
    if (up >= flat && up >= down)
        return Direction::Up;
    if (down > flat)
        return Direction::Down;
    return Direction::Flat;
}

Direction most_likely(const Prediction& p) {
    if (!p.p_up || !p.p_flat || !p.p_down)
        return Direction::None;
    const double best = std::max({*p.p_up, *p.p_flat, *p.p_down});
    if (*p.p_flat == best)
        return Direction::Flat;
    return *p.p_up == best ? Direction::Up : Direction::Down;
}

QString direction_label(Direction d) {
    switch (d) {
        case Direction::Up:
            return QStringLiteral("상승");
        case Direction::Flat:
            return QStringLiteral("보합");
        case Direction::Down:
            return QStringLiteral("하락");
        case Direction::None:
            break;
    }
    return {};
}

bool is_consensus(const QString& predictor) {
    return predictor.startsWith("consensus-");
}

bool is_baseline(const QString& predictor) {
    return predictor.startsWith("baseline-");
}

QString predictor_label(const QString& predictor) {
    static const QMap<QString, QString> labels = {
        {"consensus-final", QStringLiteral("합의 (최종)")},
        {"consensus-fast", QStringLiteral("합의 (빠른)")},
        {"baseline-flat", QStringLiteral("기준: 늘 보합")},
        {"baseline-persist", QStringLiteral("기준: 직전 4분 방향")},
        {"baseline-random", QStringLiteral("기준: 비율대로 무작위")},
        {"baseline-climate", QStringLiteral("기준: 시간대 기후")}, // krx-quant #169, named since #210
        // The range's (krx-quant #180, #182).
        {"range-climate", QStringLiteral("기준: 변동폭 시간대 기후")},
        {"range-persist", QStringLiteral("기준: 직전 4분 변동폭")},
        // The barrier's (krx-quant #183, #185).
        {"barrier-climate", QStringLiteral("기준: 장벽 시간대 기후")},
        {"barrier-persist", QStringLiteral("기준: 직전 4분 장벽")},
        // Open to close's (krx-quant #264), predicted once a day.
        {"otc-flat", QStringLiteral("기준: 시가→종가 늘 보합")},
        {"otc-climate", QStringLiteral("기준: 시가→종가 기후")},
        {"otc-persist", QStringLiteral("기준: 어제 방향 잇기")},
        // Five sessions' (krx-quant #310), predicted once a day.
        {"otc5-flat", QStringLiteral("기준: 5거래일 늘 보합")},
        {"otc5-climate", QStringLiteral("기준: 5거래일 기후")},
        {"otc5-persist", QStringLiteral("기준: 지난 5거래일 방향 잇기")},
    };
    return labels.value(predictor, predictor);
}

QStringList engine_build_parts(const EngineStatus& status) {
    if (status.commit.isEmpty())
        return {};
    const QString code = status.commit == "unknown" ? QStringLiteral("커밋 모름") : status.commit.left(7);
    QStringList parts{QStringLiteral("엔진 %1").arg(code)};
    if (status.started_at.isValid())
        parts << QStringLiteral("%1 시작").arg(kst_text(status.started_at, "MM-dd HH:mm"));
    return parts;
}

EngineDot engine_dot(bool answered, const QString& failure, const QStringList& read_errors, const QDateTime& refreshed,
                     const QDateTime& engine_now) {
    EngineDot dot;
    QStringList lines;
    if (!failure.isEmpty()) {
        dot.state = EngineDot::State::Failed;
        lines << QStringLiteral("엔진을 읽지 못함: %1").arg(failure);
    } else if (!answered) {
        lines << QStringLiteral("엔진을 읽는 중");
    } else {
        dot.state = read_errors.isEmpty() ? EngineDot::State::Ok : EngineDot::State::Failed;
    }
    if (!read_errors.isEmpty())
        lines << QStringLiteral("읽지 못한 것: %1").arg(read_errors.join("; "));
    lines << QStringLiteral("갱신 %1 · 엔진 시계 %2")
                 .arg(kst_text(refreshed, "HH:mm:ss"), kst_text(engine_now, "HH:mm:ss"));
    dot.tip = lines.join('\n');
    return dot;
}

QString phase_label(const QString& phase) {
    static const QMap<QString, QString> labels = {
        {"closed", QStringLiteral("휴장")},
        {"pre_open", QStringLiteral("개장 전")},
        {"opening_auction", QStringLiteral("시가 동시호가")},
        {"continuous", QStringLiteral("연속매매")},
        {"closing_auction", QStringLiteral("종가 동시호가")},
        {"after_close", QStringLiteral("장 마감 후")},
    };
    return labels.value(phase, phase);
}

QString status_label(const QString& status) {
    static const QMap<QString, QString> labels = {
        {"ok", QString()},
        {"timed_out", QStringLiteral("시간 초과")},
        {"error", QStringLiteral("오류")},
        {"vi", QStringLiteral("VI 중")},
        {"no_book", QStringLiteral("호가 없음")},
        {"no_members", QStringLiteral("합의 대상 없음")},
        {"none_finished", QStringLiteral("끝난 알고리즘 없음")},
        // "기준: 직전 4분 방향" when the window before could not be scored (krx-quant #191; found
        // with the bar window, krx-quant #255): as the kinds' baselines say it.
        {"no_previous", QStringLiteral("직전 창 없음")},
    };
    return labels.value(status, status);
}

QString regime_label(const QString& regime) {
    static const QMap<QString, QString> labels = {
        {"shock", QStringLiteral("충격")},
        {"risk_off", QStringLiteral("위험회피")},
        {"risk_on", QStringLiteral("위험선호")},
        {"neutral", QStringLiteral("중립")},
    };
    return labels.value(regime, regime);
}

QString standing_label(const QString& status) {
    static const QMap<QString, QString> labels = {
        {"probation", QStringLiteral("수습")}, {"active", QStringLiteral("정식")}, {"bench", QStringLiteral("벤치")},
        {"retired", QStringLiteral("은퇴")},   {"clone", QStringLiteral("복제")},
    };
    return labels.value(status, status);
}

QString training_reason_label(const QString& reason) {
    // The engine's reasons (swap.py), by how they start; the details stay as they are.
    if (reason == "applied")
        return QStringLiteral("적용됨");
    if (reason == "waiting for 08:00")
        return QStringLiteral("08:00 교체 대기");
    if (reason.startsWith("final at ") && reason.endsWith("yesterday's models run on"))
        return QStringLiteral("07:30 뒤에 확정되어 어제 모델로 돈다");
    if (reason.startsWith("not in the store: "))
        return QStringLiteral("모델 파일이 저장소에 없어 어제 모델로 돈다");
    if (reason.startsWith("applied, then replaced: "))
        return QStringLiteral("적용 뒤 바뀜: ") + reason.mid(QStringLiteral("applied, then replaced: ").size());
    return reason;
}

QString trader_mode_label(const QString& mode) {
    static const QMap<QString, QString> labels = {
        {"paper", QStringLiteral("모의")},
        {"mock", QStringLiteral("KIS 모의")},
        {"live", QStringLiteral("실전")},
    };
    return labels.value(mode, mode);
}

QString trader_status_label(const QString& status) {
    static const QMap<QString, QString> labels = {
        {"running", QStringLiteral("도는 중")},
        {"entries_paused", QStringLiteral("새 진입 정지")},
        {"paused", QStringLiteral("정지")},
        {"daily_limit", QStringLiteral("하루 한도")},
        {"cumulative_limit", QStringLiteral("누적 한도")},
    };
    return labels.value(status, status);
}

QString trader_exit_label(const QString& reason) {
    static const QMap<QString, QString> labels = {
        {"take_profit", QStringLiteral("익절")},
        {"stop_loss", QStringLiteral("손절")},
        {"time", QStringLiteral("시간 청산")},
        {"pause", QStringLiteral("정지 매도")},
        {"daily_limit", QStringLiteral("하루 한도")},
        {"cumulative_limit", QStringLiteral("누적 한도")},
    };
    return labels.value(reason, reason);
}

namespace {

const QLocale& korean() {
    static const QLocale locale(QLocale::Korean, QLocale::SouthKorea);
    return locale;
}

/// A percent change with its sign, two decimals.
QString change_text(double change_pct) {
    return QStringLiteral("%1%2%").arg(change_pct >= 0 ? "+" : "", korean().toString(change_pct, 'f', 2));
}

/// A macro value: one decimal from 100 up, else two.
QString value_text(double value) {
    return korean().toString(value, 'f', std::abs(value) >= 100 ? 1 : 2);
}

} // namespace

QString reading_text(const Reading& r) {
    const QString value = r.value ? value_text(*r.value) : QStringLiteral("모름");
    const QString change = r.change_pct ? QStringLiteral(" (%1)").arg(change_text(*r.change_pct)) : QString();
    return QStringLiteral("%1 %2%3").arg(r.label, value, change);
}

QString usd_futures_text(const std::optional<UsdFutures>& live, const QList<Reading>& readings) {
    const auto morning =
        std::find_if(readings.begin(), readings.end(), [](const Reading& r) { return r.indicator == "usdf"; });
    if (!live)
        return morning != readings.end() ? reading_text(*morning) : QString();
    const QString label = morning != readings.end() ? morning->label : QStringLiteral("달러선물");
    const QString when = kst_text(live->at, "HH:mm");
    const QString aside = live->change_pct ? QStringLiteral("%1, %2").arg(change_text(*live->change_pct), when) : when;
    return QStringLiteral("%1 %2 (%3)").arg(label, value_text(live->price), aside);
}

QString kst_text(const QDateTime& t, const QString& format) {
    static const QTimeZone kst("Asia/Seoul");
    return t.isValid() ? t.toTimeZone(kst).toString(format) : QStringLiteral("—");
}

QString price_text(double price) {
    return korean().toString(price, 'f', std::floor(price) == price ? 0 : 1);
}

QString special_label(const QString& kind) {
    static const QMap<QString, QString> labels = {
        {"vi", QStringLiteral("VI")},
        {"circuit_breaker", QStringLiteral("서킷브레이커")},
        {"sidecar", QStringLiteral("사이드카")},
    };
    return labels.value(kind, kind);
}

bool is_not_found(const QString& failure) {
    return failure.startsWith(QStringLiteral("엔진 오류 (HTTP 404)"));
}

QString failure_text(int http_status, const QString& message) {
    if (http_status == 0 && message.isEmpty())
        return QStringLiteral("엔진에 연결할 수 없습니다. 엔진 주소와 네트워크를 확인하세요.");
    // A transport failure (codeyoma/krx-quant#302): HttpClient's "Network error: <Qt's reason>". Its
    // transfer limit aborts a request the engine does not answer, which Qt calls "Operation canceled".
    static const QString kNetwork = QStringLiteral("Network error: ");
    if (http_status == 0 && message.startsWith(kNetwork)) {
        const QString reason = message.mid(kNetwork.size());
        if (reason == QLatin1String("Operation canceled"))
            return QStringLiteral("엔진이 답하지 않았습니다(시간 초과). 엔진 주소와 네트워크를 확인하세요.");
        return QStringLiteral("엔진에 연결할 수 없습니다(%1). 엔진 주소와 네트워크를 확인하세요.").arg(reason);
    }
    if (http_status == 0)
        return QStringLiteral("엔진 응답을 읽을 수 없습니다: %1").arg(message);
    if (message.isEmpty())
        return QStringLiteral("엔진 오류 (HTTP %1)").arg(http_status);
    return QStringLiteral("엔진 오류 (HTTP %1): %2").arg(http_status).arg(message);
}

namespace {

std::optional<BriefingPrice> briefing_price_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return BriefingPrice{o.value("price").toDouble(), time_of(o.value("at")), number(o.value("change_pct")),
                         o.value("window").toString()};
}

std::optional<ExpectedOpen> expected_open_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return ExpectedOpen{o.value("price").toDouble(),          number(o.value("qty")),
                        o.value("venue_code").toString(),     time_of(o.value("at")),
                        number(o.value("change_pct")),        o.value("final").toBool()};
}

QMap<QString, double> numbers_of(const QJsonValue& v) {
    QMap<QString, double> out;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.value().isDouble())
            out.insert(it.key(), it.value().toDouble());
    return out;
}

AlgorithmInputs inputs_of(const QJsonObject& o) {
    AlgorithmInputs made{o.value("algorithm").toString(), o.value("model_version").toString(),
                         numbers_of(o.value("bias")), {}};
    for (const auto& v : o.value("inputs").toArray()) {
        const QJsonObject i = v.toObject();
        made.inputs.push_back({i.value("name").toString(), number(i.value("value")), number(i.value("z")),
                               numbers_of(i.value("scores"))});
    }
    return made;
}

NightSession night_of(const QJsonObject& o) {
    NightSession night;
    night.evening = QDate::fromString(o.value("evening").toString(), Qt::ISODate);
    night.from = time_of(o.value("from"));
    night.to = time_of(o.value("to"));
    night.recorded = o.value("recorded").toBool();
    night.contract = o.value("contract").toString();
    night.last = number(o.value("last"));
    night.change_pct = number(o.value("change_pct"));
    night.high = number(o.value("high"));
    night.low = number(o.value("low"));
    night.volume = number(o.value("volume"));
    night.last_trade_at = time_of(o.value("last_trade_at"));
    for (const auto& v : o.value("minutes").toArray()) {
        const QJsonObject m = v.toObject();
        night.minutes.push_back({time_of(m.value("at")), m.value("price").toDouble()});
    }
    return night;
}

BriefingSymbol briefing_symbol_of(const QJsonObject& o) {
    BriefingSymbol made;
    made.symbol = o.value("symbol").toString();
    made.previous_close = number(o.value("previous_close"));
    made.after_hours = briefing_price_of(o.value("after_hours"));
    made.premarket = briefing_price_of(o.value("premarket"));
    made.expected_open = expected_open_of(o.value("expected_open"));
    const QJsonObject s = o.value("supply").toObject();
    made.supply = {QDate::fromString(s.value("day").toString(), Qt::ISODate),
                   number(s.value("foreign_net")),
                   number(s.value("institution_net")),
                   s.value("days").toInt(),
                   number(s.value("foreign_net_sum")),
                   number(s.value("institution_net_sum"))};
    return made;
}

std::optional<BriefingOpenToClose> open_to_close_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    BriefingOpenToClose made;
    made.day = QDate::fromString(o.value("day").toString(), Qt::ISODate);
    made.predicted_at = time_of(o.value("predicted_at"));
    made.previous = o.value("previous").toBool();
    for (const auto& sv : o.value("symbols").toArray()) {
        const QJsonObject s = sv.toObject();
        BriefingDayKind day{s.value("symbol").toString(), {}, {}};
        for (const auto& p : s.value("predictions").toArray())
            day.predictions.push_back(kind_prediction_of(p.toObject(), day.symbol, made.predicted_at));
        for (const auto& i : s.value("inputs").toArray())
            day.inputs.push_back(inputs_of(i.toObject()));
        made.symbols.push_back(day);
    }
    return made;
}

} // namespace

Result<Briefing> parse_briefing(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<Briefing>::err(obj.error());
    const QJsonObject& o = obj.value();
    Briefing b;
    b.day = QDate::fromString(o.value("day").toString(), Qt::ISODate);
    b.as_of = time_of(o.value("as_of"));
    b.phase = o.value("phase").toString();
    b.trading_day = o.value("trading_day").toBool();
    const QJsonObject session = o.value("session").toObject();
    b.open = time_of(session.value("open"));
    b.close = time_of(session.value("close"));
    b.special = session.value("special").toString();
    b.previous_day = QDate::fromString(o.value("previous_day").toString(), Qt::ISODate);
    const QJsonObject overnight = o.value("overnight").toObject();
    b.macro = macro_of(overnight.value("macro").toObject());
    for (const auto& v : overnight.value("us").toArray()) {
        const QJsonObject u = v.toObject();
        BriefingUs us{u.value("indicator").toString(), {}};
        for (const auto& cv : u.value("closes").toArray()) {
            const QJsonObject c = cv.toObject();
            us.closes.push_back({QDate::fromString(c.value("day").toString(), Qt::ISODate),
                                 c.value("close").toDouble(), number(c.value("change_pct"))});
        }
        b.us.push_back(us);
    }
    b.night = night_of(o.value("night_futures").toObject());
    for (const auto& v : o.value("symbols").toArray())
        b.symbols.push_back(briefing_symbol_of(v.toObject()));
    b.open_to_close = open_to_close_of(o.value("open_to_close"));
    const QJsonObject schedule = o.value("schedule").toObject();
    for (const auto& v : schedule.value("events").toArray()) {
        const QJsonObject e = v.toObject();
        b.events.push_back({time_of(e.value("at")), e.value("country").toString(), e.value("title").toString(),
                            e.value("importance").toString(), number(e.value("actual")),
                            number(e.value("forecast")), number(e.value("previous")), e.value("unit").toString(),
                            e.value("scale").toString()});
    }
    b.option_expiry = schedule.value("option_expiry").toBool();
    b.futures_expiry = schedule.value("futures_expiry").toBool();
    b.month_end = schedule.value("month_end").toBool();
    b.month_start = schedule.value("month_start").toBool();
    return Result<Briefing>::ok(b);
}

namespace {

FlowTotals flow_totals_of(const QJsonObject& o) {
    return {QDate::fromString(o.value("day").toString(), Qt::ISODate),
            time_of(o.value("at")),
            number(o.value("program_net_value")),
            number(o.value("member_foreign_net_qty")),
            number(o.value("estimate_foreign_net_qty")),
            number(o.value("estimate_institution_net_qty"))};
}

QList<FlowMember> flow_members_of(const QJsonValue& v) {
    QList<FlowMember> out;
    for (const auto& m : v.toArray()) {
        const QJsonObject o = m.toObject();
        out.push_back({o.value("no").toString(), o.value("name").toString(), number(o.value("qty")),
                       o.value("foreign").toBool()});
    }
    return out;
}

} // namespace

Result<Flow> parse_flow(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<Flow>::err(obj.error());
    const QJsonObject& o = obj.value();
    Flow f;
    f.symbol = o.value("symbol").toString();
    f.day = QDate::fromString(o.value("day").toString(), Qt::ISODate);
    f.as_of = time_of(o.value("as_of"));
    f.recorded = o.value("recorded").toBool();
    f.open = time_of(o.value("open"));
    f.close = time_of(o.value("close"));
    for (const auto& v : o.value("minutes").toArray()) {
        const QJsonObject m = v.toObject();
        f.minutes.push_back({time_of(m.value("at")), number(m.value("price")), number(m.value("program_net_value")),
                             number(m.value("program_buy_value")), number(m.value("program_sell_value")),
                             number(m.value("program_net_qty")), m.value("program_source").toString(),
                             number(m.value("member_foreign_net_qty")), m.value("member_source").toString()});
    }
    for (const auto& v : o.value("estimates").toArray()) {
        const QJsonObject e = v.toObject();
        f.estimates.push_back({e.value("slot").toString(), time_of(e.value("received_at")),
                               number(e.value("foreign_net_qty")), number(e.value("institution_net_qty"))});
    }
    if (o.value("members").isObject()) {
        const QJsonObject m = o.value("members").toObject();
        f.members = FlowMembers{time_of(m.value("received_at")), m.value("source").toString(),
                                flow_members_of(m.value("sell")), flow_members_of(m.value("buy"))};
    }
    f.totals = flow_totals_of(o.value("totals").toObject());
    if (o.value("previous_day_same_time").isObject())
        f.previous_day_same_time = flow_totals_of(o.value("previous_day_same_time").toObject());
    if (o.value("final").isObject()) {
        const QJsonObject final = o.value("final").toObject();
        f.final_known = true;
        f.final_foreign_net = number(final.value("foreign_net"));
        f.final_institution_net = number(final.value("institution_net"));
    }
    for (const auto& v : o.value("special").toArray())
        f.special.push_back(special_of(v.toObject()));
    return Result<Flow>::ok(f);
}


namespace {

CostRoundTrip round_trip_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    return {o.value("bp").toDouble(), o.value("ticks").toDouble()};
}

std::optional<TickEdge> tick_edge_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return TickEdge{o.value("edge").toDouble(), o.value("price").toDouble(), o.value("tick").toInt(),
                    o.value("k").toInt()};
}

std::optional<CostPnl> cost_pnl_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return CostPnl{o.value("trades").toInt(), interval_of(o.value("per_trade"))};
}

std::optional<CostPredictor> cost_predictor_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    CostPredictor p;
    p.algorithm = o.value("algorithm").toString();
    p.rank = integer_of(o.value("rank"));
    p.calls = o.value("calls").toInt();
    p.precision = interval_of(o.value("precision"));
    p.edge = number(o.value("edge"));
    p.pnl = cost_pnl_of(o.value("pnl"));
    p.pnl_inverse = cost_pnl_of(o.value("pnl_inverse"));
    if (o.value("verdict").isObject()) {
        const QJsonObject verdict = o.value("verdict").toObject();
        p.verdict_limit = verdict.value("limit").toString();
        p.verdict_market = verdict.value("market").toString();
    }
    return p;
}

std::optional<Breakeven> breakeven_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return Breakeven{o.value("c_over_w").toDouble(), o.value("p_star").toDouble()};
}

CostSymbol cost_symbol_of(const QJsonObject& o) {
    CostSymbol s;
    s.symbol = o.value("symbol").toString();
    s.name = o.value("name").toString();
    s.price = number(o.value("price"));
    s.price_at = time_of(o.value("price_at"));
    s.live = o.value("live").toBool();
    if (o.value("costs").isObject()) {
        const QJsonObject c = o.value("costs").toObject();
        CostCard card;
        card.tick = c.value("tick").toInt();
        card.tick_bp = c.value("tick_bp").toDouble();
        card.k = c.value("k").toInt();
        card.sell_tax_bp = c.value("sell_tax_bp").toDouble();
        card.fees_bp = c.value("fees_bp").toDouble();
        card.spread_bp = c.value("spread_bp").toDouble();
        card.limit = round_trip_of(c.value("limit"));
        card.market = round_trip_of(c.value("market"));
        if (c.value("inverse").isObject()) {
            const QJsonObject i = c.value("inverse").toObject();
            card.inverse = CostInverse{i.value("leverage").toInt(),
                                       QDate::fromString(i.value("listed").toString(), Qt::ISODate),
                                       i.value("bp").toDouble(), i.value("ticks").toDouble(),
                                       strings_of(i.value("omits"))};
        }
        card.below = tick_edge_of(c.value("below"));
        card.above = tick_edge_of(c.value("above"));
        s.costs = card;
    }
    return s;
}

CostRow cost_row_of(const QJsonObject& o) {
    CostRow r;
    r.kind = o.value("kind").toString();
    r.title = o.value("title").toString();
    r.horizon_minutes = integer_of(o.value("horizon_minutes"));
    r.rule = o.value("rule").toString();
    for (const auto& d : o.value("days").toArray())
        r.days.push_back(QDate::fromString(d.toString(), Qt::ISODate));
    r.unit = o.value("unit").toString();
    r.bar_closes = o.value("bar_closes").toInt();
    r.width = number(o.value("width"));
    r.reference_price = number(o.value("reference_price"));
    r.tick = integer_of(o.value("tick"));
    if (o.value("cost").isObject()) {
        const QJsonObject c = o.value("cost").toObject();
        r.cost_limit = number(c.value("limit"));
        r.cost_market = number(c.value("market"));
    }
    if (o.value("breakeven").isObject()) {
        const QJsonObject b = o.value("breakeven").toObject();
        r.breakeven_limit = breakeven_of(b.value("limit"));
        r.breakeven_market = breakeven_of(b.value("market"));
    }
    r.breakeven_missing = o.value("breakeven_missing").toString();
    r.consensus = cost_predictor_of(o.value("consensus"));
    r.top = cost_predictor_of(o.value("top"));
    r.baseline = cost_predictor_of(o.value("baseline"));
    return r;
}

BaseRates base_rates_of(const QJsonObject& o) {
    BaseRates b;
    b.computed_at = time_of(o.value("computed_at"));
    b.first_day = QDate::fromString(o.value("first_day").toString(), Qt::ISODate);
    b.last_day = QDate::fromString(o.value("last_day").toString(), Qt::ISODate);
    for (const auto& v : o.value("rows").toArray()) {
        const QJsonObject r = v.toObject();
        b.rows.push_back({r.value("symbol").toString(), r.value("period").toString(),
                          QDate::fromString(r.value("from").toString(), Qt::ISODate),
                          QDate::fromString(r.value("to").toString(), Qt::ISODate), r.value("days").toInt(),
                          r.value("windows").toInt(), r.value("up").toDouble(), r.value("flat").toDouble(),
                          r.value("down").toDouble(), r.value("k").toInt(), number(r.value("persist")),
                          r.value("persist_windows").toInt()});
    }
    return b;
}

} // namespace

Result<CostMap> parse_cost_map(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<CostMap>::err(obj.error());
    const QJsonObject& o = obj.value();
    CostMap m;
    m.symbol = o.value("symbol").toString();
    m.period = o.value("period").toString();
    m.special = o.value("special").toString();
    m.as_of = time_of(o.value("as_of"));
    m.min_sample = o.value("min_sample").toInt();
    m.fee_rate = o.value("fee_rate").toDouble();
    m.spread_ticks = o.value("spread_ticks").toInt();
    for (const auto& v : o.value("symbols").toArray())
        m.symbols.push_back(cost_symbol_of(v.toObject()));
    for (const auto& v : o.value("rows").toArray())
        m.rows.push_back(cost_row_of(v.toObject()));
    if (o.value("regimes").isObject())
        m.regimes = base_rates_of(o.value("regimes").toObject());
    m.regimes_missing = o.value("regimes_missing").toString();
    return Result<CostMap>::ok(m);
}

namespace {

QDate day_of(const QJsonValue& v) {
    return QDate::fromString(v.toString(), Qt::ISODate);
}

ClosingProduct closing_product_of(const QJsonObject& o) {
    ClosingProduct p;
    p.code = o.value("code").toString();
    p.name = o.value("name").toString();
    p.leverage = o.value("leverage").toInt();
    p.structure = o.value("structure").toString();
    p.coefficient = o.value("coefficient").toInt();
    p.net_assets = number(o.value("net_assets"));
    p.net_assets_date = day_of(o.value("net_assets_date"));
    p.net_assets_source = o.value("net_assets_source").toString();
    p.demand = number(o.value("demand"));
    return p;
}

ClosingStats closing_stats_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    return {o.value("days").toInt(),
            number(o.value("auction_bp_median")),
            number(o.value("agree")),
            o.value("agree_n").toInt(),
            number(o.value("next_gap_opposite")),
            o.value("next_gap_n").toInt(),
            number(o.value("next_close_opposite")),
            o.value("next_close_n").toInt()};
}

/// A denominator's sources (codeyoma/krx-quant#368); none from an engine before it.
std::optional<ClosingSources> closing_sources_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return ClosingSources{o.value("recorded").toInt(), o.value("krx_bar").toInt(), o.value("integrated_bar").toInt()};
}

ClosingDay closing_day_of(const QJsonObject& o) {
    ClosingDay d;
    d.date = day_of(o.value("date"));
    d.period = o.value("period").toString();
    d.demand = number(o.value("demand"));
    d.spot_futures = number(o.value("spot_futures"));
    d.futures = number(o.value("futures"));
    d.demand_source = o.value("demand_source").toString();
    d.ratio = number(o.value("ratio"));
    d.ratio_total = number(o.value("ratio_total"));
    d.denominator = number(o.value("denominator"));
    d.denominator_days = o.value("denominator_days").toInt();
    d.denominator_sources = closing_sources_of(o.value("denominator_sources"));
    d.denominator_reason = o.value("denominator_reason").toString();
    d.auction_value = number(o.value("auction_value"));
    d.auction_value_source = o.value("auction_value_source").toString();
    d.auction_bp = o.value("auction_bp").toDouble();
    d.auction_ticks = o.value("auction_ticks").toDouble();
    d.flow_bp = o.value("flow_bp").toDouble();
    d.compared_with = o.value("compared_with").toString();
    if (o.value("agree").isBool())
        d.agree = o.value("agree").toBool();
    d.next_gap_bp = number(o.value("next_gap_bp"));
    d.next_close_bp = number(o.value("next_close_bp"));
    d.special = strings_of(o.value("special"));
    d.net_assets_source = o.value("net_assets_source").toString();
    return d;
}

} // namespace

Result<ClosingFlow> parse_closing_flow(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<ClosingFlow>::err(obj.error());
    const QJsonObject& o = obj.value();
    ClosingFlow f;
    f.symbol = o.value("symbol").toString();
    f.day = day_of(o.value("day"));
    f.as_of = time_of(o.value("as_of"));
    f.at = time_of(o.value("at"));
    f.source = o.value("source").toString();
    f.price = number(o.value("price"));
    f.previous_close = number(o.value("previous_close"));
    f.r = number(o.value("r"));
    f.demand = number(o.value("demand"));
    f.spot_futures = number(o.value("spot_futures"));
    f.futures = number(o.value("futures"));
    f.ratio = number(o.value("ratio"));
    f.ratio_total = number(o.value("ratio_total"));
    f.denominator = number(o.value("denominator"));
    f.denominator_days = o.value("denominator_days").toInt();
    f.denominator_sources = closing_sources_of(o.value("denominator_sources"));
    f.denominator_reason = o.value("denominator_reason").toString();
    for (const auto& v : o.value("products").toArray())
        f.products.push_back(closing_product_of(v.toObject()));
    f.missing = strings_of(o.value("missing"));
    f.reason = o.value("reason").toString();
    return Result<ClosingFlow>::ok(f);
}

Result<ClosingDays> parse_closing_days(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<ClosingDays>::err(obj.error());
    const QJsonObject& o = obj.value();
    ClosingDays d;
    d.symbol = o.value("symbol").toString();
    d.from = day_of(o.value("from"));
    d.to = day_of(o.value("to"));
    d.computed_at = time_of(o.value("computed_at"));
    for (const auto& v : o.value("days").toArray())
        d.days.push_back(closing_day_of(v.toObject()));
    const QJsonObject summary = o.value("summary").toObject();
    for (const auto& v : summary.value("periods").toArray()) {
        const QJsonObject p = v.toObject();
        d.periods.push_back({p.value("period").toString(), day_of(p.value("from")), day_of(p.value("to")),
                             closing_stats_of(p.value("all")), closing_stats_of(p.value("without_special"))});
    }
    for (const auto& v : summary.value("tertiles").toArray()) {
        const QJsonObject t = v.toObject();
        d.tertiles.push_back({t.value("tertile").toInt(), t.value("ratio_from").toDouble(),
                              t.value("ratio_to").toDouble(), closing_stats_of(t.value("all")),
                              closing_stats_of(t.value("without_special"))});
    }
    return Result<ClosingDays>::ok(d);
}


namespace {

std::optional<TraderLimit> trader_limit_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return TraderLimit{o.value("percent").toDouble(), o.value("won").toDouble(), o.value("used").toDouble(),
                       time_of(o.value("reached_at"))};
}

std::optional<TraderPnl> trader_pnl_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    TraderPnl pnl;
    pnl.day = day_of(o.value("day"));
    pnl.since = time_of(o.value("since"));
    pnl.realized = o.value("realized").toDouble();
    pnl.unrealized = o.value("unrealized").toDouble();
    pnl.net = o.value("net").toDouble();
    pnl.trades = o.value("trades").toInt();
    pnl.worst = o.value("worst").toDouble();
    return pnl;
}

TraderControl trader_control_of(const QJsonObject& o) {
    TraderControl c;
    c.id = o.value("id").toInteger(-1);
    c.at = time_of(o.value("at"));
    c.action = o.value("action").toString();
    c.source = o.value("source").toString();
    if (o.value("sell_now").isBool())
        c.sell_now = o.value("sell_now").toBool();
    c.mode = o.value("mode").toString();
    c.applied_at = time_of(o.value("applied_at"));
    return c;
}

std::optional<qint64> long_of(const QJsonValue& v) {
    if (!v.isDouble())
        return std::nullopt;
    return v.toInteger();
}

} // namespace

Result<TraderState> parse_trader(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<TraderState>::err(obj.error());
    const QJsonObject& o = obj.value();
    TraderState t;
    t.updated_at = time_of(o.value("updated_at"));
    t.alive = o.value("alive").toBool();
    t.mode = o.value("mode").toString();
    t.status = o.value("status").toString();
    t.started_at = time_of(o.value("started_at"));
    t.capital = number(o.value("capital"));
    t.cash = number(o.value("cash"));
    if (o.value("position").isObject()) {
        const QJsonObject p = o.value("position").toObject();
        TraderPosition position;
        position.trade_id = p.value("trade_id").toInteger();
        position.kind = p.value("kind").toString();
        position.predictor = p.value("predictor").toString();
        position.quantity = p.value("quantity").toInt();
        position.entry_price = p.value("entry_price").toDouble();
        position.entry_at = time_of(p.value("entry_at"));
        position.target = p.value("target").toDouble();
        position.stop = p.value("stop").toDouble();
        position.time_barrier = time_of(p.value("time_barrier"));
        position.bid = number(p.value("bid"));
        position.bid_at = time_of(p.value("bid_at"));
        position.unrealized = p.value("unrealized").toDouble();
        position.selling = p.value("selling").toBool();
        t.position = position;
    }
    t.today = trader_pnl_of(o.value("today"));
    t.cumulative = trader_pnl_of(o.value("cumulative"));
    const QJsonObject limits = o.value("limits").toObject();
    t.daily_limit = trader_limit_of(limits.value("daily"));
    t.cumulative_limit = trader_limit_of(limits.value("cumulative"));
    if (o.value("pause").isObject()) {
        const QJsonObject p = o.value("pause").toObject();
        t.pause = TraderPause{time_of(p.value("at")), p.value("sell_now").toBool(), p.value("source").toString()};
    }
    for (const auto& v : o.value("controls").toArray()) {
        const TraderControl c = trader_control_of(v.toObject());
        if (c.id >= 0 && c.at.isValid())
            t.controls.push_back(c);
    }
    return Result<TraderState>::ok(t);
}

Result<QList<TraderDay>> parse_trader_days(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<TraderDay>>::err(obj.error());
    QList<TraderDay> days;
    for (const auto& v : obj.value().value("days").toArray()) {
        const QJsonObject o = v.toObject();
        TraderDay d;
        d.day = day_of(o.value("day"));
        d.mode = o.value("mode").toString();
        d.net = o.value("net").toDouble();
        d.trades = o.value("trades").toInt();
        const QJsonObject daily = o.value("daily").toObject();
        d.daily_limit = daily.value("limit").toDouble();
        d.daily_worst = daily.value("worst").toDouble();
        d.daily_used = daily.value("used").toDouble();
        const QJsonObject cumulative = o.value("cumulative").toObject();
        d.cumulative_limit = cumulative.value("limit").toDouble();
        d.cumulative_net = cumulative.value("net").toDouble();
        d.cumulative_worst = cumulative.value("worst").toDouble();
        d.cumulative_used = cumulative.value("used").toDouble();
        d.status = o.value("status").toString();
        d.halt = o.value("halt").toString();
        d.updated_at = time_of(o.value("updated_at"));
        if (d.day.isValid() && !d.mode.isEmpty())
            days.push_back(d);
    }
    return Result<QList<TraderDay>>::ok(days);
}

Result<QList<TraderTrade>> parse_trader_trades(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<TraderTrade>>::err(obj.error());
    QList<TraderTrade> trades;
    for (const auto& v : obj.value().value("trades").toArray()) {
        const QJsonObject o = v.toObject();
        TraderTrade t;
        t.id = o.value("id").toInteger(-1);
        t.mode = o.value("mode").toString();
        t.kind = o.value("kind").toString();
        t.predictor = o.value("predictor").toString();
        t.role = o.value("role").toString();
        t.policy = long_of(o.value("policy"));
        t.bar_close = time_of(o.value("bar_close"));
        t.probability = number(o.value("probability"));
        t.threshold = number(o.value("threshold"));
        t.p_star = number(o.value("p_star"));
        t.precision_low = number(o.value("precision_low"));
        t.kelly = number(o.value("kelly"));
        t.fraction = number(o.value("fraction"));
        t.anchor = number(o.value("anchor"));
        t.tick = number(o.value("tick"));
        t.width_ticks = o.value("width_ticks").toInt();
        t.target = o.value("target").toDouble();
        t.stop = o.value("stop").toDouble();
        t.time_barrier = time_of(o.value("time_barrier"));
        t.decided_at = time_of(o.value("decided_at"));
        const QJsonObject entry = o.value("entry").toObject();
        t.entry_at = time_of(entry.value("at"));
        t.entry_price = entry.value("price").toDouble();
        t.quantity = entry.value("quantity").toInt();
        if (o.value("exit").isObject()) {
            const QJsonObject out = o.value("exit").toObject();
            t.exit_at = time_of(out.value("at"));
            t.exit_price = number(out.value("price"));
            t.exit_reason = out.value("reason").toString();
        }
        t.buy_fee = o.value("buy_fee").toDouble();
        t.sell_fee = number(o.value("sell_fee"));
        t.tax = number(o.value("tax"));
        t.cost = number(o.value("cost"));
        t.gross = number(o.value("gross"));
        t.net = number(o.value("net"));
        if (t.id < 0 || !t.entry_at.isValid())
            continue;
        trades.push_back(t);
    }
    return Result<QList<TraderTrade>>::ok(trades);
}

Result<TraderControl> parse_trader_pause(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<TraderControl>::err(obj.error());
    const TraderControl c = trader_control_of(obj.value().value("control").toObject());
    if (c.id < 0)
        return Result<TraderControl>::err(QStringLiteral("엔진 응답에 정지 요청이 없습니다").toStdString());
    return Result<TraderControl>::ok(c);
}

} // namespace fincept::krx_quant
