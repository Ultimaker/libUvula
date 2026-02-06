#ifndef UVULA_FACE_SELECTION_H
#define UVULA_FACE_SELECTION_H

#include <vector>
#include <cstdint>

namespace uvula {

/**
 * @brief Build face neighbors array from mesh indices.
 * 
 * Computes topology by finding shared edges between triangles. Each face has 3 neighbors
 * (one per edge). Returns -1 for boundary edges with no neighbor.
 * 
 * @param indices Flat array of triangle indices [i0,i1,i2, i3,i4,i5, ...]
 * @param faceCount Number of faces (triangles)
 * @return Flat array of face neighbor IDs [n0a,n0b,n0c, n1a,n1b,n1c, ...]
 */
std::vector<int32_t> buildFaceNeighbors(
    const int32_t* indices,
    int32_t faceCount
);

/**
 * @brief Find all connected faces that are coplanar (or nearly coplanar) with a starting face.
 * 
 * Uses breadth-first search (BFS) to traverse connected triangles and selects those
 * whose normals are within the specified angle threshold from the starting face's normal.
 * This allows painting of "visual faces" - all triangles that form a flat surface together.
 * 
 * @param vertices Flat array of vertex positions [x0,y0,z0, x1,y1,z1, ...]
 * @param indices Flat array of triangle indices [i0,i1,i2, i3,i4,i5, ...]
 * @param faceNeighbors Flat array of face neighbor IDs [n0a,n0b,n0c, n1a,n1b,n1c, ...]
 *                      where each face has 3 neighbors (one per edge). Use -1 for boundary edges.
 * @param startFaceId The starting face ID (0-based index)
 * @param angleThreshold Cosine of the maximum angle difference between normals
 *                       (e.g., 0.99 ≈ 8 degrees, 0.90 ≈ 26 degrees)
 * @return Vector of face IDs that are connected and coplanar with the starting face
 */
std::vector<int32_t> getCoplanarConnectedFaces(
    const float* vertices,
    const int32_t* indices,
    const int32_t* faceNeighbors,
    int32_t startFaceId,
    float angleThreshold = 0.99f
);

/**
 * @brief Get UV coordinate polygons for a list of faces.
 * 
 * For each face in the input list, retrieves its UV coordinates and scales them
 * to texture space. Returns a flat array where each face contributes 6 floats:
 * [u0, v0, u1, v1, u2, v2]
 * 
 * @param faceIds List of face IDs to get UV polygons for
 * @param uvCoords Flat array of UV coordinates [u0,v0, u1,v1, ...]
 * @param indices Flat array of triangle indices (same as in getCoplanarConnectedFaces)
 * @param texWidth Texture width in pixels
 * @param texHeight Texture height in pixels
 * @return Flat array of UV polygon points [u0,v0,u1,v1,u2,v2, ...] for all faces
 */
std::vector<float> getUvPolygonsForFaces(
    const std::vector<int32_t>& faceIds,
    const float* uvCoords,
    const int32_t* indices,
    int32_t texWidth,
    int32_t texHeight
);

/**
 * @brief Compute face normal for a single triangle.
 * 
 * @param vertices Flat array of vertex positions
 * @param indices Flat array of triangle indices
 * @param faceId Face ID to compute normal for
 * @return Normalized normal vector [nx, ny, nz]
 */
std::vector<float> computeFaceNormal(
    const float* vertices,
    const int32_t* indices,
    int32_t faceId
);

} // namespace uvula

#endif // UVULA_FACE_SELECTION_H
