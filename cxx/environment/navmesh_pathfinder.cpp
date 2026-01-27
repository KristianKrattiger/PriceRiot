#include "navmesh_pathfinder.h"
#include <queue>
#include <unordered_set>
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
    
    std::vector<PathPoint> path;
    path.emplace_back(startX, startZ);
    
    // Add polygon centers (waypoints)
    for (int polyIdx : polygonPath) {
        auto [centerX, centerZ] = navmesh.getPolygon(polyIdx).getCenter();
        path.emplace_back(centerX, centerZ);
    }
    
    path.emplace_back(endX, endZ);
    
    return path;
}

} // namespace priceriot
