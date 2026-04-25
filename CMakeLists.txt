cmake_minimum_required(VERSION 3.23)

# --- Bootstrap Conan dependencies when building as a pip package ---
# When invoked via `pip install` (scikit-build-core sets UVULA_PIP_BUILD=ON),
# auto-run `conan install` to fetch the C++ dependencies and wire up the
# toolchain before project() is called.
option(UVULA_PIP_BUILD "Auto-run 'conan install' to fetch deps for a pip build" OFF)
if (UVULA_PIP_BUILD AND NOT CMAKE_TOOLCHAIN_FILE AND NOT _UVULA_CONAN_BOOTSTRAPPED)
    find_program(_UVULA_CONAN_EXE conan REQUIRED)
    set(_UVULA_CONAN_OUTPUT "${CMAKE_BINARY_DIR}/conan_deps")
    execute_process(
        COMMAND "${_UVULA_CONAN_EXE}" profile detect --exist-ok
        RESULT_VARIABLE _UVULA_CONAN_PROFILE_RC
    )
    message(STATUS "UVULA_PIP_BUILD: running 'conan install' into ${_UVULA_CONAN_OUTPUT}")
    execute_process(
        COMMAND "${_UVULA_CONAN_EXE}" install "${CMAKE_CURRENT_LIST_DIR}/conanfile_pip.py"
                --build=missing
                --output-folder=${_UVULA_CONAN_OUTPUT}
                -s build_type=Release
                -s compiler.cppstd=20
                -c tools.cmake.cmaketoolchain:generator=Ninja
        RESULT_VARIABLE _UVULA_CONAN_RC
    )
    if (NOT _UVULA_CONAN_RC EQUAL 0)
        message(FATAL_ERROR "conan install failed (exit ${_UVULA_CONAN_RC}). "
                            "Ensure Conan 2.x is installed and any private remotes "
                            "needed for Ultimaker packages are configured.")
    endif()
    file(GLOB_RECURSE _UVULA_TOOLCHAIN_FILES "${_UVULA_CONAN_OUTPUT}/*conan_toolchain.cmake")
    if (NOT _UVULA_TOOLCHAIN_FILES)
        message(FATAL_ERROR "conan install succeeded but no conan_toolchain.cmake was generated under ${_UVULA_CONAN_OUTPUT}")
    endif()
    list(GET _UVULA_TOOLCHAIN_FILES 0 _UVULA_TOOLCHAIN)
    set(CMAKE_TOOLCHAIN_FILE "${_UVULA_TOOLCHAIN}" CACHE FILEPATH "Conan-generated toolchain" FORCE)
    set(_UVULA_CONAN_BOOTSTRAPPED TRUE CACHE INTERNAL "Conan deps already fetched")
    message(STATUS "UVULA_PIP_BUILD: using toolchain ${CMAKE_TOOLCHAIN_FILE}")
endif()

project(uvula)

if (UVULA_PIP_BUILD)
    # The C++ sources use std::span and other C++20 features. The default
    # conan profile typically selects gnu17, so force C++20 explicitly here.
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    # libuvula is STATIC but gets linked into the pyUvula shared module.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    # Inline minimal replacements for the Ultimaker-internal
    # `standardprojectsettings` helpers so a pip install doesn't need the
    # private conan remote. Sanitizers and extensive warnings are no-ops here
    # since a redistributable wheel shouldn't ship with them enabled anyway.
    find_package(Threads)
    function(use_threads target)
        if (TARGET Threads::Threads)
            target_link_libraries(${target} PUBLIC Threads::Threads)
        endif()
    endfunction()
    function(enable_sanitizers target)
    endfunction()
    function(set_project_warnings target)
    endfunction()
else()
    find_package(standardprojectsettings REQUIRED)
endif()
find_package(spdlog REQUIRED)
find_package(range-v3 REQUIRED)
find_package(clipper REQUIRED)

option(EXTENSIVE_WARNINGS "Build with all warnings" ON)

option(WITH_PYTHON_BINDINGS "Build with Python bindings: `pyUvula`" ON)
if (WITH_PYTHON_BINDINGS)
    set(PYUVULA_VERSION "1.0.0" CACHE STRING "Version of the pyuvula python bindings")
    message(STATUS "Configuring pyUvula version: ${PYUVULA_VERSION}")
endif ()

option(WITH_JS_BINDINGS "Build with JavaScript/Emscripten bindings: `UvulaJS`" OFF)
if (WITH_JS_BINDINGS)
    message(STATUS "Configuring UvulaJS with Emscripten bindings")
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

if(NOT EMSCRIPTEN)
    use_threads(libuvula)
endif()
if(EMSCRIPTEN)
    # Skip sanitizers and threading for Emscripten builds to avoid shared memory issues
else()
    enable_sanitizers(libuvula)
endif()
if (${EXTENSIVE_WARNINGS})
    set_project_warnings(libuvula)
endif ()

# --- Setup Python bindings ---
if (WITH_PYTHON_BINDINGS)
    add_subdirectory(pyUvula)
endif ()

# --- Setup JavaScript/Emscripten bindings ---
if (WITH_JS_BINDINGS)
    add_subdirectory(UvulaJS)
endif ()

# --- Setup command line interface (for testing purposes) ---
if (WITH_CLI)
    add_subdirectory(cli)
endif ()
