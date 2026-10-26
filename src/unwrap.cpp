// (c) 2025, UltiMaker -- see LICENCE for details

#include "unwrap.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <span>
#include <unordered_map>

#include <range/v3/algorithm/partition.hpp>
#include <range/v3/view/enumerate.hpp>
#include <spdlog/spdlog.h>

#include "Matrix33F.h"
#include "Point2F.h"
#include "Point3F.h"
#include "Vector3F.h"
#include "geometry_utils.h"
#include "xatlas.h"


struct FaceData
{
    const Face* face;
    size_t face_index;
    Vector3F normal;
    float best_angle = std::numeric_limits<float>::lowest();
};

/*!
 * Calculate the best projection normals according to the given input faces
 * @param faces_data The faces data
 * @return A list of normals that are far enough from each other
 */
std::vector<Vector3F> calculateProjectionNormals(std::span<FaceData> faces_data)
{
    constexpr float group_angle_limit = 20.0;

    const float group_angle_limit_cos = std::cos(geometry_utils::deg2rad(group_angle_limit));
    const float group_angle_limit_half_cos = std::cos(geometry_utils::deg2rad(group_angle_limit / 2));

    // First group will be based on the normal of the very first face
    Vector3F project_normal = faces_data.front().normal;

    std::vector<Vector3F> projection_normals;

    // Create an internal list containing pointers to all the faces data, it will be reorganized
    std::vector<FaceData*> faces_to_process(faces_data.size());
    std::transform(
        faces_data.begin(),
        faces_data.end(),
        faces_to_process.begin(),
        [](FaceData& face_data) noexcept
        {
            return &face_data;
        });

    using FaceDataIterator = std::vector<FaceData*>::iterator;
    struct FaceDataRange
    {
        FaceDataIterator begin;
        FaceDataIterator end;
    };

    // The unprocessed_faces is a sub-range of the faces list, that contains all the faces that have not been assigned to a group yet.
    FaceDataRange unprocessed_faces = { .begin = std::next(faces_to_process.begin()), .end = faces_to_process.end() };

    while (true)
    {
        // Get all the faces that belong to the group of the current projection normal,
        // by placing them at the beginning of the unprocessed faces
        FaceDataRange current_faces_group{ .begin = unprocessed_faces.begin };
        current_faces_group.end = ranges::partition(
            unprocessed_faces.begin,
            unprocessed_faces.end,
            [&project_normal, group_angle_limit_half_cos](const FaceData* face_data)
            {
                return face_data->normal.dot(project_normal) > group_angle_limit_half_cos;
            });

        // All the faces placed to the current group are now no more in the unprocessed faces
        unprocessed_faces.begin = current_faces_group.end;

        // Sum all the normals of the current faces group to get the average direction
        Vector3F summed_normals = std::accumulate(
            current_faces_group.begin,
            current_faces_group.end,
            project_normal,
            [](const Vector3F& normal, const FaceData* face_data)
            {
                return normal + face_data->normal;
            });
        const bool added_normal = summed_normals.normalize();
        if (added_normal) [[likely]]
        {
            projection_normals.push_back(summed_normals);
        }

        // For the next iteration, try to find the most different remaining normal from all generated normals
        float best_outlier_angle = std::numeric_limits<float>::max();
        FaceDataIterator best_outlier_face = faces_to_process.end();

        if (added_normal) [[likely]]
        {
            const Vector3F& latest_normal = projection_normals.back();
            for (auto iterator = unprocessed_faces.begin; iterator != unprocessed_faces.end; ++iterator)
            {
                FaceData* face_data = *iterator;
                const float face_best_angle = std::max(face_data->best_angle, latest_normal.dot(face_data->normal));
                face_data->best_angle = face_best_angle;

                if (face_best_angle < best_outlier_angle)
                {
                    best_outlier_angle = face_best_angle;
                    best_outlier_face = iterator;
                }
            }
        }
        else
        {
            for (auto iterator = unprocessed_faces.begin; iterator != unprocessed_faces.end; ++iterator)
            {
                const float face_best_angle = (*iterator)->best_angle;
                if (face_best_angle < best_outlier_angle)
                {
                    best_outlier_angle = face_best_angle;
                    best_outlier_face = iterator;
                }
            }
        }

        if (best_outlier_angle < group_angle_limit_cos)
        {
            // Take the normal of the best outlier as the base for the iteration of the next group
            project_normal = (*best_outlier_face)->normal;

            // Remove the faces from the unprocessed faces
            std::iter_swap(best_outlier_face, unprocessed_faces.begin);
            ++unprocessed_faces.begin;
        }
        else if (! projection_normals.empty())
        {
            break;
        }
    }

    return projection_normals;
}

