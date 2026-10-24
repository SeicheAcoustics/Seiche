#include "SimulationWorker.h"
#include "Wall.h"
#include "Bvh.h"
#include "ImageSourceMethod.h"
#include "RayTracer.h"
#include "RoomImpulseResponse.h"
#include "RenderExports.h"
#include "AcousticMetrics.h"
#include "dg/DGSolver.h"
#include "rendering/RayPicking.h"
#include "rendering/MeshSimplifier.h"
#include "audio/AudioFile.h"
#include "audio/SignalProcessing.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

#include <map>

namespace prs {

namespace {

constexpr float HEAD_RADIUS  = 0.0875f;
constexpr float EAR_OFFSET   = 0.075f;
constexpr int   SIMPLIFICATION_THRESHOLD = 500;
constexpr float SIMPLIFICATION_TARGET_RATIO = 0.3f;

void earPositions(const Listener* listener, Vec3f& headCenter, Vec3f& leftEar, Vec3f& rightEar) {
    if (!listener) return;
    headCenter = listener->position;
    Vec3f forward(1.0f, 0.0f, 0.0f);
    if (listener->orientation.has_value()) {
        const Vec3f& o = listener->orientation.value();
        if (o.squaredNorm() > 1e-12f)
            forward = o.normalized();
    }
    Vec3f up(0.0f, 0.0f, 1.0f);
    Vec3f right = forward.cross(up);
    if (right.squaredNorm() < 1e-12f)
        right = Vec3f(0.0f, 1.0f, 0.0f);
    else
        right.normalize();
    leftEar  = headCenter - EAR_OFFSET * right;
    rightEar = headCenter + EAR_OFFSET * right;
}

std::vector<ImageSource> filterByHeadOcclusion(const std::vector<ImageSource>& sources,
                                                 const Vec3f& earPos, const Vec3f& headCenter) {
    std::vector<ImageSource> out;
    for (const auto& is : sources) {
        if (!RayPicking::segmentPassesThroughSphere(is.position, earPos, headCenter, HEAD_RADIUS))
            out.push_back(is);
    }
    return out;
}

std::vector<AcousticSurface> buildAcousticSurfaces(
    const std::vector<Viewport3D::WallInfo>& wallInfos,
    const std::vector<Vec3f>& modelVertices)
{
    std::vector<AcousticSurface> surfaces;
    for (const auto& wi : wallInfos) {
        Vec3f normalSum = Vec3f::Zero();
        Vec3f centroidSum = Vec3f::Zero();
        float totalArea = 0.0f;
        int validCount = 0;
        Vec3f firstPoint = Vec3f::Zero();

        for (int triIdx : wi.triangleIndices) {
            int base = triIdx * 3;
            if (base + 2 >= static_cast<int>(modelVertices.size())) continue;

            Vec3f v0 = modelVertices[base];
            Vec3f v1 = modelVertices[base + 1];
            Vec3f v2 = modelVertices[base + 2];
            Vec3f e1 = v1 - v0;
            Vec3f e2 = v2 - v0;
            Vec3f n = e1.cross(e2);
            float area = 0.5f * n.norm();
            if (area < 1e-8f) continue;

            normalSum += n.normalized() * area;
            centroidSum += (v0 + v1 + v2) / 3.0f * area;
            totalArea += area;
            if (validCount == 0) firstPoint = v0;
            validCount++;
        }

        if (totalArea < 1e-8f) continue;

        AcousticSurface s;
        s.normal = normalSum.normalized();
        s.centroid = centroidSum / totalArea;
        s.planePoint = firstPoint;
        s.area = totalArea;
        s.absorption = wi.absorption;
        s.scattering = wi.scattering;
        surfaces.push_back(s);
    }
    return surfaces;
}

} // namespace

SimulationWorker::SimulationWorker(const Params& params, QObject* parent)
    : QObject(parent), params_(params) {}

void SimulationWorker::cancel() { cancelled_.store(true); }
bool SimulationWorker::isCancelled() const { return cancelled_.load(); }

void SimulationWorker::process() {
    QElapsedTimer totalTimer;
    totalTimer.start();

    auto& scene = params_.scene;
    if (!scene.hasMinimumObjects()) {
        emit error("Need at least 1 source and 1 listener");
        return;
    }

    emit progressChanged(0, "Building room geometry...");

    QElapsedTimer phaseTimer;
    phaseTimer.start();

    // Build walls with surface IDs for potential simplification
    std::vector<Wall> walls;
    std::vector<int> wallSurfaceIds;
    int surfaceIdx = 0;
    for (auto& wi : params_.walls) {
        for (int triIdx : wi.triangleIndices) {
            int baseVert = triIdx * 3;
            if (baseVert + 2 >= static_cast<int>(params_.modelVertices.size())) continue;

            Wall wall;
            wall.triangle.v0 = params_.modelVertices[baseVert];
            wall.triangle.v1 = params_.modelVertices[baseVert + 1];
            wall.triangle.v2 = params_.modelVertices[baseVert + 2];
            Vec3f e1 = wall.triangle.v1 - wall.triangle.v0;
            Vec3f e2 = wall.triangle.v2 - wall.triangle.v0;
            wall.triangle.normal = e1.cross(e2).normalized();
            wall.absorption = wi.absorption;
            wall.scattering = wi.scattering;

            if (wall.area() > 1e-8f) {
                walls.push_back(wall);
                wallSurfaceIds.push_back(surfaceIdx);
            }
        }
        surfaceIdx++;
    }

    qInfo() << "Built" << walls.size() << "wall triangles in" << phaseTimer.elapsed() << "ms";

    // Mesh simplification for dense geometry
    if (static_cast<int>(walls.size()) > SIMPLIFICATION_THRESHOLD) {
        phaseTimer.restart();
        int target = static_cast<int>(walls.size() * SIMPLIFICATION_TARGET_RATIO);
        target = std::max(target, SIMPLIFICATION_THRESHOLD);
        emit progressChanged(2, QString("Simplifying mesh (%1 -> %2 triangles)...")
            .arg(walls.size()).arg(target));
        walls = MeshSimplifier::simplify(walls, wallSurfaceIds, target);
        qInfo() << "Simplified to" << walls.size() << "triangles in" << phaseTimer.elapsed() << "ms";
    }

    if (isCancelled()) { emit error("Cancelled"); return; }

    // Build BVH
    phaseTimer.restart();
    emit progressChanged(5, "Building BVH acceleration structure...");
    Bvh bvh;
    bvh.build(walls);
    qInfo() << "BVH built in" << phaseTimer.elapsed() << "ms";

    if (isCancelled()) { emit error("Cancelled"); return; }

    QString outputDir = params_.outputDir;
    if (outputDir.isEmpty()) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
        QString appDir = QCoreApplication::applicationDirPath();
        outputDir = QDir(appDir).filePath("outputs/simulation_" + timestamp);
    }
    QDir().mkpath(outputDir);
    scene.saveToFile(QDir(outputDir).filePath("scene.json"));

