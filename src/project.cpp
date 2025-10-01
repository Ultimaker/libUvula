// (c) 2025, UltiMaker -- see LICENCE for details

#include "project.h"

#include <polyclipping/clipper.hpp>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "Matrix44F.h"
#include "Point2F.h"
#include "Point3F.h"
#include "Triangle2F.h"
#include "Triangle3F.h"
#include "Vector2F.h"
#include "geometry_utils.h"

static constexpr float CLIPPER_PRECISION = 1000.0;


Face getFace(const std::span<Face>& mesh_indices, const uint32_t face_index)
{
    return mesh_indices.empty() ? Face{ face_index * 3, face_index * 3 + 1, face_index * 3 + 2 } : mesh_indices[face_index];
}

Triangle3F getFaceTriangle(const std::span<Point3F>& mesh_vertices, const Face& face)
{
    return Triangle3F(mesh_vertices[face.i1], mesh_vertices[face.i2], mesh_vertices[face.i3]);
}

Triangle2F getFaceUv(const std::span<Point2F>& mesh_uv, const Face& face)
{
    return Triangle2F(mesh_uv[face.i1], mesh_uv[face.i2], mesh_uv[face.i3]);
}

Point2F projectToViewport(const Point3F& point, const Matrix44F& matrix, const bool is_camera_perspective, const int viewport_width, const int viewport_height)
{
    Point3F projected = matrix.preMultiply(point);

    if (is_camera_perspective && projected.z() != 0)
    {
        projected /= (projected.z() * 2.0f);
    }

    return Point2F(projected.x() * viewport_width / 2.0f, projected.y() * viewport_height / 2.0f);
}

Triangle2F projectToViewport(const Triangle3F& triangle, const Matrix44F& matrix, const bool is_camera_perspective, const int viewport_width, const int viewport_height)
{
    return Triangle2F(
        projectToViewport(triangle.p1(), matrix, is_camera_perspective, viewport_width, viewport_height),
        projectToViewport(triangle.p2(), matrix, is_camera_perspective, viewport_width, viewport_height),
        projectToViewport(triangle.p3(), matrix, is_camera_perspective, viewport_width, viewport_height));
}

std::optional<std::vector<Point3F>> getBarycentricCoordinates(const Polygon& polygon, const Triangle2F& triangle)
{
    // Calculate base vectors
    const Vector2F v0(triangle.p1, triangle.p2);
    const Vector2F v1(triangle.p1, triangle.p3);

    // Compute dot products
    const double d00 = v0.dot(v0);
    const double d01 = v0.dot(v1);
    const double d11 = v1.dot(v1);

    // Calculate denominator for barycentric coordinates
    const double denom = d00 * d11 - d01 * d01;

    // Check if triangle is degenerate
    constexpr double epsilon_triangle_cross_products = 0.000001;
    if (std::abs(denom) < epsilon_triangle_cross_products)
    {
        return std::nullopt;
    }

    std::vector<Point3F> result;
    result.reserve(polygon.size());

    for (const Point2F& point : polygon)
    {
        const Vector2F v2(triangle.p1, point);
        const double d20 = v2.dot(v0);
        const double d21 = v2.dot(v1);

        // Calculate barycentric coordinates
        const double v = (d11 * d20 - d01 * d21) / denom;
        const double w = (d00 * d21 - d01 * d20) / denom;
        const double u = 1.0 - v - w;

        // Return as a Point3D where x/y/z represent the barycentric coordinates u/v/w
        result.emplace_back(u, v, w);
    }

    return result;
}

Point2F getTextureCoordinates(const Point3F& barycentric_coordinates, const Triangle2F& uv_coordinates, const uint32_t texture_width, const uint32_t texture_height)
{
    const float u = (uv_coordinates.p1.x * barycentric_coordinates.x()) + (uv_coordinates.p2.x * barycentric_coordinates.y()) + (uv_coordinates.p3.x * barycentric_coordinates.z());
    const float v = (uv_coordinates.p1.y * barycentric_coordinates.x()) + (uv_coordinates.p2.y * barycentric_coordinates.y()) + (uv_coordinates.p3.y * barycentric_coordinates.z());
    return Point2F(u * texture_width, v * texture_height);
}

template<typename RangeOrInitList>
void addClipperPath(const RangeOrInitList& polygon, const ClipperLib::PolyType type, ClipperLib::Clipper& clipper)
{
    ClipperLib::Path path;
    path.reserve(std::size(polygon));
    for (const Point2F& point : polygon)
    {
        path.emplace_back(std::llround(point.x * CLIPPER_PRECISION), std::llround(point.y * CLIPPER_PRECISION));
    }
    clipper.AddPath(path, type, true);
}

