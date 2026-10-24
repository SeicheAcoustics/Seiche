#include <cmath>
#include <algorithm>

#include "Viewport3D.h"
#include "RayPicking.h"
#include "GLHeaders.h"

#include <QPainter>
#include <QSettings>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace prs {

static const std::vector<Color3f> kPointColors = {
    {0.2f, 0.8f, 0.2f}, {0.8f, 0.2f, 0.2f}, {0.2f, 0.2f, 0.8f},
    {0.8f, 0.8f, 0.2f}, {0.8f, 0.2f, 0.8f}, {0.2f, 0.8f, 0.8f},
};

const std::vector<Color3f>& Viewport3D::pointColors() { return kPointColors; }

Viewport3D::Viewport3D(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 200);
    setAcceptDrops(true);
    moveTimer_.setInterval(16);
    connect(&moveTimer_, &QTimer::timeout, this, &Viewport3D::stepKeyMovement);
    applyDisplaySettings();
}

void Viewport3D::applyDisplaySettings() {
    QSettings s("Seiche", "Seiche");
    gridVisible_       = s.value("display/gridVisible", true).toBool();
    gridSpacing_       = static_cast<float>(s.value("display/gridSpacing", 1.0).toDouble());
    transparencyAlpha_ = static_cast<float>(s.value("display/transparencyAlpha", 0.55).toDouble());
    markerSize_        = s.value("display/markerSize", 15).toInt();
    texturesEnabled_   = s.value("display/texturesEnabled", true).toBool();
    for (int i = 0; i < static_cast<int>(surfaceColors_.size()); ++i)
        emit surfaceAppearanceChanged(i);
    update();
}

Viewport3D::~Viewport3D() {
    // Free GL objects while our context is current; members (TextureManager) destruct after doneCurrent().
    makeCurrent();
    if (textureId_ != 0) glDeleteTextures(1, &textureId_);
    textureManager_.releaseAll();
    doneCurrent();
}

bool Viewport3D::loadModel(const QString& filepath) {
    if (!mesh_.load(filepath)) return false;

    modelCenter_  = mesh_.center();

    featureEdges_ = SurfaceGrouper::computeFeatureEdges(mesh_, 10.0f);
    surfaces_     = SurfaceGrouper::groupTrianglesIntoSurfaces(mesh_, featureEdges_);

    surfaceColors_.assign(surfaces_.size(), defaultSurfaceColor_);
    surfaceMaterials_.assign(surfaces_.size(), std::nullopt);
    surfaceTextured_.assign(surfaces_.size(), false);
    surfaceTextureIds_.assign(surfaces_.size(), 0);

    triangleToSurface_.assign(mesh_.triangles().size(), -1);
    for (int si = 0; si < static_cast<int>(surfaces_.size()); ++si)
        for (int ti : surfaces_[si])
            triangleToSurface_[ti] = si;
    outlineSurface_ = -1;

    clearFullSelection();
    placedPoints_.clear();
    nextPointColorIndex_ = 0;

    camera_.setTarget(Vec3f::Zero());
    autoNormalizeScale();

    notifyPlacedPointsChanged();
    update();
    emit modelLoaded(filepath);

    if (!mesh_.isClosed()) {
        emit meshOpenWarning(mesh_.boundaryEdgeCount());
    }

    return true;
}

void Viewport3D::autoNormalizeScale() {
    float orig = mesh_.diagonalSize();
    float factor;
    constexpr float TARGET_SIZE = 6.0f;

    if (orig > 1000.0f)
        factor = 1.0f / 700.0f;
    else if (orig < 1.0f)
        factor = TARGET_SIZE / orig;
    else
        factor = TARGET_SIZE / orig;

    setScaleFactor(factor);
}

void Viewport3D::setScaleFactor(float factor) {
    scaleFactor_ = factor;
    float scaledSize = mesh_.diagonalSize() * factor;
    camera_.setDistance(2.5f * scaledSize);
    camera_.setDistanceLimits(0.2f * scaledSize, 5.0f * scaledSize);
    updateProjection();
    emit scaleChanged(factor);
    update();
}

