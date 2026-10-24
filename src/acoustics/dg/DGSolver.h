#pragma once

#include "DGTypes.h"
#include "DGBasis2D.h"
#include "DGBasis3D.h"
#include "DGMesh2D.h"
#include "DGMesh3D.h"
#include "core/Types.h"
#include "rendering/Viewport3D.h"
#include "acoustics/Bvh.h"

#include <vector>
#include <functional>
#include <QString>

namespace prs {
namespace dg {

// Reuse one DGSolver per room: the basis and mesh are cached across solve() calls.
class DGSolver {
public:
    struct Result {
        std::vector<float> impulseResponse;
        float sampleRate = 0.0f;
        float duration = 0.0f;
        QString error; // set when the run was refused (e.g. too large)
    };

    // Solve for a single source-listener pair.
    // Returns an impulse response suitable for convolution.
    Result solve(
        const Vec3f& sourcePos,
        const Vec3f& listenerPos,
        const std::vector<Viewport3D::WallInfo>& walls,
        const std::vector<Vec3f>& modelVertices,
        const Bvh& bvh,
        const DGParams& params,
        std::function<void(int, const QString&)> progressCallback = nullptr,
        std::function<bool()> cancelCheck = nullptr);

private:
    Result solve2D(
        const Vec3f& sourcePos,
        const Vec3f& listenerPos,
        const std::vector<Viewport3D::WallInfo>& walls,
        const std::vector<Vec3f>& modelVertices,
        const DGParams& params,
        std::function<void(int, const QString&)> progressCallback,
        std::function<bool()> cancelCheck);

    Result solve3D(
        const Vec3f& sourcePos,
        const Vec3f& listenerPos,
        const std::vector<Viewport3D::WallInfo>& walls,
        const std::vector<Vec3f>& modelVertices,
        const Bvh& bvh,
        const DGParams& params,
        std::function<void(int, const QString&)> progressCallback,
        std::function<bool()> cancelCheck);

    // The basis and mesh depend only on the room, order and h (2D: also the slice height),
    // not on source/listener positions, so consecutive pairs skip rebuilding them.
    struct Cache2D { int N = -1; double h = 0.0; float sliceZ = 0.0f; Basis2D basis; Mesh2D mesh; };
    struct Cache3D { int N = -1; double h = 0.0; Basis3D basis; Mesh3D mesh; };
    Cache2D cache2D_;
    Cache3D cache3D_;
};

} // namespace dg
} // namespace prs
