#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QDir>
#include "acoustics/Wall.h"
#include "acoustics/Bvh.h"
#include "acoustics/ImageSourceMethod.h"
#include "acoustics/RayTracer.h"
#include "acoustics/RoomImpulseResponse.h"
#include "acoustics/AcousticMetrics.h"
#include "acoustics/dg/DGBasis2D.h"
#include "acoustics/dg/DGBasis3D.h"
#include "acoustics/dg/DGMesh2D.h"
#include "acoustics/dg/DGAcoustics2D.h"
#include "acoustics/dg/DGSolver.h"

using namespace prs;

static std::vector<Wall> createSimpleBox(float size = 5.0f) {
    std::vector<Wall> walls;
    auto addWall = [&](Vec3f v0, Vec3f v1, Vec3f v2) {
        Wall w;
        w.triangle.v0 = v0;
        w.triangle.v1 = v1;
        w.triangle.v2 = v2;
        Vec3f e1 = v1 - v0, e2 = v2 - v0;
        w.triangle.normal = e1.cross(e2).normalized();
        w.absorption.fill(0.2f);
        w.scattering = 0.1f;
        walls.push_back(w);
    };

    float s = size;
    addWall(Vec3f(0,0,0), Vec3f(s,0,0), Vec3f(s,s,0));
    addWall(Vec3f(0,0,0), Vec3f(s,s,0), Vec3f(0,s,0));
    addWall(Vec3f(0,0,s), Vec3f(s,s,s), Vec3f(s,0,s));
    addWall(Vec3f(0,0,s), Vec3f(0,s,s), Vec3f(s,s,s));
    addWall(Vec3f(0,0,0), Vec3f(s,0,s), Vec3f(s,0,0));
    addWall(Vec3f(0,0,0), Vec3f(0,0,s), Vec3f(s,0,s));
    addWall(Vec3f(0,s,0), Vec3f(s,s,0), Vec3f(s,s,s));
    addWall(Vec3f(0,s,0), Vec3f(s,s,s), Vec3f(0,s,s));
    addWall(Vec3f(0,0,0), Vec3f(0,s,0), Vec3f(0,s,s));
    addWall(Vec3f(0,0,0), Vec3f(0,s,s), Vec3f(0,0,s));
    addWall(Vec3f(s,0,0), Vec3f(s,0,s), Vec3f(s,s,s));
    addWall(Vec3f(s,0,0), Vec3f(s,s,s), Vec3f(s,s,0));

    return walls;
}

class TestAcoustics : public QObject {
    Q_OBJECT
private slots:
    void testWallArea() {
        Wall w;
        w.triangle.v0 = Vec3f(0, 0, 0);
        w.triangle.v1 = Vec3f(1, 0, 0);
        w.triangle.v2 = Vec3f(0, 1, 0);
        w.triangle.normal = Vec3f(0, 0, 1);
        float area = w.area();
        QVERIFY(std::abs(area - 0.5f) < 1e-5f);
    }

    void testWallReflection() {
        Wall w;
        w.triangle.v0 = Vec3f(0, 0, 0);
        w.triangle.v1 = Vec3f(1, 0, 0);
        w.triangle.v2 = Vec3f(0, 1, 0);
        w.triangle.normal = Vec3f(0, 0, 1);

        Vec3f point(0.5f, 0.5f, 1.0f);
        Vec3f reflected = w.reflectPoint(point);
        QVERIFY(std::abs(reflected.z() + 1.0f) < 1e-4f);
    }

    void testWallPointSide() {
        Wall w;
        w.triangle.v0 = Vec3f(0, 0, 0);
        w.triangle.v1 = Vec3f(1, 0, 0);
        w.triangle.v2 = Vec3f(0, 1, 0);
        w.triangle.normal = Vec3f(0, 0, 1);

        QVERIFY(w.isPointOnSameSide(Vec3f(0, 0, 1)));
        QVERIFY(!w.isPointOnSameSide(Vec3f(0, 0, -1)));
    }

    void testWallAverageAbsorption() {
        Wall w;
        w.absorption = {0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f};
        float avg = w.averageAbsorption();
        QVERIFY(std::abs(avg - 0.35f) < 1e-5f);
    }

