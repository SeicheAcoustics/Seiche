#include <QtTest/QtTest>
#include <QApplication>
#include <QMenuBar>
#include <QSettings>
#include <QTimer>
#include <QLineEdit>
#include <QListWidget>

#include "gui/MainWindow.h"
#include "gui/PropertyPanel.h"
#include "gui/LibraryPanel.h"
#include "gui/AssetsPanel.h"
#include "gui/BottomToolbar.h"
#include "gui/UndoCommands.h"
#include <QTreeWidget>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QMimeData>
#include <memory>
#include "gui/dialogs/SettingsDialogs.h"
#include "rendering/Viewport3D.h"

using namespace prs;

class TestGUI : public QObject {
    Q_OBJECT
private slots:
    void testMainWindowCreation() {
        MainWindow w;
        QVERIFY(w.windowTitle().contains("Seiche"));
        QVERIFY(w.viewport() != nullptr);
        QVERIFY(w.propertyPanel() != nullptr);
        QVERIFY(w.libraryPanel() != nullptr);
        QVERIFY(w.assetsPanel() != nullptr);
        QVERIFY(w.bottomToolbar() != nullptr);
    }

    void testMenusExist() {
        MainWindow w;
        auto* menuBar = w.menuBar();
        QVERIFY(menuBar != nullptr);
        auto actions = menuBar->actions();
        QStringList menuTitles;
        for (auto* a : actions) menuTitles << a->text();
        QVERIFY(menuTitles.contains("&File"));
        QVERIFY(menuTitles.contains("&Edit"));
        QVERIFY(menuTitles.contains("&View"));
        QVERIFY(menuTitles.contains("&Settings"));
        QVERIFY(menuTitles.contains("&Help"));
    }

    void testPropertyPanelSignals() {
        PropertyPanel panel;
        QSignalSpy scaleSpy(&panel, &PropertyPanel::scaleChanged);
        QSignalSpy sourceSpy(&panel, &PropertyPanel::setPointSource);
        QSignalSpy nameSpy(&panel, &PropertyPanel::pointNameChanged);

        QVERIFY(scaleSpy.isValid());
        QVERIFY(sourceSpy.isValid());
        QVERIFY(nameSpy.isValid());
    }

    void testPropertyPanelSetters() {
        PropertyPanel panel;
        panel.setScaleValue(3.0f);
        panel.setDimensionText("5.0 x 3.0 x 2.5 m");
        panel.setPointDistanceValue(1.5f);
        panel.setPointType("source");
        panel.setMaterialName("Concrete");
        panel.setPointName("Test Point");
        panel.setPointVolume(0.75f);
        panel.setPointAudioFile("test.wav");
    }

    void testAssetsPanelSurfaces() {
        AssetsPanel panel;
        panel.addStlSurfaces("TestModel", {{"Surface 1", 0}, {"Surface 2", 1}});
        auto* tree = panel.findChild<QTreeWidget*>();
        QVERIFY(tree);
        auto leaves = tree->findItems("Surface 2", Qt::MatchExactly | Qt::MatchRecursive);
        QCOMPARE(leaves.size(), 1);

        QSignalSpy clickSpy(&panel, &AssetsPanel::surfaceClicked);
        emit tree->itemClicked(leaves[0], 0);
        QCOMPARE(clickSpy.count(), 1);
        QCOMPARE(clickSpy[0][0].toInt(), 1);

        panel.clearSurfaces();
        QCOMPARE(tree->topLevelItemCount(), 0);
    }

    void testAssetsPanelSearch() {
        AssetsPanel panel;
        panel.addStlSurfaces("TestModel", {{"Floor", 0}, {"Wall", 1}});

        PlacedPoint a, b;
        a.pointType = POINT_TYPE_SOURCE; a.name = "Piano";
        b.pointType = POINT_TYPE_SOURCE; b.name = "Voice";
        panel.updatePointLists({a, b}, -1);

        auto* tree = panel.findChild<QTreeWidget*>();
        auto* floor = tree->findItems("Floor", Qt::MatchExactly | Qt::MatchRecursive)[0];
        auto* wall  = tree->findItems("Wall", Qt::MatchExactly | Qt::MatchRecursive)[0];

        panel.findChild<QLineEdit*>()->setText("wAl");
        QVERIFY(floor->isHidden());
        QVERIFY(!wall->isHidden());
        QListWidget* sources = nullptr;
        for (auto* l : panel.findChildren<QListWidget*>())
            if (l->count() == 2) sources = l;
        QVERIFY(sources);
        QVERIFY(sources->item(0)->isHidden());
        QVERIFY(sources->item(1)->isHidden());

        panel.findChild<QLineEdit*>()->setText("pia");
        QVERIFY(!sources->item(0)->isHidden());
        QVERIFY(sources->item(1)->isHidden());

        panel.findChild<QLineEdit*>()->clear();
        QVERIFY(!floor->isHidden());
        QVERIFY(!wall->isHidden());
        QVERIFY(!sources->item(1)->isHidden());
    }

