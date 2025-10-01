// (c) 2025, UltiMaker -- see LICENCE for details

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include <spdlog/spdlog.h>

#include "Face.h"
#include "Matrix44F.h"
#include "Point2F.h"
#include "Point3F.h"
#include "Vector3F.h"
#include "project.h"
#include "unwrap.h"

namespace py = pybind11;

py::tuple unwrap(const py::array_t<float>& vertices_array, const py::array_t<int32_t>& indices_array)
{
    // input shaping
    const pybind11::buffer_info vertices_buf = vertices_array.request();
    const pybind11::buffer_info indices_buf = indices_array.request();
    if (vertices_buf.ndim != 2 || indices_buf.ndim != 2)
    {
        throw std::runtime_error("Vertices should be <float, float, float> and indices should be (grouped by face as) <int, int, int>.");
    }

    const auto* vertices_ptr = static_cast<Point3F*>(vertices_buf.ptr);
    const auto* indices_ptr = static_cast<Face*>(indices_buf.ptr);
    const auto vertices = std::vector<Point3F>(vertices_ptr, vertices_ptr + vertices_buf.shape[0]);
    const auto indices = std::vector<Face>(indices_ptr, indices_ptr + indices_buf.shape[0]);

    // output shaping
    const std::vector<py::ssize_t> shape = { static_cast<py::ssize_t>(vertices.size()), 2 };
    const std::vector<py::ssize_t> strides = { static_cast<py::ssize_t>(sizeof(float) * shape[1]), static_cast<py::ssize_t>(sizeof(float)) };
    std::vector<Point2F> res(shape[0], { 0.0, 0.0 });
    uint32_t texture_width;
    uint32_t texture_height;

    {
        py::gil_scoped_release release;

        // Do the actual calculation here
        if (! smartUnwrap(vertices, indices, res, texture_width, texture_height))
        {
            throw std::runtime_error("Couldn't unwrap UV's!");
        }
    }

    // send output
    return py::make_tuple(py::array(py::buffer_info(res.data(), strides[1], py::format_descriptor<float>::format(), shape.size(), shape, strides)), texture_width, texture_height);
}

py::list pyProject(
    const py::array_t<float>& stroke_polygon_array,
    const py::array_t<float>& mesh_vertices_array,
    const py::array_t<uint32_t>& mesh_indices_array,
    const py::array_t<float>& mesh_uv_coordinates,
    const uint32_t texture_width,
    const uint32_t texture_height,
    const py::array_t<float>& camera_projection_matrix_array,
    const bool is_camera_perspective,
    const uint32_t viewport_width,
    const uint32_t viewport_height,
    const py::array_t<float>& camera_normal_array,
    const py::array_t<uint32_t>& faces_array)
{
    const pybind11::buffer_info stroke_polygon_buf = stroke_polygon_array.request();
    const std::span<Point2F> stroke_polygon(static_cast<Point2F*>(stroke_polygon_buf.ptr), stroke_polygon_buf.shape[0]);

    const pybind11::buffer_info mesh_vertices_buf = mesh_vertices_array.request();
    const std::span<Point3F> mesh_vertices(static_cast<Point3F*>(mesh_vertices_buf.ptr), mesh_vertices_buf.shape[0]);

    const pybind11::buffer_info mesh_indices_buf = mesh_indices_array.request();
    const std::span<Face> mesh_indices(static_cast<Face*>(mesh_indices_buf.ptr), mesh_indices_buf.shape[0]);

    const pybind11::buffer_info mesh_uv_buf = mesh_uv_coordinates.request();
    const std::span<Point2F> mesh_uv(static_cast<Point2F*>(mesh_uv_buf.ptr), mesh_uv_buf.shape[0]);

    const pybind11::buffer_info camera_projection_matrix_buf = camera_projection_matrix_array.request();
    const Matrix44F camera_projection_matrix(*static_cast<float(*)[4][4]>(camera_projection_matrix_buf.ptr));

    const pybind11::buffer_info camera_normal_buf = camera_normal_array.request();
    const float* camera_normal_ptr = static_cast<float*>(camera_normal_buf.ptr);
    const Vector3F camera_normal(camera_normal_ptr[0], camera_normal_ptr[1], camera_normal_ptr[2]);

    const pybind11::buffer_info faces_buf = faces_array.request();
    const std::span<uint32_t> faces(static_cast<uint32_t*>(faces_buf.ptr), faces_buf.shape[0]);

    std::vector<Polygon> result = project(
        stroke_polygon,
        mesh_vertices,
        mesh_indices,
        mesh_uv,
        texture_width,
        texture_height,
        camera_projection_matrix,
        is_camera_perspective,
        viewport_width,
        viewport_height,
        camera_normal,
        faces);

    py::list py_result;
    for (Polygon& polygon : result)
    {
        py_result.append(
            py::array_t<float>(py::buffer_info(
                polygon.data(),
                sizeof(float),
                py::format_descriptor<float>::format(),
                2,
                { polygon.size(), static_cast<size_t>(2) },
                { sizeof(float) * 2, sizeof(float) })));
    }

    return py_result;
}

PYBIND11_MODULE(pyUvula, module)
{
    module.doc() = "UV-unwrapping library (or bindings to library), segmentation uses a classic normal-based grouping and charts packing uses xatlas";
    module.attr("__version__") = PYUVULA_VERSION;

    module.def("unwrap", &unwrap, "Given the vertices, indices of a mesh, unwrap UV for texture-coordinates.");
    module.def("project", &pyProject, "Projects a stroke polygon into an object texture.");
}
