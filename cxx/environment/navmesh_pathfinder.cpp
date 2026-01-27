#include "navmesh_pathfinder.h"
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <limits>

namespace priceriot {

std::vector<NavMeshPathfinder::PathPoint> NavMeshPathfinder::findPath(
    const NavMesh& navmesh,
    double startX, double startZ,
    double endX, double endZ) {
    
    // Find starting and goal polygons
    auto startPolyOpt = navmesh.findPolygonContaining(startX, startZ);
    auto goalPolyOpt = navmesh.findPolygonContaining(endX, endZ);
    
    // If not contained, find nearest
    if (!startPolyOpt) {
        startPolyOpt = navmesh.findNearestPolygon(startX, startZ);
    }
    if (!goalPolyOpt) {
        goalPolyOpt = navmesh.findNearestPolygon(endX, endZ);
    }
    
    if (!startPolyOpt || !goalPolyOpt) {
        return {}; // No path found
    }
    
    int startPolyIdx = *startPolyOpt;
    int goalPolyIdx = *goalPolyOpt;
    
    if (startPolyIdx == goalPolyIdx) {
        // Same polygon, direct path
        return {{startX, startZ}, {endX, endZ}};
    }
    
    // A* search
    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;
    std::unordered_set<int> closedSet;
    std::vector<AStarNode> allNodes; // Track all nodes for path reconstruction
    
    // Initialize start node
    AStarNode startNode;
    startNode.polygonIdx = startPolyIdx;
    startNode.gCost = 0.0;
    startNode.hCost = heuristic(navmesh.getPolygon(startPolyIdx), endX, endZ);
    startNode.parentIdx = -1;
    
    allNodes.push_back(startNode);
    openSet.push(startNode);
    
    int goalNodeIdx = -1;
    
    while (!openSet.empty()) {
        AStarNode current = openSet.top();
        openSet.pop();
        
        if (closedSet.count(current.polygonIdx) > 0) {
            continue;
        }
        
        closedSet.insert(current.polygonIdx);
        
        // Check if we reached the goal
        if (current.polygonIdx == goalPolyIdx) {
            // Find the actual node index in allNodes that matches current
            for (size_t i = 0; i < allNodes.size(); ++i) {
                if (allNodes[i].polygonIdx == current.polygonIdx && 
                    std::abs(allNodes[i].gCost - current.gCost) < 0.001 &&
                    allNodes[i].parentIdx == current.parentIdx) {
                    goalNodeIdx = static_cast<int>(i);
                    break;
                }
            }
            if (goalNodeIdx < 0) {
                goalNodeIdx = static_cast<int>(allNodes.size()) - 1;
            }
            break;
        }
        
        // Explore neighbors
        const NavPolygon& currentPoly = navmesh.getPolygon(current.polygonIdx);
        const auto& neighbors = currentPoly.getNeighbors();
        
        for (int neighborIdx : neighbors) {
            if (closedSet.count(neighborIdx) > 0) {
                continue;
            }
            
            const NavPolygon& neighborPoly = navmesh.getPolygon(neighborIdx);
            double newGCost = current.gCost + movementCost(currentPoly, neighborPoly);
            
            // Check if we've seen this polygon with a better cost
            bool foundBetter = false;
            for (const auto& node : allNodes) {
                if (node.polygonIdx == neighborIdx && node.gCost <= newGCost) {
                    foundBetter = true;
                    break;
                }
            }
            
            if (!foundBetter) {
                AStarNode neighborNode;
                neighborNode.polygonIdx = neighborIdx;
                neighborNode.gCost = newGCost;
                neighborNode.hCost = heuristic(neighborPoly, endX, endZ);
                neighborNode.parentIdx = static_cast<int>(allNodes.size()) - 1;
                
                allNodes.push_back(neighborNode);
                openSet.push(neighborNode);
            }
        }
    }
    
    if (goalNodeIdx < 0) {
        return {}; // No path found
    }
    
    // Reconstruct path
    std::vector<int> polygonPath;
    int currentIdx = goalNodeIdx;
    while (currentIdx >= 0) {
        polygonPath.push_back(allNodes[currentIdx].polygonIdx);
        currentIdx = allNodes[currentIdx].parentIdx;
    }
    std::reverse(polygonPath.begin(), polygonPath.end());
    
    // Smooth path and convert to waypoints
    return smoothPath(polygonPath, navmesh, startX, startZ, endX, endZ);
}

double NavMeshPathfinder::heuristic(const NavPolygon& poly, double targetX, double targetZ) {
    auto [centerX, centerZ] = poly.getCenter();
    double dx = targetX - centerX;
    double dz = targetZ - centerZ;
    return std::sqrt(dx * dx + dz * dz);
}

double NavMeshPathfinder::movementCost(const NavPolygon& from, const NavPolygon& to) {
    auto [fromX, fromZ] = from.getCenter();
    auto [toX, toZ] = to.getCenter();
    double dx = toX - fromX;
    double dz = toZ - fromZ;
    return std::sqrt(dx * dx + dz * dz);
}

std::vector<NavMeshPathfinder::PathPoint> NavMeshPathfinder::reconstructPath(
    const std::vector<AStarNode>& nodes,
    int goalIdx,
    const NavMesh& navmesh,
    double startX, double startZ,
    double endX, double endZ) {
    
    std::vector<PathPoint> path;
    path.emplace_back(startX, startZ);
    
    // Add polygon centers
    int currentIdx = goalIdx;
    while (currentIdx >= 0) {
        const auto& node = nodes[currentIdx];
        auto [centerX, centerZ] = navmesh.getPolygon(node.polygonIdx).getCenter();
        path.emplace_back(centerX, centerZ);
        currentIdx = node.parentIdx;
    }
    
    path.emplace_back(endX, endZ);
    return path;
}

std::vector<NavMeshPathfinder::PathPoint> NavMeshPathfinder::smoothPath(
    const std::vector<int>& polygonPath,
    const NavMesh& navmesh,
    double startX, double startZ,
    double endX, double endZ) {
    
    if (polygonPath.empty()) {
        return {{startX, startZ}, {endX, endZ}};
    }
    
    // String pulling algorithm: try to skip waypoints when we have direct line of sight
    std::vector<PathPoint> smoothedPath;
    smoothedPath.emplace_back(startX, startZ);
    
    // Collect all waypoints (polygon centers + endpoints)
    std::vector<PathPoint> waypoints;
    waypoints.emplace_back(startX, startZ);
    for (int polyIdx : polygonPath) {
        auto [centerX, centerZ] = navmesh.getPolygon(polyIdx).getCenter();
        waypoints.emplace_back(centerX, centerZ);
    }
    waypoints.emplace_back(endX, endZ);
    
    if (waypoints.size() <= 2) {
        return waypoints;
    }
    
    // Greedy string pulling: start from first waypoint, try to reach as far as possible
    size_t currentIdx = 0;
    while (currentIdx < waypoints.size() - 1) {
        // Try to find the furthest waypoint we can reach directly
        size_t furthestReachable = currentIdx + 1;
        
        for (size_t i = currentIdx + 2; i < waypoints.size(); ++i) {
            // Check if we can reach this waypoint directly (line of sight through polygons)
            if (hasLineOfSight(waypoints[currentIdx], waypoints[i], polygonPath, navmesh, currentIdx, i)) {
                furthestReachable = i;
            } else {
                // Can't reach further, stop here
                break;
            }
        }
        
        // Add the furthest reachable waypoint
        smoothedPath.push_back(waypoints[furthestReachable]);
        currentIdx = furthestReachable;
    }
    
    // Ensure endpoint is included
    if (smoothedPath.back().x != endX || smoothedPath.back().z != endZ) {
        smoothedPath.emplace_back(endX, endZ);
    }
    
    return smoothedPath;
}

bool NavMeshPathfinder::hasLineOfSight(
    const PathPoint& from, const PathPoint& to,
    const std::vector<int>& polygonPath,
    const NavMesh& navmesh,
    size_t fromPolyIdx, size_t toPolyIdx) {
    
    // Simple line-of-sight check: sample points along the line and verify they're in valid polygons
    const int samples = 8;
    double dx = (to.x - from.x) / samples;
    double dz = (to.z - from.z) / samples;
    
    // Determine which polygon indices to check (fromPolyIdx and toPolyIdx are waypoint indices)
    // Waypoint 0 is start point, waypoint 1 is first polygon center, etc.
    size_t startPoly = (fromPolyIdx > 0) ? fromPolyIdx - 1 : 0;
    size_t endPoly = std::min(toPolyIdx - 1, polygonPath.size() - 1);
    
    // Build set of valid polygon indices to check
    std::unordered_set<int> validPolygons;
    for (size_t p = startPoly; p <= endPoly && p < polygonPath.size(); ++p) {
        validPolygons.insert(polygonPath[p]);
    }
    
    // Sample points along the line
    for (int i = 1; i < samples; ++i) {
        double checkX = from.x + dx * i;
        double checkZ = from.z + dz * i;
        
        // Check if this point is in any polygon along the path
        bool inValidPolygon = false;
        for (int polyIdx : validPolygons) {
            if (navmesh.getPolygon(polyIdx).containsPoint(checkX, checkZ)) {
                inValidPolygon = true;
                break;
            }
        }
        
        if (!inValidPolygon) {
            return false;
        }
    }
    
    return true;
}

} // namespace priceriot
