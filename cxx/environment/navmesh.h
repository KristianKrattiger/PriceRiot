/**
 * @file navmesh.h
 * @brief Navigation mesh: walkable polygons for pathfinding.
 *
 * NavPolygon represents a convex walkable region. NavMesh stores polygons and provides
 * spatial queries (find polygon containing point, find nearest). Used by NavMeshPathfinder
 * for A* pathfinding with funnel smoothing.
 */
#ifndef NAVMESH_H
#define NAVMESH_H

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace priceriot {

// Forward declaration
class StoreGraph;

/**
 * Represents a single walkable polygon in the navmesh.
 * Polygons are convex and can be triangles or higher-order polygons.
 */
class NavPolygon {
  public:
    struct Vertex {
        double x, z;
        Vertex(double x_, double z_) : x(x_), z(z_) {}
    };

    NavPolygon() : polygonId(-1), associatedNodeId(-1), associatedEdgeId(-1) {}

    NavPolygon(int id, const std::vector<Vertex> &vertices_)
        : polygonId(id), vertices(vertices_), associatedNodeId(-1), associatedEdgeId(-1) {}

    int getPolygonId() const noexcept {
        return polygonId;
    }
    const std::vector<Vertex> &getVertices() const noexcept {
        return vertices;
    }
    std::vector<Vertex> &getVertices() noexcept {
        return vertices;
    }

    // Association with graph elements
    int getAssociatedNodeId() const noexcept {
        return associatedNodeId;
    }
    int getAssociatedEdgeId() const noexcept {
        return associatedEdgeId;
    }
    void setAssociatedNodeId(int nodeId) noexcept {
        associatedNodeId = nodeId;
    }
    void setAssociatedEdgeId(int edgeId) noexcept {
        associatedEdgeId = edgeId;
    }

    // Neighbor connections (indices into NavMesh polygon array)
    const std::vector<int> &getNeighbors() const noexcept {
        return neighbors;
    }
    void addNeighbor(int polygonIdx) {
        if (std::find(neighbors.begin(), neighbors.end(), polygonIdx) == neighbors.end()) {
            neighbors.push_back(polygonIdx);
        }
    }

    // Geometric queries
    bool containsPoint(double x, double z) const;
    std::pair<double, double> getCenter() const;
    double getArea() const;

    // Helper: point-in-polygon test using ray casting
    static bool pointInPolygon(const std::vector<Vertex> &vertices, double x, double z);

    // Walkability
    bool isWalkable() const noexcept {
        return walkable;
    }
    void setWalkable(bool w) noexcept {
        walkable = w;
    }

  private:
    int polygonId;
    std::vector<Vertex> vertices;
    std::vector<int> neighbors; // Indices into NavMesh polygons array
    bool walkable = true;
    int associatedNodeId = -1; // -1 if not associated with a node
    int associatedEdgeId = -1; // -1 if not associated with an edge
};

/**
 * Navigation mesh container and query system.
 * Stores all walkable polygons and provides spatial queries and pathfinding interface.
 */
class NavMesh {
  public:
    NavMesh() = default;

    // Polygon management
    int addPolygon(const NavPolygon &polygon);
    const NavPolygon &getPolygon(int index) const {
        return polygons.at(static_cast<size_t>(index));
    }
    NavPolygon &getPolygon(int index) {
        return polygons.at(static_cast<size_t>(index));
    }
    int getPolygonCount() const noexcept {
        return static_cast<int>(polygons.size());
    }

    // Spatial queries
    std::optional<int> findPolygonContaining(double x, double z) const;
    std::optional<int> findNearestPolygon(double x, double z) const;

    // Neighbor connections
    void connectPolygons(int polyIdx1, int polyIdx2);

    // Validation
    bool validate() const;

    // Clear all polygons and caches
    void clear() {
        polygons.clear();
        spatialGrid_.clear();
        spatialIndexBuilt_ = false;
        pathCache_.clear();
    }

    // Path cache: keyed by (startPolyIdx << 22 | goalPolyIdx) → polygon corridor
    const std::vector<int>* lookupPathCache(int startPoly, int goalPoly) const {
        int64_t key = (static_cast<int64_t>(startPoly) << 22) | static_cast<int64_t>(goalPoly);
        auto it = pathCache_.find(key);
        return (it != pathCache_.end()) ? &it->second : nullptr;
    }
    void insertPathCache(int startPoly, int goalPoly, const std::vector<int>& path) const {
        int64_t key = (static_cast<int64_t>(startPoly) << 22) | static_cast<int64_t>(goalPoly);
        pathCache_.emplace(key, path);
    }

  private:
    std::vector<NavPolygon> polygons;

    // Spatial acceleration index
    static constexpr double kGridCellSize = 4.0;
    mutable std::unordered_map<int64_t, std::vector<int>> spatialGrid_;
    mutable bool spatialIndexBuilt_ = false;

    // A* polygon-path cache (navmesh is static, so paths are immutable after build)
    mutable std::unordered_map<int64_t, std::vector<int>> pathCache_;

    void buildSpatialIndex() const;
    static int64_t cellKey(int cx, int cz) {
        return (static_cast<int64_t>(static_cast<uint32_t>(cx)) << 32) |
               static_cast<int64_t>(static_cast<uint32_t>(cz));
    }
    static double distanceSquared(double x1, double z1, double x2, double z2);
};

} // namespace priceriot

#endif // NAVMESH_H