void Viewport3D::updateProjection() {
    makeCurrent();
    float nearPlane = 0.1f;
    float farPlane  = std::max(100.0f, camera_.maxDistance() * 10.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = width() > 0 ? static_cast<float>(width()) / height() : 1.0f;
    gluPerspective(45.0, aspect, nearPlane, farPlane);
    glMatrixMode(GL_MODELVIEW);
    doneCurrent();
}

Vec3f Viewport3D::getRealWorldDimensions() const {
    return (mesh_.maxBound() - mesh_.minBound()) * scaleFactor_;
}

float Viewport3D::getRealWorldSize() const {
    return mesh_.diagonalSize() * scaleFactor_;
}

// ==================== Point Placement ====================

void Viewport3D::setPlacementMode(bool enabled) {
    placementMode_ = enabled;
    emit placementModeChanged(enabled);
    update();
}

void Viewport3D::setPlacementPointTypeForNew(const std::string& type) {
    placementPointTypeForNew_ = type;
    if (placementMode_)
        emit placementModeChanged(true);
}

void Viewport3D::notifyPlacedPointsChanged() {
    emit placedPointsChanged();
}

void Viewport3D::updateActivePointDistance(float distance) {
    if (activePointIndex_ >= 0 && activePointIndex_ < static_cast<int>(placedPoints_.size()))
        placedPoints_[activePointIndex_].distance = distance;
    update();
}

void Viewport3D::adjustActivePointDistance(float delta) {
    if (activePointIndex_ >= 0 && activePointIndex_ < static_cast<int>(placedPoints_.size())) {
        placedPoints_[activePointIndex_].distance += delta;
        update();
    }
}

void Viewport3D::setActivePointType(const std::string& type) {
    if (activePointIndex_ >= 0 && activePointIndex_ < static_cast<int>(placedPoints_.size())) {
        placedPoints_[activePointIndex_].pointType = type;
        update();
    }
}

void Viewport3D::removeActivePoint() {
    if (activePointIndex_ >= 0 && activePointIndex_ < static_cast<int>(placedPoints_.size())) {
        placedPoints_.erase(placedPoints_.begin() + activePointIndex_);
        activePointIndex_ = -1;
        notifyPlacedPointsChanged();
        emit pointDeselected();
        update();
    }
}

void Viewport3D::deselectPoint() {
    bool had = activePointIndex_ >= 0 || !selectedPointIndices_.empty();
    activePointIndex_ = -1;
    selectedPointIndices_.clear();
    if (had) {
        emit pointDeselected();
        update();
    }
}

void Viewport3D::clearFullSelection() {
    bool hadPoint = activePointIndex_ >= 0 || !selectedPointIndices_.empty();
    bool hadSurf  = selectedSurfaceIndex_ >= 0;
    activePointIndex_ = -1;
    selectedPointIndices_.clear();
    selectedSurfaceIndex_ = -1;
    if (hadPoint) emit pointDeselected();
    if (hadSurf) emit surfaceDeselected();
    if (hadPoint || hadSurf) update();
}

void Viewport3D::selectPoint(int index) {
    if (index < 0 || index >= static_cast<int>(placedPoints_.size())) return;
    selectedPointIndices_.clear();
    if (selectedSurfaceIndex_ >= 0) {
        selectedSurfaceIndex_ = -1;
        emit surfaceDeselected();
    }
    activePointIndex_ = index;
    emit pointSelected(index);
    update();
}

void Viewport3D::resetCamera() {
    if (!hasModel()) return;
    camera_.reset(getRealWorldSize());
    update();
}

void Viewport3D::clearPlacedPoints() {
    placedPoints_.clear();
    activePointIndex_ = -1;
    nextPointColorIndex_ = 0;
    selectedPointIndices_.clear();
    emit pointDeselected();
    notifyPlacedPointsChanged();
    update();
}

void Viewport3D::restorePlacedPoints(const std::vector<PlacedPoint>& points) {
    placedPoints_ = points;
    nextPointColorIndex_ = static_cast<int>(points.size());
    clearFullSelection();
    notifyPlacedPointsChanged();
    update();
}

// ==================== Multi-Selection ====================

void Viewport3D::selectAllPoints() {
    bool hadActive = activePointIndex_ >= 0;
    selectedPointIndices_.clear();
    for (int i = 0; i < static_cast<int>(placedPoints_.size()); ++i)
        selectedPointIndices_.insert(i);
    if (selectedSurfaceIndex_ >= 0) {
        selectedSurfaceIndex_ = -1;
        emit surfaceDeselected();
    }
    activePointIndex_ = -1;
    if (hadActive) emit pointDeselected();
    update();
}

void Viewport3D::clearPointSelection() {
    selectedPointIndices_.clear();
    update();
}

// ==================== Move Mode ====================

void Viewport3D::setMoveMode(bool enabled) {
    moveMode_ = enabled;
    movingPointIndex_ = -1;
    update();
}

// ==================== Measure Mode ====================

void Viewport3D::setMeasureMode(bool enabled) {
    measureMode_ = enabled;
    measurePoint1_ = std::nullopt;
    measurePoint2_ = std::nullopt;
    update();
}

// ==================== Surface Selection ====================

void Viewport3D::selectSurface(int index) {
    if (index >= 0 && index < static_cast<int>(surfaces_.size())) {
        bool hadPoint = activePointIndex_ >= 0 || !selectedPointIndices_.empty();
        selectedPointIndices_.clear();
        activePointIndex_ = -1;
        selectedSurfaceIndex_ = index;
        if (hadPoint) emit pointDeselected();
        emit surfaceSelected(index);
        update();
    }
}

void Viewport3D::deselectSurface() {
    if (selectedSurfaceIndex_ >= 0) {
        selectedSurfaceIndex_ = -1;
        emit surfaceDeselected();
        update();
    }
}

void Viewport3D::setSurfaceColor(int surfIdx, const Color3f& color) {
    if (surfIdx >= 0 && surfIdx < static_cast<int>(surfaceColors_.size())) {
        surfaceColors_[surfIdx] = color;
        surfaceTextured_[surfIdx] = false;
        emit surfaceAppearanceChanged(surfIdx);
        update();
    }
}

void Viewport3D::assignMaterial(int surfIdx, const Material& material) {
    if (surfIdx >= 0 && surfIdx < static_cast<int>(surfaceColors_.size())) {
        surfaceMaterials_[surfIdx] = material;
        auto srgbByteToLinear = [](int byte) -> float {
            float s = std::clamp(byte / 255.0f, 0.0f, 1.0f);
            return std::pow(s, 2.2f);
        };
        surfaceColors_[surfIdx] = {
            srgbByteToLinear(material.color[0]),
            srgbByteToLinear(material.color[1]),
            srgbByteToLinear(material.color[2])
        };

        if (!material.texturePath.empty()) {
            makeCurrent();
            GLuint texId = textureManager_.getOrLoad(QString::fromStdString(material.texturePath));
            if (texId != 0) {
                surfaceTextureIds_[surfIdx] = texId;
                surfaceTextured_[surfIdx] = true;
            } else {
                surfaceTextureIds_[surfIdx] = 0;
                surfaceTextured_[surfIdx] = false;
            }
            doneCurrent();
        } else {
            surfaceTextureIds_[surfIdx] = 0;
            surfaceTextured_[surfIdx] = false;
        }

        emit surfaceMaterialChanged(surfIdx, QString::fromStdString(material.name));
        emit surfaceAppearanceChanged(surfIdx);
        update();
    }
}

std::optional<Material> Viewport3D::getSurfaceMaterial(int surfIdx) const {
    if (surfIdx >= 0 && surfIdx < static_cast<int>(surfaceMaterials_.size()))
        return surfaceMaterials_[surfIdx];
    return std::nullopt;
}

void Viewport3D::toggleSurfaceTexture(int surfIdx) {
    if (surfIdx >= 0 && surfIdx < static_cast<int>(surfaceTextured_.size())) {
        surfaceTextured_[surfIdx] = !surfaceTextured_[surfIdx];
        emit surfaceAppearanceChanged(surfIdx);
        update();
    }
}

bool Viewport3D::isSurfaceTextureActive(int surfIdx) const {
    if (surfIdx < 0 || surfIdx >= static_cast<int>(surfaceTextured_.size()))
        return false;
    if (!texturesEnabled_ || !surfaceTextured_[surfIdx])
        return false;
    if (surfIdx < static_cast<int>(surfaceTextureIds_.size()) && surfaceTextureIds_[surfIdx] != 0)
        return true;
    return textureId_ != 0;
}

bool Viewport3D::loadTexture(const QString& filepath) {
    QImage img(filepath);
    if (img.isNull()) return false;

    const QImage texImage = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical);
    makeCurrent();
    if (textureId_ != 0) glDeleteTextures(1, &textureId_);
    glGenTextures(1, &textureId_);
    glBindTexture(GL_TEXTURE_2D, textureId_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texImage.width(), texImage.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, texImage.constBits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    doneCurrent();
    update();
    return true;
}

static Vec2f getTexCoordsFromNormal(const Vec3f& vertex, const Vec3f& normal,
                                     const Vec3f& boundsMin, const Vec3f& boundsMax) {
    Vec3f absNormal = normal.cwiseAbs();
    Vec3f extent = boundsMax - boundsMin;
    float u = 0, v = 0;

    if (absNormal.z() >= absNormal.x() && absNormal.z() >= absNormal.y()) {
        float ex = extent.x() > 1e-6f ? extent.x() : 1.0f;
        float ey = extent.y() > 1e-6f ? extent.y() : 1.0f;
        u = (vertex.x() - boundsMin.x()) / ex;
        v = (vertex.y() - boundsMin.y()) / ey;
    } else if (absNormal.y() >= absNormal.x()) {
        float ex = extent.x() > 1e-6f ? extent.x() : 1.0f;
        float ez = extent.z() > 1e-6f ? extent.z() : 1.0f;
        u = (vertex.x() - boundsMin.x()) / ex;
        v = (vertex.z() - boundsMin.z()) / ez;
    } else {
        float ey = extent.y() > 1e-6f ? extent.y() : 1.0f;
        float ez = extent.z() > 1e-6f ? extent.z() : 1.0f;
        u = (vertex.y() - boundsMin.y()) / ey;
        v = (vertex.z() - boundsMin.z()) / ez;
    }
    return {u, v};
}

// ==================== Acoustic Helpers ====================

std::pair<int, int> Viewport3D::countSourcesAndListeners() const {
    int src = 0, lst = 0;
    for (auto& p : placedPoints_) {
        if (p.pointType == POINT_TYPE_SOURCE) ++src;
        else if (p.pointType == POINT_TYPE_LISTENER) ++lst;
    }
    return {src, lst};
}

std::pair<std::vector<Viewport3D::SourceData>, std::vector<Viewport3D::ListenerData>>
Viewport3D::getSourcesAndListeners(const std::string& audioFile) const {
    std::vector<SourceData> sources;
    std::vector<ListenerData> listeners;
    int sc = 0, lc = 0;

    for (auto& pt : placedPoints_) {
        Vec3f pos = pt.getPosition() * scaleFactor_;
        if (pt.pointType == POINT_TYPE_SOURCE) {
            ++sc;
            std::string file = pt.audioFile.empty() ? audioFile : pt.audioFile;
            std::string name = pt.name.empty() ? "Source " + std::to_string(sc) : pt.name;
            sources.push_back({pos, file, name, pt.volume});
        } else if (pt.pointType == POINT_TYPE_LISTENER) {
            ++lc;
            std::string name = pt.name.empty() ? "Listener " + std::to_string(lc) : pt.name;
            listeners.push_back({pos, name, pt.getForwardDirection()});
        }
    }
    return {sources, listeners};
}

std::vector<Viewport3D::WallInfo> Viewport3D::getWallsForAcoustic() const {
    std::vector<WallInfo> walls;
    for (int si = 0; si < static_cast<int>(surfaces_.size()); ++si) {
        WallInfo wi;
        wi.triangleIndices.assign(surfaces_[si].begin(), surfaces_[si].end());
        if (si < static_cast<int>(surfaceMaterials_.size()) && surfaceMaterials_[si].has_value()) {
            wi.absorption = surfaceMaterials_[si]->absorption;
            wi.scattering = surfaceMaterials_[si]->scattering;
        }
        walls.push_back(std::move(wi));
    }
    return walls;
}

Vec3f Viewport3D::getScaledRoomCenter() const {
    return mesh_.center() * scaleFactor_;
}

std::vector<Vec3f> Viewport3D::getScaledModelVertices() const {
    return mesh_.scaledFlatVertices(scaleFactor_);
}

// ==================== OpenGL ====================

void Viewport3D::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void Viewport3D::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = h > 0 ? static_cast<float>(w) / h : 1.0f;
    float nearPlane = 0.1f;
    float farPlane  = std::max(100.0f, camera_.maxDistance() * 10.0f);
    gluPerspective(45.0, aspect, nearPlane, farPlane);
    glMatrixMode(GL_MODELVIEW);
}

