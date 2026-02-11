#ifndef NAVMESH_PATHFINDER_H
#define NAVMESH_PATHFINDER_H

#include "navmesh.h"
#include <optional>
#include <vector>

namespace priceriot {

/**
 * Pathfinding on navigation mesh using A* algorithm.
 */
class NavMeshPathfinder {
  public:
    struct PathPoint {
        double x, z;
        PathPoint() : x(0.0), z(0.0) {}
        PathPoint(double x_, double z_) : x(x_), z(z_) {}
    };

    /**
     * Find a path from start to end position on the navmesh.
     * @param navmesh The navigation mesh
     * @param startX, startZ Starting position
     * @param endX, endZ Target position
     * @return Sequence of waypoints (empty if no path found)
     */
    static std::vector<PathPoint> findPath(const NavMesh &navmesh, double startX, double startZ,
                                           double endX, double endZ);

  private:
    struct AStarNode {
        int polygonIdx;
        double gCost; // Cost from start
        double hCost; // Heuristic to goal
        double fCost() const {
            return gCost + hCost;
        }
        int parentIdx; // Index in open/closed list

        bool operator>(const AStarNode &other) const {
            return fCost() > other.fCost();
        }
    };

    /**
     * Heuristic: straight-line distance between polygon centers.
     */
    static double heuristic(const NavPolygon &poly, double targetX, double targetZ);

    /**
     * Cost to move from one polygon to a neighbor.
     * Uses distance between polygon centers.
     */
    static double movementCost(const NavPolygon &from, const NavPolygon &to);

    /**
     * Reconstruct path from A* search results.
     */
    static std::vector<PathPoint> reconstructPath(const std::vector<AStarNode> &nodes, int goalIdx,
                                                  const NavMesh &navmesh, double startX,
                                                  double startZ, double endX, double endZ);

    /**
     * Smooth path using Simple Stupid Funnel algorithm over polygon corridor.
     * Replaces greedy string-pulling to avoid corner-sticking.
     */
    static std::vector<PathPoint> smoothPath(const std::vector<int> &polygonPath,
                                             const NavMesh &navmesh, double startX, double startZ,
                                             double endX, double endZ);

    /**
     * Get portal (left, right) between two adjacent navmesh polygons.
     * Ordered relative to path direction from a toward b.
     */
    static bool getPortalBetween(const NavPolygon &a, const NavPolygon &b, PathPoint &outLeft,
                                 PathPoint &outRight);

    /**
     * Fallback when getPortalBetween fails (e.g. inset node no longer shares exact edge).
     * Uses the edge polygon's boundary segment closest to the node polygon's center.
     */
    static bool getFallbackPortalBetween(const NavPolygon &a, const NavPolygon &b,
                                         PathPoint &outLeft, PathPoint &outRight);

    /**
     * Generic fallback: use segment of b whose midpoint is closest to a's center.
     * Ensures we always get a portal for every adjacent pair (e.g. edge-edge).
     */
    static bool getGenericFallbackPortal(const NavPolygon &a, const NavPolygon &b,
                                         PathPoint &outLeft, PathPoint &outRight);

    /**
     * 2D cross product: (b - o) x (c - o). Positive => c left of (o->b).
     */
    static double cross(double ox, double oz, double ax, double az, double bx, double bz);
};

} // namespace priceriot

#endif // NAVMESH_PATHFINDER_H
