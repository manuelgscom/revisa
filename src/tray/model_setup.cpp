#include "tray/model_setup.hpp"

#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>

#include "core/fetch/fetch.hpp"

namespace revisa::tray {

namespace {

QString mb(std::uint64_t bytes) { return QString::number(fetch::size_mb(bytes)); }

}  // namespace

ModelSetup::ModelSetup(QObject* parent) : QObject(parent), spec_(fetch::default_model_spec()) {}

ModelSetup::~ModelSetup() {
    if (thread_) {
        cancel_ = true;
        thread_->wait();
        delete thread_;
    }
    delete dialog_;
}

void ModelSetup::run(const std::string& explicitModel) {
    retry_ = false;
    const std::string dir = fetch::models_dir();
    const fetch::ModelResolution res = fetch::resolve_model(explicitModel, dir, spec_);
    using S = fetch::ModelResolution::Status;
    switch (res.status) {
        case S::Ready:
            emit modelReady(QString::fromStdString(res.path));
            return;
        case S::Missing:
            emit failed(QStringLiteral("no existe el modelo configurado: %1").arg(QString::fromStdString(res.path)));
            return;
        case S::NoDir:
            emit failed(QStringLiteral("no se encuentra la carpeta de los modelos (falta la carpeta del usuario)"));
            return;
        case S::NeedsDownload:
            break;
    }
    if (qEnvironmentVariable("REVISA_TRAY_ASSUME_YES") != QLatin1String("1")) {
        QMessageBox box(QMessageBox::Question, QStringLiteral("revisa"),
                        QStringLiteral("revisa necesita descargar su modelo (%1 MB) una sola vez. ¿Descargarlo ahora?")
                            .arg(mb(spec_.size)));
        box.setInformativeText(QStringLiteral("Modelo: %1\nOrigen: %2")
                                   .arg(QString::fromStdString(spec_.name), QString::fromStdString(spec_.url)));
        QPushButton* yes = box.addButton(QStringLiteral("Descargar"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Ahora no"), QMessageBox::RejectRole);
        box.setDefaultButton(yes);
        box.exec();
        if (box.clickedButton() != yes) {
            retry_ = true;
            emit failed(QStringLiteral("falta el modelo; descárgalo desde el menú"));
            return;
        }
    }
    download();
}

void ModelSetup::download() {
    if (thread_) return;
    retry_ = false;
    cancel_ = false;
    lastPercent_ = -1;
    const std::string dir = fetch::models_dir();
    if (dir.empty()) {
        emit failed(QStringLiteral("no se encuentra la carpeta de los modelos (falta la carpeta del usuario)"));
        return;
    }
    qInfo("revisa-tray: downloading the model (%s MB)", qPrintable(mb(spec_.size)));

    if (!dialog_) {
        dialog_ = new QProgressDialog();
        dialog_->setWindowTitle(QStringLiteral("revisa"));
        dialog_->setCancelButtonText(QStringLiteral("Cancelar"));
        dialog_->setRange(0, 100);
        dialog_->setAutoClose(false);
        dialog_->setAutoReset(false);
        dialog_->setMinimumDuration(0);
        connect(dialog_, &QProgressDialog::canceled, this, [this] { cancel_ = true; });
    }
    dialog_->setLabelText(QStringLiteral("Descargando el modelo (%1 MB)…").arg(mb(spec_.size)));
    dialog_->setValue(0);
    dialog_->show();
    emit progress(0);

    const fetch::ModelSpec spec = spec_;
    thread_ = QThread::create([this, spec, dir] {
        fetch::DownloadOptions options;
        options.progress = false;
        options.ignore_interrupts = false;
        options.on_progress = [this, size = spec.size](std::uint64_t done) {
            const int percent = size > 0 ? static_cast<int>(done * 100 / size) : 0;
            if (percent != lastPercent_.exchange(percent))
                QMetaObject::invokeMethod(this, [this, percent] {
                    if (dialog_) dialog_->setValue(percent);
                    emit progress(percent);
                }, Qt::QueuedConnection);
            return !cancel_.load();
        };
        const fetch::DownloadResult d = fetch::download(spec.url, dir, spec.filename, spec.size, spec.sha256, options);
        const int status = static_cast<int>(d.status);
        const QString path = QString::fromStdString(d.path);
        const QString error = QString::fromStdString(d.error);
        QMetaObject::invokeMethod(this, [this, status, path, error] { onDownloadDone(status, path, error); },
                                  Qt::QueuedConnection);
    });
    thread_->start();
}

void ModelSetup::onDownloadDone(int status, const QString& path, const QString& error) {
    thread_->wait();
    thread_->deleteLater();
    thread_ = nullptr;
    if (dialog_) dialog_->hide();
    using S = fetch::DownloadResult::Status;
    switch (static_cast<S>(status)) {
        case S::Ok:
            qInfo("revisa-tray: model downloaded to %s", qPrintable(path));
            emit modelReady(path);
            return;
        case S::Cancelled:
        case S::Interrupted:
            retry_ = true;
            qInfo("revisa-tray: download cancelled");
            emit failed(QStringLiteral("descarga cancelada"));
            return;
        case S::Failed:
            break;
    }
    retry_ = true;
    qInfo("revisa-tray: download failed: %s", qPrintable(error));
    emit failed(QStringLiteral("no se pudo descargar el modelo: %1").arg(error));
}

}  // namespace revisa::tray
