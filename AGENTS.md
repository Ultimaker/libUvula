# Agent Operational & Onboarding Guide (AGENTS.md)

Welcome, AI Agent! This document defines the operational boundaries, design patterns, testing strategies, and collaborative conventions for the `libUvula` repository.

As a dynamic assistant, you must adhere strictly to these principles to maintain codebase sanity and ensure future developers can build upon your work efficiently.

---

## 1. SaaS Context, Sibling Services & AI Skill Discovery

### libUvula Context

**libUvula** is a high-performance, standalone C++ UV-unwrapping engine designed for processing massive 3D meshes. It partitions and projects 3D faces into non-overlapping 2D coordinates, supplying the critical coordinate layout required for real-time model painting.

- **WASM compilation**: Compiles using Emscripten to WebAssembly (`UvulaJS`), exporting JavaScript bindings consumed directly by **NeoPrep (Cura Cloud)** inside the web browser for the high-performance 3D paint pipeline.
- **Python Bindings**: Provides `pyUvula` bindings for server-side testing, preprocessing, and validation of unwrapped geometries.
- **No Side-Effects**: Entirely stateless and self-contained; accepts mesh vertices and indices and computes optimal packed textures maps.

### Sibling Services & Relative Paths

To collaborate on cross-repository features or platform emulation, locate and link with sibling services in this local workspace. Use the following relative paths and consult their corresponding `AGENTS.md` operational guides:

- **[neoprep](../neoprep/audit-stardust-atlas-submodules/)**: The interactive React 3D prep workspace. The painting pipeline is described in [painting_pipeline.md](../neoprep/audit-stardust-atlas-submodules/doc/Software%20Architecture/painting_pipeline.md).
- **[Curator](../Curator/)**: Slicing profile resolver.
- **[stardust-atlas](../stardust-atlas/audit-stardust-atlas-submodules/)**: Meta-repository and orchestration layer compiling the full Digital Factory stack.

### 🔍 Dynamic AI Skill Discovery & Installation

To obtain expert domain knowledge and specific runtime tools, agents **must** dynamically discover and install specialized engineering skills from `Ultimaker/UltiCortex` on branch `UC-3695_improve_code_base_for_agentic_development`:

```bash
# Discover and install specialized skills
gh skill install Ultimaker/UltiCortex ultimaker-neoprep-development --branch UC-3695_improve_code_base_for_agentic_development
```

---

## 2. Work Tracking, Git & Pull Request Habits

- **Jira Tracking**: All changes require an active Jira ticket starting with project key **`UC`** or **`NP`** (e.g., `UC-3697` or `NP-1325`). Branch names must be formatted as `[PROJECT_KEY]-[ID]_description`.
- **Git Commit Standards**:
  - **Bracketed Ticket Prefix**: Every Git commit title and GitHub Pull Request title **MUST** start with the active branch's Jira ticket key in bracketed format: `[PROJECT-KEY] <Description>`. For example: `[UC-3697] <Description>`.
  - **No Semantic Prefixes**: Do **NOT** use conventional/semantic commit prefix tags (such as `feat:`, `fix:`, `chore:`, etc.) in commit titles or Pull Request titles.
  - Commit message format:

    ```
    [UC-3697] Configure pre-commit and agentic enablement

    Setup pre-commit hooks and custom copilot instructions for libUvula development.

    Contributes to UC-3697
    ```

- **PR Guidelines**:
  - Always open PRs as **DRAFT** state. Merging is **strictly restricted to humans**.
  - Monitor CI status checks. Ensure build, linter, formatting, and unit tests pass cleanly.

---

## 3. Directory Organization & Architecture Index

### Core Directory Maps:

- `/src/`: C++ source files implementing Smart Project partitioning and spatial splitting.
- `/include/`: Public C++ headers exposing unwrapper interfaces.
- `/UvulaJS/`: Emscripten bindings and WebAssembly compilation scripts.
- `/pyUvula/`: Python bindings implementation.
- `/cli/`: Native CLI testing executable.
- `CMakeLists.txt` & `conanfile.py`: Compilation and package dependency manager configurations.

---

## 4. Deep Dive: Core Technical Architectures

### 4.1. Three-step Unwrapping Pipeline

1. **Normal Proximity Grouping**: Groups faces of the mesh by normal orientation (inspired by Blender's Smart UV Project) to minimize texture distortion.
2. **Spatial Split**: Splits groups based on adjacent connectivity, ensuring disconnected surfaces are unwrapped as independent patches.
3. **Texture Packing**: Packs the 2D projected patches on a flat canvas to maximize density using a high-efficiency [xatlas](https://github.com/jpcy/xatlas/) bin-packer.

---

## 5. Local Setup & Verification

### 🚀 Quick Start

1. Install dependencies and compile using Conan & CMake:
   ```bash
   conan install . --build=missing
   source build/Release/generators/conanbuild.sh
   cmake --preset conan-release
   cmake --build --preset conan-release
   ```

---

## 6. Quality Control, Tooling & Local Verification

To maintain top-tier reliability, libUvula enforces comprehensive Quality Control (QC) tools locally. Succeeding agents and developers **must** run and verify these tools before proposing any Pull Request:

### 🎨 Formatting (clang-format)

Check and write clean, standardized formatting across C++ code:

- **Verification & Auto-fix**: `clang-format -i $(find src include cli -name '*.cpp' -o -name '*.h' -o -name '*.hpp')`

### 🧹 Linting & Static Analysis (cppcheck)

Enforce code quality and memory safety:

- **cppcheck**: Fast static analysis for performance and portability issues:
  ```bash
  cppcheck --enable=warning,performance,portability --inline-suppr src/ include/
  ```

### ⚓ Pre-commit Hook Integration

Pre-commit hooks automatically execute fast checks (clang-format, cppcheck, check-yaml, check-json, talisman, local path blocking, and agent artifact checks) on staged files.

- **Manual Hook Audit**:
  ```bash
  pre-commit run --all-files
  ```
- **Opt-Out (Humans Only)**: Humans may prepend `SKIP_PRE_COMMIT=1` or run `git commit --no-verify`. AI agents **MUST** pass all pre-commit hooks cleanly.
