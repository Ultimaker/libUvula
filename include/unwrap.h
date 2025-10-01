// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <cstdint>
#include <vector>

#include "Face.h"

struct Point3F;
struct Point2F;

/*!
 * Groups, projects and packs the faces of the input mesh to non-overlapping and properly distributed UV coordinates patches
 * @param vertices List containing the position of the input vertices
 * @param faces List of faces composing the mesh
 * @param uv_coords Output list of UV coordinates, which should be pre-sized to the same size as the vertices
 * @param texture_width Output width to be used for the texture image
 * @param texture_height Output height to be used for the texture image
 * @return
 */
bool smartUnwrap(const std::vector<Point3F>& vertices, const std::vector<Face>& faces, std::vector<Point2F>& uv_coords, uint32_t& texture_width, uint32_t& texture_height);