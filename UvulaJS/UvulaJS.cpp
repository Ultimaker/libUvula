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

// Return type for unwrap function
struct UnwrapResult
{
    std::vector<float> uvCoordinates;
    uint32_t textureWidth;
    uint32_t textureHeight;
};

// Return type for project function (array of polygons as flat arrays)
struct ProjectResult
{
    std::vector<std::vector<float>> polygons;
};

// Parameter structure for project function
struct ProjectParams
{
    Float32Array strokePolygon = Float32Array{emscripten::val::array()};           // float[]
    Float32Array meshVertices = Float32Array{emscripten::val::array()};            // float[]
    Uint32Array meshIndices = Uint32Array{emscripten::val::array()};              // uint32_t[]
    Float32Array meshUV = Float32Array{emscripten::val::array()};                  // float[]
    Int32Array meshFacesConnectivity = Int32Array{emscripten::val::array()};     // int32_t[]
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;
    Float32Array cameraProjectionMatrix = Float32Array{emscripten::val::array()};  // float[16] - 4x4 matrix
    bool isCameraPerspective = false;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    Float32Array cameraNormal = Float32Array{emscripten::val::array()};            // float[3] - normal vector
    uint32_t faceId = 0;
};

std::string get_uvula_info()
{
    return { UVULA_VERSION };
}

// Typed wrapper functions for better TypeScript generation
UnwrapResult unwrapTyped(const Float32Array& vertices_js, const Uint32Array& indices_js)
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
    for (unsigned i = 0; i < indices_length; i += 3) {
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

    if (!success) {
        throw std::runtime_error("Couldn't unwrap UVs!");
    }

    // Convert result to structured return type
    std::vector<float> uv_array;
    uv_array.reserve(uv_coords.size() * 2);

    for (const auto& coord : uv_coords) {
        uv_array.push_back(coord.x);
        uv_array.push_back(coord.y);
    }

    return UnwrapResult{
        .uvCoordinates = uv_array,
        .textureWidth = texture_width,
        .textureHeight = texture_height
    };
}

ProjectResult projectTyped(
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
    const Float32Array& camera_normal_js,
    uint32_t face_id)
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

    // Convert camera normal (3-element array)
    float normal_x = camera_normal_js[0].as<float>();
    float normal_y = camera_normal_js[1].as<float>();
    float normal_z = camera_normal_js[2].as<float>();
    Vector3F normal(normal_x, normal_y, normal_z);

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
        normal,
        face_id
    );

    // Convert result to structured return type
    std::vector<std::vector<float>> result_polygons;
    result_polygons.reserve(result.size());

    for (const auto& polygon : result) {
        std::vector<float> polygon_flat;
        polygon_flat.reserve(polygon.size() * 2);
        for (const auto& point : polygon) {
            polygon_flat.push_back(point.x);
            polygon_flat.push_back(point.y);
        }
        result_polygons.push_back(std::move(polygon_flat));
    }

    return ProjectResult{.polygons = result_polygons};
}

// Structured wrapper for project function using ProjectParams
ProjectResult projectWithParams(const ProjectParams& params)
{
    return projectTyped(
        params.strokePolygon,
        params.meshVertices,
        params.meshIndices,
        params.meshUV,
        params.meshFacesConnectivity,
        params.textureWidth,
        params.textureHeight,
        params.cameraProjectionMatrix,
        params.isCameraPerspective,
        params.viewportWidth,
        params.viewportHeight,
        params.cameraNormal,
        params.faceId
    );
}

