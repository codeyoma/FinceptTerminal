#include "services/krx_quant/EngineCalendar.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>

namespace fincept::krx_quant {

namespace {

constexpr int kKstSeconds = 9 * 60 * 60;

/// A value as the widget shows it: the number without trailing zeros, then its unit ("3.7%",
/// "1.2B", "120 K"); empty when there is none.
QString value_text(const QJsonValue& value, const QString& unit) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        return {};
    QString text = QString::number(value.toDouble(), 'f', 6);
    while (text.contains('.') && (text.endsWith('0') || text.endsWith('.')))
        text.chop(1);
    if (text == QLatin1String("-0"))
        text = QStringLiteral("0");
    if (unit.isEmpty())
        return text;
    return unit.size() == 1 ? text + unit : text + QLatin1Char(' ') + unit;
}

int importance_level(const QString& importance) {
    if (importance == QLatin1String("high"))
        return 3;
    if (importance == QLatin1String("medium"))
        return 2;
    return 1;
}

} // namespace

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
        QJsonObject row;
        row["event"] = title;
        row["category"] = e.value("category").toString();
        row["country"] = e.value("country").toString();
        row["date"] = kst.toString(QStringLiteral("yyyy-MM-dd"));
        row["time"] = kst.toString(QStringLiteral("HH:mm"));
        row["reference_period"] = e.value("period").toString();
        row["actual"] = value_text(e.value("actual"), unit);
        row["forecast"] = value_text(e.value("forecast"), unit);
        row["consensus"] = QString(); // the feed has one forecast, shown as such
        row["previous"] = value_text(e.value("previous"), unit);
        row["importance"] = importance_level(e.value("importance").toString());
        row["url"] = e.value("source_url").toString();
        out.append(row);
    }
    return out;
}

} // namespace fincept::krx_quant
