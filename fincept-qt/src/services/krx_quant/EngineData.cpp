#include "services/krx_quant/EngineData.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <cmath>

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

std::optional<Outcome> outcome_of(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    return Outcome{class_of(o.value("class_a")), class_of(o.value("class_b")), o.value("special").toBool(),
                   o.value("unscorable").toString()};
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
                    p.value("blocked").toString()};
    if (made.status == "ok") {
        made.p_up = number(p.value("p_up"));
        made.p_flat = number(p.value("p_flat"));
        made.p_down = number(p.value("p_down"));
        made.expected_ticks = number(p.value("expected_ticks"));
    }
    return made;
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
    s.commit = o.value("commit").toString();
    s.started_at = time_of(o.value("started_at"));
    s.last_bar_close = time_of(o.value("last_bar_close"));
    s.pending_outcomes = o.value("pending_outcomes").toInt();
    for (const auto& v : o.value("special").toArray()) {
        const QJsonObject w = v.toObject();
        s.special.push_back({w.value("kind").toString(), w.value("symbol").toString(), time_of(w.value("start")),
                             time_of(w.value("end"))});
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

Result<QList<SymbolQuote>> parse_symbols(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<QList<SymbolQuote>>::err(obj.error());
    QList<SymbolQuote> out;
    for (const auto& v : obj.value().value("symbols").toArray()) {
        const QJsonObject s = v.toObject();
        out.push_back({s.value("symbol").toString(), number(s.value("mid"))});
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
        out.push_back({time_of(b.value("start")), b.value("open").toDouble(), b.value("high").toDouble(),
                       b.value("low").toDouble(), b.value("close").toDouble(), b.value("volume").toDouble()});
    }
    return Result<QList<Bar>>::ok(out);
}

Result<Leaderboard> parse_leaderboard(const QJsonDocument& doc) {
    const auto obj = object_of(doc);
    if (obj.is_err())
        return Result<Leaderboard>::err(obj.error());
    const QJsonObject& o = obj.value();
    Leaderboard board;
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
    return Result<AlgorithmStats>::ok(stats);
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

Result<Event> parse_event(const QString& message) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return Result<Event>::err(QStringLiteral("엔진 스트림 메시지를 읽을 수 없습니다").toStdString());
    const QJsonObject o = doc.object();
    static const QMap<QString, Event::Type> types = {
        {"prediction", Event::Type::Prediction}, {"consensus", Event::Type::Consensus},
        {"outcome", Event::Type::Outcome},       {"health", Event::Type::Health},
        {"lifecycle", Event::Type::Lifecycle},
    };
    Event event;
    event.type = types.value(o.value("type").toString(), Event::Type::Other);
    event.at = time_of(o.value("at"));
    event.kind = o.value("kind").toString();
    event.bar_close = time_of(o.value("bar_close"));
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
