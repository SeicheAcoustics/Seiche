#include "DGSolver.h"
#include "DGBasis2D.h"
#include "DGBasis3D.h"
#include "DGMesh2D.h"
#include "DGMesh3D.h"
#include "DGAcoustics2D.h"
#include "DGAcoustics3D.h"
#include "DGGpuCompute.h"
#include "audio/SignalProcessing.h"

#include <QDebug>
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>
#include <chrono>

namespace prs {
namespace dg {

// Resample the DG-rate IR to DEFAULT_SAMPLE_RATE (linear interpolation), then peak-normalize.
static void finalizeImpulseResponse(DGSolver::Result& r) {
    if (r.sampleRate > 0 && std::abs(r.sampleRate - DEFAULT_SAMPLE_RATE) > 1.0) {
        double ratio = static_cast<double>(DEFAULT_SAMPLE_RATE) / r.sampleRate;
        int outLen = static_cast<int>(r.impulseResponse.size() * ratio);
        std::vector<float> resampled(outLen);
        for (int i = 0; i < outLen; ++i) {
            double srcIdx = i / ratio;
            int lo = static_cast<int>(srcIdx);
            int hi = lo + 1;
            double frac = srcIdx - lo;
            if (hi >= static_cast<int>(r.impulseResponse.size())) hi = lo;
            resampled[i] = static_cast<float>(
                (1.0 - frac) * r.impulseResponse[lo]
                + frac * r.impulseResponse[hi]);
        }
        r.impulseResponse = std::move(resampled);
        r.sampleRate = static_cast<float>(DEFAULT_SAMPLE_RATE);
    }

    // Normalize
    float maxVal = 0.0f;
    for (float v : r.impulseResponse)
        maxVal = std::max(maxVal, std::abs(v));
    if (maxVal > 1e-10f) {
        float scale = 1.0f / maxVal;
        for (float& v : r.impulseResponse)
            v *= scale;
    }
}

// shortcut: fixed DOF cap, not physical RAM; upgrade to a RAM query if users hit it on big machines
static constexpr double MAX_DG_DOFS = 10e6;
static constexpr double BYTES_PER_DOF = 400.0; // rough: mesh geometry + solver fields/scratch

// Estimate the mesh size from the model bounding box before meshing; empty string if it fits.
static QString checkProblemSize(const std::vector<Vec3f>& modelVertices, double h,
                                const DGParams& params, int dim)
{
    if (modelVertices.empty()) return {};
    Vec3f lo = modelVertices[0], hi = modelVertices[0];
    for (const auto& v : modelVertices) {
        lo = lo.cwiseMin(v);
        hi = hi.cwiseMax(v);
    }
    Vec3f size = hi - lo;
    int N = params.polynomialOrder;
    double elements, Np;
    if (dim == 2) {
        elements = double(size.x()) * size.y() / (std::sqrt(3.0) / 4.0 * h * h);
        Np = (N + 1) * (N + 2) / 2.0;
    } else {
        elements = 6.0 * size.x() * size.y() * size.z() / (h * h * h); // buildMesh3D: 6 tets per cube
        Np = (N + 1) * (N + 2) * (N + 3) / 6.0;
    }
    double dofs = elements * Np;
    if (dofs <= MAX_DG_DOFS) return {};

    double fitFreq = params.maxFrequency * std::pow(MAX_DG_DOFS / dofs, 1.0 / dim);
    return QString("DG %1D run too large: ~%2M elements, ~%3 GB. "
                   "Lower max frequency to %4 Hz or less, or lower the polynomial order.")
        .arg(dim).arg(elements / 1e6, 0, 'f', 1)
        .arg(dofs * BYTES_PER_DOF / 1e9, 0, 'f', 0)
        .arg(static_cast<int>(fitFreq));
}

DGSolver::Result DGSolver::solve(
    const Vec3f& sourcePos,
    const Vec3f& listenerPos,
    const std::vector<Viewport3D::WallInfo>& walls,
    const std::vector<Vec3f>& modelVertices,
    const Bvh& bvh,
    const DGParams& params,
    std::function<void(int, const QString&)> progressCallback,
    std::function<bool()> cancelCheck)
{
    if (params.method == DGMethod::DG_2D)
        return solve2D(sourcePos, listenerPos, walls, modelVertices, params,
                       progressCallback, cancelCheck);
    else
        return solve3D(sourcePos, listenerPos, walls, modelVertices, bvh, params,
                       progressCallback, cancelCheck);
}

DGSolver::Result DGSolver::solve2D(
    const Vec3f& sourcePos,
    const Vec3f& listenerPos,
    const std::vector<Viewport3D::WallInfo>& walls,
    const std::vector<Vec3f>& modelVertices,
    const DGParams& params,
    std::function<void(int, const QString&)> progressCallback,
    std::function<bool()> cancelCheck)
{
    using Clock = std::chrono::steady_clock;
    QElapsedTimer timer;
    timer.start();

    double h = SOUND_SPEED / (params.maxFrequency * (2.0 * params.polynomialOrder + 1.0)) * 0.8;
    float sliceZ = (sourcePos.z() + listenerPos.z()) * 0.5f;

    Result refused;
    refused.error = checkProblemSize(modelVertices, h, params, 2);
    if (!refused.error.isEmpty()) return refused;

    if (cache2D_.N != params.polynomialOrder || cache2D_.h != h || cache2D_.sliceZ != sliceZ) {
        if (progressCallback)
            progressCallback(0, "DG 2D: Building basis...");

        cache2D_.N = -1;
        cache2D_.basis = buildBasis2D(params.polynomialOrder);
        qInfo() << "DG 2D basis: N=" << params.polynomialOrder
                << "Np=" << cache2D_.basis.Np << "in" << timer.elapsed() << "ms";

        if (cancelCheck && cancelCheck()) return {};

        if (progressCallback)
            progressCallback(5, "DG 2D: Extracting room polygon...");

        RoomPolygon2D polygon = extractRoomPolygon(walls, modelVertices, sliceZ);
        qInfo() << "Room polygon:" << polygon.vertices.size() << "vertices, z=" << sliceZ;

        if (progressCallback)
            progressCallback(10, QString("DG 2D: Meshing (h=%1m)...").arg(h, 0, 'f', 3));

        timer.restart();
        cache2D_.mesh = buildMesh2D(polygon, cache2D_.basis, h);
        qInfo() << "DG 2D mesh:" << cache2D_.mesh.K << "elements," << cache2D_.mesh.Nv
                << "vertices in" << timer.elapsed() << "ms";
        cache2D_.N = params.polynomialOrder;
        cache2D_.h = h;
        cache2D_.sliceZ = sliceZ;
    }
    const Basis2D& basis = cache2D_.basis;
    const Mesh2D& mesh = cache2D_.mesh;

    if (mesh.K == 0) {
        qWarning() << "DG 2D: Empty mesh, aborting";
        return {};
    }

    if (cancelCheck && cancelCheck()) return {};

    // Try GPU acceleration
    DGGpuCompute gpu;
    bool useGpu = false;
    if (gpu.initContext() && gpu.init2D(basis, mesh)) {
        useGpu = true;
        qInfo() << "DG 2D: GPU compute shaders active";
        if (progressCallback)
            progressCallback(15, QString("DG 2D: GPU mode (%1 elems, N=%2)")
                .arg(mesh.K).arg(params.polynomialOrder));
    } else {
        qInfo() << "DG 2D: GPU unavailable (" << QString::fromStdString(gpu.lastError())
                << "), using optimized CPU path";
        if (progressCallback)
            progressCallback(15, QString("DG 2D: CPU mode (%1 elems, N=%2)")
                .arg(mesh.K).arg(params.polynomialOrder));
    }

    // Wrap progress for the inner solver (15% to 95%)
    auto solverProgress = [&](int pct, const std::string& msg) {
        if (progressCallback)
            progressCallback(15 + pct * 80 / 100, QString::fromStdString(msg));
    };
    auto solverCancel = [&]() -> bool {
        return cancelCheck && cancelCheck();
    };

    timer.restart();
    Vec2d src2D(sourcePos.x(), sourcePos.y());
    Vec2d lis2D(listenerPos.x(), listenerPos.y());

    Result result;

    if (useGpu) {
        // --- GPU path: run entire time-stepping on GPU ---
        DGAcoustics2D cpuSolver(basis, mesh, params);

        int sourceElem = cpuSolver.findElement(src2D);
        int listenerElem = cpuSolver.findElement(lis2D);
        double dt = cpuSolver.computeTimeStep();
        double sourceSigma = 1.0 / (M_PI * params.maxFrequency);
        double sourceT0 = 4.0 * sourceSigma;

        // Source weights
        VecXd sourceWeights(basis.Np);
        {
            double totalW = 0.0;
            for (int i = 0; i < basis.Np; ++i) {
                double dx = mesh.x(i, sourceElem) - src2D.x();
                double dy = mesh.y(i, sourceElem) - src2D.y();
                double h2 = std::abs(mesh.J(0, sourceElem));
                sourceWeights(i) = std::exp(-(dx*dx + dy*dy) / (2.0 * h2 * 0.01));
                totalW += sourceWeights(i);
            }
            if (totalW > 1e-30) sourceWeights /= totalW;
        }

        VecXd listenerWeights(basis.Np);
        {
            double totalW = 0.0;
            for (int i = 0; i < basis.Np; ++i) {
                double dx = mesh.x(i, listenerElem) - lis2D.x();
                double dy = mesh.y(i, listenerElem) - lis2D.y();
                double w = 1.0 / (dx*dx + dy*dy + 1e-20);
                listenerWeights(i) = w;
                totalW += w;
            }
            listenerWeights /= totalW;
        }

        double simDuration = params.simulationDuration;
        if (simDuration <= 0.0) {
            double minX = mesh.VX.minCoeff(), maxX = mesh.VX.maxCoeff();
            double minY = mesh.VY.minCoeff(), maxY = mesh.VY.maxCoeff();
            double roomDiag = std::sqrt((maxX-minX)*(maxX-minX) + (maxY-minY)*(maxY-minY));
            simDuration = std::clamp(3.0 * roomDiag / SOUND_SPEED, 0.1, 2.0);
        }

        int nSteps = static_cast<int>(std::ceil(simDuration / dt));
        float dtF = static_cast<float>(dt);

        qInfo() << "DG 2D GPU: K=" << mesh.K << ", dt=" << (dt * 1e6) << "us,"
                << "steps=" << nSteps << ", duration=" << simDuration << "s";

        gpu.setSource2D(sourceElem, sourceWeights);
        gpu.setListener2D(listenerElem, listenerWeights);
        gpu.resetFields2D();
        gpu.allocImpulseResponse(nSteps);

        auto wallStart = Clock::now();
        double stepTimeSec = 0.0;
        int warmupSteps = std::min(50, nSteps);

        for (int step = 0; step < nSteps; ++step) {
            if (solverCancel()) break;

            double time = step * dt;

            for (int stage = 0; stage < 5; ++stage) {
                double stageTime = time + LSRK4_C[stage] * dt;
                double arg = (stageTime - sourceT0) / sourceSigma;
                float pulse = static_cast<float>(std::exp(-0.5 * arg * arg));

                gpu.computeRHS2D(pulse);
                gpu.rkStageUpdate2D(float(LSRK4_A[stage]), float(LSRK4_B[stage]), dtF);
            }

            gpu.recordListenerPressure2D(step);

            if (step == warmupSteps - 1) {
                gpu.finish(); // dispatches are async: sync so the ETA reflects GPU time
                auto now = Clock::now();
                stepTimeSec = std::chrono::duration<double>(now - wallStart).count() / warmupSteps;
            }

            if (step >= warmupSteps) {
                int reportEvery = std::max(1, std::min(nSteps / 200,
                    static_cast<int>(0.5 / std::max(stepTimeSec, 1e-9))));
                if (step % reportEvery == 0) {
                    gpu.finish(); // keep the CPU at most one report interval ahead (cancel stays responsive)
                    int pct = static_cast<int>(100.0 * step / nSteps);
                    double remaining = stepTimeSec * (nSteps - step);
                    int etaMin = static_cast<int>(remaining / 60.0);
                    int etaSec = static_cast<int>(remaining) % 60;
                    solverProgress(pct, "DG 2D GPU: step " + std::to_string(step)
                        + "/" + std::to_string(nSteps)
                        + " [ETA " + std::to_string(etaMin) + "m" + std::to_string(etaSec) + "s]");
                }
            }
        }

        std::vector<float> irData = gpu.readImpulseResponse(nSteps);
        gpu.cleanup();

        AcousticsResult2D acousticResult;
        acousticResult.impulseResponse = std::move(irData);
        acousticResult.sampleRate = 1.0 / dt;
        acousticResult.duration = nSteps * dt;
        acousticResult.numTimeSteps = nSteps;

        qInfo() << "DG 2D GPU solve:" << nSteps << "steps in" << timer.elapsed() << "ms";

        result.duration = static_cast<float>(acousticResult.duration);
        result.impulseResponse = std::move(acousticResult.impulseResponse);
        result.sampleRate = static_cast<float>(acousticResult.sampleRate);
    } else {
        // --- CPU path ---
        DGAcoustics2D solver(basis, mesh, params);
        AcousticsResult2D acousticResult = solver.solve(src2D, lis2D, solverProgress, solverCancel);

        qInfo() << "DG 2D CPU solve:" << acousticResult.numTimeSteps << "steps,"
                << "dt=" << (1.0 / acousticResult.sampleRate * 1e6) << "us,"
                << timer.elapsed() << "ms total";

        result.duration = static_cast<float>(acousticResult.duration);

        result.impulseResponse = std::move(acousticResult.impulseResponse);
        result.sampleRate = static_cast<float>(acousticResult.sampleRate);
    }

    finalizeImpulseResponse(result);

    if (progressCallback)
        progressCallback(100, "DG 2D: Complete");

    return result;
}

DGSolver::Result DGSolver::solve3D(
    const Vec3f& sourcePos,
    const Vec3f& listenerPos,
    const std::vector<Viewport3D::WallInfo>& walls,
    const std::vector<Vec3f>& modelVertices,
    const Bvh& bvh,
    const DGParams& params,
    std::function<void(int, const QString&)> progressCallback,
    std::function<bool()> cancelCheck)
{
    using Clock = std::chrono::steady_clock;
    QElapsedTimer timer;
    timer.start();

    double h = SOUND_SPEED / (params.maxFrequency * (2.0 * params.polynomialOrder + 1.0)) * 0.8;

    Result refused;
    refused.error = checkProblemSize(modelVertices, h, params, 3);
    if (!refused.error.isEmpty()) return refused;

    if (cache3D_.N != params.polynomialOrder || cache3D_.h != h) {
        if (progressCallback)
            progressCallback(0, "DG 3D: Building basis...");

        cache3D_.N = -1;
        cache3D_.basis = buildBasis3D(params.polynomialOrder);
        qInfo() << "DG 3D basis: N=" << params.polynomialOrder
                << "Np=" << cache3D_.basis.Np << "in" << timer.elapsed() << "ms";

        if (cancelCheck && cancelCheck()) return {};

        if (progressCallback)
            progressCallback(5, QString("DG 3D: Meshing (h=%1m)...").arg(h, 0, 'f', 3));

        timer.restart();
        cache3D_.mesh = buildMesh3D(walls, modelVertices, bvh, cache3D_.basis, h);
        qInfo() << "DG 3D mesh:" << cache3D_.mesh.K << "elements," << cache3D_.mesh.Nv
                << "vertices in" << timer.elapsed() << "ms";
        cache3D_.N = params.polynomialOrder;
        cache3D_.h = h;
    }
    const Basis3D& basis = cache3D_.basis;
    const Mesh3D& mesh = cache3D_.mesh;

    if (mesh.K == 0) {
        qWarning() << "DG 3D: Empty mesh, aborting";
        return {};
    }

    if (cancelCheck && cancelCheck()) return {};

    // Try GPU
    DGGpuCompute gpu;
    bool useGpu = false;
    if (gpu.initContext() && gpu.init3D(basis, mesh)) {
        useGpu = true;
        qInfo() << "DG 3D: GPU compute shaders active";
        if (progressCallback)
            progressCallback(15, QString("DG 3D: GPU mode (%1 elems, N=%2)")
                .arg(mesh.K).arg(params.polynomialOrder));
    } else {
        qInfo() << "DG 3D: GPU unavailable (" << QString::fromStdString(gpu.lastError())
                << "), using optimized CPU path";
        if (progressCallback)
            progressCallback(15, QString("DG 3D: CPU mode (%1 elems, N=%2)")
                .arg(mesh.K).arg(params.polynomialOrder));
    }

    auto solverProgress = [&](int pct, const std::string& msg) {
        if (progressCallback)
            progressCallback(15 + pct * 80 / 100, QString::fromStdString(msg));
    };
    auto solverCancel = [&]() -> bool {
        return cancelCheck && cancelCheck();
    };

    timer.restart();
    Vec3d src3D(sourcePos.x(), sourcePos.y(), sourcePos.z());
    Vec3d lis3D(listenerPos.x(), listenerPos.y(), listenerPos.z());

    Result result;

    if (useGpu) {
        DGAcoustics3D cpuSolver(basis, mesh, params);

        int sourceElem = cpuSolver.findElement(src3D);
        int listenerElem = cpuSolver.findElement(lis3D);
        double dt = cpuSolver.computeTimeStep();
        double sourceSigma = 1.0 / (M_PI * params.maxFrequency);
        double sourceT0 = 4.0 * sourceSigma;

        VecXd sourceWeights(basis.Np);
        {
            double totalW = 0.0;
            for (int i = 0; i < basis.Np; ++i) {
                double dx = mesh.x(i, sourceElem) - src3D.x();
                double dy = mesh.y(i, sourceElem) - src3D.y();
                double dz = mesh.z(i, sourceElem) - src3D.z();
                double h2 = std::abs(mesh.J(0, sourceElem));
                sourceWeights(i) = std::exp(-(dx*dx + dy*dy + dz*dz) / (2.0 * h2 * 0.01));
                totalW += sourceWeights(i);
            }
            if (totalW > 1e-30) sourceWeights /= totalW;
        }

        VecXd listenerWeights(basis.Np);
        {
            double totalW = 0.0;
            for (int i = 0; i < basis.Np; ++i) {
                double dx = mesh.x(i, listenerElem) - lis3D.x();
                double dy = mesh.y(i, listenerElem) - lis3D.y();
                double dz = mesh.z(i, listenerElem) - lis3D.z();
                double w = 1.0 / (dx*dx + dy*dy + dz*dz + 1e-20);
                listenerWeights(i) = w;
                totalW += w;
            }
            listenerWeights /= totalW;
        }

        double simDuration = params.simulationDuration;
        if (simDuration <= 0.0) {
            double minX = mesh.VX.minCoeff(), maxX = mesh.VX.maxCoeff();
            double minY = mesh.VY.minCoeff(), maxY = mesh.VY.maxCoeff();
            double minZ = mesh.VZ.minCoeff(), maxZ = mesh.VZ.maxCoeff();
            double diag = std::sqrt((maxX-minX)*(maxX-minX) + (maxY-minY)*(maxY-minY) + (maxZ-minZ)*(maxZ-minZ));
            simDuration = std::clamp(3.0 * diag / SOUND_SPEED, 0.1, 2.0);
        }

        int nSteps = static_cast<int>(std::ceil(simDuration / dt));
        float dtF = static_cast<float>(dt);

        qInfo() << "DG 3D GPU: K=" << mesh.K << ", dt=" << (dt * 1e6) << "us,"
                << "steps=" << nSteps << ", duration=" << simDuration << "s";

        gpu.setSource3D(sourceElem, sourceWeights);
        gpu.setListener3D(listenerElem, listenerWeights);
        gpu.resetFields3D();
        gpu.allocImpulseResponse(nSteps);
        auto wallStart = Clock::now();
        double stepTimeSec = 0.0;
        int warmupSteps = std::min(50, nSteps);

        for (int step = 0; step < nSteps; ++step) {
            if (solverCancel()) break;
            double time = step * dt;

            for (int stage = 0; stage < 5; ++stage) {
                double stageTime = time + LSRK4_C[stage] * dt;
                double arg = (stageTime - sourceT0) / sourceSigma;
                float pulse = static_cast<float>(std::exp(-0.5 * arg * arg));
                gpu.computeRHS3D(pulse);
                gpu.rkStageUpdate3D(float(LSRK4_A[stage]), float(LSRK4_B[stage]), dtF);
            }

            gpu.recordListenerPressure3D(step);

            if (step == warmupSteps - 1) {
                gpu.finish(); // dispatches are async: sync so the ETA reflects GPU time
                auto now = Clock::now();
                stepTimeSec = std::chrono::duration<double>(now - wallStart).count() / warmupSteps;
            }
            if (step >= warmupSteps) {
                int reportEvery = std::max(1, std::min(nSteps / 200,
                    static_cast<int>(0.5 / std::max(stepTimeSec, 1e-9))));
                if (step % reportEvery == 0) {
                    gpu.finish(); // keep the CPU at most one report interval ahead (cancel stays responsive)
                    int pct = static_cast<int>(100.0 * step / nSteps);
                    double remaining = stepTimeSec * (nSteps - step);
                    int etaMin = static_cast<int>(remaining / 60.0);
                    int etaSec = static_cast<int>(remaining) % 60;
                    solverProgress(pct, "DG 3D GPU: step " + std::to_string(step)
                        + "/" + std::to_string(nSteps)
                        + " [ETA " + std::to_string(etaMin) + "m" + std::to_string(etaSec) + "s]");
                }
            }
        }

        std::vector<float> irData = gpu.readImpulseResponse(nSteps);
        gpu.cleanup();

        result.duration = static_cast<float>(nSteps * dt);
        result.impulseResponse = std::move(irData);
        result.sampleRate = static_cast<float>(1.0 / dt);

        qInfo() << "DG 3D GPU solve:" << nSteps << "steps in" << timer.elapsed() << "ms";
    } else {
        DGAcoustics3D solver(basis, mesh, params);
        AcousticsResult3D acousticResult = solver.solve(src3D, lis3D, solverProgress, solverCancel);

        qInfo() << "DG 3D CPU solve:" << acousticResult.numTimeSteps << "steps,"
                << "dt=" << (1.0 / acousticResult.sampleRate * 1e6) << "us,"
                << timer.elapsed() << "ms total";

        result.duration = static_cast<float>(acousticResult.duration);

        result.impulseResponse = std::move(acousticResult.impulseResponse);
        result.sampleRate = static_cast<float>(acousticResult.sampleRate);
    }

    finalizeImpulseResponse(result);

    if (progressCallback)
        progressCallback(100, "DG 3D: Complete");

    return result;
}

} // namespace dg
} // namespace prs
