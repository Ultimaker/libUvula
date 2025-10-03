// (c) 2025, UltiMaker -- see LICENCE for details

#include "unwrap.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>

#include <range/v3/algorithm/partition.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/map.hpp>
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
};

/*!
 * Calculate the best projection normals according to the given input faces
 * @param faces_data The faces data
 * @return A list of normals that are far enough from each other
 */
std::vector<Vector3F> calculateProjectionNormals(const std::vector<FaceData>& faces_data)
{
    constexpr float group_angle_limit = 20.0;

    const float group_angle_limit_cos = std::cos(geometry_utils::deg2rad(group_angle_limit));
    const float group_angle_limit_half_cos = std::cos(geometry_utils::deg2rad(group_angle_limit / 2));

    // First group will be based on the normal of the very first face
    const Vector3F* project_normal = &faces_data.front().normal;

    std::vector<Vector3F> projection_normals;

    // Create an internal list containing pointers to all the faces data, it will be reorganized
    std::vector<const FaceData*> faces_to_process(faces_data.size());
    std::transform(
        faces_data.begin(),
        faces_data.end(),
        faces_to_process.begin(),
        [](const FaceData& face_data)
        {
            return &face_data;
        });

    using FaceDataIterator = std::vector<const FaceData*>::iterator;
    struct FaceDataRange
    {
        FaceDataIterator begin;
        FaceDataIterator end;
    };

    // The unprocessed_faces is a sub-range of the faces list, that contains all the faces that have not been assigned to a group yet.
    FaceDataRange unprocessed_faces = { .begin = faces_to_process.begin(), .end = faces_to_process.end() };

    while (true)
    {
        // Get all the faces that belong to the group of the current projection normal,
        // by placing them at the beginning of the unprocessed faces
        FaceDataRange current_faces_group{ .begin = unprocessed_faces.begin };
        current_faces_group.end = ranges::partition(
            unprocessed_faces.begin,
            unprocessed_faces.end,
            [&project_normal, &group_angle_limit_half_cos](const FaceData* face_data)
            {
                return face_data->normal.dot(*project_normal) > group_angle_limit_half_cos;
            });

        // All the faces placed to the current group are now no more in the unprocessed faces
        unprocessed_faces.begin = current_faces_group.end;

        // Sum all the normals of the current faces group to get the average direction
        Vector3F summed_normals = std::accumulate(
            current_faces_group.begin,
            current_faces_group.end,
            Vector3F(),
            [](const Vector3F& normal, const FaceData* face_data)
            {
                return normal + face_data->normal;
            });
        if (summed_normals.normalize()) [[likely]]
        {
            projection_normals.push_back(summed_normals);
        }

        // For the next iteration, try to find the most different remaining normal from all generated normals
        float best_outlier_angle = std::numeric_limits<float>::max();
        FaceDataIterator best_outlier_face = faces_to_process.end();

        for (auto iterator = unprocessed_faces.begin; iterator != unprocessed_faces.end; ++iterator)
        {
            float face_best_angle = std::numeric_limits<float>::lowest();
            for (const Vector3F& projection_normal : projection_normals)
            {
                face_best_angle = std::max(face_best_angle, projection_normal.dot((*iterator)->normal));
            }

            if (face_best_angle < best_outlier_angle)
            {
                best_outlier_angle = face_best_angle;
                best_outlier_face = iterator;
            }
        }

        if (best_outlier_angle < group_angle_limit_cos)
        {
            // Take the normal of the best outlier as the base for the iteration of the next group
            project_normal = &(*best_outlier_face)->normal;

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
        const Point3F& v1 = vertices[face.i1];
        const Point3F& v2 = vertices[face.i2];
        const Point3F& v3 = vertices[face.i3];

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
    const std::vector<FaceData> faces_data = makeFacesData(vertices, faces);
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
    std::map<const Vector3F*, std::vector<const FaceData*>> projected_faces_groups;
    for (const FaceData& face_data : faces_data)
    {
        const Vector3F* best_projection_normal = nullptr;
        float angle_best = std::numeric_limits<float>::lowest();

        for (const Vector3F& projection_normal : project_normal_array)
        {
            const float angle = face_data.normal.dot(projection_normal);
            if (angle > angle_best)
            {
                angle_best = angle;
                best_projection_normal = &projection_normal;
            }
        }

        projected_faces_groups[best_projection_normal].push_back(&face_data);
    }

    // Now project each faces according to the closest matching normal and create indices groups
    std::vector<std::vector<size_t>> grouped_faces_indices;
    for (const auto& [normal, faces_data] : projected_faces_groups)
    {
        const Matrix33F axis_mat = Matrix33F::makeOrthogonalBasis(*normal);
        std::vector<size_t> faces_group;
        faces_group.reserve(faces_data.size());

        for (const FaceData* face_from_group : faces_data)
        {
            faces_group.push_back(face_from_group->face_index);

            for (const uint32_t vertex_index : { face_from_group->face->i1, face_from_group->face->i2, face_from_group->face->i3 })
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

    struct AssignedVertex
    {
        uint32_t index;
        bool assigned;
    };

    for (const std::vector<size_t>& faces_group : grouped_faces)
    {
        size_t max_group_index = 0; // Incrementing group index

        // Keep a double cache so that we can find very quickly the group of a vertex, and all the vertices from a group
        std::map<size_t, size_t> new_indices_groups; // vertex_index: group_index
        std::map<size_t, std::set<size_t>> new_groups_vertices; // group_index: [vertex_index]

        for (const size_t face_index : faces_group)
        {
            const Face& face = faces[face_index];
            std::set<size_t> assigned_groups;
            std::array<AssignedVertex, 3> assigned_vertices = { AssignedVertex{ .index = face.i1 }, AssignedVertex{ .index = face.i2 }, AssignedVertex{ .index = face.i3 } };
            for (AssignedVertex& assigned_vertex : assigned_vertices)
            {
                auto iterator = new_indices_groups.find(assigned_vertex.index);
                assigned_vertex.assigned = iterator != new_indices_groups.end();
                if (assigned_vertex.assigned)
                {
                    assigned_groups.insert(iterator->second);
                }
            }

            if (assigned_groups.empty())
            {
                // None of the points are assigned yet, just assign them to a new group
                const size_t new_group_index = max_group_index++;
                new_indices_groups[face.i1] = new_group_index;
                new_indices_groups[face.i2] = new_group_index;
                new_indices_groups[face.i3] = new_group_index;
                new_groups_vertices[new_group_index] = { face.i1, face.i2, face.i3 };
            }
            else
            {
                const size_t target_group = *assigned_groups.begin();
                std::set<size_t>& target_group_vertices = new_groups_vertices[target_group];

                std::set<size_t> source_groups = assigned_groups;
                source_groups.erase(source_groups.begin());

                // First assign vertices that are not assigned yet
                for (const AssignedVertex& assigned_vertex : assigned_vertices)
                {
                    if (! assigned_vertex.assigned)
                    {
                        new_indices_groups[assigned_vertex.index] = target_group;
                        target_group_vertices.insert(assigned_vertex.index);
                    }
                }

                // Now merge source groups to the target group, including actually processed vertices
                for (const size_t source_group : source_groups)
                {
                    auto it_source_group = new_groups_vertices.find(source_group);
                    for (const size_t vertex_from_source_group : it_source_group->second)
                    {
                        size_t& vertex_group = new_indices_groups[vertex_from_source_group];
                        vertex_group = target_group;
                        target_group_vertices.insert(vertex_from_source_group);
                    }

                    new_groups_vertices.erase(it_source_group);
                }
            }
        }

        std::map<size_t, std::vector<size_t>> new_faces_groups; // group_index: [face_index]
        for (const size_t face_index : faces_group)
        {
            const Face& face = faces[face_index];
            new_faces_groups[new_indices_groups[face.i1]].push_back(face_index);
        }

        for (const std::vector<size_t>& faces_indices : new_faces_groups | ranges::views::values)
        {
            result.push_back(faces_indices);
        }
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
    std::map<Point3F, size_t> unique_vertices_indices;
    std::vector<uint32_t> new_vertices_indices(vertices.size());

    for (const auto [index, vertex] : vertices | ranges::views::enumerate)
    {
        auto iterator = unique_vertices_indices.find(vertex);
        if (iterator == unique_vertices_indices.end())
        {
            // This is the very first time we see this position, register it
            unique_vertices_indices[vertex] = index;
            new_vertices_indices[index] = index;
        }
        else
        {
            new_vertices_indices[index] = iterator->second;
        }
    }

    for (const Face& face : faces)
    {
        faces_with_similar_indices.push_back(Face{ new_vertices_indices[face.i1], new_vertices_indices[face.i2], new_vertices_indices[face.i3] });
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
    // Create an xatlas object and register the mesh with the basic UV coordinates
    xatlas::Atlas* atlas = xatlas::Create();
    xatlas::UvMeshDecl mesh;
    mesh.vertexUvData = uv_coords.data();
    mesh.indexData = faces.data();
    mesh.vertexCount = vertices.size();
    mesh.vertexStride = sizeof(Point2F);
    mesh.indexCount = faces.size() * 3;
    mesh.indexFormat = xatlas::IndexFormat::UInt32;

    if (xatlas::AddUvMesh(atlas, mesh) != xatlas::AddMeshError::Success)
    {
        xatlas::Destroy(atlas);
        spdlog::error("Error adding mesh");
        return false;
    }

    // Use a smaller calculation definition, which makes the calculation much faster and adds more margin between the islands, then scale it up
    constexpr uint32_t calculation_definition = 512;
    constexpr uint32_t desired_definition = 4096;

    // Set the pre-calculated faces groups
    xatlas::SetCharts(atlas, charts);

    // Now pack the charts on the image
    constexpr xatlas::PackOptions pack_options{ .padding = 0, .resolution = calculation_definition };
    xatlas::PackCharts(atlas, pack_options);

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

    xatlas::Destroy(atlas);
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
