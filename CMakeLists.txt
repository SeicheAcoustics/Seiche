cmake_minimum_required(VERSION 3.16)
project(Seiche VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Multimedia Svg)

# Eigen3: try system install first, fall back to FetchContent
find_package(Eigen3 3.3 QUIET)
if(NOT Eigen3_FOUND)
    message(STATUS "Eigen3 not found on system -- downloading via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG        3.4.0
        GIT_SHALLOW    TRUE
    )
    # Use Populate (not MakeAvailable) to skip Eigen's CMakeLists.txt entirely.
    # MakeAvailable triggers BLAS detection which calls enable_language(Fortran)
    # and fails on CI runners that have a broken gfortran in PATH.
    # Eigen is header-only for our use; no Fortran is needed.
    FetchContent_GetProperties(eigen)
    if(NOT eigen_POPULATED)
        if(POLICY CMP0168)
            cmake_policy(SET CMP0168 OLD) # suppress FetchContent_Populate deprecation (CMake 3.30+)
        endif()
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD) # allow FetchContent_Populate with declared details (CMake 3.30+)
        endif()
        FetchContent_Populate(eigen)
    endif()
    add_library(Eigen3::Eigen INTERFACE IMPORTED GLOBAL)
    target_include_directories(Eigen3::Eigen INTERFACE "${eigen_SOURCE_DIR}")
    set(EIGEN3_INCLUDE_DIR "${eigen_SOURCE_DIR}")
endif()

if(APPLE AND NOT DEFINED OpenMP_CXX_FLAGS)
    # AppleClang ships no libomp; use Homebrew's when installed
    execute_process(COMMAND brew --prefix libomp OUTPUT_VARIABLE LIBOMP_PREFIX
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(LIBOMP_PREFIX AND EXISTS "${LIBOMP_PREFIX}/lib/libomp.dylib")
        set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${LIBOMP_PREFIX}/include")
        set(OpenMP_CXX_LIB_NAMES omp)
        set(OpenMP_omp_LIBRARY "${LIBOMP_PREFIX}/lib/libomp.dylib")
    endif()
endif()
find_package(OpenMP)

option(SEICHE_ENABLE_GL_COMPUTE "Enable OpenGL 4.3 compute/SSBO backend" ON)
if(APPLE)
    set(SEICHE_ENABLE_GL_COMPUTE OFF CACHE BOOL "" FORCE)
endif()

if(SEICHE_ENABLE_GL_COMPUTE)
    set(DG_GPU_SOURCE src/acoustics/dg/DGGpuCompute.cpp)
else()
    set(DG_GPU_SOURCE src/acoustics/dg/DGGpuCompute_stub.cpp)
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFTW3 fftw3f)
endif()

if(NOT FFTW3_FOUND)
    find_library(FFTW3_LIBRARIES NAMES fftw3f libfftw3f-3)
    find_path(FFTW3_INCLUDE_DIRS fftw3.h)
endif()

set(CORE_SOURCES
    src/core/Material.cpp
    src/core/MaterialLoader.cpp
    src/core/SoundSource.cpp
    src/core/Listener.cpp
    src/core/PlacedPoint.cpp
    src/core/ProjectFile.cpp
    src/scene/SceneManager.cpp
    src/rendering/Viewport3D.cpp
    src/rendering/Camera.cpp
    src/rendering/MeshData.cpp
    src/rendering/SurfaceGrouper.cpp
    src/rendering/RayPicking.cpp
    src/rendering/MeshSimplifier.cpp
    src/rendering/TextureManager.cpp
    src/acoustics/Bvh.cpp
    src/acoustics/ImageSourceMethod.cpp
    src/acoustics/RayTracer.cpp
    src/acoustics/RoomImpulseResponse.cpp
    src/acoustics/Wall.cpp
    src/acoustics/SimulationWorker.cpp
    src/acoustics/RenderExports.cpp
    src/acoustics/RenderPipeline.cpp
    src/acoustics/SimulationQueue.cpp
    src/acoustics/AcousticMetrics.cpp
    src/acoustics/dg/DGBasis2D.cpp
    src/acoustics/dg/DGBasis3D.cpp
    src/acoustics/dg/DGMesh2D.cpp
    src/acoustics/dg/DGMesh3D.cpp
    src/acoustics/dg/DGAcoustics2D.cpp
    src/acoustics/dg/DGAcoustics3D.cpp
    src/acoustics/dg/DGSolver.cpp
    ${DG_GPU_SOURCE}
    src/audio/AudioFile.cpp
    src/audio/SignalProcessing.cpp
    src/gui/MainWindow.cpp
    src/gui/LibraryPanel.cpp
    src/gui/PropertyPanel.cpp
    src/gui/AssetsPanel.cpp
    src/gui/BottomToolbar.cpp
    src/gui/IconUtils.cpp
    src/gui/TranslationManager.cpp
    src/gui/SimulationQueueWindow.cpp
    src/gui/UndoCommands.cpp
    src/gui/SimulationQueuePanel.cpp
    src/gui/dialogs/SettingsDialogs.cpp
    src/gui/dialogs/AudioComparisonDialog.cpp
    src/gui/dialogs/RenderOptionsDialog.cpp
    src/utils/ResourcePath.cpp
)

