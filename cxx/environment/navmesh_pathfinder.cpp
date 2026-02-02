#include "navmesh_pathfinder.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_set>

namespace priceriot {

// --- Section: A* search and path reconstruction ---
std::vector<NavMeshPathfinder::PathPoint> NavMeshPathfinder::findPath(const NavMesh &navmesh,
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
    AStarNode startNode{};
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
        const NavPolygon &currentPoly = navmesh.getPolygon(current.polygonIdx);
        const auto &neighbors = currentPoly.getNeighbors();

        for (int neighborIdx : neighbors) {
            if (closedSet.count(neighborIdx) > 0) {
                continue;
            }

            const NavPolygon &neighborPoly = navmesh.getPolygon(neighborIdx);
            double newGCost = current.gCost + movementCost(currentPoly, neighborPoly);

            // Check if we've seen this polygon with a better cost
            bool foundBetter = false;
            for (const auto &node : allNodes) {
                if (node.polygonIdx == neighborIdx && node.gCost <= newGCost) {
                    foundBetter = true;
                    break;
                }
            }

            if (!foundBetter) {
                AStarNode neighborNode{};
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

double NavMeshPathfinder::heuristic(const NavPolygon &poly, double targetX, double targetZ) {
    auto [centerX, centerZ] = poly.getCenter();
    double dx = targetX - centerX;
    double dz = targetZ - centerZ;
    return std::sqrt(dx * dx + dz * dz);
}

double NavMeshPathfinder::movementCost(const NavPolygon &from, const NavPolygon &to) {
    auto [fromX, fromZ] = from.getCenter();
    auto [toX, toZ] = to.getCenter();
    double dx = toX - fromX;
    double dz = toZ - fromZ;
    return std::sqrt(dx * dx + dz * dz);
}

std::vector<NavMeshPathfinder::PathPoint>
NavMeshPathfinder::reconstructPath(const std::vector<AStarNode> &nodes, int goalIdx,
                                   const NavMesh &navmesh, double startX, double startZ,
                                   double endX, double endZ) {

    std::vector<PathPoint> path;
    path.emplace_back(startX, startZ);

    // Add polygon centers
    int currentIdx = goalIdx;
    while (currentIdx >= 0) {
        const auto &node = nodes[currentIdx];
        auto [centerX, centerZ] = navmesh.getPolygon(node.polygonIdx).getCenter();
        path.emplace_back(centerX, centerZ);
        currentIdx = node.parentIdx;
    }

    path.emplace_back(endX, endZ);
    return path;
}

static const double kEps = 1e-5;

static bool samePoint(double x1, double z1, double x2, double z2) {
    return std::abs(x1 - x2) <= kEps && std::abs(z1 - z2) <= kEps;
}

static bool segmentMatches(double a1x, double a1z, double a2x, double a2z, double b1x, double b1z,
                           double b2x, double b2z) {
    return (samePoint(a1x, a1z, b1x, b1z) && samePoint(a2x, a2z, b2x, b2z)) ||
           (samePoint(a1x, a1z, b2x, b2z) && samePoint(a2x, a2z, b1x, b1z));
}

double NavMeshPathfinder::cross(double ox, double oz, double ax, double az, double bx, double bz) {
    return (ax - ox) * (bz - oz) - (az - oz) * (bx - ox);
}

bool NavMeshPathfinder::getPortalBetween(const NavPolygon &a, const NavPolygon &b,
                                         PathPoint &outLeft, PathPoint &outRight) {
    const auto &va = a.getVertices();
    const auto &vb = b.getVertices();
    if (va.size() < 2 || vb.size() < 2)
        return false;

    const size_t na = va.size();
    const size_t nb = vb.size();

    for (size_t i = 0; i < na; ++i) {
        size_t j = (i + 1) % na;
        double p0x = va[i].x, p0z = va[i].z;
        double p1x = va[j].x, p1z = va[j].z;

        for (size_t k = 0; k < nb; ++k) {
            size_t l = (k + 1) % nb;
            double q0x = vb[k].x, q0z = vb[k].z;
            double q1x = vb[l].x, q1z = vb[l].z;

            if (!segmentMatches(p0x, p0z, p1x, p1z, q0x, q0z, q1x, q1z))
                continue;

            auto [cxA, czA] = a.getCenter();
            auto [cxB, czB] = b.getCenter();
            double dx = cxB - cxA, dz = czB - czA;
            double perpX = -dz, perpZ = dx;
            double mx = (p0x + p1x) * 0.5, mz = (p0z + p1z) * 0.5;
            double d0 = (p0x - mx) * perpX + (p0z - mz) * perpZ;
            double d1 = (p1x - mx) * perpX + (p1z - mz) * perpZ;
            if (d0 >= d1) {
                outLeft.x = p1x;
                outLeft.z = p1z;
                outRight.x = p0x;
                outRight.z = p0z;
            } else {
                outLeft.x = p0x;
                outLeft.z = p0z;
                outRight.x = p1x;
                outRight.z = p1z;
            }
            return true;
        }
    }
    return false;
}

std::vector<NavMeshPathfinder::PathPoint>
NavMeshPathfinder::smoothPath(const std::vector<int> &polygonPath, const NavMesh &navmesh,
                              double startX, double startZ, double endX, double endZ) {

    if (polygonPath.empty()) {
        return {{startX, startZ}, {endX, endZ}};
    }

    std::vector<PathPoint> path;
    path.emplace_back(startX, startZ);

    std::vector<PathPoint> leftBoundary, rightBoundary;
    for (size_t i = 0; i + 1 < polygonPath.size(); ++i) {
        const NavPolygon &pa = navmesh.getPolygon(polygonPath[i]);
        const NavPolygon &pb = navmesh.getPolygon(polygonPath[i + 1]);
        PathPoint left, right;
        if (!getPortalBetween(pa, pb, left, right))
            continue;
        leftBoundary.push_back(left);
        rightBoundary.push_back(right);
    }

    if (leftBoundary.empty() || rightBoundary.empty()) {
        path.emplace_back(endX, endZ);
        return path;
    }

    double apexX = startX, apexZ = startZ;
    size_t leftIdx = 0, rightIdx = 0;
    const size_t numPortals = leftBoundary.size();

    for (size_t k = 0; k < numPortals; ++k) {
        const double lx = leftBoundary[k].x, lz = leftBoundary[k].z;
        const double rx = rightBoundary[k].x, rz = rightBoundary[k].z;

        while (rightIdx < k && cross(apexX, apexZ, rightBoundary[rightIdx].x,
                                     rightBoundary[rightIdx].z, rx, rz) < 0.0) {
            path.emplace_back(rightBoundary[rightIdx].x, rightBoundary[rightIdx].z);
            apexX = rightBoundary[rightIdx].x;
            apexZ = rightBoundary[rightIdx].z;
            ++rightIdx;
        }
        while (leftIdx < k && cross(apexX, apexZ, leftBoundary[leftIdx].x, leftBoundary[leftIdx].z,
                                    lx, lz) > 0.0) {
            path.emplace_back(leftBoundary[leftIdx].x, leftBoundary[leftIdx].z);
            apexX = leftBoundary[leftIdx].x;
            apexZ = leftBoundary[leftIdx].z;
            ++leftIdx;
        }
    }

    path.emplace_back(endX, endZ);
    return path;
}

} // namespace priceriot
