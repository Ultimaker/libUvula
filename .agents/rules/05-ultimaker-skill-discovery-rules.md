---
name: ultimaker-skill-discovery
description: Skills from the UltiCortex catalogue that apply to this repository, and when to load them.
trigger: always_on
---
# UltiMaker Skill Discovery & Usage

This repository has been matched against the UltiCortex skill catalogue. Loading
the relevant skill is **not optional** for the work it covers: these skills carry
the standards, idioms, and tooling knowledge that the rules in this directory
assume you already have.

```bash
# Search the catalogue
gh skill search ultimaker --owner Ultimaker

# Install a specific skill
gh skill install Ultimaker/UltiCortex <skill-name>
```

Load the skill **before** designing or implementing, not after review comments
arrive. If a skill contradicts a rule in this directory, raise the conflict
rather than silently picking one.

## Skills Matched To This Repository

### `conan-2` — when touching `**/conanfile.py`, `**/conanfile.txt`, `**/conandata.yml`

Engineering guide for Conan 2 dependency management, cross-compilation, CMake integrations, and packaging workflows.

**Why it applies here:** Dependency graph, profiles, cross-compilation and packaging are Conan 2 concerns; the skill carries the v2 idioms and the CMakeToolchain/CMakeDeps integration this repository relies on.

```bash
gh skill install Ultimaker/UltiCortex conan-2
```

### `cmake` — when touching `**/CMakeLists.txt`, `**/*.cmake`, `**/CMakePresets.json`

Modern target-centric C/C++ engineering with CMake 3 & 4.

**Why it applies here:** Target-centric CMake, presets, and CTest wiring — avoid reinventing build logic or reaching for directory-scoped commands.

```bash
gh skill install Ultimaker/UltiCortex cmake
```

### `cpp-pro` — when touching `**/*.cpp`, `**/*.hpp`, `**/*.cc`, `**/*.cxx`

Expert modern C++ (C++11 through C++23) engineering grounded in the ISO C++ Core Guidelines.

**Why it applies here:** Modern C++20/23 implementation work: templates, zero-overhead abstractions, and the idioms mined into the C++ architecture rule.

```bash
gh skill install Ultimaker/UltiCortex cpp-pro
```

### `software-architect` — always relevant

Expert software-architecture advisor: design patterns (GoF), SOLID/DRY, enterprise patterns (Fowler PoEAA), DDD/CQRS, evolutionary architecture (monolith-first, strangler fig, microservices), distributed systems, C4 diagrams, C++….

**Why it applies here:** SOLID, DRY and the design-pattern catalogue — the reference to consult when a change needs decomposition rather than more lines in an existing module.

```bash
gh skill install Ultimaker/UltiCortex software-architect
```

### `ultimaker-neoprep-development` — when touching `UvulaJS/**`

Comprehensive guide to developing, building, testing, and operating Neoprep React/Three.js applications, including integration with WebAssembly modules (`@ultimaker/uvulajs`).

**Why it applies here:** UvulaJS Emscripten bindings, Web Worker execution, Three.js matrix conventions, and `@ultimaker/uvulajs` packaging.

```bash
gh skill install Ultimaker/UltiCortex ultimaker-neoprep-development
```

### `ultimaker-cura-development` — when touching `pyUvula/**`

Comprehensive guide to working with Ultimaker Cura application architecture, Uranium plugins, container stacks, and native C++/Python bindings.

**Why it applies here:** Cura native C++ core and `pyUvula` Python bindings packaging and integration.

```bash
gh skill install Ultimaker/UltiCortex ultimaker-cura-development
```

## Other Catalogue Skills

23 further skill(s) exist that no automatic trigger matched.
They are indexed in `.agents/rules/40-skill-discovery-index-rules.md` (a
model-decision rule, loaded only when judged relevant) rather than here,
because by construction they are the ones static detection judged irrelevant —
and this file is loaded every session.

```bash
gh skill search ultimaker --owner Ultimaker
```