template<typename RangeOrInitList>
ClipperLib::Path toPath(const RangeOrInitList& polygon)
{
    ClipperLib::Path path;
    path.reserve(std::size(polygon));
    for (const Point2F& point : polygon)
    {
        path.emplace_back(std::llround(point.x * CLIPPER_PRECISION), std::llround(point.y * CLIPPER_PRECISION));
    }
    return path;
}

ClipperLib::Paths intersect(const ClipperLib::Path& polygon1, const ClipperLib::Path& polygon2)
{
    ClipperLib::Clipper clipper;
    clipper.AddPath(polygon1, ClipperLib::ptSubject, true);
    clipper.AddPath(polygon2, ClipperLib::ptClip, true);

    ClipperLib::Paths ret;
    clipper.Execute(ClipperLib::ctIntersection, ret);

    return ret;
}

ClipperLib::Paths unionPaths(const ClipperLib::Paths& paths)
{
    ClipperLib::Clipper clipper;
    clipper.AddPaths(paths, ClipperLib::ptSubject, true);

    ClipperLib::Paths ret;
    clipper.Execute(ClipperLib::ctUnion, ret);

    return ret;
}

std::vector<Polygon> toPolygons(const ClipperLib::Paths& paths)
{
    std::vector<Polygon> result;
    result.reserve(paths.size());
    for (const ClipperLib::Path& path : paths)
    {
        Polygon result_polygon;
        result_polygon.reserve(path.size());
        for (const ClipperLib::IntPoint& point : path)
        {
            result_polygon.emplace_back(point.X / CLIPPER_PRECISION, point.Y / CLIPPER_PRECISION);
        }

        result.emplace_back(std::move(result_polygon));
    }

    return result;
}

std::vector<Polygon> project(
    const std::span<Point2F>& stroke_polygon,
    const std::span<Point3F>& mesh_vertices,
    const std::span<Face>& mesh_indices,
    const std::span<Point2F>& mesh_uv,
    const std::span<FaceSigned>& mesh_faces_connectivity,
    const uint32_t texture_width,
    const uint32_t texture_height,
    const Matrix44F& camera_projection_matrix,
    const bool is_camera_perspective,
    const uint32_t viewport_width,
    const uint32_t viewport_height,
    const Vector3F& camera_normal,
    const uint32_t face_id)
{
    std::vector<Polygon> result;
    const ClipperLib::Path stroke_polygon_path = toPath(stroke_polygon);

    std::unordered_set<uint32_t> candidate_faces{ face_id };
    std::unordered_set<uint32_t> processed_faces;

    while (! candidate_faces.empty())
    {
        auto iterator = candidate_faces.begin();
        const uint32_t candidate_face_id = *iterator;
        candidate_faces.erase(iterator);

        // if (processed_faces.contains(candidate_face_id))
        // {
        //     continue;
        // }
        processed_faces.insert(candidate_face_id);

        const Face face = getFace(mesh_indices, candidate_face_id);
        const Triangle3F face_triangle = getFaceTriangle(mesh_vertices, face);
        const Vector3F face_normal = face_triangle.normal();

        if (face_normal.dot(camera_normal) < 0)
        {
            // Facing away from the viewer
            continue;
        }

        const Triangle2F projected_face_triangle = projectToViewport(face_triangle, camera_projection_matrix, is_camera_perspective, viewport_width, viewport_height);
        const ClipperLib::Path projected_face_triangle_path = toPath(std::initializer_list{ projected_face_triangle.p1, projected_face_triangle.p2, projected_face_triangle.p3 });
        const std::vector<Polygon> uv_areas = toPolygons(intersect(stroke_polygon_path, projected_face_triangle_path));

        if (uv_areas.empty())
        {
            continue;
        }

        const Triangle2F face_uv = getFaceUv(mesh_uv, face);
        for (const Polygon& uv_area : uv_areas)
        {
            const std::optional<std::vector<Point3F>> projected_stroke_polygon = getBarycentricCoordinates(uv_area, projected_face_triangle);
            if (! projected_stroke_polygon.has_value())
            {
                continue;
            }

            Polygon result_polygon;
            result_polygon.reserve(projected_stroke_polygon.value().size());
            for (const Point3F& point : projected_stroke_polygon.value())
            {
                result_polygon.push_back(getTextureCoordinates(point, face_uv, texture_width, texture_height));
            }

            result.push_back(std::move(result_polygon));
        }

        const FaceSigned& connected_faces = mesh_faces_connectivity[candidate_face_id];
        for (const int32_t connected_face : { connected_faces.i1, connected_faces.i2, connected_faces.i3 })
        {
            if (connected_face >= 0 && ! processed_faces.contains(connected_face))
            {
                candidate_faces.insert(connected_face);
            }
        }
    }

    ClipperLib::Paths uv_areas_path;
    uv_areas_path.reserve(result.size());
    for (const Polygon& polygon : result)
    {
        uv_areas_path.push_back(toPath(polygon));
    }
    uv_areas_path = unionPaths(uv_areas_path);

    return toPolygons(uv_areas_path);
}