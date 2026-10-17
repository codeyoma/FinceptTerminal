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

/// A barrier entry's extra (krx-quant #183): its barrier P&L; none for another kind's.
std::optional<BarrierStats> barrier_stats_of(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    if (!o.contains("pnl") || !o.contains("long") || !o.contains("short"))
        return std::nullopt;
    return BarrierStats{o.value("n").toInt(), barrier_pnl_of(o.value("pnl")), barrier_pnl_of(o.value("long")),
                        barrier_pnl_of(o.value("short"))};
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
    entry.excluded = excluded_of(e.value("excluded"));
    entry.range = range_stats_of(e.value("extra"));
    entry.barrier = barrier_stats_of(e.value("extra"));
    if (e.value("lift").isObject()) {
        const QJsonObject lift = e.value("lift").toObject();
        entry.lift_low = number(lift.value("low"));
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
    return stage;
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
        info.trained_until = model.value("trained_until").toString();
        const QJsonObject status = a.value("status").toObject();
        info.status = status.value("status").toString();
        info.since = QDateTime::fromString(status.value("since").toString(), Qt::ISODate);
        for (const auto& h : status.value("history").toArray()) {
            const QJsonObject record = h.toObject();
            info.history << StatusRecord{QDateTime::fromString(record.value("at").toString(), Qt::ISODate),
                                         record.value("from").toString(), record.value("to").toString(),
                                         record.value("reason").toString()};
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

Result<Macro> parse_macro(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<Macro>::err(obj.error());
    const QJsonObject& o = obj.value();
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
    return Result<Macro>::ok(macro);
}

Result<QStringList> parse_members(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QStringList>::err(obj.error());
    return Result<QStringList>::ok(strings_of(obj.value().value("members")));
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

QString failure_text(int http_status, const QString& message) {
    if (http_status == 0 && message.isEmpty())
        return QStringLiteral("엔진에 연결할 수 없습니다. 엔진 주소와 네트워크를 확인하세요.");
    if (http_status == 0)
        return QStringLiteral("엔진 응답을 읽을 수 없습니다: %1").arg(message);
    if (message.isEmpty())
        return QStringLiteral("엔진 오류 (HTTP %1)").arg(http_status);
    return QStringLiteral("엔진 오류 (HTTP %1): %2").arg(http_status).arg(message);
}

} // namespace fincept::krx_quant
