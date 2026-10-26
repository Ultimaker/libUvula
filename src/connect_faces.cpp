// (c) 2026, UltiMaker -- see LICENCE for details

#include "connect_faces.h"

#include "Point3F.h"

#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/iota.hpp>
#include <unordered_map>

std::tuple<int32_t, int32_t, int32_t> getIndexEquivFull(const std::vector<int32_t>& index_equivalances, const std::span<const Face>& indices, const int32_t face_idx)
{
    const Face& face = indices[face_idx];
    return { index_equivalances[face[0]], index_equivalances[face[1]], index_equivalances[face[2]] };
}

std::tuple<int32_t, int32_t, int32_t> getIndexEquivEmpty(const std::vector<int32_t>& index_equivalances, const std::span<const Face>& _, const int32_t face_idx)
{
    const auto base_idx = face_idx * 3;
    return { index_equivalances[base_idx], index_equivalances[base_idx + 1], index_equivalances[base_idx + 2] };
}

void connectFaces(const std::span<const Point3F>& vertices, const std::span<const Face>& indices, std::vector<FaceSigned>& out_face_connects)
{
    if (vertices.empty())
    {
        return;
    }

    // Build index equivalence mapping (in case of triangle-soup meshes).
    std::unordered_map<Point3F, int32_t> position_to_index;
    position_to_index.reserve(vertices.size() / 4 + 1);
    // ^^^ Note that yes, this needs to be a Point3F, not a pointer to a Point3F, since we need to  be prepared for equivalent points.
    std::vector<int32_t> index_equivalaneces(vertices.size());
    for (const auto [i_vertex, vertex] : vertices | ranges::views::enumerate)
    {
        const auto [it, inserted] = position_to_index.try_emplace(vertex, static_cast<int32_t>(i_vertex));
        index_equivalaneces[i_vertex] = it->second;
    }

    constexpr auto pack_edge = [](const int32_t u, const int32_t v) noexcept -> uint64_t
    {
        const auto lo = static_cast<uint32_t>(std::min(u, v));
        const auto hi = static_cast<uint32_t>(std::max(u, v));
        return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
    };

    const auto& get_index_equiv_func = indices.empty() ? getIndexEquivEmpty : getIndexEquivFull;
    const auto get_edge_list_func = [&indices, &index_equivalaneces, &get_index_equiv_func, &pack_edge](const int32_t face_idx)
    {
        // An edge is represented by a packed 64-bit pair of two vertex indices (smaller index in upper 32 bits).
        const auto [a, b, c] = get_index_equiv_func(index_equivalaneces, indices, face_idx);
        return std::array{
            pack_edge(a, b),
            pack_edge(b, c),
            pack_edge(c, a)
        };
    };

    struct EdgeFaceRef
    {
        int32_t face_idx;
        uint8_t edge_idx;
    };

    const auto face_count = indices.empty() ? vertices.size() / 3 : indices.size();
    std::unordered_map<uint64_t, EdgeFaceRef> edge_to_face;
    edge_to_face.reserve(face_count * 3 / 2 + 1);
    for (const int32_t face_idx : ranges::views::iota(0UL, face_count))
    {
        const auto edges = get_edge_list_func(face_idx);
        for (const auto [edge_idx, edge] : edges | ranges::views::enumerate)
        {
            const auto [it, inserted] = edge_to_face.try_emplace(edge, EdgeFaceRef{ face_idx, static_cast<uint8_t>(edge_idx) });
            if (!inserted)
            {
                const int32_t other_face = it->second.face_idx;
                const uint8_t other_edge = it->second.edge_idx;
                out_face_connects.at(face_idx).at(edge_idx) = other_face;
                out_face_connects[other_face].at(other_edge) = face_idx;
            }
        }
    }
}