// Wrapper for the unwrap function that works with JavaScript arrays
emscripten::val jsUnwrap(const emscripten::val& vertices_js, const emscripten::val& indices_js)
{
    // Convert JavaScript arrays to C++ vectors
    std::vector<Point3F> vertices;
    std::vector<Face> indices;

    // Get array lengths
    unsigned vertices_length = vertices_js["length"].as<unsigned>();
    unsigned indices_length = indices_js["length"].as<unsigned>();

    // Convert vertices (expecting flat array of [x1, y1, z1, x2, y2, z2, ...])
    vertices.reserve(vertices_length / 3);
    for (unsigned i = 0; i < vertices_length; i += 3) {
        float x = vertices_js[i].as<float>();
        float y = vertices_js[i + 1].as<float>();
        float z = vertices_js[i + 2].as<float>();
        vertices.emplace_back(x, y, z);
    }

    // Convert indices (expecting flat array of [i1, i2, i3, i4, i5, i6, ...])
    indices.reserve(indices_length / 3);
    for (unsigned i = 0; i < indices_length; i += 3) {
        uint32_t i1 = indices_js[i].as<uint32_t>();
        uint32_t i2 = indices_js[i + 1].as<uint32_t>();
        uint32_t i3 = indices_js[i + 2].as<uint32_t>();
        indices.push_back({i1, i2, i3});
    }

    // Prepare output
    std::vector<Point2F> uv_coords(vertices.size(), {0.0f, 0.0f});
    uint32_t texture_width;
    uint32_t texture_height;

    // Perform unwrapping
    bool success = smartUnwrap(vertices, indices, uv_coords, texture_width, texture_height);

    if (!success) {
        throw std::runtime_error("Couldn't unwrap UVs!");
    }

    // Convert result to JavaScript object
    emscripten::val result = emscripten::val::object();
    emscripten::val uv_array = emscripten::val::array();

    for (size_t i = 0; i < uv_coords.size(); ++i) {
        uv_array.set(i * 2, uv_coords[i].x);
        uv_array.set(i * 2 + 1, uv_coords[i].y);
    }

    result.set("uvCoordinates", uv_array);
    result.set("textureWidth", texture_width);
    result.set("textureHeight", texture_height);

    return result;
}

// Wrapper for the project function that works with JavaScript arrays
emscripten::val jsProject(
    const emscripten::val& stroke_polygon_js,
    const emscripten::val& mesh_vertices_js,
    const emscripten::val& mesh_indices_js,
    const emscripten::val& mesh_uv_js,
    const emscripten::val& mesh_faces_connectivity_js,
    uint32_t texture_width,
    uint32_t texture_height,
    const emscripten::val& camera_projection_matrix_js,
    bool is_camera_perspective,
    uint32_t viewport_width,
    uint32_t viewport_height,
    const emscripten::val& camera_normal_js,
    uint32_t face_id)
{
    // Convert stroke polygon (flat array of [x1, y1, x2, y2, ...])
    std::vector<Point2F> stroke_polygon;
    unsigned stroke_length = stroke_polygon_js["length"].as<unsigned>();
    stroke_polygon.reserve(stroke_length / 2);
    for (unsigned i = 0; i < stroke_length; i += 2) {
        float x = stroke_polygon_js[i].as<float>();
        float y = stroke_polygon_js[i + 1].as<float>();
        stroke_polygon.push_back({x, y});
    }

    // Convert mesh vertices (flat array of [x1, y1, z1, x2, y2, z2, ...])
    std::vector<Point3F> mesh_vertices;
    unsigned vertices_length = mesh_vertices_js["length"].as<unsigned>();
    mesh_vertices.reserve(vertices_length / 3);
    for (unsigned i = 0; i < vertices_length; i += 3) {
        float x = mesh_vertices_js[i].as<float>();
        float y = mesh_vertices_js[i + 1].as<float>();
        float z = mesh_vertices_js[i + 2].as<float>();
        mesh_vertices.emplace_back(x, y, z);
    }

    // Convert mesh indices (flat array of [i1, i2, i3, i4, i5, i6, ...])
    std::vector<Face> mesh_indices;
    unsigned indices_length = mesh_indices_js["length"].as<unsigned>();
    mesh_indices.reserve(indices_length / 3);
    for (unsigned i = 0; i < indices_length; i += 3) {
        uint32_t i1 = mesh_indices_js[i].as<uint32_t>();
        uint32_t i2 = mesh_indices_js[i + 1].as<uint32_t>();
        uint32_t i3 = mesh_indices_js[i + 2].as<uint32_t>();
        mesh_indices.push_back({i1, i2, i3});
    }

    // Convert mesh UV coordinates (flat array of [u1, v1, u2, v2, ...])
    std::vector<Point2F> mesh_uv;
    unsigned uv_length = mesh_uv_js["length"].as<unsigned>();
    mesh_uv.reserve(uv_length / 2);
    for (unsigned i = 0; i < uv_length; i += 2) {
        float u = mesh_uv_js[i].as<float>();
        float v = mesh_uv_js[i + 1].as<float>();
        mesh_uv.push_back({u, v});
    }

    // Convert mesh faces connectivity (flat array of [f1, f2, f3, f4, f5, f6, ...])
    std::vector<FaceSigned> mesh_faces_connectivity;
    unsigned connectivity_length = mesh_faces_connectivity_js["length"].as<unsigned>();
    mesh_faces_connectivity.reserve(connectivity_length / 3);
    for (unsigned i = 0; i < connectivity_length; i += 3) {
        int32_t i1 = mesh_faces_connectivity_js[i].as<int32_t>();
        int32_t i2 = mesh_faces_connectivity_js[i + 1].as<int32_t>();
        int32_t i3 = mesh_faces_connectivity_js[i + 2].as<int32_t>();
        mesh_faces_connectivity.push_back({i1, i2, i3});
    }

    // Convert camera projection matrix (4x4 matrix as flat array)
    float matrix_data[4][4];
    for (int i = 0; i < 16; ++i) {
        matrix_data[i / 4][i % 4] = camera_projection_matrix_js[i].as<float>();
    }
    Matrix44F camera_projection_matrix(matrix_data);

    // Convert camera normal (3-element array)
    float normal_x = camera_normal_js[0].as<float>();
    float normal_y = camera_normal_js[1].as<float>();
    float normal_z = camera_normal_js[2].as<float>();
    Vector3F camera_normal(normal_x, normal_y, normal_z);

    // Call the projection function
    std::vector<Polygon> result = doProject(
        stroke_polygon,
        mesh_vertices,
        mesh_indices,
        mesh_uv,
        mesh_faces_connectivity,
        texture_width,
        texture_height,
        camera_projection_matrix,
        is_camera_perspective,
        viewport_width,
        viewport_height,
        camera_normal,
        face_id
    );

    // Convert result to JavaScript array of arrays
    emscripten::val js_result = emscripten::val::array();
    for (size_t i = 0; i < result.size(); ++i) {
        emscripten::val polygon_array = emscripten::val::array();
        const Polygon& polygon = result[i];
        for (size_t j = 0; j < polygon.size(); ++j) {
            polygon_array.set(j * 2, polygon[j].x);
            polygon_array.set(j * 2 + 1, polygon[j].y);
        }
        js_result.set(i, polygon_array);
    }

    return js_result;
}

