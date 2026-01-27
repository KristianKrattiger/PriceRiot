#include "storeLayout.h"
#include <cmath>
#include <algorithm>

namespace priceriot {

// Ray-Box Intersection Helper (2D Top Down X, Z)
// Box centered at Cx, Cz with half-extents Hx, Hz
// Ray Origin Ox, Oz, Direction Dx, Dz
static sf::Vector2f intersectRayBox(float Ox, float Oz, float Dx, float Dz, 
                                    float Cx, float Cz, float Hx, float Hz) {
    // We want the point where the ray *leaves* the box.
    // Simple slab method
    // In our case, the Ray Origin is the center of the box (Cx, Cz).
    // So we just need to scale D to hit the boundary.
    
    // Avoid div by zero
    if (std::abs(Dx) < 1e-5f) Dx = (Dx > 0 ? 1e-5f : -1e-5f);
    if (std::abs(Dz) < 1e-5f) Dz = (Dz > 0 ? 1e-5f : -1e-5f);

    float t_x = (Dx > 0) ? (Hx / Dx) : (-Hx / Dx);
    float t_z = (Dz > 0) ? (Hz / Dz) : (-Hz / Dz);

    // The ray hits the nearest boundary
    float t = std::min(t_x, t_z);
    
    return sf::Vector2f(Cx + Dx * t, Cz + Dz * t);
}

std::vector<sf::Vector2f> NodeGeometry::getCorners() const {
    float hw = width / 2.0f;
    float hl = length / 2.0f;
    return {
        {x - hw, z - hl}, // Top-Left (min X, min Z)
        {x + hw, z - hl}, // Top-Right
        {x + hw, z + hl}, // Bottom-Right
        {x - hw, z + hl}  // Bottom-Left
    };
}

std::vector<sf::Vector2f> EdgeGeometry::getCorners() const {
    float hw = width / 2.0f;
    float dx = std::cos(angle + 1.5708f) * hw; // 90 deg rotation
    float dz = std::sin(angle + 1.5708f) * hw;
    
    return {
        {startX - dx, startZ - dz},
        {startX + dx, startZ + dz},
        {endX + dx, endZ + dz},
        {endX - dx, endZ - dz}
    };
}

void StoreLayout::buildGeometry(const StoreGraph& graph) {
    nodeGeoms.clear();
    edgeGeoms.clear();

    // 1. Process Nodes
    const auto& nodes = graph.getNodes();
    for (const auto& n : nodes) {
        NodeGeometry ng;
        ng.x = static_cast<float>(n->getX());
        ng.z = static_cast<float>(n->getZ());
        ng.width = static_cast<float>(n->getWidth());
        ng.length = static_cast<float>(n->getLength());
        nodeGeoms[n->getNodeId()] = ng;
    }

    // 2. Process Edges
    const auto& edges = graph.getEdges();
    for (const auto& e : edges) {
        int uID = graph.nodeAt(e->getFromNode()).getNodeId();
        int vID = graph.nodeAt(e->getToNode()).getNodeId();

        NodeGeometry& nU = nodeGeoms[uID];
        NodeGeometry& nV = nodeGeoms[vID];

        // Vector U -> V
        float dx = nV.x - nU.x;
        float dz = nV.z - nU.z;
        float dist = std::sqrt(dx*dx + dz*dz);
        float dirX = dx / dist;
        float dirZ = dz / dist;

        // Calculate exit points from the Node Hubs
        // Start Point: From Center U towards V, clipped to U's box
        sf::Vector2f pStart = intersectRayBox(nU.x, nU.z, dirX, dirZ,
            nU.x, nU.z, nU.width/2.0f, nU.length/2.0f);
        
        // End Point: From Center V towards U (Reverse dir), clipped to V's box
        sf::Vector2f pEnd = intersectRayBox(nV.x, nV.z, -dirX, -dirZ,
            nV.x, nV.z, nV.width/2.0f, nV.length/2.0f);

        EdgeGeometry eg;
        eg.startX = pStart.x;
        eg.startZ = pStart.y; // Vector2f.y is our Z
        eg.endX = pEnd.x;
        eg.endZ = pEnd.y;
        eg.width = static_cast<float>(e->getWidth());
        eg.shelfLeft = static_cast<float>(e->getShelfLeft());
        eg.shelfRight = static_cast<float>(e->getShelfRight());
        eg.angle = std::atan2(dz, dx);

        edgeGeoms[e->getEdgeId()] = eg;
    }
}

void StoreLayout::getBoundingBox(float& minX, float& maxX, float& minZ, float& maxZ) const {
    if (nodeGeoms.empty() && edgeGeoms.empty()) {
        minX = maxX = minZ = maxZ = 0.0f;
        return;
    }
    
    bool first = true;
    
    // Check all node corners
    for (const auto& [id, nodeGeo] : nodeGeoms) {
        auto corners = nodeGeo.getCorners();
        for (const auto& corner : corners) {
            if (first) {
                minX = maxX = corner.x;
                minZ = maxZ = corner.y;
                first = false;
            } else {
                minX = std::min(minX, corner.x);
                maxX = std::max(maxX, corner.x);
                minZ = std::min(minZ, corner.y);
                maxZ = std::max(maxZ, corner.y);
            }
        }
    }
    
    // Check all edge corners
    for (const auto& [id, edgeGeo] : edgeGeoms) {
        auto corners = edgeGeo.getCorners();
        for (const auto& corner : corners) {
            if (first) {
                minX = maxX = corner.x;
                minZ = maxZ = corner.y;
                first = false;
            } else {
                minX = std::min(minX, corner.x);
                maxX = std::max(maxX, corner.x);
                minZ = std::min(minZ, corner.y);
                maxZ = std::max(maxZ, corner.y);
            }
        }
    }
}

void StoreLayout::getCenter(float& centerX, float& centerZ) const {
    float minX, maxX, minZ, maxZ;
    getBoundingBox(minX, maxX, minZ, maxZ);
    centerX = (minX + maxX) / 2.0f;
    centerZ = (minZ + maxZ) / 2.0f;
}

} // namespace priceriot