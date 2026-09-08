// (c) 2026, UltiMaker -- see LICENCE for details

#include "needs_supports.h"

#include "geometry_utils.h"
#include "Vector3F.h"

#include <algorithm>
#include <array>
#include <numeric>
#include <range/v3/view/enumerate.hpp>
#include <unordered_map>
#include <set>


bool checkForDownVertices(const float close_to_buildplate_dist, const std::span<const Point3F>& vertices, const std::span<const Face>& indices)
{
    std::unordered_map<Point3F, bool> verts_with_lower;
    const auto handle_edge_func = [close_to_buildplate_dist, &verts_with_lower](const Point3F& a, const Point3F& b)
    {
        if (std::min(a.y(), b.y()) < close_to_buildplate_dist || a.y() == b.y())
        {
            verts_with_lower[a] = false;
            verts_with_lower[b] = false;
        }
        verts_with_lower[a.y() > b.y() ? a : b] = false;
    };

    // Create a vertex adjacency graph -- but only append vertices that are _lower_ (except too close or below the BP).
    for (const auto& face : indices)
    {
        const auto& a = vertices[face.i1];
        const auto& b = vertices[face.i2];
        const auto& c = vertices[face.i3];

        // Check the angle; NOTE: Not against the support angle this time, but whether this tri is facing up or down.
        const auto maybe_face_norm = geometry_utils::triangleNormal(a, b, c);
        if (! maybe_face_norm.has_value())
        {
            // Should not happen, but if it does, the triangle was probably degenerate, so just continue.
            continue;
        }
        const auto& face_norm = maybe_face_norm.value();
        const bool norm_down = ((-1.0f <= face_norm.y() && face_norm.y() <= 1.0f) ? asinf(face_norm.y()) : 0.0f) < 0.0f;

        // Mark each vertex as handled if the norm when that's up, otherwise check each edge.
        for (const auto& v : std::array{ a, b, c })
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
    std::unordered_map<ptrdiff_t, float> candidate_overhangs;
    for (const auto [face_idx, face] : ranges::views::enumerate(indices))
    {
        const auto& a = vertices[face.i1];
        const auto& b = vertices[face.i2];
        const auto& c = vertices[face.i3];

        const auto maybe_face_norm = geometry_utils::triangleNormal(a, b, c);
        if (! maybe_face_norm.has_value())
        {
            // Should not happen, but if it does, the triangle was probably degenerate, so just continue.
            continue;
        }
        const auto& face_norm = maybe_face_norm.value();

        // Check the angle.
        const float angle = (-1.0f <= face_norm.y() && face_norm.y() <= 1.0f) ? -asinf(face_norm.y()) : 0.0f;
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
        const float area = 0.5f * Vector3F(b, a).cross(Vector3F(c, a)).abs_().length();
        candidate_overhangs[face_idx] = area;
    }

    if (candidate_overhangs.empty())
    {
        return false;
    }

    std::set<ptrdiff_t> visited;
    const std::function<std::set<ptrdiff_t>(const ptrdiff_t&)> collect_neighbours_func = [&](const ptrdiff_t& face_idx)
    {
        std::set<ptrdiff_t> res{face_idx};
        visited.insert(face_idx);
        const auto& nb_face_ids = mesh_connects[face_idx];
        for (const auto& nb_face_idx : std::array{ nb_face_ids.i1, nb_face_ids.i2, nb_face_ids.i3 })
        {
            if (nb_face_idx >= 0 && ! visited.contains(nb_face_idx) && candidate_overhangs.contains(nb_face_idx))
            {
                const auto res_nb = collect_neighbours_func(nb_face_idx);
                res.insert(res_nb.begin(), res_nb.end());
            }
        }
        return res;
    };

    // Is there a big enough area that needs to be supported?
    for (const auto [face_idx, angle] : candidate_overhangs)
    {
        if (visited.contains(face_idx))
        {
            continue;
        }
        const auto group{ collect_neighbours_func(face_idx) };
        const float group_area = std::accumulate(
            candidate_overhangs.begin(),
            candidate_overhangs.end(),
            0.0f,
            [](const float& a, const std::pair<ptrdiff_t, float>& b)
            {
                return a + b.second;
            });
        if (group_area >= min_support_area)
        {
            return true;
        }
    }

    return false;
}
