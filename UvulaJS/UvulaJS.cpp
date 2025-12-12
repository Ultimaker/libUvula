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

// Info structure for library version information
struct uvula_info_t
{
    std::string uvula_version;
    std::string git_hash;
};

// Parameter structure for project function
struct ProjectParams
{
    std::vector<float> strokePolygon;
    std::vector<float> meshVertices;
    std::vector<uint32_t> meshIndices;
    std::vector<float> meshUV;
    std::vector<int> meshFacesConnectivity;
    uint32_t textureWidth;
    uint32_t textureHeight;
    std::vector<float> cameraProjectionMatrix;
    bool isCameraPerspective;
    uint32_t viewportWidth;
    uint32_t viewportHeight;
    std::vector<float> cameraNormal;
    uint32_t faceId;
};

uvula_info_t get_uvula_info()
{
    return { UVULA_VERSION, "" }; // Git hash can be added later if needed
}

// Typed wrapper functions for better TypeScript generation
emscripten::val unwrapTyped(const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
{
    // Convert flat arrays to Point3F and Face vectors
    std::vector<Point3F> vertex_points;
    std::vector<Face> face_indices;
    
    // Convert vertices (expecting flat array of [x1, y1, z1, x2, y2, z2, ...])
    vertex_points.reserve(vertices.size() / 3);
    for (size_t i = 0; i < vertices.size(); i += 3) {
        vertex_points.emplace_back(vertices[i], vertices[i + 1], vertices[i + 2]);
    }

    // Convert indices (expecting flat array of [i1, i2, i3, i4, i5, i6, ...])
    face_indices.reserve(indices.size() / 3);
    for (size_t i = 0; i < indices.size(); i += 3) {
        face_indices.push_back({indices[i], indices[i + 1], indices[i + 2]});
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

    // Convert result to JavaScript object
    emscripten::val result = emscripten::val::object();
    std::vector<float> uv_array;
    uv_array.reserve(uv_coords.size() * 2);
    
    for (const auto& coord : uv_coords) {
        uv_array.push_back(coord.x);
        uv_array.push_back(coord.y);
    }
    
    result.set("uvCoordinates", emscripten::val::array(uv_array));
    result.set("textureWidth", texture_width);
    result.set("textureHeight", texture_height);
    
    return result;
}

emscripten::val projectTyped(
    const std::vector<float>& stroke_polygon,
    const std::vector<float>& mesh_vertices,
    const std::vector<uint32_t>& mesh_indices,
    const std::vector<float>& mesh_uv,
    const std::vector<int>& mesh_faces_connectivity,
    uint32_t texture_width,
    uint32_t texture_height,
    const std::vector<float>& camera_projection_matrix,
    bool is_camera_perspective,
    uint32_t viewport_width,
    uint32_t viewport_height,
    const std::vector<float>& camera_normal,
    uint32_t face_id)
{
    // Convert stroke polygon
    std::vector<Point2F> stroke_points;
    stroke_points.reserve(stroke_polygon.size() / 2);
    for (size_t i = 0; i < stroke_polygon.size(); i += 2) {
        stroke_points.push_back({stroke_polygon[i], stroke_polygon[i + 1]});
    }

    // Convert mesh vertices
    std::vector<Point3F> vertex_points;
    vertex_points.reserve(mesh_vertices.size() / 3);
    for (size_t i = 0; i < mesh_vertices.size(); i += 3) {
        vertex_points.emplace_back(mesh_vertices[i], mesh_vertices[i + 1], mesh_vertices[i + 2]);
    }

    // Convert mesh indices
    std::vector<Face> face_indices;
    face_indices.reserve(mesh_indices.size() / 3);
    for (size_t i = 0; i < mesh_indices.size(); i += 3) {
        face_indices.push_back({mesh_indices[i], mesh_indices[i + 1], mesh_indices[i + 2]});
    }

    // Convert mesh UV coordinates
    std::vector<Point2F> uv_points;
    uv_points.reserve(mesh_uv.size() / 2);
    for (size_t i = 0; i < mesh_uv.size(); i += 2) {
        uv_points.push_back({mesh_uv[i], mesh_uv[i + 1]});
    }

    // Convert mesh faces connectivity
    std::vector<FaceSigned> connectivity;
    connectivity.reserve(mesh_faces_connectivity.size() / 3);
    for (size_t i = 0; i < mesh_faces_connectivity.size(); i += 3) {
        connectivity.push_back({mesh_faces_connectivity[i], mesh_faces_connectivity[i + 1], mesh_faces_connectivity[i + 2]});
    }

    // Convert camera projection matrix (4x4 matrix as flat array)
    float matrix_data[4][4];
    for (int i = 0; i < 16; ++i) {
        matrix_data[i / 4][i % 4] = camera_projection_matrix[i];
    }
    Matrix44F projection_matrix(matrix_data);

    // Convert camera normal (3-element array)
    Vector3F normal(camera_normal[0], camera_normal[1], camera_normal[2]);

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

    // Convert result to JavaScript array of arrays
    emscripten::val js_result = emscripten::val::array();
    for (size_t i = 0; i < result.size(); ++i) {
        std::vector<float> polygon_flat;
        const Polygon& polygon = result[i];
        polygon_flat.reserve(polygon.size() * 2);
        for (const auto& point : polygon) {
            polygon_flat.push_back(point.x);
            polygon_flat.push_back(point.y);
        }
        js_result.set(i, emscripten::val::array(polygon_flat));
    }

    return js_result;
}

// Structured wrapper for project function using ProjectParams
emscripten::val projectWithParams(const ProjectParams& params)
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
    // Register array types for better TypeScript generation
    register_vector<float>("FloatArray");
    register_vector<int>("IntArray");
    register_vector<uint32_t>("UInt32Array");

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

    // Version information
    value_object<uvula_info_t>("uvula_info_t")
        .field("uvula_version", &uvula_info_t::uvula_version)
        .field("git_hash", &uvula_info_t::git_hash);

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