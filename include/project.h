// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Face.h"

struct Point3F;
struct Point2F;
class Matrix44F;
class Vector3F;

using Polygon = std::vector<Point2F>;

/**
 * \brief Projects a 2D stroke polygon onto a 3D mesh and returns the resulting 2D polygons in UV space.
 * \param stroke_polygon           The 2D stroke polygon to project.
 * \param mesh_vertices            The coordinates of the 3D vertices of the mesh.
 * \param mesh_indices             The mesh faces as indices into the vertex array, which may be empty of the mesh doesn't have indices.
 * \param mesh_uv                  The UV coordinates for each mesh vertex.
 * \param mesh_faces_connectivity  For each face of the mesh, contains the 3 indices of the adjacent faces, or -1 is edge is not connected.
 * \param texture_width            The width of the texture in pixels.
 * \param texture_height           The height of the texture in pixels.
 * \param camera_projection_matrix The camera projection matrix.
 * \param is_camera_perspective    True if the camera uses perspective projection, false for orthographic.
 * \param viewport_width           The width of the viewport in pixels.
 * \param viewport_height          The height of the viewport in pixels.
 * \param camera_normal            The normal vector of the camera.
 * \param face_id                  The ID of the initial face to project onto, other will be propagated using connectivity information.
 * \return A vector of polygons in UV space resulting from the projection.
 */
std::vector<Polygon> doProject(
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
    const uint32_t face_id);