    void testSurfaceTreeRestoreValidates() {
        AssetsPanel panel;
        panel.addStlSurfaces("TestModel", {{"Surface 1", 0}, {"Surface 2", 1}, {"Surface 3", 2}});

        // Out-of-range (99), duplicate (1) and junk entries are dropped; surface 0 is appended.
        auto in = QJsonDocument::fromJson(R"([
            {"name": "Walls", "children": [1, {"name": "North", "children": [2]}, {"name": "Empty", "children": []}]},
            99, 1, "junk", {"children": [0]}
        ])").array();
        panel.restoreSurfaceTree(in);

        auto expected = QJsonDocument::fromJson(R"([
            {"name": "Walls", "children": [1, {"name": "North", "children": [2]}, {"name": "Empty", "children": []}]},
            0
        ])").array();
        QCOMPARE(panel.surfaceTree(), expected);

        // Empty groups stay visible with no filter.
        auto* tree = panel.findChild<QTreeWidget*>();
        QVERIFY(!tree->findItems("Empty", Qt::MatchExactly | Qt::MatchRecursive)[0]->isHidden());
    }

    void testSurfaceGroupsProfile() {
        QStandardPaths::setTestModeEnabled(true);
        const QString profile =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/profile.json";
        QFile::remove(profile);

        QJsonArray a{QJsonObject{{"name", "G"}, {"children", QJsonArray{0}}}};
        QJsonArray b{1, 0};
        QVERIFY(saveSurfaceGroups("/models/a.stl", a));
        QVERIFY(saveSurfaceGroups("/models/b.stl", b));
        QCOMPARE(loadSurfaceGroups("/models/a.stl"), a);
        QCOMPARE(loadSurfaceGroups("/models/b.stl"), b);
        QVERIFY(loadSurfaceGroups("/models/none.stl").isEmpty());

        // A corrupt profile reads as empty instead of failing.
        QFile f(profile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{not json");
        f.close();
        QVERIFY(loadSurfaceGroups("/models/a.stl").isEmpty());

        QFile::remove(profile);
        QStandardPaths::setTestModeEnabled(false);
    }

    void testBottomToolbarButtons() {
        BottomToolbar toolbar;
        QSignalSpy importRoomSpy(&toolbar, &BottomToolbar::importRoomClicked);
        QSignalSpy addSourceSpy(&toolbar, &BottomToolbar::addSourceClicked);
        QSignalSpy addListenerSpy(&toolbar, &BottomToolbar::addListenerClicked);
        QSignalSpy renderSpy(&toolbar, &BottomToolbar::renderClicked);

        QVERIFY(importRoomSpy.isValid());
        QVERIFY(addSourceSpy.isValid());
        QVERIFY(addListenerSpy.isValid());
        QVERIFY(renderSpy.isValid());
    }

    void testLibraryPanelSignals() {
        LibraryPanel panel;
        QSignalSpy matSpy(&panel, &LibraryPanel::materialSelected);
        QSignalSpy sndSpy(&panel, &LibraryPanel::soundFileSelected);
        QVERIFY(matSpy.isValid());
        QVERIFY(sndSpy.isValid());
    }

    void testLibraryMaterialTree() {
        LibraryPanel panel;
        auto* tree = panel.findChild<QTreeWidget*>();
        QVERIFY(tree);

        // Nested group from materials/wall_treatments/curtains_fabrics
        auto hits = tree->findItems("Curtains Fabrics", Qt::MatchExactly | Qt::MatchRecursive);
        QCOMPARE(hits.size(), 1);
        QVERIFY(hits[0]->parent());
        QCOMPARE(hits[0]->parent()->text(0), QString("Wall Treatments"));

        // Leaves drag as materials the viewport accepts
        QTreeWidgetItem* leaf = hits[0]->child(0);
        auto index = tree->model()->match(tree->model()->index(0, 0), Qt::DisplayRole, leaf->text(0), 1,
                                          Qt::MatchExactly | Qt::MatchRecursive).value(0);
        QVERIFY(index.isValid());
        std::unique_ptr<QMimeData> mime(tree->model()->mimeData({index}));
        QVERIFY(mime && mime->hasFormat("application/x-prs-material"));

        QSignalSpy matSpy(&panel, &LibraryPanel::materialSelected);
        emit tree->itemClicked(leaf, 0);
        QCOMPARE(matSpy.count(), 0);  // single click only selects; double-click applies
        emit tree->itemDoubleClicked(leaf, 0);
        QCOMPARE(matSpy.count(), 1);

        panel.findChild<QLineEdit*>()->setText("velour");
        int shown = 0;
        for (auto* item : tree->findItems("", Qt::MatchContains | Qt::MatchRecursive))
            if (item->childCount() == 0 && !item->isHidden()) ++shown;
        QCOMPARE(shown, 1);
        QVERIFY(!hits[0]->isHidden());
        QVERIFY(tree->findItems("Ceilings", Qt::MatchExactly)[0]->isHidden());
    }

    void testWindowTitleUpdates() {
        MainWindow w;
        QVERIFY(w.windowTitle() == "Seiche");
    }

    // ==================== New Feature Tests ====================

    void testDisplaySettingsApplied() {
        QSettings s("Seiche", "Seiche");
        s.setValue("display/gridVisible", false);
        s.setValue("display/gridSpacing", 2.5);
        s.setValue("display/transparencyAlpha", 0.3);
        s.setValue("display/markerSize", 25);
        s.setValue("display/texturesEnabled", false);

        Viewport3D vp;
        vp.applyDisplaySettings();

        QCOMPARE(vp.gridVisible(), false);
        QVERIFY(std::abs(vp.gridSpacing() - 2.5f) < 0.01f);
        QVERIFY(std::abs(vp.transparencyAlpha() - 0.3f) < 0.01f);
        QCOMPARE(vp.markerSize(), 25);
        QVERIFY(!vp.texturesEnabled());

        s.setValue("display/gridVisible", true);
        s.setValue("display/gridSpacing", 1.0);
        s.setValue("display/transparencyAlpha", 0.55);
        s.setValue("display/markerSize", 15);
        s.setValue("display/texturesEnabled", true);
    }

    void testDisplaySettingsSignalEmitted() {
        DisplaySettingsDialog dlg;
        QSignalSpy spy(&dlg, &DisplaySettingsDialog::settingsChanged);
        QVERIFY(spy.isValid());
    }

    void testDefaultProjectDirPreference() {
        QSettings s("Seiche", "Seiche");
        s.setValue("defaultProjectDir", "/tmp/test_projects");

        MainWindow w;
        s.setValue("defaultProjectDir", "");
    }

    void testAutoSaveTimerSetup() {
        QSettings s("Seiche", "Seiche");
        s.setValue("autoSaveInterval", 5);

        MainWindow w;
        auto* timer = w.findChild<QTimer*>();
        QVERIFY(timer != nullptr);
        QVERIFY(timer->isActive());
        QCOMPARE(timer->interval(), 5 * 60 * 1000);

        s.setValue("autoSaveInterval", 0);
    }

    void testAutoSaveTimerDisabled() {
        QSettings s("Seiche", "Seiche");
        s.setValue("autoSaveInterval", 0);

        MainWindow w;
        auto* timer = w.findChild<QTimer*>();
        QVERIFY(timer != nullptr);
        QVERIFY(!timer->isActive());
    }

    void testSelectAllPoints() {
        Viewport3D vp;

        PlacedPoint p1;
        p1.surfacePoint = Vec3f(1, 0, 0);
        p1.normal = Vec3f(0, 0, 1);
        PlacedPoint p2;
        p2.surfacePoint = Vec3f(2, 0, 0);
        p2.normal = Vec3f(0, 0, 1);
        PlacedPoint p3;
        p3.surfacePoint = Vec3f(3, 0, 0);
        p3.normal = Vec3f(0, 0, 1);

        vp.placedPoints().push_back(p1);
        vp.placedPoints().push_back(p2);
        vp.placedPoints().push_back(p3);

        QCOMPARE(static_cast<int>(vp.selectedPointIndices().size()), 0);

        vp.selectAllPoints();
        QCOMPARE(static_cast<int>(vp.selectedPointIndices().size()), 3);
        QVERIFY(vp.selectedPointIndices().count(0));
        QVERIFY(vp.selectedPointIndices().count(1));
        QVERIFY(vp.selectedPointIndices().count(2));

        vp.clearPointSelection();
        QCOMPARE(static_cast<int>(vp.selectedPointIndices().size()), 0);
    }

    void testSelectAllClearedOnRestore() {
        Viewport3D vp;

        PlacedPoint p1;
        p1.surfacePoint = Vec3f(1, 0, 0);
        p1.normal = Vec3f(0, 0, 1);
        vp.placedPoints().push_back(p1);
        vp.selectAllPoints();
        QCOMPARE(static_cast<int>(vp.selectedPointIndices().size()), 1);

        vp.clearPlacedPoints();
        QCOMPARE(static_cast<int>(vp.selectedPointIndices().size()), 0);
    }

    void testPropertyPanelMaterialPreview() {
        PropertyPanel panel;
        panel.setSurfaceControlsEnabled(true);
        panel.setMaterialPreview(QColor(200, 100, 50));
        QLabel* shown = nullptr;
        for (auto* l : panel.findChildren<QLabel*>())
            if (!l->pixmap().isNull()) shown = l;
        QVERIFY(shown);
        QVERIFY(!shown->isHidden());
        QCOMPARE(shown->pixmap().toImage().pixelColor(4, 4), QColor(200, 100, 50));
        panel.setSurfaceControlsEnabled(false);
        QVERIFY(shown->isHidden());
    }

    void testRemoveActivePointDeselects() {
        Viewport3D vp;
        PlacedPoint p1, p2;
        vp.placedPoints() = {p1, p2};
        vp.selectPoint(0);
        QSignalSpy spy(&vp, &Viewport3D::pointDeselected);
        vp.removeActivePoint();
        QCOMPARE(vp.activePointIndex(), -1);
        QCOMPARE(spy.count(), 1);
    }

    void testRestorePointsDeselects() {
        Viewport3D vp;
        PlacedPoint p1;
        vp.placedPoints() = {p1};
        vp.selectPoint(0);
        QSignalSpy spy(&vp, &Viewport3D::pointDeselected);
        vp.restorePlacedPoints({p1});
        QCOMPARE(vp.activePointIndex(), -1);
        QCOMPARE(spy.count(), 1);
    }

    void testPropertyPanelFollowsPointRemoval() {
        MainWindow w;
        PlacedPoint p1, p2;
        w.viewport()->placedPoints() = {p1, p2};
        w.viewport()->selectPoint(1);
        w.viewport()->removeActivePoint();
        QCOMPARE(w.propertyPanel()->propertyContext(), PropertyPanel::Context::Room);
    }

    void testMoveMode() {
        Viewport3D vp;
        QCOMPARE(vp.moveMode(), false);
        vp.setMoveMode(true);
        QCOMPARE(vp.moveMode(), true);
        vp.setMoveMode(false);
        QCOMPARE(vp.moveMode(), false);
    }

    void testMovePointCommand() {
        Viewport3D vp;

        PlacedPoint pt;
        pt.surfacePoint = Vec3f(1, 0, 0);
        pt.normal = Vec3f(0, 0, 1);
        pt.distance = 0.5f;
        vp.placedPoints().push_back(pt);

        PlacedPoint oldState = pt;
        PlacedPoint newState = pt;
        newState.surfacePoint = Vec3f(3, 2, 0);
        newState.normal = Vec3f(0, 1, 0);

        QUndoStack stack;
        stack.push(new MovePointCommand(&vp, 0, oldState, newState));

        QVERIFY((vp.placedPoints()[0].surfacePoint - Vec3f(3, 2, 0)).norm() < 1e-4f);
        QVERIFY((vp.placedPoints()[0].normal - Vec3f(0, 1, 0)).norm() < 1e-4f);

        stack.undo();
        QVERIFY((vp.placedPoints()[0].surfacePoint - Vec3f(1, 0, 0)).norm() < 1e-4f);
        QVERIFY((vp.placedPoints()[0].normal - Vec3f(0, 0, 1)).norm() < 1e-4f);

        stack.redo();
        QVERIFY((vp.placedPoints()[0].surfacePoint - Vec3f(3, 2, 0)).norm() < 1e-4f);
    }

    void testMoveToolEnabled() {
        MainWindow w;
        auto toolbars = w.findChildren<QToolBar*>();
        QVERIFY(!toolbars.isEmpty());
        bool foundMove = false;
        for (auto* toolbar : toolbars) {
            for (auto* a : toolbar->actions()) {
                if (a->text() == "Move") {
                    foundMove = true;
                    QVERIFY(a->isEnabled());
                    QVERIFY(a->isCheckable());
                }
            }
        }
        QVERIFY(foundMove);
    }
};

QTEST_MAIN(TestGUI)
#include "test_gui.moc"