    // Build list of listener indices to render
    std::vector<int> listenerIndices = params_.selectedListenerIndices;
    if (listenerIndices.empty()) {
        for (int i = 0; i < scene.listenerCount(); ++i)
            listenerIndices.push_back(i);
    }
    int numListeners = static_cast<int>(listenerIndices.size());

    // ========== DG solver path ==========
    if (params_.method == SimMethod::DG_2D || params_.method == SimMethod::DG_3D) {
        dg::DGParams dgParams;
        dgParams.method = (params_.method == SimMethod::DG_2D)
                        ? dg::DGMethod::DG_2D : dg::DGMethod::DG_3D;
        dgParams.polynomialOrder = params_.dgPolyOrder;
        dgParams.maxFrequency = params_.dgMaxFrequency;

        dg::DGSolver dgSolver;
        int totalPairsDG = scene.soundSourceCount() * numListeners;
        int pairsDoneDG = 0;

        for (int si = 0; si < scene.soundSourceCount(); ++si) {
            auto* source = scene.getSoundSource(si);
            if (!source) continue;
            if (isCancelled()) { emit error("Cancelled"); return; }

            AudioFile audioFile;
            if (!audioFile.load(QString::fromStdString(source->audioFile))) {
                qWarning() << "Failed to load:" << QString::fromStdString(source->audioFile);
                pairsDoneDG += numListeners;
                continue;
            }
            audioFile.applyVolume(source->volume);
            int fs = audioFile.sampleRate();
            const std::vector<float>& inputSamples = audioFile.samples();
            if (inputSamples.empty()) { pairsDoneDG += numListeners; continue; }

            for (int li : listenerIndices) {
                if (isCancelled()) { emit error("Cancelled"); return; }
                auto* listener = scene.getListener(li);
                if (!listener) continue;

                int pct = 10 + (pairsDoneDG * 80) / std::max(totalPairsDG, 1);
                emit progressChanged(pct, QString("DG: %1 -> %2")
                    .arg(QString::fromStdString(source->name),
                         QString::fromStdString(listener->name)));

                auto dgProgress = [this, pct, totalPairsDG](int subPct, const QString& msg) {
                    emit progressChanged(pct + subPct * 80 / (100 * std::max(totalPairsDG, 1)), msg);
                };
                auto dgCancel = [this]() -> bool { return isCancelled(); };

                auto dgResult = dgSolver.solve(
                    source->position, listener->position,
                    params_.walls, params_.modelVertices, bvh,
                    dgParams, dgProgress, dgCancel);

                if (isCancelled()) { emit error("Cancelled"); return; } // don't save a partial IR
                if (!dgResult.error.isEmpty()) { emit error(dgResult.error); return; }
                if (dgResult.impulseResponse.empty()) {
                    ++pairsDoneDG;
                    continue;
                }

                auto outMono = SignalProcessing::fftConvolve(inputSamples, dgResult.impulseResponse);
                SignalProcessing::normalize(outMono, 0.95f);

                // Save mono (DG doesn't inherently produce stereo)
                QString listenerName = QString::fromStdString(listener->name).replace(' ', '_');
                QString sourceName = QString::fromStdString(source->name).replace(' ', '_');
                QString filename = QString("%1_from_%2.wav").arg(listenerName, sourceName);
                if (!RenderExports::saveMonoWav(QDir(outputDir).filePath(filename), fs, outMono))
                    qWarning() << "Failed to write" << filename;

                ++pairsDoneDG;
            }
        }

        qInfo() << "=== DG SIMULATION COMPLETE in" << totalTimer.elapsed() << "ms ===";
        emit progressChanged(100, QString("DG simulation complete! (%1s)")
            .arg(totalTimer.elapsed() / 1000.0, 0, 'f', 1));
        emit finished(outputDir);
        return;
    }