static std::vector<FaceData> makeFacesData(const std::vector<Point3F>& vertices, const std::vector<Face>& faces)
{
    std::vector<FaceData> faces_data;
    faces_data.reserve(faces.size());

    for (const auto& [index, face] : faces | ranges::views::enumerate)
    {
        const Point3F& v1 = vertices[face[0]];
        const Point3F& v2 = vertices[face[1]];
        const Point3F& v3 = vertices[face[2]];

        const std::optional<Vector3F> triangle_normal = geometry_utils::triangleNormal(v1, v2, v3);
        if (triangle_normal.has_value())
        {
            faces_data.push_back(FaceData{ &face, index, triangle_normal.value() });
        }
    }

    return faces_data;
}

/*!
 * Groups the faces that have a similar normal, and project their points as raw UV coordinates along this normal
 * @param vertices The list of vertices positions
 * @param faces The list of faces we want to project
 * @param uv_coords The UV coordinates, which should be properly sized but the input content doesn't matter. As output, they will be filled with
 *                  raw UV coordinates that overlap and are not in the [0,1] range
 * @return A list containing grouped indices of faces
 */
static std::vector<std::vector<size_t>> makeCharts(const std::vector<Point3F>& vertices, const std::vector<Face>& faces, std::vector<Point2F>& uv_coords)
{
    std::vector<FaceData> faces_data = makeFacesData(vertices, faces);
    if (faces_data.empty()) [[unlikely]]
    {
        return {};
    }

    // Calculate the best normals to group the faces
    const std::vector<Vector3F> project_normal_array = calculateProjectionNormals(faces_data);
    if (project_normal_array.empty()) [[unlikely]]
    {
        return {};
    }

    // For each face, find the best projection normal and make groups
    std::vector<std::vector<const FaceData*>> projected_faces_groups(project_normal_array.size());
    for (const FaceData& face_data : faces_data)
    {
        size_t best_normal_index = 0;
        float angle_best = std::numeric_limits<float>::lowest();

        for (size_t i = 0; i < project_normal_array.size(); ++i)
        {
            const float angle = face_data.normal.dot(project_normal_array[i]);
            if (angle > angle_best)
            {
                angle_best = angle;
                best_normal_index = i;
            }
        }

        projected_faces_groups[best_normal_index].push_back(&face_data);
    }

    // Now project each faces according to the closest matching normal and create indices groups
    std::vector<std::vector<size_t>> grouped_faces_indices;
    grouped_faces_indices.reserve(project_normal_array.size());
    for (size_t i = 0; i < project_normal_array.size(); ++i)
    {
        const std::vector<const FaceData*>& group_faces = projected_faces_groups[i];
        if (group_faces.empty())
        {
            continue;
        }
        const Matrix33F axis_mat = Matrix33F::makeOrthogonalBasis(project_normal_array[i]);
        std::vector<size_t> faces_group;
        faces_group.reserve(group_faces.size());

        for (const FaceData* face_from_group : group_faces)
        {
            faces_group.push_back(face_from_group->face_index);

            for (const uint32_t vertex_index : *face_from_group->face)
            {
                uv_coords[vertex_index] = axis_mat.project(vertices[vertex_index]);
            }
        }

        grouped_faces_indices.push_back(std::move(faces_group));
    }

    return grouped_faces_indices;
}

/*!
 * When projecting faces groups along a normal, it is possible that we project faces that are actually far away from each other spatially. This sometimes
 * results in overlapping projections, which we really want to avoid. The purpose of this function is to make sub-groups of faces groups for faces that are
 * adjacent to each other.
 * @param grouped_faces Contains the grouped indices of faces
 * @param faces The actual faces definitions, whose vertices should have been merged before, @sa groupSimilarVertices()
 * @return Grouped faces with groups containing only adjacent faces. It may be identical to the original groups, or contain more smaller groups
 */
