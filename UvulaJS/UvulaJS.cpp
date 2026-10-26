// (c) 2025, UltiMaker -- see LICENCE for details

#include <algorithm>
#include <array>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <emscripten/wire.h>
#include <span>
#include <type_traits>
#include <vector>

#include "Face.h"
#include "Matrix44F.h"
#include "Point2F.h"
#include "Point3F.h"
#include "Vector3F.h"
#include "project.h"
#include "unwrap.h"

using namespace emscripten;

static_assert(sizeof(Point2F) == 2 * sizeof(float) && std::is_trivially_copyable_v<Point2F>);
static_assert(sizeof(Point3F) == 3 * sizeof(float) && std::is_trivially_copyable_v<Point3F>);
static_assert(sizeof(Face) == 3 * sizeof(uint32_t) && std::is_trivially_copyable_v<Face>);
static_assert(sizeof(FaceSigned) == 3 * sizeof(int32_t) && std::is_trivially_copyable_v<FaceSigned>);

namespace
{
template <typename Scalar>
    requires std::is_arithmetic_v<Scalar>
void copyJsArrayToSpan(const emscripten::val& source_js, size_t source_len, std::span<Scalar> dest)
{
    if (dest.empty())
    {
        return;
    }
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(dest.size(), dest.data()));
    if (source_len > dest.size())
    {
        emscripten::val truncated = !source_js["subarray"].isUndefined()
            ? source_js.call<emscripten::val>("subarray", size_t(0), dest.size())
            : source_js.call<emscripten::val>("slice", size_t(0), dest.size());
        view.call<void>("set", truncated);
    }
    else
    {
        view.call<void>("set", source_js);
    }
}

template <typename Elem, typename Scalar, size_t Arity>
    requires (std::is_trivially_copyable_v<Elem> && std::is_arithmetic_v<Scalar> && sizeof(Elem) == Arity * sizeof(Scalar))
std::vector<Elem> copyTypedArrayToVector(const emscripten::val& source_js)
{
    const auto source_len = source_js["length"].as<size_t>();
    const size_t elem_count = source_len / Arity;
    std::vector<Elem> result(elem_count);
    copyJsArrayToSpan(
        source_js,
        source_len,
        std::span<Scalar>{ reinterpret_cast<Scalar*>(result.data()), elem_count * Arity });
    return result;
}

std::vector<Point3F> copyFloat32ArrayToPoints3F(const emscripten::val& vertices_js)
{
    return copyTypedArrayToVector<Point3F, float, 3>(vertices_js);
}

std::vector<Face> copyUint32ArrayToFaces(const emscripten::val& indices_js)
{
    return copyTypedArrayToVector<Face, uint32_t, 3>(indices_js);
}

std::vector<Point2F> copyFloat32ArrayToPoints2F(const emscripten::val& uvs_js)
{
    return copyTypedArrayToVector<Point2F, float, 2>(uvs_js);
}

std::vector<FaceSigned> copyInt32ArrayToConnectivity(const emscripten::val& connectivity_js)
{
    return copyTypedArrayToVector<FaceSigned, int32_t, 3>(connectivity_js);
}
} // namespace

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

struct Geometry
{
    std::vector<Point3F> vertices;
    std::vector<Face> indices;
    std::vector<Point2F> uvs;
    std::vector<FaceSigned> connectivity;

    Geometry(const Float32Array& vertices_js, const Uint32Array& indices_js, const Float32Array& uvs_js, const Int32Array& connectivity_js)
        : vertices(copyFloat32ArrayToPoints3F(vertices_js))
        , indices(copyUint32ArrayToFaces(indices_js))
        , uvs(copyFloat32ArrayToPoints2F(uvs_js))
        , connectivity(copyInt32ArrayToConnectivity(connectivity_js))
    {
    }
};

std::string get_uvula_info()
{
    return { UVULA_VERSION };
}

// Typed wrapper functions for better TypeScript generation
UnwrapResult unwrap(const Float32Array& vertices_js, const Uint32Array& indices_js)
{
    // Convert JavaScript TypedArrays to C++ vectors using bulk memory views
    std::vector<Point3F> vertex_points = copyFloat32ArrayToPoints3F(vertices_js);
    std::vector<Face> face_indices = copyUint32ArrayToFaces(indices_js);

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

    // Copy flat Point2F buffer into a JS Float32Array in a single bulk operation
    emscripten::val uv_view = emscripten::val(
        emscripten::typed_memory_view(uv_coords.size() * 2, reinterpret_cast<const float*>(uv_coords.data())));
    emscripten::val uv_array = emscripten::val::global("Float32Array").new_(uv_view);

    return UnwrapResult{
        .uvCoordinates = Float32Array{uv_array},
        .textureWidth = texture_width,
        .textureHeight = texture_height
    };
}

PolygonArray project(
    const Float32Array& stroke_polygon_js,
    const Geometry& geometry,
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
    std::vector<Point2F> stroke_points = copyFloat32ArrayToPoints2F(stroke_polygon_js);

    // Convert camera projection matrix (4x4 matrix as flat array)
    const size_t source_matrix_len = camera_projection_matrix_js["length"].as<size_t>();
    const size_t matrix_len = std::min<size_t>(16, source_matrix_len);
    std::array<float, 16> raw_matrix{};
    copyJsArrayToSpan(camera_projection_matrix_js, source_matrix_len, std::span<float>{ raw_matrix }.first(matrix_len));
    float matrix_data[4][4]{};
    for (size_t i = 0; i < matrix_len; ++i)
    {
        matrix_data[i % 4][i / 4] = raw_matrix[i];
    }
    Matrix44F projection_matrix(matrix_data);

    // Call the projection function
    std::vector<Polygon> result = doProject(
        stroke_points,
        geometry.vertices,
        geometry.indices,
        geometry.uvs,
        geometry.connectivity,
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

    class_<Geometry>("Geometry")
        .constructor<const Float32Array&, const Uint32Array&, const Float32Array&, const Int32Array&>();

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
        .field("i1",
               emscripten::optional_override([](const Face& self) -> uint32_t { return std::get<0>(self);} ),
               emscripten::optional_override([](Face& self, const uint32_t value) -> void {std::get<0>(self) = value;} ))
        .field("i2",
               emscripten::optional_override([](const Face& self) -> uint32_t { return std::get<1>(self);} ),
               emscripten::optional_override([](Face& self, const uint32_t value) -> void {std::get<1>(self) = value;} ))
        .field("i3",
               emscripten::optional_override([](const Face& self) -> uint32_t { return std::get<2>(self);} ),
               emscripten::optional_override([](Face& self, const uint32_t value)-> void {std::get<2>(self) = value;} ));

    value_object<FaceSigned>("FaceSigned")
        .field("i1",
               emscripten::optional_override([](const FaceSigned& self) -> int32_t { return std::get<0>(self);} ),
               emscripten::optional_override([](FaceSigned& self, const int32_t value) -> void {std::get<0>(self) = value;} ))
        .field("i2",
               emscripten::optional_override([](const FaceSigned& self) -> int32_t { return std::get<1>(self);} ),
               emscripten::optional_override([](FaceSigned& self, const int32_t value) -> void {std::get<1>(self) = value;} ))
        .field("i3",
               emscripten::optional_override([](const FaceSigned& self) -> int32_t { return std::get<2>(self);} ),
               emscripten::optional_override([](FaceSigned& self, const int32_t value)-> void {std::get<2>(self) = value;} ));
}