    // ========== Ray-tracing path (existing) ==========
    // Acoustic surfaces are only used by the ISM (the DG path returned above)
    phaseTimer.restart();
    auto acousticSurfaces = buildAcousticSurfaces(params_.walls, params_.modelVertices);
    qInfo() << "Built" << acousticSurfaces.size() << "acoustic surfaces for ISM in" << phaseTimer.elapsed() << "ms";

    int totalPairs = scene.soundSourceCount() * numListeners;
    int pairsDone = 0;
    int mixedFs = 44100;

    struct MixedStereo { std::vector<float> left; std::vector<float> right; };
    std::map<int, MixedStereo> mixedPerListener;
    QJsonArray metricsArray;

    for (int si = 0; si < scene.soundSourceCount(); ++si) {
        auto* source = scene.getSoundSource(si);
        if (!source) continue;

        if (isCancelled()) { emit error("Cancelled"); return; }

        emit progressChanged(10 + pairsDone * 80 / std::max(totalPairs, 1),
            QString("Loading audio: %1").arg(QString::fromStdString(source->name)));

        AudioFile audioFile;
        if (!audioFile.load(QString::fromStdString(source->audioFile))) {
            qWarning() << "Failed to load:" << QString::fromStdString(source->audioFile);
            pairsDone += numListeners;
            continue;
        }
        audioFile.applyVolume(source->volume);
        int fs = audioFile.sampleRate();
        mixedFs = fs;
        Vec3f sourcePos = source->position;
        const std::vector<float>& inputSamples = audioFile.samples();
        if (inputSamples.empty()) {
            qWarning() << "No samples loaded for source, skipping";
            pairsDone += numListeners;
            continue;
        }

        for (int li : listenerIndices) {
            if (isCancelled()) { emit error("Cancelled"); return; }

            auto* listener = scene.getListener(li);
            if (!listener) continue;

            int pct = 10 + (pairsDone * 80) / std::max(totalPairs, 1);
            emit progressChanged(pct, QString("Processing %1 -> %2 (stereo)")
                .arg(QString::fromStdString(source->name),
                     QString::fromStdString(listener->name)));

            Vec3f headCenter, leftEar, rightEar;
            earPositions(listener, headCenter, leftEar, rightEar);

            // ISM using surface-level walls + BVH visibility
            phaseTimer.restart();
            ImageSourceMethod ism;
            auto isLeft  = ism.compute(sourcePos, leftEar,  acousticSurfaces, bvh, params_.maxOrder);
            auto isRight = ism.compute(sourcePos, rightEar, acousticSurfaces, bvh, params_.maxOrder);
            isLeft  = filterByHeadOcclusion(isLeft,  leftEar,  headCenter);
            isRight = filterByHeadOcclusion(isRight, rightEar, headCenter);
            qInfo() << "  ISM:" << phaseTimer.elapsed() << "ms"
                    << "(left:" << isLeft.size() << "right:" << isRight.size() << "sources)";

            if (isCancelled()) { emit error("Cancelled"); return; }

            // Ray tracing with BVH
            phaseTimer.restart();
            RayTracer rt;
            auto rayLeft  = rt.trace(sourcePos, leftEar,  walls, bvh, params_.nRays, 0.5f, 100, 1e-6f, &headCenter, HEAD_RADIUS, params_.airAbsorption);
            auto rayRight = rt.trace(sourcePos, rightEar, walls, bvh, params_.nRays, 0.5f, 100, 1e-6f, &headCenter, HEAD_RADIUS, params_.airAbsorption);
            qInfo() << "  Ray tracing:" << phaseTimer.elapsed() << "ms"
                    << "(left:" << rayLeft.size() << "right:" << rayRight.size() << "contributions)";

            if (isCancelled()) { emit error("Cancelled"); return; }

            phaseTimer.restart();
            RoomImpulseResponse rir;
            auto multibandLeft  = rir.computeMultiband(isLeft,  rayLeft,  fs);
            auto multibandRight = rir.computeMultiband(isRight, rayRight, fs);

            auto impulseLeft  = SignalProcessing::combineMultibandRIR(multibandLeft,  fs);
            auto impulseRight = SignalProcessing::combineMultibandRIR(multibandRight, fs);

            auto outLeft  = SignalProcessing::fftConvolve(inputSamples, impulseLeft);
            auto outRight = SignalProcessing::fftConvolve(inputSamples, impulseRight);
            qInfo() << "  RIR + convolution:" << phaseTimer.elapsed() << "ms";

            auto rtResult = AcousticMetrics::computeRT(impulseLeft, fs);
            auto splResult = AcousticMetrics::computeSPL(
                source->volume, isLeft, rayLeft, params_.nRays);

            QJsonObject pairMetrics;
            pairMetrics["source"] = QString::fromStdString(source->name);
            pairMetrics["listener"] = QString::fromStdString(listener->name);
            if (rtResult.valid) {
                QJsonObject rt;
                rt["T20"] = static_cast<double>(rtResult.t20);
                rt["T30"] = static_cast<double>(rtResult.t30);
                rt["EDT"] = static_cast<double>(rtResult.edt);
                pairMetrics["reverberation_time"] = rt;
            }
            if (splResult.valid) {
                QJsonObject spl;
                spl["direct_dB"]    = static_cast<double>(splResult.directSPL);
                spl["reflected_dB"] = static_cast<double>(splResult.reflectedSPL);
                spl["total_dB"]     = static_cast<double>(splResult.totalSPL);
                pairMetrics["sound_pressure_level"] = spl;
            }
            metricsArray.append(pairMetrics);
            qInfo() << "  Metrics: T20=" << rtResult.t20 << "s T30=" << rtResult.t30
                    << "s SPL=" << splResult.totalSPL << "dB";

            if (outLeft.empty() && outRight.empty())
                continue;
            if (outLeft.empty()) outLeft.resize(outRight.size(), 0.0f);
            if (outRight.empty()) outRight.resize(outLeft.size(), 0.0f);
            size_t outLen = std::max(outLeft.size(), outRight.size());
            outLeft.resize(outLen, 0.0f);
            outRight.resize(outLen, 0.0f);

            SignalProcessing::normalize(outLeft,  0.95f);
            SignalProcessing::normalize(outRight, 0.95f);

            QString listenerName = QString::fromStdString(listener->name).replace(' ', '_');
            QString sourceName = QString::fromStdString(source->name).replace(' ', '_');
            QString filename = QString("%1_from_%2.wav").arg(listenerName, sourceName);
            if (!RenderExports::saveStereoWav(QDir(outputDir).filePath(filename), fs, outLeft, outRight))
                qWarning() << "Failed to write" << filename;

            MixedStereo& mix = mixedPerListener[li];
            size_t n = std::max(mix.left.size(), outLen);
            mix.left.resize(n, 0.0f);
            mix.right.resize(n, 0.0f);
            for (size_t i = 0; i < outLeft.size(); ++i) mix.left[i] += outLeft[i];
            for (size_t i = 0; i < outRight.size(); ++i) mix.right[i] += outRight[i];

            ++pairsDone;
        }
    }