EMSCRIPTEN_BINDINGS(uvula)
{
    // Register typed array types
    register_vector<float>("Float32Array");
    register_vector<std::vector<float>>("Float32ArrayArray");

    // Register TypeScript-style typed arrays
    emscripten::register_type<Float32Array>("Float32Array");
    emscripten::register_type<Uint32Array>("Uint32Array");
    emscripten::register_type<Int32Array>("Int32Array");

    // Register structured return types
    value_object<UnwrapResult>("UnwrapResult")
        .field("uvCoordinates", &UnwrapResult::uvCoordinates)
        .field("textureWidth", &UnwrapResult::textureWidth)
        .field("textureHeight", &UnwrapResult::textureHeight);

    value_object<ProjectResult>("ProjectResult")
        .field("polygons", &ProjectResult::polygons);

    // Register ProjectParams structure
    value_object<ProjectParams>("ProjectParams")
        .field("strokePolygon", &ProjectParams::strokePolygon)
        .field("meshVertices", &ProjectParams::meshVertices)
        .field("meshIndices", &ProjectParams::meshIndices)
        .field("meshUV", &ProjectParams::meshUV)
        .field("meshFacesConnectivity", &ProjectParams::meshFacesConnectivity)
        .field("textureWidth", &ProjectParams::textureWidth)
        .field("textureHeight", &ProjectParams::textureHeight)
        .field("cameraProjectionMatrix", &ProjectParams::cameraProjectionMatrix)
        .field("isCameraPerspective", &ProjectParams::isCameraPerspective)
        .field("viewportWidth", &ProjectParams::viewportWidth)
        .field("viewportHeight", &ProjectParams::viewportHeight)
        .field("cameraNormal", &ProjectParams::cameraNormal)
        .field("faceId", &ProjectParams::faceId);

    // Main typed functions with proper TypeScript signatures
    function("unwrap", &unwrapTyped);
    function("project", &projectWithParams);

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
        .constructor<float, float, float>();

    value_object<Face>("Face")
        .field("i1", &Face::i1)
        .field("i2", &Face::i2)
        .field("i3", &Face::i3);

    value_object<FaceSigned>("FaceSigned")
        .field("i1", &FaceSigned::i1)
        .field("i2", &FaceSigned::i2)
        .field("i3", &FaceSigned::i3);
}