std::vector<std::vector<size_t>> splitNonLinkedFacesCharts(const std::vector<std::vector<size_t>>& grouped_faces, const std::vector<Face>& faces)
{
    std::vector<std::vector<size_t>> result;
    if (grouped_faces.empty() || faces.empty())
    {
        return result;
    }

    uint32_t max_vertex_index = 0;
    for (const Face& face : faces)
    {
        max_vertex_index = std::max({ max_vertex_index, face[0], face[1], face[2] });
    }

    constexpr size_t unassigned = std::numeric_limits<size_t>::max();
    std::vector<size_t> vertex_group(static_cast<size_t>(max_vertex_index) + 1, unassigned);
    std::vector<size_t> parent;
    std::vector<std::vector<size_t>> new_faces_groups;

    auto find_root = [&parent](size_t g) noexcept -> size_t
    {
        while (parent[g] != g)
        {
            parent[g] = parent[parent[g]];
            g = parent[g];
        }
        return g;
    };

    for (const std::vector<size_t>& faces_group : grouped_faces)
    {
        parent.clear();

        for (const size_t face_index : faces_group)
        {
            const Face& face = faces[face_index];
            size_t target_group = unassigned;
            size_t g0 = vertex_group[face[0]];
            size_t g1 = vertex_group[face[1]];
            size_t g2 = vertex_group[face[2]];

            if (g0 != unassigned)
            {
                g0 = find_root(g0);
                target_group = g0;
            }
            if (g1 != unassigned)
            {
                g1 = find_root(g1);
                target_group = std::min(target_group, g1);
            }
            if (g2 != unassigned)
            {
                g2 = find_root(g2);
                target_group = std::min(target_group, g2);
            }

            if (target_group == unassigned)
            {
                const size_t new_group_index = parent.size();
                parent.push_back(new_group_index);
                vertex_group[face[0]] = new_group_index;
                vertex_group[face[1]] = new_group_index;
                vertex_group[face[2]] = new_group_index;
            }
            else
            {
                if (g0 != unassigned && g0 != target_group)
                {
                    parent[g0] = target_group;
                }
                if (g1 != unassigned && g1 != target_group)
                {
                    parent[g1] = target_group;
                }
                if (g2 != unassigned && g2 != target_group)
                {
                    parent[g2] = target_group;
                }
                vertex_group[face[0]] = target_group;
                vertex_group[face[1]] = target_group;
                vertex_group[face[2]] = target_group;
            }
        }

        new_faces_groups.resize(parent.size());
        for (const size_t face_index : faces_group)
        {
            const Face& face = faces[face_index];
            const size_t root_group = find_root(vertex_group[face[0]]);
            new_faces_groups[root_group].push_back(face_index);
        }

        for (const size_t face_index : faces_group)
        {
            const Face& face = faces[face_index];
            vertex_group[face[0]] = unassigned;
            vertex_group[face[1]] = unassigned;
            vertex_group[face[2]] = unassigned;
        }

        for (std::vector<size_t>& faces_indices : new_faces_groups)
        {
            if (! faces_indices.empty())
            {
                result.push_back(std::move(faces_indices));
            }
        }
        new_faces_groups.clear();
    }

    return result;
}

/*!
 * When loading the mesh, each vertex of each triangle is given a unique index, even if it is used in multiple adjacent triangles. The purpose
 * of this function is to remove double vertices so that we can make adjacency detection easier.
 * @param faces The original list of faces
 * @param vertices The original list of vertices position
 * @return The modified list of faces, which contains as many faces but with merged vertices
 */
std::vector<Face> groupSimilarVertices(const std::vector<Face>& faces, const std::vector<Point3F>& vertices)
{
    std::vector<Face> faces_with_similar_indices;
    faces_with_similar_indices.reserve(faces.size());
    std::unordered_map<Point3F, uint32_t> unique_vertices_indices;
    unique_vertices_indices.reserve(vertices.size() / 4 + 1);
    std::vector<uint32_t> new_vertices_indices(vertices.size());

    for (const auto [index, vertex] : vertices | ranges::views::enumerate)
    {
        const auto [iterator, inserted] = unique_vertices_indices.try_emplace(vertex, static_cast<uint32_t>(index));
        new_vertices_indices[index] = iterator->second;
    }

    for (const Face& face : faces)
    {
        faces_with_similar_indices.push_back(Face{ new_vertices_indices[face[0]], new_vertices_indices[face[1]], new_vertices_indices[face[2]] });
    }

    return faces_with_similar_indices;
}

