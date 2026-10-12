// Fork addition: the capture tool's AppConfig (core/config/AppConfig.h), in a
// scratch ini file instead of the app's settings, so a capture never changes
// the engine address or the alert settings of the Fincept installed on the same
// machine.
#include "core/config/AppConfig.h"

#include <QDir>

namespace fincept {

AppConfig& AppConfig::instance() {
    static AppConfig s;
    return s;
}

AppConfig::AppConfig() : settings_(QDir::temp().filePath("krx-capture.ini"), QSettings::IniFormat) {}

QVariant AppConfig::get(const QString& key, const QVariant& default_val) const {
    return settings_.value(key, default_val);
}

void AppConfig::set(const QString& key, const QVariant& value) {
    settings_.setValue(key, value);
}

void AppConfig::remove(const QString& key) {
    settings_.remove(key);
}

QString AppConfig::api_base_url() const {
    return {};
}

QString AppConfig::cloud_base_url() const {
    return {};
}

bool AppConfig::dark_mode() const {
    return true;
}

int AppConfig::refresh_interval_ms() const {
    return 30000;
}

} // namespace fincept
