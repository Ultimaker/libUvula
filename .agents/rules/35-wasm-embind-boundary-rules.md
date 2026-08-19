---
name: wasm-embind-boundary
description: WebAssembly/Embind boundary rules, link options contract, Three.js matrix conventions, and TypeScript type definition synchronization for UvulaJS.
trigger: glob
glob: "UvulaJS/**,src/xatlas.cpp"
paths:
  - "UvulaJS/**"
  - "src/xatlas.cpp"
---
# WebAssembly & Embind Boundary Rules (UvulaJS)

The `UvulaJS` module compiles `libUvula` geometry and unwrap algorithms to WebAssembly via Emscripten (`@ultimaker/uvulajs`). It is consumed directly by **Neoprep** (React 18 / Three.js web preparation) and **Digital Factory** web runtimes inside browser Web Workers.

## 1. Bundler Link Options Contract
The Emscripten link options in `UvulaJS/CMakeLists.txt` constitute a strict public contract with downstream bundlers (Webpack 5, Vite, Rollup):
- `-s ENVIRONMENT=web,worker,node`: Ensures execution in browser Web Workers and Node.js test runners.
- `-s USE_ES6_IMPORT_META=1`: Enables ES6 module asset resolution for bundlers.
- `-s EXPORT_NAME=uvula`: Defines the default factory export name.
- `-s MODULARIZE=1`: Emits a factory function (`uvula()`) returning a Promise resolving to the initialized WASM module instance.
- `-s EXPORT_ES6=1`: Emits standard ES module syntax (`export default`).
- `-s SINGLE_FILE=1`: Inlines the WASM binary directly into the JS bundle as base64, eliminating asynchronous binary file loading in worker bundles.
- `-s ALLOW_MEMORY_GROWTH=1`: Accommodates large mesh allocations dynamically.
- `--emit-tsd uvula_js.d.ts`: Emits TypeScript declarations synchronized with Embind bindings.

Changing any of these flags alters how downstream consumers import the package and constitutes a breaking public API change.

## 2. Three.js Matrix & Coordinate System Conventions
1. **Column-Major Matrix Transposition**:
   - Three.js represents transform and camera projection matrices in 16-element column-major flat arrays (`matrixWorldInverse`, `projectionMatrix`).
   - `Matrix44F` in `libUvula` uses row-major nested arrays `float[4][4]`.
   - `UvulaJS.cpp` MUST transpose elements during marshaling: `matrix_data[i % 4][i / 4] = matrix_array[i]`.
2. **Viewport Stroke Projection Parameters**:
   - `stroke_polygon`: 2D viewport coordinates in screen pixels (`Point2F`).
   - `camera_projection_matrix`: Combined Model-View-Projection (MVP) matrix transforming 3D mesh vertices into 2D viewport space.
   - `camera_normal`: World-space view direction vector (`Vector3F`) used for backface culling (`face_normal.dot(camera_normal) < 0`).
   - `face_id`: Seed face index on the 3D mesh hit by the mouse raycast in the Three.js viewport.
3. **UV Space & Texture Coordinates**:
   - `smartUnwrap` generates normalized UV atlas coordinates in `[0, 1]`.
   - `doProject` outputs polygon vertices scaled to the target texture pixel dimensions (`texture_width`, `texture_height`).
   - WebGL / Three.js textures use standard UV bottom-left origin `(0, 0)`; avoid unintended V-axis flipping.

## 3. Embind Type Declarations & TypeScript Sync
- `EMSCRIPTEN_DECLARE_VAL_TYPE` aliases (`Float32Array`, `Int32Array`, `PolygonArray`) must match the typed arrays passed across the boundary.
- Any change to `EMSCRIPTEN_BINDINGS(uvula)` requires regenerating and verifying `uvula_js.d.ts`.
- Ensure all public functions exposed to JavaScript take and return strongly typed structures compatible with `@ultimaker/uvulajs` declarations in Neoprep.

## 4. Memory & Exception Safety Across the WASM Boundary
- **No Uncaught C++ Exceptions**: WebAssembly builds without `-fwasm-exceptions` trigger uncatchable runtime aborts if a C++ exception crosses Embind. Catch all internal exceptions at the binding layer and return structured status objects (`{ success: false, error: "..." }`) or boolean indicators.
- **Buffer Bounds Validation**: Flat coordinate arrays passed from JS MUST have their lengths verified before indexing (`vertices.length % 3 == 0`, `indices.length % 3 == 0`, `matrix.length == 16`).
- **Single-Threaded Pure Computation**: WASM runs in single-threaded Web Workers. Avoid thread-local statics, global shared state, or blocking mutexes. Ensure `XA_MULTITHREADED 0` is maintained for `xatlas`.
