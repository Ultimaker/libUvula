# Custom Rule Proposals & Resolutions — libUvula

These are observations about how *this* repository actually works, gathered during agentic bootstrap on `UC-3697_AI_DF` and audited by the adversarial Red Team.

---

## 1. Commit messages carry explanatory bodies

**Category:** commit-style  **Confidence:** high
**Status:** **RESOLVED** — Promoted to Managed/Custom Rule `.agents/rules/15-commit-body-standards.md`.

**Evidence & Rationale**
- 79% of sampled commits historically contain an explanatory body (average 1.5 lines).
- As a mathematical geometry library consumed by Cura and Neoprep, commit messages serve as critical architectural documentation.
- Rule 15 mandates structured commit bodies explaining *why* and *how*.

---

## 2. A WebAssembly/embind boundary crosses out of this repository

**Category:** ecosystem_contract  **Confidence:** high
**Status:** **RESOLVED** — Promoted to Custom Rule `.agents/rules/35-wasm-embind-boundary-rules.md`.

**Evidence & Rationale**
- `UvulaJS/CMakeLists.txt`, `UvulaJS/UvulaJS.cpp`, `src/xatlas.cpp`.
- Emscripten link options (`-s MODULARIZE=1`, `-s EXPORT_ES6=1`, `--emit-tsd`, `-s SINGLE_FILE=1`) form a strict contract with Neoprep bundlers (Webpack / Vite).
- Rule 35 defines Three.js column-major matrix transposition, viewport stroke projection parameters, single-threaded execution (`XA_MULTITHREADED 0`), and exception-safe boundary handling.

---

## 3. This repository's public surface is mounted into other repositories

**Category:** ecosystem_contract  **Confidence:** high
**Status:** **RESOLVED** — Promoted to Rule `.agents/rules/34-library-consumer-contract-rules.md`.

**Evidence & Rationale**
- Public surfaces: `include/` (C++ headers), `UvulaJS/` (WebAssembly / npm), `pyUvula/` (Python pybind11).
- Downstream consumers: UltiMaker Cura (C++ core + Python) and Neoprep / Digital Factory (WASM).
- Rule 34 defines cross-repository breaking change protocols, additive modification preferences, and consumer PR synchronization.

---

## 4. Observed file-size distribution (input to the ratchet)

**Category:** code-pattern  **Confidence:** medium
**Status:** **RESOLVED** — Governed by `.agents/rules/10-file-size-and-decomposition-rules.md`.

**Evidence & Rationale**
- Median file size is 35 lines; 90th percentile is 246 lines.
- 400-line budget in Rule 10 is well-matched to this codebase.
- Grandfathered files and third-party vendored code (`src/xatlas.cpp`) are managed via `.agents/file-size-baseline.json` and `file_size_scope.py`.

---

## 5. Commits are small and tightly scoped

**Category:** commit-style  **Confidence:** medium
**Status:** **RESOLVED** — Governed by `.agents/rules/08-scoped-changes-and-minimal-diffs.md`.

**Evidence & Rationale**
- Median commit touches 1 file; 90th percentile touches 24 files.
- Rule 08 strictly enforces Single Responsibility PRs (SRP-PR) and the Boy Scouting isolation protocol.

---

## 6. This repository publishes a package whose identity is generated, not committed

**Category:** ecosystem_contract  **Confidence:** medium
**Status:** **RESOLVED** — Promoted to Custom Rule `.agents/rules/37-package-identity-rules.md`.

**Evidence & Rationale**
- `conanfile.py` publishes native Conan 2 package `uvula` for Cura.
- `.github/workflows/package.yml` publishes npm WebAssembly package `@ultimaker/uvulajs` for Neoprep / Digital Factory.
- Branch triggers in `package.yml` updated to include `- 'UC-*'` alongside `NP-*`, `CURA-*`, and `main`/`master`.

---

## 7. History keeps merge commits

**Category:** git-workflow  **Confidence:** medium
**Status:** **RESOLVED** — Integrated into `.agents/rules/06-pull-request-lifecycle-rules.md`.

**Evidence & Rationale**
- 17/91 sampled commits are merge commits.
- Rule 06 defines "Git Merge Topology Preservation" to preserve ISO-27001 auditability and forbid force-pushing published review branches.

---

## 8. File names are predominantly PascalCase

**Category:** code-pattern  **Confidence:** low
**Status:** **RESOLVED / SETTLED** — C++ source files in `src/` and `include/` follow PascalCase convention.

**Evidence & Rationale**
- 16/19 multi-word files use PascalCase (e.g. `FaceSigned.h`, `Triangle3F.cpp`, `Matrix44F.cpp`).
- Retained as idiomatic project style documented in `AGENTS.md`.

---

## 9. Reverts are frequent

**Category:** git-workflow  **Confidence:** low
**Status:** **RESOLVED / SETTLED** — Pre-PR automated gate prevents regressions.

**Evidence & Rationale**
- 3/91 sampled commits are reverts.
- Automated pre-PR verification script (`scripts/verify_and_create_pr.sh`) with comprehensive linters and adversarial checks enforces verification before human review.
