#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QString>

/// The economic calendar from the KRX engine (codeyoma/krx-quant#150, custom/README.md): with the
/// engine's address set, the dashboard's Economic Calendar reads the engine's GET /v1/calendar
/// instead of api.fincept.in/macro/upcoming-events, which spends Fincept credits. Qt Core only,
/// unit-tested on its own (tests/tst_krx_engine_calendar.cpp).
namespace fincept::krx_quant {

/// The events the widget shows at most (the upstream request's limit).
inline constexpr int kCalendarEvents = 25;

/// GET <engine>/v1/calendar?limit=<limit>; ``engine`` may end with a slash.
QString calendar_url(const QString& engine, int limit = kCalendarEvents);

/// Headers for the engine request that keep Fincept's credentials off it: X-API-Key and
/// X-Session-Token as null values, which QNetworkRequest::setRawHeader takes as removing them.
/// HttpClient applies a request's own headers last, and otherwise adds the credentials to any
/// URL on the Fincept API's host name, whatever the port: a self-hosted Fincept API and the
/// engine on one machine would share it.
QMap<QByteArray, QByteArray> engine_request_headers();

/// The engine's answer as the widget's events (MacroCalendarService's topic payload): event,
/// category, country, date (yyyy-MM-dd) and time (HH:mm) in KST, reference_period, actual,
/// forecast and previous as text with their scale and unit ("3.7%", "23.75K", "-$1.2B",
/// "90.45B CNY"), importance 1 (low) to 3 (high), url. An answer that is not the engine's
/// calendar gives none.
QJsonArray calendar_widget_events(const QJsonDocument& answer);

} // namespace fincept::krx_quant
