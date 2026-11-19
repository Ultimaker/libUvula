// (c) 2026, UltiMaker -- see LICENCE for details

#include "connect_faces.h"

#include "Point3F.h"

#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/numeric/iota.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
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
    // ^^^ Note that yes, this needs to be a Point3F, not a pointer to a Point3F, since we need to  be prepared for equivalent points.
    position_to_index.reserve(vertices.size());
    std::vector<int32_t> index_equivalaneces(vertices.size());
    for (const auto [i_vertex, vertex] : vertices | ranges::views::enumerate)
    {
        const auto [it, inserted] = position_to_index.try_emplace(vertex, static_cast<int32_t>(i_vertex));
        index_equivalaneces[i_vertex] = it->second;
    }

    const auto& get_index_equiv_func = indices.empty() ? getIndexEquivEmpty : getIndexEquivFull;

    struct EdgeRef
    {
        int32_t face_idx;
        int32_t edge_idx;
    };

    // An undirected edge is represented by the pair of its two vertex indices (smaller index first) packed into 64 bits.
    const auto pack_edge_func = [](const int32_t vertex_idx1, const int32_t vertex_idx2)
    {
        return (static_cast<uint64_t>(std::min(vertex_idx1, vertex_idx2)) << 32) | static_cast<uint64_t>(std::max(vertex_idx1, vertex_idx2));
    };

    const auto face_count = indices.empty() ? vertices.size() / 3 : indices.size();
    std::unordered_map<uint64_t, EdgeRef> edge_to_face;
    edge_to_face.reserve(face_count * 3);
    for (const int32_t face_idx : ranges::views::iota(0UL, face_count))
    {
        const auto [a, b, c] = get_index_equiv_func(index_equivalaneces, indices, face_idx);
        const std::array<uint64_t, 3> edges{ pack_edge_func(a, b), pack_edge_func(b, c), pack_edge_func(c, a) };
        for (const auto [edge_idx, edge] : edges | ranges::views::enumerate)
        {
            const auto [it, inserted] = edge_to_face.try_emplace(edge, EdgeRef{ face_idx, static_cast<int32_t>(edge_idx) });
            if (! inserted)
            {
                const EdgeRef& other = it->second;
                out_face_connects.at(face_idx).at(edge_idx) = other.face_idx;
                out_face_connects[other.face_idx].at(other.edge_idx) = face_idx;
            }
        }
    }
}
