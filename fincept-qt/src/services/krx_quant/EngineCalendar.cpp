#include "services/krx_quant/EngineCalendar.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>

namespace fincept::krx_quant {

namespace {

constexpr int kKstSeconds = 9 * 60 * 60;

/// A value as the widget shows it: the number without trailing zeros and its scale, with the
/// unit around it as written ("3.7%", "23.75K", "-$1.2B", "90.45B CNY"); empty when none.
QString value_text(const QJsonValue& value, const QString& unit, const QString& scale) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        return {};
    QString number = QString::number(std::abs(value.toDouble()), 'f', 6);
    while (number.contains('.') && (number.endsWith('0') || number.endsWith('.')))
        number.chop(1);
    const QString sign = value.toDouble() < 0 && number != QLatin1String("0") ? QStringLiteral("-") : QString();
    const QString scaled = number + scale;
    static const QStringList kCurrencySigns = {"$", "€", "£", "¥", "₩"};
    if (unit.isEmpty())
        return sign + scaled;
    if (unit == QLatin1String("%"))
        return sign + scaled + unit;
    if (kCurrencySigns.contains(unit))
        return sign + unit + scaled;
    return sign + scaled + QLatin1Char(' ') + unit;
}

int importance_level(const QString& importance) {
    if (importance == QLatin1String("high"))
        return 3;
    if (importance == QLatin1String("medium"))
        return 2;
    return 1;
}

} // namespace

QMap<QByteArray, QByteArray> engine_request_headers() {
    return {{"X-API-Key", QByteArray()}, {"X-Session-Token", QByteArray()}};
}

QString calendar_url(const QString& engine, int limit) {
    QString base = engine.trimmed();
    while (base.endsWith('/'))
        base.chop(1);
    return base + QStringLiteral("/v1/calendar?limit=%1").arg(limit);
}

QJsonArray calendar_widget_events(const QJsonDocument& answer) {
    QJsonArray out;
    const QJsonValue events = answer.object().value(QStringLiteral("events"));
    if (!events.isArray())
        return out;
    for (const QJsonValue& v : events.toArray()) {
        const QJsonObject e = v.toObject();
        const QString title = e.value("title").toString();
        const QDateTime at = QDateTime::fromString(e.value("at").toString(), Qt::ISODate);
        if (title.isEmpty() || !at.isValid())
            continue;
        const QDateTime kst = at.toOffsetFromUtc(kKstSeconds);
        const QString unit = e.value("unit").toString();
        const QString scale = e.value("scale").toString();
        QJsonObject row;
        row["event"] = title;
        row["category"] = e.value("category").toString();
        row["country"] = e.value("country").toString();
        row["date"] = kst.toString(QStringLiteral("yyyy-MM-dd"));
        row["time"] = kst.toString(QStringLiteral("HH:mm"));
        row["reference_period"] = e.value("period").toString();
        row["actual"] = value_text(e.value("actual"), unit, scale);
        row["forecast"] = value_text(e.value("forecast"), unit, scale);
        row["consensus"] = QString(); // the feed has one forecast, shown as such
        row["previous"] = value_text(e.value("previous"), unit, scale);
        row["importance"] = importance_level(e.value("importance").toString());
        row["url"] = e.value("source_url").toString();
        out.append(row);
    }
    return out;
}

} // namespace fincept::krx_quant
