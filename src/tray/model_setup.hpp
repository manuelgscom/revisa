#pragma once
// Finds the model at startup and, if it is missing, asks once and downloads it in a background
// thread with a progress dialog. revisa-tray's only network request (fetch::download, curl).
//
//   run(model) ─► Ready ──────────────────────────────────► modelReady(path)
//              ├► Missing / NoDir ────────────────────────► failed(msg)
//              └► NeedsDownload ─► question ─► yes ─► download ─► modelReady(path) | failed(msg)
//                                            └► no ─────────────► failed(msg)
// After failed(), retryAvailable() says whether «Descargar el modelo» makes sense (download()).
// REVISA_TRAY_ASSUME_YES=1 skips the question (automated smoke tests).

#include <QObject>
#include <QPointer>
#include <QProgressDialog>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>
#include <string>

#include "core/fetch/model_resolve.hpp"

namespace revisa::tray {

class ModelSetup : public QObject {
    Q_OBJECT

public:
    explicit ModelSetup(QObject* parent = nullptr);
    ~ModelSetup() override;  // cancels and waits for a running download

    // explicitModel: the config's model (empty = the default one).
    void run(const std::string& explicitModel);
    // Downloads the default model without asking (menu «Descargar el modelo»).
    void download();

    bool retryAvailable() const { return retry_; }
    bool downloading() const { return thread_ != nullptr; }

signals:
    void modelReady(const QString& path);
    void failed(const QString& message);
    void progress(int percent);

private:
    void onDownloadDone(int status, const QString& path, const QString& error);

    fetch::ModelSpec spec_;
    bool retry_ = false;
    QThread* thread_ = nullptr;
    std::atomic<bool> cancel_{false};
    std::atomic<int> lastPercent_{-1};
    QPointer<QProgressDialog> dialog_;
};

}  // namespace revisa::tray