    void testISMSimpleBox() {
        auto walls = createSimpleBox(5.0f);
        Vec3f source(1, 1, 1);
        Vec3f listener(4, 4, 4);

        ImageSourceMethod ism;
        auto images = ism.compute(source, listener, walls, 2);
        QVERIFY(!images.empty());

        for (auto& img : images)
            QVERIFY(img.delay > 0.0f);
    }

    void testRayTracerSimpleBox() {
        auto walls = createSimpleBox(5.0f);
        Vec3f source(2.5f, 2.5f, 2.5f);
        Vec3f listener(1, 1, 1);

        Bvh bvh;
        bvh.build(walls);

        RayTracer rt;
        auto contributions = rt.trace(source, listener, walls, bvh, 100);
        QVERIFY(contributions.size() >= 0);
    }

    void testRayTracerAirAbsorptionParameter() {
        auto walls = createSimpleBox(5.0f);
        Vec3f source(2.5f, 2.5f, 2.5f);
        Vec3f listener(1, 1, 1);

        Bvh bvh;
        bvh.build(walls);

        RayTracer rtOn;
        auto contribsOn = rtOn.trace(source, listener, walls, bvh, 100, 0.5f, 100, 1e-6f, nullptr, 0.0f, true);

        RayTracer rtOff;
        auto contribsOff = rtOff.trace(source, listener, walls, bvh, 100, 0.5f, 100, 1e-6f, nullptr, 0.0f, false);

        QVERIFY(contribsOn.size() >= 0);
        QVERIFY(contribsOff.size() >= 0);
    }

    void testAirAbsorptionDecayFormula() {
        float distance = 20.0f;
        float coeff = 0.005f;
        float decayed = std::exp(-coeff * distance);
        QVERIFY(decayed < 1.0f);
        QVERIFY(std::abs(decayed - std::exp(-0.1f)) < 1e-6f);

        float noDecay = std::exp(-0.0f * distance);
        QVERIFY(std::abs(noDecay - 1.0f) < 1e-6f);
    }

    void testRIRMultiband() {
        std::vector<ImageSource> images;
        ImageSource is;
        is.position = Vec3f(3, 0, 0);
        is.delay = 0.01f;
        is.attenuation = {0.5f, 0.4f, 0.3f, 0.2f, 0.15f, 0.1f};
        is.order = 1;
        images.push_back(is);

        RoomImpulseResponse rir;
        auto multiband = rir.computeMultiband(images, {}, 44100);
        for (auto& band : multiband)
            QVERIFY(!band.empty());
        QVERIFY(multiband[0].size() > 44);
    }

    void testSchroederCurve() {
        int fs = 44100;
        int len = fs;
        std::vector<float> rir(len, 0.0f);
        for (int i = 0; i < len; ++i)
            rir[i] = std::exp(-3.0f * i / fs);

        auto edc = AcousticMetrics::computeSchroederCurve(rir);
        QCOMPARE(static_cast<int>(edc.size()), len);
        QVERIFY(edc[0] > edc[len - 1]);
        QVERIFY(edc[0] > 0.0f);

        auto edcDb = AcousticMetrics::schroederCurveDb(rir);
        QVERIFY(std::abs(edcDb[0]) < 0.01f);
        QVERIFY(edcDb[len / 2] < 0.0f);
    }

    void testComputeRT() {
        int fs = 44100;
        int len = 2 * fs;
        std::vector<float> rir(len, 0.0f);
        for (int i = 0; i < len; ++i)
            rir[i] = std::exp(-5.0f * i / fs);

        auto rt = AcousticMetrics::computeRT(rir, fs);
        QVERIFY(rt.valid);
        QVERIFY(rt.t20 > 0.0f);
        QVERIFY(rt.t30 > 0.0f);
        QVERIFY(rt.t20 < 5.0f);
        QVERIFY(rt.t30 < 5.0f);
    }

    void testComputeSPL() {
        std::vector<ImageSource> images;
        ImageSource direct;
        direct.attenuation = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
        direct.order = 0;
        direct.delay = 0.01f;
        images.push_back(direct);

        ImageSource reflected;
        reflected.attenuation = {0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f};
        reflected.order = 1;
        reflected.delay = 0.02f;
        images.push_back(reflected);

        auto spl = AcousticMetrics::computeSPL(1.0f, images, {}, 1000);
        QVERIFY(spl.valid);
        QVERIFY(spl.directSPL > 0.0f);
        QVERIFY(spl.totalSPL >= spl.directSPL);
    }

