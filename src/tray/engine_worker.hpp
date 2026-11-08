#pragma once
// The engine of revisa-tray on its own thread. The app moves an EngineWorker to a QThread and
// talks to it only through queued calls (start, check) and its signals, so the GUI never blocks
// on the model. The engine and its sentence cache live on that thread only.

#include <QMetaType>
#include <QObject>
#include <QString>

#include <memory>

#include "core/check/check.hpp"
#include "core/infer/engine.hpp"

namespace revisa::tray {

// true if the REVISA_FAKE_* test engine will be used (only with REVISA_TEST_HOOKS): then no
// model file is needed.
bool fakeEngineRequested();

// The Spanish message of an engine error of check_text («no se pudo cargar el modelo (…)»).
QString engineErrorText(const check::Result& r);

class EngineWorker : public QObject {
    Q_OBJECT

public:
    EngineWorker();
    ~EngineWorker() override;

    // Worker thread. (Re)creates the engine: the fake one if requested, else llama.cpp with
    // modelPath and threads (0 = default_threads()); then checks a short sentence so the model
    // is loaded before the first real request. Emits ready(generation) or failed(generation, …).
    void start(int generation, const QString& modelPath, int threads);

    // Worker thread. Emits finished(id, result). Without an engine, the result carries an error.
    void check(quint64 id, const QString& text, const revisa::check::Options& options);

signals:
    void ready(int generation);
    void failed(int generation, const QString& message);
    void finished(quint64 id, const revisa::check::Result& result);

private:
    std::unique_ptr<infer::Engine> inner_;
    std::unique_ptr<infer::Engine> cached_;
};

}  // namespace revisa::tray

Q_DECLARE_METATYPE(revisa::check::Result)