void Viewport3D::paintGL() {
    glUseProgram(0); // the grid legend's QPainter may leave a shader bound; fixed-function below
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!hasModel()) {
        drawPlaceholder();
        return;
    }

    drawModel();
    drawMeasurementGrid();
    drawSelectedSurfaceOutline();
    drawPointMarkers();

    if (measurePoint1_ && measurePoint2_) {
        glEnable(GL_BLEND);
        glPushMatrix();
        camera_.applyViewMatrix();
        glScalef(scaleFactor_, scaleFactor_, scaleFactor_);
        glTranslatef(-modelCenter_.x(), -modelCenter_.y(), -modelCenter_.z());
        glDisable(GL_DEPTH_TEST);
        glColor4f(1.0f, 1.0f, 0.0f, 1.0f);
        glLineWidth(3);
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(4, 0xAAAA);
        glBegin(GL_LINES);
        glVertex3f(measurePoint1_->x(), measurePoint1_->y(), measurePoint1_->z());
        glVertex3f(measurePoint2_->x(), measurePoint2_->y(), measurePoint2_->z());
        glEnd();
        glDisable(GL_LINE_STIPPLE);
        glEnable(GL_DEPTH_TEST);
        glPopMatrix();
        glDisable(GL_BLEND);
    }

    if (gridVisible_) {
        // Scale legend; major lines are every 5th (see drawMeasurementGrid).
        QPainter painter(this);
        painter.setPen(QColor(64, 64, 64));
        painter.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignLeft | Qt::AlignBottom,
            QString("Grid: %1 m \u00B7 major %2 m")
                .arg(gridSpacing_, 0, 'g', 3).arg(gridSpacing_ * 5, 0, 'g', 3));
        painter.end();
    }
}

