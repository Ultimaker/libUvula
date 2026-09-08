// (c) 2026, UltiMaker -- see LICENCE for details

#pragma once

#include "Face.h"
#include "Point3F.h"

#include <span>

/**
 * \brief Checks for downwards pointing (so, smaller in y-axis) vertices (unless 'close enough' to y=0) given a mesh.
 * \param close_to_buildplate_dist Any vertex below this on the y-axis will be ignored.
 * \param vertices The vertices of the mesh.
 * \param indices The indices of the faces of the mesh, grouped by face.
 * \return True if there are any such vertices, false otherwise.
 */
bool checkForDownVertices(
    const float close_to_buildplate_dist,
    const std::span<const Point3F>& vertices,
    const std::span<const Face>& indices);

/**
 * \brief Checks for neighbouring faces with a normal 'close enough' to pointing downwards (so, smaller in y-axis)
 *        that form at least one 'large enough' area (unless 'close enough' to y=0) given a mesh (+ connectivity).
 * \param support_angle Determines when a face is 'downwards' (has a normal close enough to 'down') enough to count.
 * \param close_to_buildplate_dist Any face below this on the y-axis will be ignored.
 * \param min_support_area How large any grouped area of faces should be (measured by area) for it to be counted.
 * \param vertices The vertices of the mesh.
 * \param indices The indices of the faces of the mesh, grouped by face.
 * \param mes_connects The mesh-connectivity; per face, contains the indices of the neighbour-faces (-1 for none).
 * \return True if there are any such vertices, false otherwise.
 */
bool checkForDownFaces(
    const float support_angle,
    const float close_to_buildplate_dist,
    const float min_support_area,
    const std::span<const Point3F>& vertices,
    const std::span<const Face>& indices,
    const std::span<const FaceSigned>& mesh_connects);