    if (scene.soundSourceCount() > 1) {
        for (int li : listenerIndices) {
            auto* listener = scene.getListener(li);
            if (!listener || mixedPerListener.find(li) == mixedPerListener.end()
                || mixedPerListener[li].left.empty()) continue;
            MixedStereo& m = mixedPerListener[li];
            float maxVal = 0.0f;
            for (float s : m.left)  maxVal = std::max(maxVal, std::abs(s));
            for (float s : m.right) maxVal = std::max(maxVal, std::abs(s));
            if (maxVal > 1.0f) {
                for (float& s : m.left)  s /= maxVal;
                for (float& s : m.right) s /= maxVal;
            }
            QString listenerName = QString::fromStdString(listener->name).replace(' ', '_');
            RenderExports::saveStereoWav(QDir(outputDir).filePath(listenerName + "_mixed.wav"), mixedFs, m.left, m.right);
        }
    }

    // Save metrics JSON
    if (!metricsArray.isEmpty()) {
        QJsonObject metricsRoot;
        metricsRoot["version"] = "1.0.0";
        metricsRoot["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        metricsRoot["sample_rate"] = params_.sampleRate;
        metricsRoot["pairs"] = metricsArray;

        if (RenderExports::saveJsonObject(QDir(outputDir).filePath("metrics.json"), metricsRoot))
            qInfo() << "Saved metrics for" << metricsArray.size() << "source-listener pairs";
        RenderExports::saveJsonObject(QDir(outputDir).filePath("summary.json"),
                                      RenderExports::buildMetricsSummary(metricsArray, params_.sampleRate));
        RenderExports::saveMetricsCsv(QDir(outputDir).filePath("metrics.csv"), metricsArray);
    }

    qInfo() << "=== SIMULATION COMPLETE in" << totalTimer.elapsed() << "ms ===";
    emit progressChanged(100, QString("Simulation complete! (%1s)").arg(totalTimer.elapsed() / 1000.0, 0, 'f', 1));
    emit finished(outputDir);
}

} // namespace prs