void Viewport3D::drawPlaceholder() {
    // Use QPainter overlay for placeholder text
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect r = rect();
    painter.fillRect(r, QColor(200, 200, 200));
    painter.setPen(QColor(64, 64, 64));

    QFont font = painter.font();
    font.setPointSize(14);
    painter.setFont(font);
    painter.drawText(r, Qt::AlignCenter,
        "No 3D Model Loaded\n\nFile \u2192 New Project\nto load an STL or OBJ file");
    painter.end();
}

void Viewport3D::drawMeasurementGrid() {
    if (!gridVisible_) return;

    glEnable(GL_BLEND);
    glPushMatrix();
    camera_.applyViewMatrix();
    glScalef(scaleFactor_, scaleFactor_, scaleFactor_);
    glTranslatef(-modelCenter_.x(), -modelCenter_.y(), -modelCenter_.z());

    Vec3f mn = mesh_.minBound();
    Vec3f mx = mesh_.maxBound();
    // Drawn after the model, a hair above the floor and without depth writes, so it
    // sits on top of the floor instead of z-fighting with it.
    float gridZ = mn.z() + 1e-4f * mesh_.diagonalSize();
    float cx = (mn.x() + mx.x()) * 0.5f;
    float cy = (mn.y() + mx.y()) * 0.5f;
    float extent = std::max(mx.x() - mn.x(), mx.y() - mn.y()) * 0.75f;

    float meterInOrigUnits = gridSpacing_ / scaleFactor_;
    float minorSpacing = meterInOrigUnits;
    int numLines = static_cast<int>(extent / minorSpacing) + 1;

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);

    // Line widths are in device pixels; scale so lines don't thin out on HiDPI screens.
    const float dpr = static_cast<float>(devicePixelRatioF());

    // One batch for minor lines, then one for major (every 5th) lines on top.
    for (bool major : {false, true}) {
        if (major) { glColor4f(0.3f, 0.3f, 0.3f, 0.9f); glLineWidth(2 * dpr); }
        else       { glColor4f(0.55f, 0.55f, 0.55f, 0.7f); glLineWidth(dpr); }

        glBegin(GL_LINES);
        for (int i = -numLines; i <= numLines; ++i) {
            if ((i % 5 == 0) != major) continue;
            float offset = i * minorSpacing;
            glVertex3f(cx - extent, cy + offset, gridZ);
            glVertex3f(cx + extent, cy + offset, gridZ);
            glVertex3f(cx + offset, cy - extent, gridZ);
            glVertex3f(cx + offset, cy + extent, gridZ);
        }
        glEnd();
    }

    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glPopMatrix();
    glDisable(GL_BLEND);
}

