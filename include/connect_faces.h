// (c) 2026, UltiMaker -- see LICENCE for details

#pragma once

#include <span>
#include <vector>

#include "Face.h"
#include "Point3F.h"

/**
 * \brief Creates a face-connectivity data-structure.
 * \param vertices The vertices of the mesh.
 * \param indices The indices of the faces of the mesh, grouped by face.
 * \param out_face_connects The face-connectivity is saved here; each face has 3 connections; the index '-1' is used when there is no neighbour.
 */
void connectFaces(const std::span<const Point3F>& vertices, const std::span<const Face>& indices, std::vector<FaceSigned>& out_face_connects);
