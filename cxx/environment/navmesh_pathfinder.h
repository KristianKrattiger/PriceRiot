#ifndef NAVMESH_PATHFINDER_H
#define NAVMESH_PATHFINDER_H

#include "navmesh.h"
#include <vector>
#include <optional>

namespace priceriot {

/**
 * Pathfinding on navigation mesh using A* algorithm.
 */
class NavMeshPathfinder {
public:
    struct PathPoint {
        double x, z;
        PathPoint(double x_, double z_) : x(x_), z(z_) {}
    };

    /**
     * Find a path from start to end position on the navmesh.
     * @param navmesh The navigation mesh
     * @param startX, startZ Starting position
     * @param endX, endZ Target position
     * @return Sequence of waypoints (empty if no path found)
     */
    static std::vector<PathPoint> findPath(const NavMesh& navmesh,
                                           double startX, double startZ,
                                           double endX, double endZ);

private:
    struct AStarNode {
        int polygonIdx;
        double gCost;  // Cost from start
        double hCost;  // Heuristic to goal
        double fCost() const { return gCost + hCost; }
        int parentIdx; // Index in open/closed list
        
        bool operator>(const AStarNode& other) const {
            return fCost() > other.fCost();
        }
    };
    
    /**
     * Heuristic: straight-line distance between polygon centers.
     */
    static double heuristic(const NavPolygon& poly, double targetX, double targetZ);
    
    /**
     * Cost to move from one polygon to a neighbor.
     * Uses distance between polygon centers.
     */
    static double movementCost(const NavPolygon& from, const NavPolygon& to);
    
    /**
     * Reconstruct path from A* search results.
     */
    static std::vector<PathPoint> reconstructPath(
        const std::vector<AStarNode>& nodes,
        int goalIdx,
        const NavMesh& navmesh,
        double startX, double startZ,
        double endX, double endZ);
    
    /**
     * Smooth path using polygon centers and endpoints.
     */
    static std::vector<PathPoint> smoothPath(const std::vector<int>& polygonPath,
                                             const NavMesh& navmesh,
                                             double startX, double startZ,
                                             double endX, double endZ);
};

} // namespace priceriot

#endif // NAVMESH_PATHFINDER_H