/*!
 * Packs the charts (faces groups) onto a texture image by using as much space as possible without having them overlap
 * @param vertices The list of vertices position
 * @param faces The list of vertices position
 * @param charts The list of grouped faces indices
 * @param uv_coords The original UV coordinates, which may be overlapping and not fitting on an image. As an output they
 *                  will be properly scaled and distributed on the image.
 * @param texture_width Output width to be used for the texture image
 * @param texture_height Output height to be used for the texture image
 * @return
 */
bool packCharts(
    const std::vector<Point3F>& vertices,
    const std::vector<Face>& faces,
    const std::vector<std::vector<size_t>>& charts,
    std::vector<Point2F>& uv_coords,
    uint32_t& texture_width,
    uint32_t& texture_height)
{
    // Create an xatlas object managed via RAII and register the mesh with the basic UV coordinates
    using AtlasPtr = std::unique_ptr<xatlas::Atlas, decltype([](xatlas::Atlas* a) noexcept { xatlas::Destroy(a); })>;
    const AtlasPtr atlas{ xatlas::Create() };
    xatlas::UvMeshDecl mesh;
    mesh.vertexUvData = uv_coords.data();
    mesh.indexData = faces.data();
    mesh.vertexCount = vertices.size();
    mesh.vertexStride = sizeof(Point2F);
    mesh.indexCount = faces.size() * 3;
    mesh.indexFormat = xatlas::IndexFormat::UInt32;

    if (xatlas::AddUvMesh(atlas.get(), mesh) != xatlas::AddMeshError::Success)
    {
        spdlog::error("Error adding mesh");
        return false;
    }

    // Use a smaller calculation definition, which makes the calculation much faster and adds more margin between the islands, then scale it up
    constexpr uint32_t calculation_definition = 512;
    constexpr uint32_t desired_definition = 4096;

    // Set the pre-calculated faces groups
    xatlas::SetCharts(atlas.get(), charts);

    // Now pack the charts on the image
    constexpr xatlas::PackOptions pack_options{ .padding = 0, .resolution = calculation_definition };
    xatlas::PackCharts(atlas.get(), pack_options);

    // Now scale up the size
    texture_width = atlas->width;
    texture_height = atlas->height;
    const uint32_t max_side = std::max(texture_width, texture_height);
    const double scale = static_cast<double>(desired_definition) / static_cast<double>(max_side);
    texture_width = std::llrint(texture_width * scale);
    texture_height = std::llrint(texture_height * scale);

    // Convert the output data
    const xatlas::Mesh& output_mesh = *atlas->meshes;
    const auto width = static_cast<float>(atlas->width);
    const auto height = static_cast<float>(atlas->height);
    for (size_t i = 0; i < output_mesh.vertexCount; ++i)
    {
        const xatlas::PlacedVertex& vertex = output_mesh.vertexArray[i];
        uv_coords[vertex.xref] = Point2F{ .x = vertex.uv[0] / width, .y = vertex.uv[1] / height };
    }

    return true;
}

bool smartUnwrap(const std::vector<Point3F>& vertices, const std::vector<Face>& faces, std::vector<Point2F>& uv_coords, uint32_t& texture_width, uint32_t& texture_height)
{
    // Make a first projection and grouping of the faces to UV coordinates
    std::vector<std::vector<size_t>> charts = makeCharts(vertices, faces, uv_coords);

    // Split faces group to get only groups of adjacent faces
    std::vector<Face> const faces_with_similar_indices = groupSimilarVertices(faces, vertices);
    charts = splitNonLinkedFacesCharts(charts, faces_with_similar_indices);

    // Now pack the UV coordinates onto a proper image surface
    return packCharts(vertices, faces, charts, uv_coords, texture_width, texture_height);
}
