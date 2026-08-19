---
name: pyuvula-binding
description: Python bindings rules for pyUvula (UltiMaker Cura) using pybind11, numpy buffer safety, and GIL release during geometry calculations.
trigger: glob
glob: "pyUvula/**"
paths:
  - "pyUvula/**"
---
# Python Bindings Rules (pyUvula)

The `pyUvula` module exposes `libUvula` unwrapping and projection algorithms to Python environments (such as Cura).

## 1. Zero-Copy & Buffer Safety
- Use `pybind11::buffer_info` to inspect raw numpy arrays and construct non-owning `std::span` views where appropriate.
- Strictly validate input dimensions (`ndim == 2`) and types before interpreting buffer pointers.
- Construct output numpy arrays using appropriate strides and format descriptors (`py::format_descriptor<float>::format()`).

## 2. GIL Management
- Heavy mathematical computation (e.g., `smartUnwrap` or mesh projection) MUST release the Python GIL via `py::gil_scoped_release release;` to allow multi-threaded Python execution.
- Re-acquire or retain GIL only when interacting with Python objects (`py::tuple`, `py::list`, `py::array_t`).

## 3. Versioning & Package Synchronization
- The Python module version is sourced from `PYUVULA_VERSION` (defined via CMake/Conan package recipe).
- Any change to the signature of `unwrap` or `project` must be synchronized with Cura and Python integration workflows.