void Viewport3D::drawModel() {
    glPushMatrix();
    camera_.applyViewMatrix();
    glScalef(scaleFactor_, scaleFactor_, scaleFactor_);
    glTranslatef(-modelCenter_.x(), -modelCenter_.y(), -modelCenter_.z());

    const auto& tris = mesh_.triangles();
    Vec3f mn = mesh_.minBound();
    Vec3f mx = mesh_.maxBound();
    float alpha = transparentMode_ ? transparencyAlpha_ : 1.0f;

    if (transparentMode_)
        glEnable(GL_BLEND);

    // One batch per surface; textured surfaces use their own texture or the global one.
    for (int si = 0; si < static_cast<int>(surfaces_.size()); ++si) {
        const bool textured = texturesEnabled_ && surfaceTextured_[si];
        if (textured) {
            GLuint texId = (si < static_cast<int>(surfaceTextureIds_.size()) && surfaceTextureIds_[si] != 0)
                           ? surfaceTextureIds_[si] : textureId_;
            if (texId == 0) continue;
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texId);
            glColor4f(1.0f, 1.0f, 1.0f, alpha);
        } else {
            auto& c = surfaceColors_[si];
            glColor4f(c[0], c[1], c[2], alpha);
        }

        glBegin(GL_TRIANGLES);
        for (int ti : surfaces_[si]) {
            const auto& tri = tris[ti];
            for (const Vec3f* v : {&tri.v0, &tri.v1, &tri.v2}) {
                if (textured) {
                    Vec2f uv = getTexCoordsFromNormal(*v, tri.normal, mn, mx);
                    glTexCoord2f(uv.x(), uv.y());
                }
                glVertex3f(v->x(), v->y(), v->z());
            }
        }
        glEnd();

        if (textured) {
            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    if (transparentMode_)
        glDisable(GL_BLEND);

    // Draw feature edges
    glColor3f(0, 0, 0);
    glLineWidth(3);
    glBegin(GL_LINES);
    for (auto& [e1, e2] : featureEdges_) {
        glVertex3f(std::get<0>(e1), std::get<1>(e1), std::get<2>(e1));
        glVertex3f(std::get<0>(e2), std::get<1>(e2), std::get<2>(e2));
    }
    glEnd();

    glPopMatrix();
}

void Viewport3D::drawPointMarkers() {
    if (placedPoints_.empty()) return;

    glEnable(GL_BLEND);
    glPushMatrix();
    camera_.applyViewMatrix();
    glScalef(scaleFactor_, scaleFactor_, scaleFactor_);
    glTranslatef(-modelCenter_.x(), -modelCenter_.y(), -modelCenter_.z());

    glDisable(GL_TEXTURE_2D);
    if (transparentMode_) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-2.0f, -2.0f);
    }

    float baseSize = (markerSize_ / 100.0f) / scaleFactor_;

    GLfloat modelview[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, modelview);

    for (int i = 0; i < static_cast<int>(placedPoints_.size()); ++i) {
        auto& pt = placedPoints_[i];
        Vec3f pos = pt.getPosition();
        bool isActive = (i == activePointIndex_);
        bool isSelected = selectedPointIndices_.count(i) > 0;
        float size = isActive ? baseSize * 1.3f : baseSize;
        float alpha = (isActive || isSelected) ? 1.0f : 0.8f;

        Color3f color = pt.color;
        if (pt.pointType == POINT_TYPE_SOURCE)
            color = {0.8f, 0.2f, 0.2f};
        else if (pt.pointType == POINT_TYPE_LISTENER)
            color = {0.2f, 0.2f, 0.8f};

        // Billboard circle
        glPushMatrix();
        glTranslatef(pos.x(), pos.y(), pos.z());

        GLfloat billboard[16] = {
            modelview[0], modelview[4], modelview[8], 0,
            modelview[1], modelview[5], modelview[9], 0,
            modelview[2], modelview[6], modelview[10], 0,
            0, 0, 0, 1
        };
        glMultMatrixf(billboard);

        constexpr int segs = 24;
        glColor4f(color[0], color[1], color[2], alpha);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0, 0, 0);
        for (int s = 0; s <= segs; ++s) {
            float angle = 2.0f * static_cast<float>(M_PI) * s / segs;
            glVertex3f(size * std::cos(angle), size * std::sin(angle), 0);
        }
        glEnd();

        if (isActive) { glColor4f(1, 1, 1, 1); glLineWidth(3); }
        else if (isSelected) { glColor4f(1, 1, 0, 1); glLineWidth(3); }
        else          { glColor4f(color[0]*0.5f, color[1]*0.5f, color[2]*0.5f, alpha); glLineWidth(2); }
        glBegin(GL_LINE_LOOP);
        for (int s = 0; s < segs; ++s) {
            float angle = 2.0f * static_cast<float>(M_PI) * s / segs;
            glVertex3f(size * std::cos(angle), size * std::sin(angle), 0);
        }
        glEnd();

        glPopMatrix();

        // Direction arrow for listener points (always horizontal, parallel to grid floor)
        if (pt.pointType == POINT_TYPE_LISTENER) {
            Vec3f fwd = pt.getForwardDirection();
            float arrowLen = size * 4.2f;
            Vec3f tip = pos + fwd * arrowLen;

            Vec3f right(-fwd.y(), fwd.x(), 0.0f);
            float headSize = arrowLen * 0.35f;
            Vec3f head1 = tip - fwd * headSize + right * headSize * 0.5f;
            Vec3f head2 = tip - fwd * headSize - right * headSize * 0.5f;

            glColor4f(1.0f, 1.0f, 1.0f, alpha);
            glLineWidth(4);
            glBegin(GL_LINES);
            glVertex3f(pos.x(), pos.y(), pos.z());
            glVertex3f(tip.x(), tip.y(), tip.z());
            glEnd();

            glBegin(GL_TRIANGLES);
            glVertex3f(tip.x(), tip.y(), tip.z());
            glVertex3f(head1.x(), head1.y(), head1.z());
            glVertex3f(head2.x(), head2.y(), head2.z());
            glEnd();
        }
    }

    if (transparentMode_)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_POLYGON_OFFSET_FILL);
    glPopMatrix();
    glDisable(GL_BLEND);
}

