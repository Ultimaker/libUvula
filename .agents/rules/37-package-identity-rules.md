---
name: package-identity
description: Package identity, dual packaging pipeline (Conan 2 uvula and npm @ultimaker/uvulajs via cura-workflows), and versioning rules.
trigger: glob
glob: "conanfile.py,UvulaJS/package.json,UvulaJS/CMakeLists.txt,.github/workflows/package.yml"
paths:
  - "conanfile.py"
  - "UvulaJS/package.json"
  - "UvulaJS/CMakeLists.txt"
  - ".github/workflows/package.yml"
---
# Package Identity & Dual Packaging Pipeline

`libUvula` publishes two distinct artifacts through central UltiMaker reusable GitHub Actions workflows (`Ultimaker/cura-workflows`):

## 1. Native / Python Package: `uvula` (Conan 2)
- **Manifest**: `conanfile.py` (requires Conan `>=2.7.0`).
- **CI Workflow**: `cura-workflows/.github/workflows/conan-package.yml` in `.github/workflows/package.yml`.
- **Consumers**: **UltiMaker Cura** and Uranium plugins.
- **Published Surface**: Static C++ library `libuvula`, public C++ headers `include/`, and Python pybind11 module `pyUvula`.
- **Versioning**: Version is computed and assigned during CI packaging via `conan create` based on Git tags/refs. Do not hardcode conflicting version manifests.

## 2. WebAssembly Package: `@ultimaker/uvulajs` (npm)
- **Manifest**: `UvulaJS/package.json` and `UvulaJS/CMakeLists.txt`.
- **CI Workflow**: `cura-workflows/.github/workflows/npm-package.yml` in `.github/workflows/package.yml`.
- **Consumers**: **Neoprep** (`@ultimaker/neoprep`) and **Digital Factory** web runtimes.
- **Published Surface**: WebAssembly binary bundle `uvula_js.js` / `uvula_js.wasm` with generated TypeScript definitions `uvula_js.d.ts`.
- **Workflow Triggers**: Package builds are triggered on `main`, `master`, and feature branches matching `NP-*`, `PP-*`, `CURA-*`, `DEV-*`, and `UC-*` (ensuring Neoprep / Cloud branches receive package test artifacts).

## 3. Package Invariants
- **No Hand-Rolled Manifest Divergence**: `conanfile.py` and `UvulaJS/package.json` must remain the single sources of truth.
- **Downstream Consumer Pinning**: Consumers pin specific versions or commit refs. Any breaking change to `include/`, `UvulaJS/`, or `pyUvula/` requires coordinating paired downstream PRs across Cura and Neoprep repositories.