set(HEADERS
    src/core/Types.h
    src/core/Material.h
    src/core/MaterialLoader.h
    src/core/SoundSource.h
    src/core/Listener.h
    src/core/PlacedPoint.h
    src/core/ProjectFile.h
    src/scene/SceneManager.h
    src/rendering/GLHeaders.h
    src/rendering/Viewport3D.h
    src/rendering/Camera.h
    src/rendering/MeshData.h
    src/rendering/SurfaceGrouper.h
    src/rendering/RayPicking.h
    src/rendering/MeshSimplifier.h
    src/rendering/TextureManager.h
    src/acoustics/Bvh.h
    src/acoustics/ImageSourceMethod.h
    src/acoustics/RayTracer.h
    src/acoustics/RoomImpulseResponse.h
    src/acoustics/Wall.h
    src/acoustics/SimulationWorker.h
    src/acoustics/RenderOptions.h
    src/acoustics/RenderExports.h
    src/acoustics/RenderPipeline.h
    src/acoustics/SimulationQueue.h
    src/acoustics/AcousticMetrics.h
    src/acoustics/dg/DGTypes.h
    src/acoustics/dg/DGBasis2D.h
    src/acoustics/dg/DGBasis3D.h
    src/acoustics/dg/DGMesh2D.h
    src/acoustics/dg/DGMesh3D.h
    src/acoustics/dg/DGAcoustics2D.h
    src/acoustics/dg/DGAcoustics3D.h
    src/acoustics/dg/DGSolver.h
    src/acoustics/dg/DGGpuCompute.h
    src/audio/AudioFile.h
    src/audio/SignalProcessing.h
    src/gui/MainWindow.h
    src/gui/LibraryPanel.h
    src/gui/PropertyPanel.h
    src/gui/AssetsPanel.h
    src/gui/BottomToolbar.h
    src/gui/IconUtils.h
    src/gui/TranslationManager.h
    src/gui/UndoCommands.h
    src/gui/SimulationQueuePanel.h
    src/gui/SimulationQueueWindow.h
    src/gui/dialogs/SettingsDialogs.h
    src/gui/dialogs/AudioComparisonDialog.h
    src/gui/dialogs/RenderOptionsDialog.h
    src/gui/widgets/TreeFilter.h
    src/utils/ResourcePath.h
)

# Everything except the entry points, shared by the GUI, the CLI and the tests.
add_library(seiche_core STATIC ${CORE_SOURCES} ${HEADERS})

target_include_directories(seiche_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${EIGEN3_INCLUDE_DIR}
)

target_link_libraries(seiche_core PUBLIC
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Multimedia
    Qt6::Svg
    Eigen3::Eigen
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(seiche_core PUBLIC OpenMP::OpenMP_CXX)
endif()

if(FFTW3_LIBRARIES)
    target_include_directories(seiche_core PUBLIC ${FFTW3_INCLUDE_DIRS})
    target_link_libraries(seiche_core PUBLIC ${FFTW3_LIBRARIES})
    target_compile_definitions(seiche_core PUBLIC HAS_FFTW3)
endif()

if(WIN32)
    target_link_libraries(seiche_core PUBLIC opengl32 glu32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(seiche_core PUBLIC OpenGL::GL OpenGL::GLU)
    # Apple deprecated GL/GLU in 10.14; it still works, so mute the per-call warnings.
    target_compile_definitions(seiche_core PUBLIC $<$<PLATFORM_ID:Darwin>:GL_SILENCE_DEPRECATION>)
endif()

target_compile_definitions(seiche_core PUBLIC
    _USE_MATH_DEFINES
    SEICHE_VERSION="${PROJECT_VERSION}"
    SEICHE_ENABLE_GL_COMPUTE=$<BOOL:${SEICHE_ENABLE_GL_COMPUTE}>
)

qt6_add_resources(RESOURCES resources/resources.qrc)

# WIN32 / MACOSX_BUNDLE are ignored on other platforms; the bundle lets macdeployqt package it.
add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE src/main.cpp ${RESOURCES})
# macOS needs a non-empty bundle ID or the out-of-process NSOpenPanel silently never appears.
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.seiche.Seiche
    MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE resources/app.rc)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE seiche_core)
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:QT_QML_DEBUG>)

install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ── Headless CLI render tool ─────────────────────────────────────────────────
add_executable(SeicheCLI src/cli/main.cpp)
target_link_libraries(SeicheCLI PRIVATE seiche_core)

install(TARGETS SeicheCLI
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

enable_testing()
add_subdirectory(tests)
