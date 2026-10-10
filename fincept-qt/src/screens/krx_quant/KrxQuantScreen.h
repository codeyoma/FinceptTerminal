#pragma once

#include "services/krx_quant/KrxQuantService.h"

#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace fincept::screens {

/// KRX 4-minute predictions (fork addition, see custom/README.md): the prediction
/// engine's status and each symbol's latest predictions, read every few seconds while
/// the screen is shown. The text is Korean.
class KrxQuantScreen : public QWidget {
    Q_OBJECT
  public:
    explicit KrxQuantScreen(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void apply_base_url();
    void show_snapshot(const services::KrxQuantService::Snapshot& snapshot);
    void show_failure(const QString& reason);
    void fill_table(const services::KrxQuantService::Snapshot& snapshot);

    services::KrxQuantService* service_ = nullptr;
    QLineEdit* url_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* special_label_ = nullptr;
    QLabel* footer_label_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTimer* timer_ = nullptr;
};

} // namespace fincept::screens
