#include "services/krx_quant/EngineData.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

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
    if (algorithm.startsWith("baseline-"))
        return 2;
    return 1;
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
    if (o.value("kis").isObject())
        s.kis_connected = o.value("kis").toObject().value("connected").toBool();
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
    for (const auto& v : o.value("predictions").toArray()) {
        const QJsonObject p = v.toObject();
        Prediction made{p.value("algorithm").toString(),
                        p.value("model_version").toString(),
                        p.value("status").toString(),
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt};
        if (made.status == "ok") {
            made.p_up = number(p.value("p_up"));
            made.p_flat = number(p.value("p_flat"));
            made.p_down = number(p.value("p_down"));
            made.expected_ticks = number(p.value("expected_ticks"));
        }
        latest.predictions.push_back(made);
    }
    // The final consensus before the fast one: it is the one to read once both are there.
    std::stable_sort(latest.predictions.begin(), latest.predictions.end(),
                     [](const Prediction& a, const Prediction& b) {
                         const int ga = group_of(a.algorithm), gb = group_of(b.algorithm);
                         if (ga != gb)
                             return ga < gb;
                         if (ga == 0)
                             return a.algorithm > b.algorithm; // consensus-final, then consensus-fast
                         return false;
                     });
    return Result<Latest>::ok(latest);
}

Direction direction(const Prediction& p) {
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
        {"no_members", QStringLiteral("합의 대상 없음")},
        {"none_finished", QStringLiteral("끝난 알고리즘 없음")},
    };
    return labels.value(status, status);
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
