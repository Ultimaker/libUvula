#include "face_selection.h"
#include <cmath>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

namespace uvula {

namespace {
    // Helper function to normalize a 3D vector
    inline void normalize(float& x, float& y, float& z) {
        float length = std::sqrt(x * x + y * y + z * z);
        if (length > 1e-6f) {
            x /= length;
            y /= length;
            z /= length;
        }
    }
    
    // Helper function to compute dot product
    inline float dot(float x1, float y1, float z1, float x2, float y2, float z2) {
        return x1 * x2 + y1 * y2 + z1 * z2;
    }
    
    // Edge key for hash map (order-independent)
    struct Edge {
        int32_t v0, v1;
        Edge(int32_t a, int32_t b) : v0(std::min(a, b)), v1(std::max(a, b)) {}
        bool operator==(const Edge& other) const {
            return v0 == other.v0 && v1 == other.v1;
        }
    };
    
    struct EdgeHash {
        std::size_t operator()(const Edge& e) const {
            return std::hash<int32_t>()(e.v0) ^ (std::hash<int32_t>()(e.v1) << 1);
        }
    };
}

std::vector<int32_t> buildFaceNeighbors(
    const int32_t* indices,
    int32_t faceCount
) {
    std::vector<int32_t> neighbors(faceCount * 3, -1);
    std::unordered_map<Edge, std::pair<int32_t, int32_t>, EdgeHash> edgeToFace;
    
    // Build edge-to-face mapping
    for (int32_t faceId = 0; faceId < faceCount; ++faceId) {
        int32_t i0 = indices[faceId * 3 + 0];
        int32_t i1 = indices[faceId * 3 + 1];
        int32_t i2 = indices[faceId * 3 + 2];
        
        // Three edges of this triangle
        Edge edges[3] = {Edge(i0, i1), Edge(i1, i2), Edge(i2, i0)};
        
        for (int edgeIdx = 0; edgeIdx < 3; ++edgeIdx) {
            auto& edge = edges[edgeIdx];
            auto it = edgeToFace.find(edge);
            
            if (it == edgeToFace.end()) {
                // First face for this edge
                edgeToFace[edge] = {faceId, edgeIdx};
            } else {
                // Second face - update both neighbors
                int32_t otherFace = it->second.first;
                int32_t otherEdgeIdx = it->second.second;
                
                neighbors[faceId * 3 + edgeIdx] = otherFace;
                neighbors[otherFace * 3 + otherEdgeIdx] = faceId;
            }
        }
    }
    
    return neighbors;
}

std::vector<float> computeFaceNormal(
    const float* vertices,
    const int32_t* indices,
    int32_t faceId
) {
    // Get the three vertex indices for this face
    int32_t idx0 = indices[faceId * 3 + 0];
    int32_t idx1 = indices[faceId * 3 + 1];
    int32_t idx2 = indices[faceId * 3 + 2];
    
    // Get vertex positions
    float v0x = vertices[idx0 * 3 + 0];
    float v0y = vertices[idx0 * 3 + 1];
    float v0z = vertices[idx0 * 3 + 2];
    
    float v1x = vertices[idx1 * 3 + 0];
    float v1y = vertices[idx1 * 3 + 1];
    float v1z = vertices[idx1 * 3 + 2];
    
    float v2x = vertices[idx2 * 3 + 0];
    float v2y = vertices[idx2 * 3 + 1];
    float v2z = vertices[idx2 * 3 + 2];
    
    // Compute edge vectors
    float e1x = v1x - v0x;
    float e1y = v1y - v0y;
    float e1z = v1z - v0z;
    
    float e2x = v2x - v0x;
    float e2y = v2y - v0y;
    float e2z = v2z - v0z;
    
    // Compute cross product (normal)
    float nx = e1y * e2z - e1z * e2y;
    float ny = e1z * e2x - e1x * e2z;
    float nz = e1x * e2y - e1y * e2x;
    
    // Normalize
    normalize(nx, ny, nz);
    
    return {nx, ny, nz};
}

std::vector<int32_t> getCoplanarConnectedFaces(
    const float* vertices,
    const int32_t* indices,
    const int32_t* faceNeighbors,
    int32_t startFaceId,
    float angleThreshold
) {
    // Validate input
    if (startFaceId < 0) {
        return {startFaceId};
    }
    
    // Compute the normal of the starting face
    std::vector<float> startNormal = computeFaceNormal(vertices, indices, startFaceId);
    float startNx = startNormal[0];
    float startNy = startNormal[1];
    float startNz = startNormal[2];
    
    // BFS to find all connected coplanar faces
    std::unordered_set<int32_t> visited;
    std::queue<int32_t> toVisit;
    std::vector<int32_t> coplanarFaces;
    
    toVisit.push(startFaceId);
    visited.insert(startFaceId);
    
    while (!toVisit.empty()) {
        int32_t currentFace = toVisit.front();
        toVisit.pop();
        
        // Compute normal for current face
        std::vector<float> currentNormal = computeFaceNormal(vertices, indices, currentFace);
        float currentNx = currentNormal[0];
        float currentNy = currentNormal[1];
        float currentNz = currentNormal[2];
        
        // Calculate dot product (cosine of angle between normals)
        float dotProduct = std::abs(dot(startNx, startNy, startNz, currentNx, currentNy, currentNz));
        
        // Check if this face is coplanar with the start face
        if (dotProduct >= angleThreshold) {
            coplanarFaces.push_back(currentFace);
            
            // Add unvisited neighbors to the queue
            for (int i = 0; i < 3; ++i) {
                int32_t neighborId = faceNeighbors[currentFace * 3 + i];
                
                // Check if neighbor exists and hasn't been visited
                if (neighborId >= 0 && visited.find(neighborId) == visited.end()) {
                    visited.insert(neighborId);
                    toVisit.push(neighborId);
                }
            }
        }
    }
    
    // Return the list of coplanar faces, or just the start face if none found
    return coplanarFaces.empty() ? std::vector<int32_t>{startFaceId} : coplanarFaces;
}

std::vector<float> getUvPolygonsForFaces(
    const std::vector<int32_t>& faceIds,
    const float* uvCoords,
    const int32_t* indices,
    int32_t texWidth,
    int32_t texHeight
) {
    std::vector<float> result;
    result.reserve(faceIds.size() * 6); // Each face has 3 vertices * 2 coords = 6 floats
    
    for (int32_t faceId : faceIds) {
        // Get the three vertex indices for this face
        int32_t idx0 = indices[faceId * 3 + 0];
        int32_t idx1 = indices[faceId * 3 + 1];
        int32_t idx2 = indices[faceId * 3 + 2];
        
        // Get UV coordinates and scale to texture space
        float u0 = uvCoords[idx0 * 2 + 0] * texWidth;
        float v0 = uvCoords[idx0 * 2 + 1] * texHeight;
        
        float u1 = uvCoords[idx1 * 2 + 0] * texWidth;
        float v1 = uvCoords[idx1 * 2 + 1] * texHeight;
        
        float u2 = uvCoords[idx2 * 2 + 0] * texWidth;
        float v2 = uvCoords[idx2 * 2 + 1] * texHeight;
        
        // Add to result array
        result.push_back(u0);
        result.push_back(v0);
        result.push_back(u1);
        result.push_back(v1);
        result.push_back(u2);
        result.push_back(v2);
    }
    
    return result;
}

} // namespace uvula
