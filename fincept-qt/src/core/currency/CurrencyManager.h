#pragma once
// CurrencyManager owns the preferred display currency and dated reference rates.
// fork addition (codeyoma/krx-quant#142): explicit-source conversion only;
// nominal calculator formatting and order/storage values are not converted here.

#include "core/currency/fx/FxRates.h"

#include <QTimer>

class QNetworkAccessManager;

#include <QObject>
#include <QString>
#include <QVector>

namespace fincept::currency {

struct CurrencyInfo {
    QString code;   // ISO 4217, e.g. "USD"
    QString symbol; // display glyph, e.g. "$"
    QString name;   // human label for the settings combo
};

class CurrencyManager : public QObject {
    Q_OBJECT
  public:
    static CurrencyManager& instance();

    /// All selectable currencies (single source of truth for the combo).
    static QVector<CurrencyInfo> available();

    /// Symbol for a given code; falls back to the code itself if unknown.
    static QString symbol_for(const QString& code);

    /// Read the persisted choice from SettingsRepository (default "USD").
    /// Call once on startup before windows are shown.
    void initialize();

    QString code() const { return code_; }
    QString symbol() const { return symbol_for(code_); }

    DisplayAmount display_amount(double value, const QString& source) const;
    QDate rates_date() const { return rates_.date(); }
    bool rates_usable() const { return rates_.usable(QDate::currentDate()); }
    bool refreshing() const { return refreshing_; }
    bool rates_cached() const { return cached_; }
    QString rates_error() const { return rates_error_; }
    void refresh_rates();

    /// Switch currency: persist + emit. No-op if unchanged or unknown code.
    void set_currency(const QString& code);

  signals:
    void currency_changed(const QString& code, const QString& symbol);
    void display_changed(); // Preference, valid rates, or rate expiry changed.
    void rates_status_changed();

  private:
    CurrencyManager() = default;
    QString code_ = "USD";
    FxRates rates_;
    QNetworkAccessManager* network_ = nullptr; // Dedicated public-data connection: no account headers.
    QTimer refresh_timer_;
    QTimer expiry_timer_;
    bool initialized_ = false;
    bool refreshing_ = false;
    bool cached_ = false;
    bool last_usable_ = false;
    QString rates_error_;
};

} // namespace fincept::currency
