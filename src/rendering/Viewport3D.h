#pragma once

#include "core/Types.h"
#include "core/PlacedPoint.h"
#include "core/Material.h"
#include "Camera.h"
#include "MeshData.h"
#include "SurfaceGrouper.h"
#include "TextureManager.h"

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QElapsedTimer>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImage>

#include <vector>
#include <set>
#include <map>
#include <optional>
#include <functional>

namespace prs {

class Viewport3D : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit Viewport3D(QWidget* parent = nullptr);
    ~Viewport3D() override;

    bool loadModel(const QString& filepath);
    bool hasModel() const { return mesh_.triangleCount() > 0; }

    // Scale
    void setScaleFactor(float factor);
    float scaleFactor() const { return scaleFactor_; }
    Vec3f getRealWorldDimensions() const;
    float getRealWorldSize() const;

    // Point placement
    void setPlacementMode(bool enabled);
    bool placementMode() const { return placementMode_; }
    void setPlacementPointTypeForNew(const std::string& type);
    const std::string& placementPointTypeForNew() const { return placementPointTypeForNew_; }
    void updateActivePointDistance(float distance);
    void adjustActivePointDistance(float delta);
    void setActivePointType(const std::string& type);
    void removeActivePoint();
    void deselectPoint();
    void clearFullSelection();
    void clearPlacedPoints();
    void resetCamera();
    void restorePlacedPoints(const std::vector<PlacedPoint>& points);
    void selectPoint(int index);
    void notifyPlacedPointsChanged();
    int  activePointIndex() const { return activePointIndex_; }
    const std::vector<PlacedPoint>& placedPoints() const { return placedPoints_; }
    std::vector<PlacedPoint>& placedPoints() { return placedPoints_; }

    // Measure mode
    void setMeasureMode(bool enabled);

    // Surface selection
    int  selectedSurfaceIndex() const { return selectedSurfaceIndex_; }
    void selectSurface(int index);
    void deselectSurface();
    void setSurfaceColor(int surfIdx, const Color3f& color);
    void assignMaterial(int surfIdx, const Material& material);
    std::optional<Material> getSurfaceMaterial(int surfIdx) const;
    void toggleSurfaceTexture(int surfIdx);
    bool isSurfaceTextureActive(int surfIdx) const;
    bool loadTexture(const QString& filepath);

    // Acoustic simulation helpers
    std::pair<int, int> countSourcesAndListeners() const;
    struct SourceData { Vec3f position; std::string audioFile; std::string name; float volume; };
    struct ListenerData { Vec3f position; std::string name; Vec3f orientation; };
    std::pair<std::vector<SourceData>, std::vector<ListenerData>>
        getSourcesAndListeners(const std::string& audioFile) const;

    struct WallInfo {
        std::vector<int> triangleIndices;
        std::array<float, NUM_FREQ_BANDS> absorption = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f};
        float scattering = DEFAULT_SCATTERING;
    };
    std::vector<WallInfo> getWallsForAcoustic() const;
    Vec3f getScaledRoomCenter() const;
    std::vector<Vec3f> getScaledModelVertices() const;

    const std::vector<std::set<int>>& surfaces() const { return surfaces_; }
    const std::vector<Color3f>& surfaceColors() const { return surfaceColors_; }

    // Display settings
    void applyDisplaySettings();
    bool gridVisible() const { return gridVisible_; }
    float gridSpacing() const { return gridSpacing_; }
    float transparencyAlpha() const { return transparencyAlpha_; }
    int markerSize() const { return markerSize_; }
    bool texturesEnabled() const { return texturesEnabled_; }

    // Select all / multi-selection
    void selectAllPoints();
    void clearPointSelection();
    const std::set<int>& selectedPointIndices() const { return selectedPointIndices_; }

    // Move mode
    void setMoveMode(bool enabled);
    bool moveMode() const { return moveMode_; }

    // Mesh data access
    const MeshData& meshData() const { return mesh_; }

signals:
    void modelLoaded(const QString& filepath);
    void meshOpenWarning(int boundaryEdges);
    void pointPlaced(int index);
    void pointSelected(int index);
    void pointDeselected();
    void surfaceSelected(int index);
    void surfaceDeselected();
    void surfaceMaterialChanged(int index, const QString& materialName);
    void surfaceAppearanceChanged(int index);
    void placementModeChanged(bool enabled);
    void scaleChanged(float factor);
    void measurementResult(float distance);
    void moveFinished(int pointIndex, const PlacedPoint& oldState, const PlacedPoint& newState);
    void placedPointsChanged();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void autoNormalizeScale();
    void updateProjection();
    void stepKeyMovement();

    // Drawing helpers
    void drawMeasurementGrid();
    void drawModel();
    void drawPointMarkers();
    void drawSelectedSurfaceOutline();
    void drawPlaceholder();

    // Ray picking
    std::pair<Vec3f, Vec3f> getRayFromMouse(const QPoint& pos);
    struct IntersectionResult { Vec3f point; Vec3f normal; int triIndex; };
    std::optional<IntersectionResult> getIntersectionPoint(const QPoint& pos);
    std::optional<int> getPointAtMouse(const QPoint& pos);
    bool trySelectPointAtMouse(const QPoint& pos);
    bool trySelectSurfaceAtMouse(const QPoint& pos);
    void addPointAtMouse(const QPoint& pos);

    // Data
    MeshData mesh_;
    Camera camera_;

    // Held WASD/arrow keys pan every frame, not per OS key repeat
    std::set<int> heldMoveKeys_;
    QTimer moveTimer_;
    QElapsedTimer moveClock_;
    float scaleFactor_ = 1.0f;
    Vec3f modelCenter_ = Vec3f::Zero();

    // Surface grouping
    SurfaceGrouper::EdgeSet featureEdges_;
    std::vector<std::set<int>> surfaces_;
    std::vector<Color3f> surfaceColors_;
    std::vector<std::optional<Material>> surfaceMaterials_;
    std::vector<bool> surfaceTextured_;
    std::vector<int> triangleToSurface_; // -1 = triangle not in any surface

    // Points
    std::vector<PlacedPoint> placedPoints_;
    int activePointIndex_ = -1;
    bool placementMode_ = false;
    int nextPointColorIndex_ = 0;
    std::string placementPointTypeForNew_ = POINT_TYPE_SOURCE;
    static const std::vector<Color3f>& pointColors();

    // Surface selection
    int selectedSurfaceIndex_ = -1;
    int outlineSurface_ = -1;         // surface whose boundary is cached in outlineVerts_
    std::vector<Vec3f> outlineVerts_; // GL_LINES vertex pairs

    // Multi-selection
    std::set<int> selectedPointIndices_;

    // Move mode
    bool moveMode_ = false;
    int movingPointIndex_ = -1;
    PlacedPoint moveOriginal_;

    // Mouse state
    bool mouseDown_ = false;
    QPoint lastMousePos_;
    QPoint mouseDownPos_;
    bool transparentMode_ = false;
    bool measureMode_ = false;
    std::optional<Vec3f> measurePoint1_;
    std::optional<Vec3f> measurePoint2_;

    // Display settings (read from QSettings)
    bool gridVisible_ = true;
    float gridSpacing_ = 1.0f;
    float transparencyAlpha_ = 0.55f;
    int markerSize_ = 15;
    bool texturesEnabled_ = true;

    Color3f defaultSurfaceColor_ = {0.6f, 0.8f, 1.0f};
    unsigned int textureId_ = 0;
    TextureManager textureManager_;
    std::vector<GLuint> surfaceTextureIds_;
};

} // namespace prs