void Viewport3D::drawSelectedSurfaceOutline() {
    if (selectedSurfaceIndex_ < 0) return;

    glPushMatrix();
    camera_.applyViewMatrix();
    glScalef(scaleFactor_, scaleFactor_, scaleFactor_);
    glTranslatef(-modelCenter_.x(), -modelCenter_.y(), -modelCenter_.z());

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glColor4f(1, 1, 1, 1);
    glLineWidth(3);

    if (outlineSurface_ != selectedSurfaceIndex_) {
        // Boundary = edges used by one triangle of the surface, plus feature edges.
        const auto& tris = mesh_.triangles();
        std::map<Edge, int> edgeCount;
        for (int ti : surfaces_[selectedSurfaceIndex_]) {
            const Vec3f* verts[3] = {&tris[ti].v0, &tris[ti].v1, &tris[ti].v2};
            for (int j = 0; j < 3; ++j)
                edgeCount[makeEdge(*verts[j], *verts[(j + 1) % 3])]++;
        }
        outlineVerts_.clear();
        for (auto& [edge, count] : edgeCount) {
            if (count == 1 || featureEdges_.count(edge)) {
                outlineVerts_.emplace_back(std::get<0>(edge.first), std::get<1>(edge.first), std::get<2>(edge.first));
                outlineVerts_.emplace_back(std::get<0>(edge.second), std::get<1>(edge.second), std::get<2>(edge.second));
            }
        }
        outlineSurface_ = selectedSurfaceIndex_;
    }

    glBegin(GL_LINES);
    for (const Vec3f& v : outlineVerts_)
        glVertex3f(v.x(), v.y(), v.z());
    glEnd();

    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
}

// ==================== Ray Picking ====================

std::pair<Vec3f, Vec3f> Viewport3D::getRayFromMouse(const QPoint& pos) {
    GLint viewport[4];
    GLdouble modelview[16], projection[16];
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
    glGetDoublev(GL_PROJECTION_MATRIX, projection);

    const double dpr = devicePixelRatioF();  // GL_VIEWPORT is in device pixels, Qt events are logical
    double mx = pos.x() * dpr;
    double my = viewport[3] - pos.y() * dpr;

    GLdouble nx, ny, nz, fx, fy, fz;
    gluUnProject(mx, my, 0.0, modelview, projection, viewport, &nx, &ny, &nz);
    gluUnProject(mx, my, 1.0, modelview, projection, viewport, &fx, &fy, &fz);

    Vec3f origin(nx, ny, nz);
    Vec3f dir = Vec3f(fx - nx, fy - ny, fz - nz).normalized();
    return {origin, dir};
}

std::optional<Viewport3D::IntersectionResult> Viewport3D::getIntersectionPoint(const QPoint& pos) {
    makeCurrent();
    glPushMatrix();
    camera_.applyViewMatrix();
    glScalef(scaleFactor_, scaleFactor_, scaleFactor_);
    glTranslatef(-modelCenter_.x(), -modelCenter_.y(), -modelCenter_.z());

    auto [rayOrigin, rayDir] = getRayFromMouse(pos);
    glPopMatrix();
    doneCurrent();

    const auto& tris = mesh_.triangles();
    float minT = std::numeric_limits<float>::max();
    int hitIdx = -1;

    for (int i = 0; i < static_cast<int>(tris.size()); ++i) {
        auto t = RayPicking::rayTriangleIntersect(rayOrigin, rayDir, tris[i]);
        if (t && *t < minT) { minT = *t; hitIdx = i; }
    }

    if (hitIdx < 0) return std::nullopt;

    Vec3f hitPoint = rayOrigin + rayDir * minT;
    Vec3f normal = tris[hitIdx].normal.normalized();
    if (normal.dot(rayOrigin - hitPoint) < 0) normal = -normal;

    return IntersectionResult{hitPoint, normal, hitIdx};
}

