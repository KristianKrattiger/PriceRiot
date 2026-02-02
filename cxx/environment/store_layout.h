/**
 * @file store_layout.h
 * @brief Geometry for visualization: node hubs and edge centerlines.
 *
 * StoreLayout::buildGeometry() converts StoreGraph nodes/edges into NodeGeometry and
 * EdgeGeometry (world coordinates, corners) for SFML rendering. Used by sim and
 * NavMeshGenerator/PhysicsGenerator.
 */
#ifndef STORE_LAYOUT_H
#define STORE_LAYOUT_H

#include "environment.h"
#include <SFML/System/Vector2.hpp>
#include <map>
#include <vector>

namespace priceriot {

struct NodeGeometry {
    float x, z;          // World center in meters
    float width, length; // Hub dimensions

    // Helper to get global corners (for debugging/rendering)
    // Returns {TopLeft, TopRight, BottomRight, BottomLeft} in X/Z
    std::vector<sf::Vector2f> getCorners() const;
};

struct EdgeGeometry {
    // Defines the centerline segment on the floor
    float startX, startZ;
    float endX, endZ;

    // Metadata for subtraction/visuals
    float width;
    float shelfLeft, shelfRight;
    float angle; // Radians, from Start to End

    // Helper to get corners of the full aisle rectangle
    std::vector<sf::Vector2f> getCorners() const;
};

class StoreLayout {
  public:
    std::map<int, NodeGeometry> nodeGeoms; // Key: NodeID
    std::map<int, EdgeGeometry> edgeGeoms; // Key: EdgeID

    void buildGeometry(const StoreGraph &graph);

    // Calculate bounding box of the entire store layout
    void getBoundingBox(float &minX, float &maxX, float &minZ, float &maxZ) const;
    void getCenter(float &centerX, float &centerZ) const;
};

} // namespace priceriot

#endif // STORE_LAYOUT_H
