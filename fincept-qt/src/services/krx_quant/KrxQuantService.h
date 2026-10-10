#pragma once

#include "services/krx_quant/EngineData.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <string>

namespace fincept::services {

/// Reads the KRX prediction engine (a separate service) over its HTTP API v1 for the KRX
/// prediction screen, which owns it. Screens never call HttpClient directly.
///
/// The engine's address is a setting (`krx_quant/base_url`), empty until set. Nothing is
/// cached: each refresh reads the engine again.
class KrxQuantService : public QObject {
    Q_OBJECT

  public:
    /// Everything the screen shows, read in one refresh.
    struct Snapshot {
        krx_quant::EngineStatus status;
        QList<krx_quant::SymbolQuote> symbols;
        QMap<QString, krx_quant::Latest> latest; ///< by symbol; missing where its answer failed
        QStringList errors;                      ///< why those answers failed, one per symbol
    };

    explicit KrxQuantService(QObject* parent = nullptr);

    static QString base_url();
    /// Save the engine's address (a trailing slash is dropped).
    static void set_base_url(const QString& url);

    /// Read the status, the symbols and each symbol's latest predictions. Either
    /// snapshot_ready or refresh_failed follows. While a refresh is under way another is
    /// skipped (a slow engine is not asked again and again), unless it is a restart:
    /// then the one under way is dropped (the address changed).
    void refresh();
    void restart();

  signals:
    void snapshot_ready(const fincept::services::KrxQuantService::Snapshot& snapshot);
    void refresh_failed(const QString& reason);

  private:
    /// A step of a refresh: the answer to one GET, or why it failed.
    using Step = std::function<void(Result<QJsonDocument>)>;

    void get(const QString& path, int generation, Step step);
    void read_symbols(int generation, Snapshot snapshot);
    void read_latest(int generation, Snapshot snapshot);
    void finish_with(const Snapshot& snapshot);
    void fail_with(const QString& reason);

    int generation_ = 0;
    bool in_flight_ = false;
};

/// Why a request failed, in Korean, from HttpClient's error string.
QString krx_failure(const std::string& error);

} // namespace fincept::services

Q_DECLARE_METATYPE(fincept::services::KrxQuantService::Snapshot)
