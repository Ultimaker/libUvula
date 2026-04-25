# Minimal Conan recipe used exclusively by the `pip install` path (see
# pyproject.toml / CMakeLists.txt UVULA_PIP_BUILD bootstrap). It mirrors the
# subset of `conanfile.py` needed to build the Python binding, but without the
# Emscripten-only `npmpackage` python_requires — which would otherwise force
# every conan operation to resolve an Ultimaker-internal package even when
# building the Python wheel.

from conan import ConanFile
from conan.tools.cmake import cmake_layout


class UvulaPipBuildConan(ConanFile):
    name = "uvula-pip-build"
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("spdlog/1.15.1")
        self.requires("range-v3/0.12.0")
        # The main conanfile pins Ultimaker's `clipper/6.4.2@ultimaker/stable`
        # fork, which is hosted on a private remote. Fall back to the upstream
        # `clipper/6.4.2` on conancenter so `pip install` works without that
        # remote. If the build breaks on fork-specific API, configure the
        # Ultimaker remote and pin the @ultimaker/stable revision here instead.
        self.requires("clipper/6.4.2")

    def layout(self):
        cmake_layout(self)
