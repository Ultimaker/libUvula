---
name: commit-body-standards
description: Enforces explanatory commit message bodies describing why the change was needed and how it works.
trigger: always_on
---
# Commit Body Standards

Every commit in `libUvula` MUST include a substantive commit body in addition to the conventional subject line.

## Rationale
Historical analysis demonstrates that 79% of commits in `libUvula` provide explanatory bodies. Given that `libUvula` is a core mathematical and geometry library consumed across Python (Cura) and WebAssembly (Digital Factory / Neoprep) pipelines, commit messages serve as permanent documentation for downstream consumers and bisect analysis.

## Invariants

1. **Explain Why and How**:
   - The subject line states **what** changed (`[UC-3697] Add normal clustering threshold option`).
   - The commit body MUST explain **why** the change was necessary (the motivation or bug addressed) and **how** it achieves the goal (architectural approach, edge cases handled).

2. **Structure**:
   ```
   [UC-3697] Short description of what changed (max 72 chars)

   Explain why this change is necessary and the problem being solved.

   Detail how the implementation works, any mathematical or algorithmic
   assumptions (e.g. normal grouping angle, xatlas atlas layout), and
   downstream impact on bindings (pyUvula, UvulaJS).
   ```

3. **No Empty Bodies**:
   - Commits with only a single subject line (unless purely trivial reverts or automated version bumps) are prohibited.
