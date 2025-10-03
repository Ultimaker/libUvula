cmake_minimum_required(VERSION 3.23)
project(uvula)

find_package(standardprojectsettings REQUIRED)
find_package(spdlog REQUIRED)
find_package(range-v3 REQUIRED)
find_package(clipper REQUIRED)

option(EXTENSIVE_WARNINGS "Build with all warnings" ON)

option(WITH_PYTHON_BINDINGS "Build with Python bindings: `pyUvula`" ON)
if (WITH_PYTHON_BINDINGS)
    set(PYUVULA_VERSION "1.0.0" CACHE STRING "Version of the pyuvula python bindings")
    message(STATUS "Configuring pyUvula version: ${PYUVULA_VERSION}")
endif ()

if (NOT DEFINED UVULA_VERSION)
    message(FATAL_ERROR "UVULA_VERSION is not defined!")
endif ()
message(STATUS "Configuring Uvula version: ${UVULA_VERSION}")

set(UVULA_SRC
        src/xatlas.cpp
        src/unwrap.cpp
        src/project.cpp
        src/Vector3F.cpp
        src/Vector2F.cpp
        src/Matrix33F.cpp
        src/Matrix44F.cpp
        src/Triangle3F.cpp
        src/Point3F.cpp
        src/geometry_utils.cpp
)
add_library(libuvula STATIC ${UVULA_SRC})

target_link_libraries(libuvula
        PUBLIC
        spdlog::spdlog
        range-v3::range-v3
        clipper::clipper
)

target_include_directories(libuvula
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(libuvula
        PUBLIC
        UVULA_VERSION="${UVULA_VERSION}"
)

use_threads(libuvula)
enable_sanitizers(libuvula)
if (${EXTENSIVE_WARNINGS})
    set_project_warnings(libuvula)
endif ()

# --- Setup Python bindings ---
if (WITH_PYTHON_BINDINGS)
    add_subdirectory(pyUvula)
endif ()

# --- Setup command line interface (for testing purposes) ---
if (WITH_CLI)
    add_subdirectory(cli)
endif ()
