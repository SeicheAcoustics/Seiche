find_package(Qt6 REQUIRED COMPONENTS Test Widgets OpenGL OpenGLWidgets Multimedia)

set(TEST_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/Material.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MaterialLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SoundSource.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Listener.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PlacedPoint.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ProjectFile.cpp
    ${CMAKE_SOURCE_DIR}/src/scene/SceneManager.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/Camera.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/MeshData.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/SurfaceGrouper.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/RayPicking.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/MeshSimplifier.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/TextureManager.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/Wall.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/Bvh.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/ImageSourceMethod.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/RayTracer.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/RoomImpulseResponse.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/AcousticSimulator.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/AcousticMetrics.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/AudioFile.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/SignalProcessing.cpp
)

set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
)

function(add_prs_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE} ${TEST_COMMON_SOURCES})
    target_include_directories(${TEST_NAME} PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(${TEST_NAME} PRIVATE
        Qt6::Test Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Eigen3::Eigen)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(${TEST_NAME} PRIVATE OpenMP::OpenMP_CXX)
    endif()
    if(WIN32)
        target_link_libraries(${TEST_NAME} PRIVATE opengl32 glu32)
    elseif(APPLE)
        find_package(OpenGL REQUIRED)
        target_link_libraries(${TEST_NAME} PRIVATE OpenGL::GL OpenGL::GLU)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(${TEST_NAME} PRIVATE OpenGL::GL OpenGL::GLU)
    endif()
    target_compile_definitions(${TEST_NAME} PRIVATE
        _USE_MATH_DEFINES
        SEICHE_ENABLE_GL_COMPUTE=$<BOOL:${SEICHE_ENABLE_GL_COMPUTE}>
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endfunction()

add_prs_test(test_core test_core.cpp)
add_prs_test(test_scene_manager test_scene_manager.cpp)
add_prs_test(test_audio test_audio.cpp)
add_prs_test(test_mesh test_mesh.cpp)
add_prs_test(test_rendering test_rendering.cpp)
add_prs_test(test_acoustics test_acoustics.cpp)
add_prs_test(test_project_file test_project_file.cpp)

# GUI test needs MOC and more sources
set(GUI_TEST_EXTRA
    ${CMAKE_SOURCE_DIR}/src/gui/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/LibraryPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/PropertyPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/AssetsPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/BottomToolbar.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/IconUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/TranslationManager.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/TranslationManager.h
    ${CMAKE_SOURCE_DIR}/src/gui/UndoCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/dialogs/SettingsDialogs.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/dialogs/AudioComparisonDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/dialogs/RenderOptionsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/widgets/MaterialGallery.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/widgets/SurfaceGallery.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/widgets/ColorSwatch.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/ResourcePath.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/SimulationWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/RenderExports.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/SimulationQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGBasis2D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGBasis3D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGMesh2D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGMesh3D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGAcoustics2D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGAcoustics3D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGSolver.cpp
    ${CMAKE_SOURCE_DIR}/${DG_GPU_SOURCE}
    ${CMAKE_SOURCE_DIR}/src/gui/SimulationQueuePanel.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/SimulationQueueWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/Viewport3D.cpp
)
qt6_add_resources(TEST_RESOURCES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
add_executable(test_gui test_gui.cpp ${TEST_COMMON_SOURCES} ${GUI_TEST_EXTRA} ${TEST_RESOURCES})
target_include_directories(test_gui PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_gui PRIVATE
    Qt6::Test Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Multimedia Qt6::Svg Eigen3::Eigen)
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_gui PRIVATE OpenMP::OpenMP_CXX)
endif()
if(WIN32)
    target_link_libraries(test_gui PRIVATE opengl32 glu32)
elseif(APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_gui PRIVATE OpenGL::GL OpenGL::GLU)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_gui PRIVATE OpenGL::GL OpenGL::GLU)
endif()
target_compile_definitions(test_gui PRIVATE
    _USE_MATH_DEFINES
    SEICHE_ENABLE_GL_COMPUTE=$<BOOL:${SEICHE_ENABLE_GL_COMPUTE}>
)
set_target_properties(test_gui PROPERTIES AUTOMOC ON AUTORCC ON)
add_test(NAME test_gui COMMAND test_gui)
set_tests_properties(test_gui PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# CLI render integration test
set(RENDER_CLI_TEST_EXTRA
    ${CMAKE_SOURCE_DIR}/src/acoustics/RenderExports.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/RenderPipeline.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/SimulationWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/SimulationQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGBasis2D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGBasis3D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGMesh2D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGMesh3D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGAcoustics2D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGAcoustics3D.cpp
    ${CMAKE_SOURCE_DIR}/src/acoustics/dg/DGSolver.cpp
    ${CMAKE_SOURCE_DIR}/${DG_GPU_SOURCE}
    ${CMAKE_SOURCE_DIR}/src/utils/ResourcePath.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/Viewport3D.cpp
)
add_executable(test_render_cli test_render_cli.cpp ${TEST_COMMON_SOURCES} ${RENDER_CLI_TEST_EXTRA})
target_include_directories(test_render_cli PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_render_cli PRIVATE
    Qt6::Test Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Multimedia Qt6::Svg Eigen3::Eigen)
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_render_cli PRIVATE OpenMP::OpenMP_CXX)
endif()
if(WIN32)
    target_link_libraries(test_render_cli PRIVATE opengl32 glu32)
elseif(APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_render_cli PRIVATE OpenGL::GL OpenGL::GLU)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_render_cli PRIVATE OpenGL::GL OpenGL::GLU)
endif()
target_compile_definitions(test_render_cli PRIVATE
    _USE_MATH_DEFINES
    SEICHE_ENABLE_GL_COMPUTE=$<BOOL:${SEICHE_ENABLE_GL_COMPUTE}>
)
set_target_properties(test_render_cli PROPERTIES AUTOMOC ON)
add_test(NAME test_render_cli COMMAND test_render_cli)
set_tests_properties(test_render_cli PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
