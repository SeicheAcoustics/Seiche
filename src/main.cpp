#include "gui/MainWindow.h"
#include "gui/IconUtils.h"
#include "gui/TranslationManager.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QPalette>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette light;
    light.setColor(QPalette::Window, QColor(240, 240, 240));
    light.setColor(QPalette::WindowText, QColor(30, 30, 30));
    light.setColor(QPalette::Base, QColor(255, 255, 255));
    light.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    light.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
    light.setColor(QPalette::ToolTipText, QColor(30, 30, 30));
    light.setColor(QPalette::Text, QColor(30, 30, 30));
    light.setColor(QPalette::Button, QColor(230, 230, 230));
    light.setColor(QPalette::ButtonText, QColor(30, 30, 30));
    light.setColor(QPalette::BrightText, Qt::red);
    light.setColor(QPalette::Link, QColor(0, 100, 200));
    light.setColor(QPalette::Highlight, QColor(76, 163, 224));
    light.setColor(QPalette::HighlightedText, Qt::white);
    light.setColor(QPalette::PlaceholderText, QColor(110, 110, 110));
    app.setPalette(light);

    app.setApplicationName("PyRoomStudio");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("PyRoomStudio");
    app.setWindowIcon(prs::iconFromSvgResource(":/logo.svg", 48));

    // Request OpenGL context with depth buffer
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    QSurfaceFormat::setDefaultFormat(format);

    prs::TranslationManager::instance().initialize();

    prs::MainWindow window;
    window.show();
    prs::TranslationManager::instance().translateApplication();

    return app.exec();
}