std::optional<int> Viewport3D::getPointAtMouse(const QPoint& pos) {
    if (placedPoints_.empty()) return std::nullopt;

    makeCurrent();
    glPushMatrix();
    camera_.applyViewMatrix();
    glScalef(scaleFactor_, scaleFactor_, scaleFactor_);
    glTranslatef(-modelCenter_.x(), -modelCenter_.y(), -modelCenter_.z());

    auto [rayOrigin, rayDir] = getRayFromMouse(pos);
    glPopMatrix();
    doneCurrent();

    float markerRadius = (markerSize_ / 100.0f) / scaleFactor_;
    float hitRadius    = std::max(0.48f / scaleFactor_, markerRadius * 1.8f);
    float minT = std::numeric_limits<float>::max();
    int hitIdx = -1;

    for (int i = 0; i < static_cast<int>(placedPoints_.size()); ++i) {
        auto t = RayPicking::raySphereIntersect(
            rayOrigin, rayDir, placedPoints_[i].getPosition(), hitRadius);
        if (t && *t < minT) { minT = *t; hitIdx = i; }
    }

    if (hitIdx >= 0) return hitIdx;
    return std::nullopt;
}

bool Viewport3D::trySelectPointAtMouse(const QPoint& pos) {
    auto idx = getPointAtMouse(pos);
    if (idx) {
        selectPoint(*idx);
        return true;
    }
    return false;
}

bool Viewport3D::trySelectSurfaceAtMouse(const QPoint& pos) {
    makeCurrent();
    glPushMatrix();
    camera_.applyViewMatrix();
    glScalef(scaleFactor_, scaleFactor_, scaleFactor_);
    glTranslatef(-modelCenter_.x(), -modelCenter_.y(), -modelCenter_.z());

    auto [rayOrigin, rayDir] = getRayFromMouse(pos);
    glPopMatrix();
    doneCurrent();

    const auto& tris = mesh_.triangles();
    float minT = std::numeric_limits<float>::max();
    int hitTri = -1;

    for (int i = 0; i < static_cast<int>(tris.size()); ++i) {
        auto t = RayPicking::rayTriangleIntersect(rayOrigin, rayDir, tris[i]);
        if (t && *t < minT) { minT = *t; hitTri = i; }
    }

    if (hitTri >= 0 && triangleToSurface_[hitTri] >= 0) {
        selectSurface(triangleToSurface_[hitTri]);
        return true;
    }
    return false;
}

void Viewport3D::addPointAtMouse(const QPoint& pos) {
    auto result = getIntersectionPoint(pos);
    if (!result) return;

    const auto& colors = pointColors();
    Color3f color = colors[nextPointColorIndex_ % colors.size()];
    ++nextPointColorIndex_;

    PlacedPoint pt;
    pt.surfacePoint = result->point;
    pt.normal       = result->normal;
    pt.distance     = 0.0f;
    pt.color        = color;

    pt.pointType = placementPointTypeForNew_;
    deselectSurface();
    placedPoints_.push_back(pt);
    activePointIndex_ = static_cast<int>(placedPoints_.size()) - 1;
    notifyPlacedPointsChanged();
    emit pointPlaced(activePointIndex_);
}

// ==================== Mouse/Key Events ====================

void Viewport3D::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        mouseDown_ = true;
        lastMousePos_ = event->pos();
        mouseDownPos_ = event->pos();

        if (moveMode_ && hasModel()) {
            auto idx = getPointAtMouse(event->pos());
            if (idx) {
                movingPointIndex_ = *idx;
                moveOriginal_ = placedPoints_[movingPointIndex_];
            }
        }
    }
    QOpenGLWidget::mousePressEvent(event);
}

