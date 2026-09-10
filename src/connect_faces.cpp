// (c) 2026, UltiMaker -- see LICENCE for details

#include "connect_faces.h"

#include "Point3F.h"

#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/numeric/iota.hpp>
#include <map>
#include <unordered_map>

std::tuple<int32_t, int32_t, int32_t> getIndexEquivFull(const std::vector<int32_t>& index_equivalances, const std::span<const Face>& indices, const int32_t face_idx)
{
    const Face& face = indices[face_idx];
    return { index_equivalances[face.i1], index_equivalances[face.i2], index_equivalances[face.i3] };
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
    std::vector<int32_t> index_equivalaneces(vertices.size());
    ranges::iota(index_equivalaneces, 0);
    for (const auto [i_vertex, vertex] : vertices | ranges::views::enumerate)
    {
        if (position_to_index.contains(vertex))
        {
            index_equivalaneces[i_vertex] = position_to_index[vertex];
        }
        else
        {
            position_to_index[vertex] = i_vertex;
        }
    }

    const auto& get_index_equiv_func = indices.empty() ? getIndexEquivEmpty : getIndexEquivFull;
    const auto get_edge_list_func = [&indices, &index_equivalaneces, &get_index_equiv_func](const int32_t face_idx)
    {
        // An edge is represented by an pair of two vertex indices (smaller index first).
        const auto [a, b, c] = get_index_equiv_func(index_equivalaneces, indices, face_idx);
        return std::array{
            std::make_pair(std::min(a, b), std::max(a, b)),
            std::make_pair(std::min(b, c), std::max(b, c)),
            std::make_pair(std::min(c, a), std::max(c, a))
        };
    };

    std::map<std::pair<int32_t, int32_t>, int32_t> edge_to_face;
    // ^^^ Would need to make hash for pair to make unordered_map work.
    const auto face_count = indices.empty() ? vertices.size() / 3 : indices.size();
    for (const int32_t face_idx : ranges::views::iota(0UL, face_count))
    {
        const auto edges = get_edge_list_func(face_idx);
        for (const auto [edge_idx, edge] : edges | ranges::views::enumerate)
        {
            if (edge_to_face.contains(edge))
            {
                const int32_t other_face = edge_to_face[edge];
                out_face_connects.at(face_idx).at(edge_idx) = other_face;

                const auto other_edges = get_edge_list_func(other_face);
                for (const auto [i_edge, e] : other_edges | ranges::views::enumerate)
                {
                    if (e == edge)
                    {
                        out_face_connects[other_face].at(i_edge) = face_idx;
                        break;
                    }
                }
            }
            else
            {
                edge_to_face[edge] = face_idx;
            }
        }
    }
}
