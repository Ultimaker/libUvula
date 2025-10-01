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
    const uint32_t face_id);