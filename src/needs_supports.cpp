// (c) 2026, UltiMaker -- see LICENCE for details

#include "needs_supports.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <cstdint>
#include <unordered_map>

#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/iota.hpp>

#include "Point3F.h"
#include "Vector3F.h"
#include "geometry_utils.h"

std::tuple<const Point3F&, const Point3F&, const Point3F&> getVerticesFull(const std::span<const Point3F>& vertices, const std::span<const Face>& indices, const int32_t face_idx)
{
    const Face& face = indices[face_idx];
    return { vertices[face[0]], vertices[face[1]], vertices[face[2]] };
}

std::tuple<const Point3F&, const Point3F&, const Point3F&> getVerticesEmpty(const std::span<const Point3F>& vertices, const std::span<const Face>& _, const int32_t face_idx)
{
    const auto base_idx = face_idx * 3;
    return { vertices[base_idx], vertices[base_idx + 1], vertices[base_idx + 2] };
}

bool checkForDownVertices(const float close_to_buildplate_dist, const std::span<const Point3F>& vertices, const std::span<const Face>& indices)
{
    std::unordered_map<Point3F, bool> verts_with_lower;
    // ^^^ Note that yes, this needs to be a Point3F, not a pointer to a Point3F, since we need to  be prepared for equivalent points.
    const auto handle_edge_func = [close_to_buildplate_dist, &verts_with_lower](const Point3F& a, const Point3F& b)
    {
        if (std::min(a.y(), b.y()) < close_to_buildplate_dist || a.y() == b.y())
        {
            verts_with_lower[a] = false;
            verts_with_lower[b] = false;
            return;
        }
        verts_with_lower[a.y() > b.y() ? a : b] = false;
    };

    // Create a vertex adjacency graph -- but only append vertices that are _lower_ (except too close or below the BP).
    const auto& get_vertices_func = indices.empty() ? getVerticesEmpty : getVerticesFull;
    const auto face_count = indices.empty() ? vertices.size() / 3 : indices.size();
    for (const auto& face_idx : ranges::views::iota(0UL, face_count))
    {
        const auto [a, b, c] = get_vertices_func(vertices, indices, face_idx);

        // Check the angle; NOTE: Not against the support angle this time, but whether this tri is facing up or down.
        const auto maybe_face_norm = geometry_utils::triangleNormal(a, b, c);
        if (! maybe_face_norm.has_value())
        {
            // Should not happen, but if it does, the triangle was probably degenerate, so just continue.
            continue;
        }
        const auto& face_norm = maybe_face_norm.value();
        const bool norm_down = face_norm.y() < 0.0f;

        // Mark each vertex as handled if the norm when that's up, otherwise check each edge.
        for (const auto& v : { a, b, c })
        {
            if (! verts_with_lower.contains(v))
            {
                verts_with_lower[v] = norm_down;
            }
        }
        if (norm_down)
        {
            handle_edge_func(a, b);
            handle_edge_func(b, c);
            handle_edge_func(c, a);
        }
    }

    // Any vertex that has no adjacencies (that is, no vertices that are lower than it) is downward.
    return std::any_of(
        verts_with_lower.begin(),
        verts_with_lower.end(),
        [](const auto& v)
        {
            return v.second;
        });
}

bool checkForDownFaces(
    const float support_angle,
    const float close_to_buildplate_dist,
    const float min_support_area,
    const std::span<const Point3F>& vertices,
    const std::span<const Face>& indices,
    const std::span<const FaceSigned>& mesh_connects)
{
    const auto& get_vertices_func = indices.empty() ? getVerticesEmpty : getVerticesFull;
    const auto face_count = indices.empty() ? vertices.size() / 3 : indices.size();

    // Area of each candidate overhang face, or a negative value if the face is not a candidate
    std::vector<float> candidate_overhangs(face_count, -1.0F);

    bool has_candidates = false;
    for (const auto& face_idx : ranges::views::iota(0UL, face_count))
    {
        const auto [a, b, c] = get_vertices_func(vertices, indices, face_idx);

        const auto maybe_face_norm = geometry_utils::triangleNormal(a, b, c);
        if (! maybe_face_norm.has_value())
        {
            // Should not happen, but if it does, the triangle was probably degenerate, so just continue.
            continue;
        }
        const auto& face_norm = maybe_face_norm.value();

        // Upward-facing triangles can never be overhangs (support_angle is positive)
        if (face_norm.y() >= 0.0f && support_angle > 0.0f)
        {
            continue;
        }

        // Check the angle.
        const float angle = (-1.0f <= face_norm.y() && face_norm.y() <= 1.0f) ? -std::asin(face_norm.y()) : 0.0f;
        if (angle < support_angle)
        {
            continue;
        }

        // Check if the face is too close to (or underneath) the build-plate to 'count'.
        if (std::max({ a.y(), b.y(), c.y() }) < close_to_buildplate_dist)
        {
            continue;
        }

        // Collect the area for further analysis.
        const float area = 0.5f * Vector3F(b, a).cross(Vector3F(c, a)).abs().length();
        candidate_overhangs[face_idx] = area;
        has_candidates = true;
    }

    if (! has_candidates)
    {
        return false;
    }

    std::vector<bool> visited_faces(face_count, false);
    std::vector<size_t> faces_to_visit;

    // Is there a big enough area that needs to be supported?
    for (size_t start_face_idx = 0; start_face_idx < face_count; ++start_face_idx)
    {
        if (visited_faces[start_face_idx] || candidate_overhangs[start_face_idx] < 0.0f)
        {
            continue;
        }

        float island_area = 0.0f;
        visited_faces[start_face_idx] = true;
        faces_to_visit.push_back(start_face_idx);
        while (! faces_to_visit.empty())
        {
            const size_t face_idx = faces_to_visit.back();
            faces_to_visit.pop_back();
            island_area += candidate_overhangs[face_idx];
            for (const auto& nb_face_idx : mesh_connects[face_idx])
            {
                if (nb_face_idx >= 0 && static_cast<size_t>(nb_face_idx) < face_count && ! visited_faces[nb_face_idx] && candidate_overhangs[nb_face_idx] >= 0.0f)
                {
                    visited_faces[nb_face_idx] = true;
                    faces_to_visit.push_back(nb_face_idx);
                }
            }
        }

        if (island_area >= min_support_area)
        {
            return true;
        }
    }

    return false;
}
