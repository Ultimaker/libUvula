set(ENV{LD_LIBRARY_PATH} "${CMAKE_LIBRARY_PATH}:${LD_LIBRARY_PATH}")  # Needed to ensure that CMake finds the Conan CPython library
find_package(Python3 COMPONENTS Interpreter Development.Module)
find_package(pybind11 REQUIRED)

pybind11_add_module(pyUvula pyUvula.cpp)
set(NEEDED_DEPS pybind11::pybind11)
if (NOT MSVC)
    list(APPEND NEEDED_DEPS stdc++)
endif()
target_link_libraries(pyUvula PUBLIC libuvula ${NEEDED_DEPS})
target_compile_definitions(pyUvula PRIVATE PYUVULA_VERSION="${PYUVULA_VERSION}")
if (NOT MSVC AND NOT "${CMAKE_BUILD_TYPE}" MATCHES "Debug|RelWithDebInfo")
    pybind11_strip(pyUvula)
endif ()

# Install rule for pip / scikit-build-core wheel packaging.
# Dropping the module at the wheel root makes `import pyUvula` work.
install(TARGETS pyUvula
        LIBRARY DESTINATION .
        RUNTIME DESTINATION .
        COMPONENT pyUvula
)
