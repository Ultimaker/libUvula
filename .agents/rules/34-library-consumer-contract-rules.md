---
name: library-consumer-contract
description: This repository's public surfaces (include/, UvulaJS/, pyUvula/) are consumed by Cura and Neoprep/DF — every exported symbol and binding is a cross-repository contract.
trigger: glob
glob: "include/**,UvulaJS/**,pyUvula/**"
paths:
  - "include/**"
  - "UvulaJS/**"
  - "pyUvula/**"
---
# Library Consumer Contract

`libUvula` is a core geometry library consumed across native, Python, and WebAssembly ecosystems:

- **UltiMaker Cura**: Consumes native C++ headers (`include/`) and Python bindings (`pyUvula`) via Conan 2 packages.
- **Neoprep & Digital Factory**: Consumes WebAssembly bindings (`UvulaJS`) via the npm package `@ultimaker/uvulajs`.
- **CLI / Tools**: Consumes `include/` and `libuvula` static library in `cli/`.

A service is bounded by its own process: rename an internal function and nothing outside notices. `libUvula` has no such luxury. Its public surfaces (`include/`, `UvulaJS/`, `pyUvula/`) are compiled into downstream binaries and bundlers across different repositories.

## What Constitutes a Breaking Contract Change

Across `include/`, `UvulaJS/`, and `pyUvula/`, all of the following are breaking contract changes, not mere refactors:

1. **Renaming or removing any exported symbol** — class, function, constant,
   type, or module path. A consumer imports it by name.
2. **Changing a model or schema field** — removing it, renaming it, narrowing
   its type, or making an optional field required. Adding an optional field
   with a default is the only safe shape change.
3. **Changing a default value or an enum member**, including its wire value.
4. **Moving a file between packages**, even with the symbol re-exported: a
   consumer may import the module path directly.

## How to make one anyway

1. **Name the consumers in the pull request.**
   Enumerate them before you start — a consumer you did not check is a
   consumer you broke. Search the organisation for pins of this repository:

   ```bash
   gh search code --owner Ultimaker --filename .gitmodules "$(basename "$PWD")"
   ```

2. **Land this repository first, then move each consumer's pointer.** A
   submodule pointer bump is its own commit and names the revision it moves to.
   Never commit inside a consumer's mounted copy of this tree.
3. **Additive first.** Where a breaking shape is unavoidable, ship the new
   surface alongside the old one, migrate the consumers, and remove the old
   surface in a later ticket — not in the same one.
4. **Say so in the commit message.** The consumers' agents read this
   repository's history to work out what moved under them.

## What this rule does not cover

The *meaning* of the contract — which peer owns which definition, what happens
operationally when a field changes — is the ecosystem-contract investigator's
subject (`.agents/agents/ecosystem_contract_investigator/agent.md`). This rule
covers only what is provable from the tree: that the surface is shared, and
that a change to it is never local.
