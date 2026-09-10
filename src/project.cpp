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


Face getFace(const std::span<const Face>& mesh_indices, const uint32_t face_index)
{
    return mesh_indices.empty() ? Face{ face_index * 3, face_index * 3 + 1, face_index * 3 + 2 } : mesh_indices[face_index];
}

Triangle3F getFaceTriangle(const std::span<const Point3F>& mesh_vertices, const Face& face)
{
    return Triangle3F(mesh_vertices[face[0]], mesh_vertices[face[1]], mesh_vertices[face[2]]);
}

Triangle2F getFaceUv(const std::span<const Point2F>& mesh_uv, const Face& face)
{
    return Triangle2F{ mesh_uv[face[0]], mesh_uv[face[1]], mesh_uv[face[2]] };
}

Point2F projectToViewport(const Point3F& point, const Matrix44F& matrix, const bool is_camera_perspective, const int viewport_width, const int viewport_height)
{
    Point3F projected = matrix.preMultiply(point);

    if (is_camera_perspective && projected.z() != 0)
    {
        projected /= (projected.z() * 2.0f);
    }

    return Point2F{ projected.x() * viewport_width / 2.0f, projected.y() * viewport_height / 2.0f };
}

Triangle2F projectToViewport(const Triangle3F& triangle, const Matrix44F& matrix, const bool is_camera_perspective, const int viewport_width, const int viewport_height)
{
    return Triangle2F{ projectToViewport(triangle.p1(), matrix, is_camera_perspective, viewport_width, viewport_height),
                       projectToViewport(triangle.p2(), matrix, is_camera_perspective, viewport_width, viewport_height),
                       projectToViewport(triangle.p3(), matrix, is_camera_perspective, viewport_width, viewport_height) };
}

std::vector<Point3F> getBarycentricCoordinates(const Polygon& polygon, const Triangle2F& triangle)
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
    constexpr double epsilon_triangle_cross_products = 0.001;
    if (std::abs(denom) < epsilon_triangle_cross_products)
    {
        return {};
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
    return Point2F{ u * texture_width, v * texture_height };
}

template<typename RangeOrInitList>
ClipperLib::Path toPath(const RangeOrInitList& polygon)
{
    ClipperLib::Path path;
    path.reserve(std::size(polygon));
    for (const Point2F& point : polygon)
    {
        path.push_back(ClipperLib::IntPoint{ std::llround(point.x * CLIPPER_PRECISION), std::llround(point.y * CLIPPER_PRECISION) });
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
            result_polygon.push_back(Point2F{ point.X / CLIPPER_PRECISION, point.Y / CLIPPER_PRECISION });
        }

        result.push_back(std::move(result_polygon));
    }

    return result;
}

std::vector<Polygon> unionPolygons(const std::vector<Polygon>& polygons)
{
    ClipperLib::Paths uv_areas_path;
    uv_areas_path.reserve(polygons.size());
    for (const Polygon& polygon : polygons)
    {
        uv_areas_path.push_back(toPath(polygon));
    }
    uv_areas_path = unionPaths(uv_areas_path);

    return toPolygons(uv_areas_path);
}

