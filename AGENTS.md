# Agentic Context: libUvula

## Core Ecosystem
`libUvula` is an UltiMaker native C++20 geometry library providing high-performance UV mesh unwrapping and viewport texture projection algorithms.

### Primary Consumers
- **Digital Factory & Neoprep**: Consumes WebAssembly bindings (`UvulaJS` / `@ultimaker/uvulajs`) compiled via Emscripten and executed inside Web Workers with Three.js / React 18 frontends.
- **UltiMaker Cura**: Consumes native C++ core and Python bindings (`pyUvula`) via Conan 2 packages.
- **CLI / Tools**: Standalone `uvula` command-line executable (`cli/`) for inspecting and unwrapping OBJ/STL models.

---

## Architecture & Algorithm Pipeline

`libUvula` provides fast, low-distortion UV atlas generation and live viewport paint stroke projection:

### 1. Smart UV Unwrapping Pipeline (`src/unwrap.cpp`)
1. **Normal Proximity Clustering (`calculateProjectionNormals`)**:
   Groups mesh faces by surface normal similarity within an angular threshold (`group_angle_limit = 20.0°`) to minimize planar projection distortion.
2. **Planar Chart Generation (`makeCharts`)**:
   Projects 3D vertices of clustered faces onto 2D chart planes using orthogonal basis matrices (`Matrix33F::makeOrthogonalBasis`).
3. **Vertex Deduplication & Spatial Segmentation (`groupSimilarVertices`, `splitNonLinkedFacesCharts`)**:
   Deduplicates identical 3D vertex coordinates into shared vertex-group indices (`std::map<Point3F, size_t>`), and segments disjoint coplanar charts using disjoint sets on shared vertex indices.
4. **Atlas Packing (`packCharts` & `xatlas`)**:
   Packs individual 2D charts into a unified, non-overlapping texture atlas coordinate space `[0, 1]` using the embedded `xatlas` packing engine (`src/xatlas.cpp`).

### 2. Viewport Stroke Projection Engine (`src/project.cpp`)
1. **Triangle Forward-Projection**:
   Forward-projects 3D mesh triangles into 2D viewport coordinates via Model-View-Projection (`Matrix44F::preMultiply`) and perspective division (`projected /= (projected.z() * 2.0f)`).
2. **Backface Culling & Clipper Polygon Clipping**:
   Culls backfaces against camera view direction (`face_normal.dot(camera_normal) < 0`), and clips the 2D viewport stroke polygon against the projected 2D triangle using ClipperLib (`CLIPPER_PRECISION = 1000.0`).
3. **Barycentric Interpolation & UV Mapping**:
   Calculates 2D barycentric coordinates `(u, v, w)` for clipped polygon vertices, interpolates texture UV coordinates `[0, width] x [0, height]`, and propagates BFS traversal across adjacent faces using `FaceSigned` adjacency tables (`mesh_faces_connectivity`).
4. **Three.js Matrix Transposition**:
   Three.js flat column-major 16-element matrices (`projectionMatrix`, `matrixWorldInverse`) are transposed into row-major `Matrix44F` in `UvulaJS.cpp`.

---

## Directory Structure

```
libUvula/
├── include/            # Public C++ headers (geometry primitives, unwrap, project, xatlas)
├── src/                # Core C++ implementation (clustering, projection, matrix math)
├── pyUvula/            # Python pybind11 bindings with zero-copy numpy buffer support
├── UvulaJS/            # Emscripten / Embind WebAssembly bindings for web runtimes
├── cli/                # Command-line test utility (cxxopts + assimp mesh loader)
├── conanfile.py        # Conan 2 package recipe (requires >=2.7.0)
├── CMakeLists.txt      # Root CMake configuration (target-centric, C++20)
└── .agents/            # Quad-Agent rules, hooks, and verification harnesses
```

---

## Build & Verification Commands

### Native Build (Conan 2 + CMake)
```bash
# 1. Install dependencies via Conan 2
conan install . --build=missing

# 2. Configure with CMake preset
source build/Release/generators/conanbuild.sh  # (or environment script)
cmake --preset conan-release

# 3. Compile native targets (libuvula, pyUvula, cli)
cmake --build --preset conan-release
```

### WebAssembly Build (Emscripten / UvulaJS)
```bash
# Configure and compile UvulaJS WASM package
emcmake cmake -B build-wasm -DWITH_JS_BINDINGS=ON
cmake --build build-wasm
```

### Formatting & Static Analysis
```bash
# Format C++ source files (excluding vendored xatlas)
clang-format -i $(git ls-files "*.cpp" "*.h" | grep -v "xatlas")

# Run static analysis
clang-tidy -p build src/*.cpp include/*.h
```

---

## Agentic Development & Quality Invariants

- **Jira & Commit Discipline**: Every branch and commit MUST start with `[UC-3697]` (or target ticket key). Commit messages MUST include an explanatory body detailing the rationale and algorithmic impact.
- **Public API Contract (`include/`, `UvulaJS/`, `pyUvula/`)**: All exported headers and bindings constitute a cross-repository contract with Cura and Neoprep. Prefer additive modifications and synchronize downstream PRs.
- **WebAssembly Boundary Contract**: `UvulaJS/CMakeLists.txt` link options (`-s MODULARIZE=1`, `-s EXPORT_ES6=1`, `--emit-tsd`, `-s SINGLE_FILE=1`) must remain synchronized with TypeScript declarations and Neoprep bundlers.
- **Python Buffer Safety**: In `pyUvula`, strictly validate numpy buffer shapes and release the GIL (`py::gil_scoped_release`) during heavy geometry calculations.
- **File Size & Complexity Budgets**: Keep individual files within the budget (max 400 lines for new files, max cyclomatic complexity 10 per function).
