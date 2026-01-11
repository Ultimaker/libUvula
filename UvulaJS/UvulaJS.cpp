// (c) 2025, UltiMaker -- see LICENCE for details

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <vector>

#include "Face.h"
#include "Matrix44F.h"
#include "Point2F.h"
#include "Point3F.h"
#include "Vector3F.h"
#include "project.h"
#include "unwrap.h"

using namespace emscripten;

// TypeScript type aliases for better type safety
EMSCRIPTEN_DECLARE_VAL_TYPE(Float32Array);
EMSCRIPTEN_DECLARE_VAL_TYPE(Uint32Array);
EMSCRIPTEN_DECLARE_VAL_TYPE(Int32Array);
EMSCRIPTEN_DECLARE_VAL_TYPE(UInt32Array);
EMSCRIPTEN_DECLARE_VAL_TYPE(PolygonArray);

// Return type for unwrap function
struct UnwrapResult
{
    Float32Array uvCoordinates = Float32Array{emscripten::val::array()};
    uint32_t textureWidth;
    uint32_t textureHeight;
};

std::string get_uvula_info()
{
    return { UVULA_VERSION };
}

// Typed wrapper functions for better TypeScript generation
UnwrapResult unwrap(const Float32Array& vertices_js, const Uint32Array& indices_js)
{
    // Convert JavaScript arrays to C++ vectors
    std::vector<Point3F> vertex_points;
    std::vector<Face> face_indices;

    // Get array lengths
    unsigned vertices_length = vertices_js["length"].as<unsigned>();
    unsigned indices_length = indices_js["length"].as<unsigned>();

    // Convert vertices (expecting flat array of [x1, y1, z1, x2, y2, z2, ...])
    vertex_points.reserve(vertices_length / 3);
    for (unsigned i = 0; i < vertices_length; i += 3) {
        float x = vertices_js[i].as<float>();
        float y = vertices_js[i + 1].as<float>();
        float z = vertices_js[i + 2].as<float>();
        vertex_points.emplace_back(x, y, z);
    }

    // Convert indices (expecting flat array of [i1, i2, i3, i4, i5, i6, ...])
    face_indices.reserve(indices_length / 3);
    for (unsigned i = 0; i < indices_length; i += 3)
    {
        uint32_t i1 = indices_js[i].as<uint32_t>();
        uint32_t i2 = indices_js[i + 1].as<uint32_t>();
        uint32_t i3 = indices_js[i + 2].as<uint32_t>();
        face_indices.push_back({i1, i2, i3});
    }

    // Prepare output
    std::vector<Point2F> uv_coords(vertex_points.size(), {0.0f, 0.0f});
    uint32_t texture_width;
    uint32_t texture_height;

    // Perform unwrapping
    bool success = smartUnwrap(vertex_points, face_indices, uv_coords, texture_width, texture_height);

    if (!success)
    {
        throw std::runtime_error("Couldn't unwrap UVs!");
    }

    // Convert result to structured return type
    emscripten::val uv_array = emscripten::val::array();

    for (size_t i = 0; i < uv_coords.size(); ++i)
    {
        uv_array.set(i * 2, uv_coords[i].x);
        uv_array.set(i * 2 + 1, uv_coords[i].y);
    }

    return UnwrapResult{
        .uvCoordinates = Float32Array{uv_array},
        .textureWidth = texture_width,
        .textureHeight = texture_height
    };
}

