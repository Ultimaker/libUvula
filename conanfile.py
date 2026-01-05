# (c) 2025, UltiMaker -- see LICENCE for details

from conan import ConanFile
import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.env import VirtualBuildEnv
from conan.tools.files import copy, mkdir, update_conandata
from conan.tools.microsoft import check_min_vs, is_msvc_static_runtime, is_msvc
from conan.tools.scm import Version

required_conan_version = ">=2.7.0"


class UvulaConan(ConanFile):
    name = "uvula"
    description = "Library to unwrap UV coordinates of a 3D mesh"
    author = "UltiMaker"
    license = ""
    url = "https://github.com/Ultimaker/libUvula"
    homepage = "https://ultimaker.com"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "enable_extensive_warnings": [True, False],
        "with_python_bindings": [True, False],
        "with_cli": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "enable_extensive_warnings": False,
        "with_python_bindings": True,
        "with_cli": False,
    }

    def set_version(self):
        if not self.version:
            self.version = self.conan_data["version"]

    @property
    def _min_cppstd(self):
        return 20

    @property
    def _compilers_minimum_version(self):
        return {
            "gcc": "11",
            "clang": "14",
            "apple-clang": "13",
            "msvc": "192",
            "visual_studio": "17",
        }

    def export(self):
        update_conandata(self, {"version": self.version})

    def export_sources(self):
        copy(self, "CMakeLists.txt", self.recipe_folder, self.export_sources_folder)
        copy(self, "*", os.path.join(self.recipe_folder, "src"), os.path.join(self.export_sources_folder, "src"))
        copy(self, "*", os.path.join(self.recipe_folder, "include"),
             os.path.join(self.export_sources_folder, "include"))
        copy(self, "*", os.path.join(self.recipe_folder, "pyUvula"), os.path.join(self.export_sources_folder, "pyUvula"))

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC
        if self.settings.arch == "wasm" and self.settings.os == "Emscripten":
            del self.options.with_python_bindings

    def configure(self):
        if self.options.get_safe("with_python_bindings", False):
            self.options["cpython"].shared = True

    def layout(self):
        cmake_layout(self)

        self.cpp.package.lib = ["uvula", "pyUvula"]
        self.cpp.package.libdirs = ["lib"]
        self.cpp.package.bindirs = ["bin"]

        if self.options.get_safe("with_python_bindings", False):
            self.layouts.build.runenv_info.prepend_path("PYTHONPATH", "pyUvula")
            self.layouts.package.runenv_info.prepend_path("PYTHONPATH", os.path.join("lib", "pyUvula"))


    def requirements(self):
        self.requires("spdlog/1.15.1")
        self.requires("range-v3/0.12.0")
        self.requires("clipper/6.4.2@ultimaker/stable")
        if self.options.get_safe("with_python_bindings", False):
            self.requires("cpython/3.12.7")
            self.requires("pybind11/2.11.1")
        if self.options.get_safe("with_cli", False):
            self.requires("assimp/5.4.3")
            self.requires("cxxopts/3.3.1")

    def build_requirements(self):
        self.test_requires("standardprojectsettings/[>=0.1.0]")

    def validate(self):
        if self.settings.compiler.cppstd:
            check_min_cppstd(self, self._min_cppstd)
        check_min_vs(self, 193)
        if not is_msvc(self):
            minimum_version = self._compilers_minimum_version.get(str(self.settings.compiler), False)
            if minimum_version and Version(self.settings.compiler.version) < minimum_version:
                raise ConanInvalidConfiguration(
                    f"{self.ref} requires C++{self._min_cppstd}, which your compiler does not support."
                )
        if is_msvc(self) and self.options.shared:
            raise ConanInvalidConfiguration(f"{self.ref} can not be built as shared on Visual Studio and msvc.")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["EXTENSIVE_WARNINGS"] = self.options.enable_extensive_warnings
        tc.variables["UVULA_VERSION"] = self.version

        tc.variables["WITH_PYTHON_BINDINGS"] = self.options.get_safe("with_python_bindings", False)
        if self.options.get_safe("with_python_bindings", False):
            tc.variables["PYUVULA_VERSION"] = self.version

        tc.variables["WITH_CLI"] = self.options.get_safe("with_cli", False)

        if is_msvc(self):
            tc.variables["USE_MSVC_RUNTIME_LIBRARY_DLL"] = not is_msvc_static_runtime(self)
        tc.cache_variables["CMAKE_POLICY_DEFAULT_CMP0077"] = "NEW"
        tc.generate()

        tc = CMakeDeps(self)
        tc.generate()

        vb = VirtualBuildEnv(self)
        vb.generate(scope="build")

        for dep in self.dependencies.values():
            if len(dep.cpp_info.libdirs) > 0:
                copy(self, "*.dylib", dep.cpp_info.libdirs[0], self.build_folder)
                copy(self, "*.dll", dep.cpp_info.libdirs[0], self.build_folder)
            if len(dep.cpp_info.bindirs) > 0:
                copy(self, "*.dll", dep.cpp_info.bindirs[0], self.build_folder)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, pattern="LICENSE", dst=os.path.join(self.package_folder, "licenses"), src=self.source_folder)
        copy(self, "*.pyd", src = os.path.join(self.build_folder, "pyUvula"), dst = os.path.join(self.package_folder, "lib", "pyUvula"), keep_path = False)
        copy(self, f"*.d.ts", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"), keep_path = False)
        copy(self, f"*.js", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"), keep_path = False)
        copy(self, pattern="*.h", src=os.path.join(self.source_folder, "include"), dst=os.path.join(self.package_folder, "include"))
        copy(self, pattern="*.a", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, pattern="*.so", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=True)
        copy(self, pattern="*.lib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, pattern="*.dll", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"), keep_path=True)
        copy(self, pattern="*.dylib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=True)

    def package_info(self):
        if self.options.get_safe("with_python_bindings", False):
            self.conf_info.define("user.uvula:pythonpath",
                                  os.path.join(self.package_folder, "lib", "pyUvula"))
