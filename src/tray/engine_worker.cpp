#include "tray/engine_worker.hpp"

#include <QByteArray>
#include <QtGlobal>

#include <string>

#include "core/infer/cached_engine.hpp"
#include "core/infer/fake_engine.hpp"
#include "core/infer/llama_engine.hpp"

namespace revisa::tray {

bool fakeEngineRequested() {
#if REVISA_TEST_HOOKS
    for (const char* v : {"REVISA_FAKE_ERROR", "REVISA_FAKE_MAP", "REVISA_FAKE_OUTPUT"})
        if (qEnvironmentVariableIsSet(v)) return true;
#endif
    return false;
}

QString engineErrorText(const check::Result& r) {
    const QString detail = QString::fromStdString(r.error_detail);
    if (r.error == "load_failed") return QStringLiteral("no se pudo cargar el modelo (%1)").arg(detail);
    if (r.error == "decode_failed") return QStringLiteral("falló la inferencia del modelo (%1)").arg(detail);
    return QStringLiteral("error del motor: %1 (%2)").arg(QString::fromStdString(r.error), detail);
}

EngineWorker::EngineWorker() { qRegisterMetaType<check::Result>(); }

EngineWorker::~EngineWorker() {
    cached_.reset();  // wraps inner_: goes first
    inner_.reset();
}

void EngineWorker::start(int generation, const QString& modelPath, int threads) {
    cached_.reset();
    inner_.reset();
    std::string modelId;
#if REVISA_TEST_HOOKS
    inner_ = infer::fake_engine_from_env();
    if (inner_) modelId = "fake";
#endif
    if (!inner_) {
        if (modelPath.isEmpty()) {
            emit failed(generation, QStringLiteral("no hay modelo"));
            return;
        }
        infer::LlamaOptions options;
        options.model_path = modelPath.toStdString();
        options.threads = threads;
        options.debug = qgetenv("REVISA_DEBUG") == "1";
        inner_ = infer::make_llama_engine(options);
        modelId = options.model_path;
    }
    cached_ = std::make_unique<infer::CachedEngine>(*inner_, modelId);

    // Warm-up: the llama engine loads the model on its first call.
    const check::Result r = check::check_text("Hola, ¿qué tal?", *cached_);
    if (!r.error.empty()) {
        emit failed(generation, engineErrorText(r));
        return;
    }
    emit ready(generation);
}

void EngineWorker::check(quint64 id, const QString& text, const check::Options& options) {
    check::Result r;
    if (!cached_) {
        r.error = "load_failed";
        r.error_detail = "no engine";
    } else {
        const QByteArray utf8 = text.toUtf8();
        r = check::check_text(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())), *cached_,
                              options);
    }
    emit finished(id, r);
}

}  // namespace revisa::tray