PolygonArray project(
    const Float32Array& stroke_polygon_js,
    const Float32Array& mesh_vertices_js,
    const Uint32Array& mesh_indices_js,
    const Float32Array& mesh_uv_js,
    const Int32Array& mesh_faces_connectivity_js,
    uint32_t texture_width,
    uint32_t texture_height,
    const Float32Array& camera_projection_matrix_js,
    bool is_camera_perspective,
    uint32_t viewport_width,
    uint32_t viewport_height,
    const Vector3F& camera_normal,
    uint32_t face_id
)
{
    // Convert stroke polygon (flat array of [x1, y1, x2, y2, ...])
    std::vector<Point2F> stroke_points;
    unsigned stroke_length = stroke_polygon_js["length"].as<unsigned>();
    stroke_points.reserve(stroke_length / 2);
    for (unsigned i = 0; i < stroke_length; i += 2) {
        float x = stroke_polygon_js[i].as<float>();
        float y = stroke_polygon_js[i + 1].as<float>();
        stroke_points.push_back({x, y});
    }

    // Convert mesh vertices (flat array of [x1, y1, z1, x2, y2, z2, ...])
    std::vector<Point3F> vertex_points;
    unsigned vertices_length = mesh_vertices_js["length"].as<unsigned>();
    vertex_points.reserve(vertices_length / 3);
    for (unsigned i = 0; i < vertices_length; i += 3) {
        float x = mesh_vertices_js[i].as<float>();
        float y = mesh_vertices_js[i + 1].as<float>();
        float z = mesh_vertices_js[i + 2].as<float>();
        vertex_points.emplace_back(x, y, z);
    }

    // Convert mesh indices (flat array of [i1, i2, i3, i4, i5, i6, ...])
    std::vector<Face> face_indices;
    unsigned indices_length = mesh_indices_js["length"].as<unsigned>();
    face_indices.reserve(indices_length / 3);
    for (unsigned i = 0; i < indices_length; i += 3) {
        uint32_t i1 = mesh_indices_js[i].as<uint32_t>();
        uint32_t i2 = mesh_indices_js[i + 1].as<uint32_t>();
        uint32_t i3 = mesh_indices_js[i + 2].as<uint32_t>();
        face_indices.push_back({i1, i2, i3});
    }

    // Convert mesh UV coordinates (flat array of [u1, v1, u2, v2, ...])
    std::vector<Point2F> uv_points;
    unsigned uv_length = mesh_uv_js["length"].as<unsigned>();
    uv_points.reserve(uv_length / 2);
    for (unsigned i = 0; i < uv_length; i += 2) {
        float u = mesh_uv_js[i].as<float>();
        float v = mesh_uv_js[i + 1].as<float>();
        uv_points.push_back({u, v});
    }

    // Convert mesh faces connectivity (flat array of [f1, f2, f3, f4, f5, f6, ...])
    std::vector<FaceSigned> connectivity;
    unsigned connectivity_length = mesh_faces_connectivity_js["length"].as<unsigned>();
    connectivity.reserve(connectivity_length / 3);
    for (unsigned i = 0; i < connectivity_length; i += 3) {
        int32_t i1 = mesh_faces_connectivity_js[i].as<int32_t>();
        int32_t i2 = mesh_faces_connectivity_js[i + 1].as<int32_t>();
        int32_t i3 = mesh_faces_connectivity_js[i + 2].as<int32_t>();
        connectivity.push_back({i1, i2, i3});
    }

    // Convert camera projection matrix (4x4 matrix as flat array)
    float matrix_data[4][4];
    for (int i = 0; i < 16; ++i) {
        matrix_data[i / 4][i % 4] = camera_projection_matrix_js[i].as<float>();
    }
    Matrix44F projection_matrix(matrix_data);

    // Call the projection function
    std::vector<Polygon> result = doProject(
        stroke_points,
        vertex_points,
        face_indices,
        uv_points,
        connectivity,
        texture_width,
        texture_height,
        projection_matrix,
        is_camera_perspective,
        viewport_width,
        viewport_height,
        camera_normal,
        face_id
    );

    // Convert result to structured return type
    emscripten::val result_polygons = emscripten::val::array();

    for (size_t i = 0; i < result.size(); ++i)
    {
        emscripten::val polygon_array = emscripten::val::array();
        const auto& polygon = result[i];
        for (size_t j = 0; j < polygon.size(); ++j)
        {
            polygon_array.set(j, polygon[j]);
        }

        result_polygons.set(i, polygon_array);
    }

    return PolygonArray{ result_polygons };
}

EMSCRIPTEN_BINDINGS(uvula)
{
    // Register TypeScript-style typed arrays
    emscripten::register_type<Float32Array>("Float32Array");
    emscripten::register_type<Uint32Array>("Uint32Array");
    emscripten::register_type<Int32Array>("Int32Array");
    emscripten::register_type<PolygonArray>("Point2F[][]");

    // Register structured return types
    value_object<UnwrapResult>("UnwrapResult")
        .field("uvCoordinates", &UnwrapResult::uvCoordinates)
        .field("textureWidth", &UnwrapResult::textureWidth)
        .field("textureHeight", &UnwrapResult::textureHeight);

    // Main typed functions with proper TypeScript signatures
    function("unwrap", &unwrap);
    function("project", &project);

    function("uvula_info", &get_uvula_info);

    // Utility classes for direct access if needed
    class_<Point2F>("Point2F")
        .constructor<>()
        .property("x", &Point2F::x)
        .property("y", &Point2F::y);

    class_<Point3F>("Point3F")
        .constructor<float, float, float>()
        .function("x", &Point3F::x)
        .function("y", &Point3F::y)
        .function("z", &Point3F::z);

    class_<Vector3F>("Vector3F")
        .constructor<float, float, float>()
        .function("x", &Vector3F::x)
        .function("y", &Vector3F::y)
        .function("z", &Vector3F::z);

    value_object<Face>("Face")
        .field("i1", &Face::i1)
        .field("i2", &Face::i2)
        .field("i3", &Face::i3);

    value_object<FaceSigned>("FaceSigned")
        .field("i1", &FaceSigned::i1)
        .field("i2", &FaceSigned::i2)
        .field("i3", &FaceSigned::i3);
}