std::vector<Polygon> doProject(
    const std::span<Point2F>& stroke_polygon,
    const std::span<const Point3F>& mesh_vertices,
    const std::span<const Face>& mesh_indices,
    const std::span<const Point2F>& mesh_uv,
    const std::span<const FaceSigned>& mesh_faces_connectivity,
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

        processed_faces.insert(candidate_face_id);

        const Face face = getFace(mesh_indices, candidate_face_id);
        const Triangle3F face_triangle = getFaceTriangle(mesh_vertices, face);
        const std::optional<Vector3F> face_normal = face_triangle.normal();

        if (! face_normal.has_value() || face_normal->dot(camera_normal) < 0)
        {
            // Facing away from the viewer
            continue;
        }

        const Triangle2F projected_face_triangle = projectToViewport(face_triangle, camera_projection_matrix, is_camera_perspective, viewport_width, viewport_height);
        const ClipperLib::Path projected_face_triangle_path
            = toPath(std::initializer_list<Point2F>{ projected_face_triangle.p1, projected_face_triangle.p2, projected_face_triangle.p3 });
        const std::vector<Polygon> uv_areas = toPolygons(intersect(stroke_polygon_path, projected_face_triangle_path));

        if (uv_areas.empty())
        {
            continue;
        }

        const Triangle2F face_uv = getFaceUv(mesh_uv, face);
        for (const Polygon& uv_area : uv_areas)
        {
            const std::vector<Point3F> projected_stroke_polygon = getBarycentricCoordinates(uv_area, projected_face_triangle);
            if (projected_stroke_polygon.empty())
            {
                continue;
            }

            Polygon result_polygon;
            result_polygon.reserve(projected_stroke_polygon.size());
            for (const Point3F& point : projected_stroke_polygon)
            {
                result_polygon.push_back(getTextureCoordinates(point, face_uv, texture_width, texture_height));
            }

            result.push_back(std::move(result_polygon));
        }

        const FaceSigned& connected_faces = mesh_faces_connectivity[candidate_face_id];
        for (const int32_t connected_face : connected_faces)
        {
            if (connected_face >= 0 && ! processed_faces.contains(connected_face))
            {
                candidate_faces.insert(connected_face);
            }
        }
    }

    return unionPolygons(result);
}

std::vector<Polygon> doGetConnectedFaces(
    const std::span<const Point3F>& mesh_vertices,
    const std::span<const Face>& mesh_indices,
    const std::span<const Point2F>& mesh_uv,
    const std::span<const FaceSigned>& mesh_faces_connectivity,
    const uint32_t texture_width,
    const uint32_t texture_height,
    const uint32_t face_id,
    const double threshold_angle)
{
    constexpr double threshold_angle_limit = 0.02;
    const double actual_threshold_angle = std::cos(std::max(threshold_angle, threshold_angle_limit));

    const Face initial_face = getFace(mesh_indices, face_id);
    const Triangle3F initial_triangle = getFaceTriangle(mesh_vertices, initial_face);
    const std::optional<Vector3F> initial_face_normal = initial_triangle.normal();

    if (! initial_face_normal.has_value())
    {
        return {};
    }

    std::vector<Polygon> result;
    std::unordered_set<uint32_t> visited_faces;
    std::queue<uint32_t> faces_to_visit;
    faces_to_visit.push(face_id);
    while (! faces_to_visit.empty())
    {
        const uint32_t current_face_id = faces_to_visit.front();
        faces_to_visit.pop();

        if (visited_faces.contains(current_face_id))
        {
            continue;
        }

        visited_faces.insert(current_face_id);

        const Face current_face = getFace(mesh_indices, current_face_id);
        const Triangle3F current_triangle = getFaceTriangle(mesh_vertices, current_face);
        const std::optional<Vector3F> current_face_normal = current_triangle.normal();

        if (! current_face_normal.has_value())
        {
            continue;
        }

        const double angle_cosine = std::abs(initial_face_normal->dot(*current_face_normal));
        if (angle_cosine >= actual_threshold_angle)
        {
            // Add the polygon to the result
            const Triangle2F current_uv = getFaceUv(mesh_uv, current_face);
            Polygon current_polygon;
            for (const Point2F& point_uv : { current_uv.p1, current_uv.p2, current_uv.p3 })
            {
                current_polygon.emplace_back(point_uv.x * texture_width, point_uv.y * texture_height);
            }
            result.push_back(std::move(current_polygon));

            // Visit the connected faces
            const FaceSigned& connected_faces = mesh_faces_connectivity[current_face_id];

            for (int32_t connected_face_id : { connected_faces.i1, connected_faces.i2, connected_faces.i3 })
            {
                if (connected_face_id >= 0 && ! visited_faces.contains(connected_face_id))
                {
                    faces_to_visit.push(connected_face_id);
                }
            }
        }
    }

    return unionPolygons(result);
}