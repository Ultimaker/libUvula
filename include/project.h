// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <cstdint>
#include <span>
#include <vector>

struct Face;
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
    const uint32_t texture_width,
    const uint32_t texture_height,
    const Matrix44F& camera_projection_matrix,
    const bool is_camera_perspective,
    const int viewport_width,
    const int viewport_height,
    const Vector3F& camera_normal,
    const std::span<uint32_t>& faces);