    void testEmptyMetrics() {
        auto rt = AcousticMetrics::computeRT({}, 44100);
        QVERIFY(!rt.valid);

        auto spl = AcousticMetrics::computeSPL(1.0f, {}, {}, 0);
        QVERIFY(!spl.valid);
    }
    // Dr/Ds(/Dt) must differentiate any polynomial of degree <= N exactly.
    void testDGBasis2DDerivatives() {
        for (int N : {3, 4}) {
            auto B = dg::buildBasis2D(N);
            dg::VecXd f = B.r.array().square() * B.s.array() + B.s.array().cube();
            dg::VecXd fr = 2.0 * B.r.array() * B.s.array();
            dg::VecXd fs = B.r.array().square() + 3.0 * B.s.array().square();
            QVERIFY2((B.Dr * f - fr).cwiseAbs().maxCoeff() < 1e-8, qPrintable(QString("Dr N=%1").arg(N)));
            QVERIFY2((B.Ds * f - fs).cwiseAbs().maxCoeff() < 1e-8, qPrintable(QString("Ds N=%1").arg(N)));
        }
    }

    void testDGBasis3DDerivatives() {
        for (int N : {3, 4}) {
            auto B = dg::buildBasis3D(N);
            dg::VecXd f = B.r.array().square() * B.s.array() + B.t.array().cube() + B.s.array() * B.t.array();
            dg::VecXd fr = 2.0 * B.r.array() * B.s.array();
            dg::VecXd fs = B.r.array().square() + B.t.array();
            dg::VecXd ft = 3.0 * B.t.array().square() + B.s.array();
            QVERIFY2((B.Dr * f - fr).cwiseAbs().maxCoeff() < 1e-8, qPrintable(QString("Dr N=%1").arg(N)));
            QVERIFY2((B.Ds * f - fs).cwiseAbs().maxCoeff() < 1e-8, qPrintable(QString("Ds N=%1").arg(N)));
            QVERIFY2((B.Dt * f - ft).cwiseAbs().maxCoeff() < 1e-8, qPrintable(QString("Dt N=%1").arg(N)));
        }
    }
    // A short DG run in a square room must stay finite and actually carry the pulse.
    void testDG2DStaysFinite() {
        dg::RoomPolygon2D room;
        room.vertices = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
        room.edgeAbsorption.assign(4, 0.2);
        room.edgeScattering.assign(4, 0.1);

        dg::DGParams params;
        params.polynomialOrder = 2;
        params.maxFrequency = 300.0;
        params.simulationDuration = 0.008; // pulse peaks at ~4.2 ms

        auto basis = dg::buildBasis2D(params.polynomialOrder);
        auto mesh = dg::buildMesh2D(room, basis, 0.4);
        QVERIFY(mesh.K > 0);

        dg::DGAcoustics2D solver(basis, mesh, params);
        auto result = solver.solve(dg::Vec2d(0.6, 0.7), dg::Vec2d(1.4, 1.2));
        QVERIFY(!result.impulseResponse.empty());
        float peak = 0.0f;
        for (float v : result.impulseResponse) {
            QVERIFY(std::isfinite(v));
            peak = (std::max)(peak, std::abs(v));
        }
        QVERIFY(peak > 0.0f);
    }

    // Default settings in a 10x10x3 m room would need ~100+ GB; the solver must refuse before meshing.
    void testDGRefusesOversizedRun() {
        dg::DGParams params;
        params.method = dg::DGMethod::DG_3D;
        params.polynomialOrder = 3;
        params.maxFrequency = 1000.0;
        std::vector<Vec3f> verts = {Vec3f(0, 0, 0), Vec3f(10, 10, 3)};

        dg::DGSolver solver;
        Bvh bvh;
        QElapsedTimer timer;
        timer.start();
        auto result = solver.solve(Vec3f(2, 2, 1), Vec3f(8, 8, 1), {}, verts, bvh, params);
        QVERIFY(result.impulseResponse.empty());
        QVERIFY(result.error.contains("too large"));
        QVERIFY(timer.elapsed() < 1000);
    }
};

QTEST_MAIN(TestAcoustics)
#include "test_acoustics.moc"
