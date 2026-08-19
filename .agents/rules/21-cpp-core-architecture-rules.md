---
name: cpp-core-architecture
description: Core C++ architecture, error handling, mined idiomatic preferences, memory safety, and testing rules.
trigger: glob
glob: "**/*.cpp,**/*.hpp,**/*.cc,**/*.cxx,**/*.h,**/CMakeLists.txt,**/*.cmake"
paths:
  - "**/*.cpp"
  - "**/*.hpp"
  - "**/*.cc"
  - "**/*.cxx"
  - "**/*.h"
  - "**/CMakeLists.txt"
  - "**/*.cmake"
---
# Core C++ Architecture Rules

0. **Load the domain skills first**: Load the **`cpp-pro`** skill before designing, implementing, or refactoring C++ code (alongside `cmake`, and `conan-2` for dependencies). All C++23 modernization work MUST follow the staged modernization playbook from `cpp-pro`.

1. **Modern C++ & Memory Safety (`cpp-pro` Modernization Playbook)**:
   - Adhere to C++20/23 standards. Modernize new code and refactorings following the staged `cpp-pro` progression:
     1. **Mechanical & Static Analysis**: Enforce static analysis (`clang-format`, `clang-tidy`, `cppcheck`) with zero warnings.
     2. **Ownership & RAII Modernization (ISO Core Guidelines R.1, R.3, F.7)**: Zero raw owning `new`/`delete`; default to `std::unique_ptr`, use `std::shared_ptr` only for shared ownership, and pass non-owning references as `T&` or `std::span<T>`.
     3. **API & Vocabulary Modernization**: Replace out-parameters with structured returns, sentinel values with `std::optional` (using monadic `and_then`, `transform`, `or_else`), and transition error paths towards `std::expected<T, E>`.
     4. **C++23 Standard Features**: Adopt standard `std::ranges`, `std::to_underlying` for enums, `std::unreachable()` for exhaustive branching, compile-time `constexpr` / `consteval` evaluation, and deducing `this`.
   - No raw owning `new`/`delete`; use smart pointers and containers.
2. **Error Handling — Exception Safety**:
   - The codebase uses exceptions. New code MUST provide at least the strong or basic exception-safety guarantee via RAII.
   - Never swallow exceptions silently; catch narrowly, log with context, and rethrow or convert at module boundaries.
3. **Automated Unit Testing**:
   - Every C++ module MUST be covered by unit tests run via `ctest` (Catch2/GoogleTest as configured).
   - Keep computation pure and stateless where the module design allows (e.g. WASM / settings resolution targets).

5. **Emscripten / WASM Target**:
   - This library compiles to WebAssembly via Emscripten. Keep the computation core pure and stateless; side effects belong in the host bindings layer.
   - Do NOT let exceptions cross the embind/WASM boundary — catch at the binding layer and convert to structured error results.
   - Any change to the exported API (`EMSCRIPTEN_BINDINGS`, embind interfaces) MUST keep the JS/TS consumer contract in sync.
   - Verify the Emscripten target still builds before PR creation (in addition to the native build).

6. **Conan 2 Dependency Management** (required: `>=2.7.0`):
   - Use Conan 2 idioms only: `from conan import ConanFile`, `conan.tools.cmake` (CMakeToolchain/CMakeDeps), `cmake_layout`.
   - NEVER import from the legacy `conans` (v1) namespace.
   - Pin dependency versions/ranges in `conanfile.py` or `requirements.yaml`; do not add ad-hoc FetchContent or system-package dependencies for things Conan should own.