void Viewport3D::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        mouseDown_ = false;

        if (moveMode_ && movingPointIndex_ >= 0) {
            PlacedPoint newState = placedPoints_[movingPointIndex_];
            if (moveOriginal_.surfacePoint != newState.surfacePoint ||
                moveOriginal_.normal != newState.normal) {
                emit moveFinished(movingPointIndex_, moveOriginal_, newState);
            }
            movingPointIndex_ = -1;
            QOpenGLWidget::mouseReleaseEvent(event);
            return;
        }

        QPoint delta = event->pos() - mouseDownPos_;
        float dragDist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());

        if (dragDist < 5 && hasModel()) {
            if (measureMode_) {
                // Snap to a placed point's position when one is under the cursor, else the surface hit.
                std::optional<Vec3f> target;
                if (auto idx = getPointAtMouse(event->pos()))
                    target = placedPoints_[*idx].getPosition();
                else if (auto hit = getIntersectionPoint(event->pos()))
                    target = hit->point;
                if (target) {
                    if (!measurePoint1_) {
                        measurePoint1_ = target;
                    } else {
                        measurePoint2_ = target;
                        float dist = (*measurePoint2_ - *measurePoint1_).norm() * scaleFactor_;
                        emit measurementResult(dist);
                    }
                }
            } else if (trySelectPointAtMouse(event->pos())) {
            } else if (placementMode_) {
                addPointAtMouse(event->pos());
            } else {
                if (!trySelectSurfaceAtMouse(event->pos())) {
                    clearFullSelection();
                }
            }
            update();
        }
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void Viewport3D::mouseMoveEvent(QMouseEvent* event) {
    if (mouseDown_) {
        if (moveMode_ && movingPointIndex_ >= 0) {
            auto result = getIntersectionPoint(event->pos());
            if (result) {
                placedPoints_[movingPointIndex_].surfacePoint = result->point;
                placedPoints_[movingPointIndex_].normal = result->normal;
                update();
            }
        } else {
            QPoint delta = event->pos() - lastMousePos_;
            camera_.orbit(delta.x(), delta.y());
            lastMousePos_ = event->pos();
            update();
        }
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void Viewport3D::wheelEvent(QWheelEvent* event) {
    float delta = event->angleDelta().y() / 120.0f;

    if (placementMode_ && activePointIndex_ >= 0) {
        adjustActivePointDistance(delta > 0 ? 0.1f : -0.1f);
    } else {
        // Zoom toward the surface under the cursor: shift the target by the same
        // fraction the distance changed, so that point stays put on screen.
        auto hit = getIntersectionPoint(event->position().toPoint());
        float oldDist = camera_.distance();
        camera_.zoom(delta, getRealWorldSize());
        if (hit && oldDist > 0.0f) {
            Vec3f p = (hit->point - modelCenter_) * scaleFactor_;
            Vec3f t = camera_.target();
            camera_.setTarget(t + (p - t) * (1.0f - camera_.distance() / oldDist));
        }
    }
    update();
    QOpenGLWidget::wheelEvent(event);
}

static bool isMoveKey(int key) {
    switch (key) {
    case Qt::Key_W: case Qt::Key_Up:
    case Qt::Key_S: case Qt::Key_Down:
    case Qt::Key_A: case Qt::Key_Left:
    case Qt::Key_D: case Qt::Key_Right:
        return true;
    default:
        return false;
    }
}

void Viewport3D::stepKeyMovement() {
    const float dt = moveClock_.restart() / 1000.0f;
    float right = 0.0f, forward = 0.0f;
    for (int key : heldMoveKeys_) {
        switch (key) {
        case Qt::Key_W: case Qt::Key_Up:    forward += 1.0f; break;
        case Qt::Key_S: case Qt::Key_Down:  forward -= 1.0f; break;
        case Qt::Key_A: case Qt::Key_Left:  right -= 1.0f;   break;
        case Qt::Key_D: case Qt::Key_Right: right += 1.0f;   break;
        }
    }
    const float step = 1.0f * camera_.distance() * dt;  // one camera distance per second
    camera_.pan(right * step, forward * step);
    update();
}

void Viewport3D::keyReleaseEvent(QKeyEvent* event) {
    if (isMoveKey(event->key()) && !event->isAutoRepeat()) {
        heldMoveKeys_.erase(event->key());
        if (heldMoveKeys_.empty()) moveTimer_.stop();
    }
    QOpenGLWidget::keyReleaseEvent(event);
}

void Viewport3D::focusOutEvent(QFocusEvent* event) {
    // A key released while unfocused never sends a release event here
    heldMoveKeys_.clear();
    moveTimer_.stop();
    QOpenGLWidget::focusOutEvent(event);
}

void Viewport3D::keyPressEvent(QKeyEvent* event) {
    if (isMoveKey(event->key())) {
        if (!event->isAutoRepeat()) {
            heldMoveKeys_.insert(event->key());
            if (!moveTimer_.isActive()) {
                moveClock_.start();
                moveTimer_.start();
            }
        }
        QOpenGLWidget::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_T:
        transparentMode_ = !transparentMode_;
        update();
        break;
    case Qt::Key_R:
        resetCamera();
        break;
    case Qt::Key_P:
        if (!placementMode_) {
            placementPointTypeForNew_ = POINT_TYPE_SOURCE;
            setPlacementMode(true);
        } else if (placementPointTypeForNew_ == POINT_TYPE_SOURCE) {
            placementPointTypeForNew_ = POINT_TYPE_LISTENER;
            emit placementModeChanged(true);
            update();
        } else {
            setPlacementMode(false);
        }
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (activePointIndex_ >= 0) removeActivePoint();
        break;
    case Qt::Key_C:
        clearPlacedPoints();
        break;
    default:
        break;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void Viewport3D::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-prs-material") && hasModel())
        event->acceptProposedAction();
}

void Viewport3D::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/x-prs-material"))
        event->acceptProposedAction();
}

void Viewport3D::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasFormat("application/x-prs-material"))
        return;

    QByteArray data = event->mimeData()->data("application/x-prs-material");
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    Material mat;
    mat.name = obj["name"].toString().toStdString();
    mat.category = obj["category"].toString().toStdString();
    mat.thickness = obj["thickness"].toString().toStdString();
    mat.scattering = static_cast<float>(obj["scattering"].toDouble(0.1));
    mat.texturePath = obj["texture"].toString().toStdString();

    if (obj.contains("absorption") && obj["absorption"].isObject()) {
        QJsonObject abs = obj["absorption"].toObject();
        for (int i = 0; i < NUM_FREQ_BANDS; ++i) {
            QString key = QString::number(FREQ_BANDS[i]);
            if (abs.contains(key))
                mat.absorption[i] = static_cast<float>(abs[key].toDouble(0.2));
        }
    }

    if (obj.contains("color") && obj["color"].isArray()) {
        QJsonArray c = obj["color"].toArray();
        if (c.size() >= 3)
            mat.color = {c[0].toInt(160), c[1].toInt(160), c[2].toInt(160)};
    }

    QPoint dropPos = event->position().toPoint();
    auto hit = getIntersectionPoint(dropPos);
    if (hit) {
        if (triangleToSurface_[hit->triIndex] >= 0) {
            assignMaterial(triangleToSurface_[hit->triIndex], mat);
            event->acceptProposedAction();
        }
    }
}

} // namespace prs
