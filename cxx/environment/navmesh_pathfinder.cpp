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
    std::vector<PathPoint> result = smoothPath(polygonPath, navmesh, startX, startZ, endX, endZ);

    return result;
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

bool NavMeshPathfinder::getFallbackPortalBetween(const NavPolygon &a, const NavPolygon &b,
                                                 PathPoint &outLeft, PathPoint &outRight) {
    const bool aIsEdge = a.getAssociatedEdgeId() >= 0;
    const bool bIsEdge = b.getAssociatedEdgeId() >= 0;
    const bool aIsNode = a.getAssociatedNodeId() >= 0;
    const bool bIsNode = b.getAssociatedNodeId() >= 0;
    if (!((aIsEdge && bIsNode) || (aIsNode && bIsEdge)))
        return false;

    const NavPolygon &edgePoly = aIsEdge ? a : b;
    const NavPolygon &nodePoly = aIsEdge ? b : a;
    auto [nodeCx, nodeCz] = nodePoly.getCenter();
    auto [edgeCx, edgeCz] = edgePoly.getCenter();

    const auto &ve = edgePoly.getVertices();
    if (ve.size() < 2)
        return false;

    size_t bestI = 0;
    double bestDistSq = std::numeric_limits<double>::max();
    for (size_t i = 0; i < ve.size(); ++i) {
        size_t j = (i + 1) % ve.size();
        double mx = (ve[i].x + ve[j].x) * 0.5;
        double mz = (ve[i].z + ve[j].z) * 0.5;
        double dx = nodeCx - mx, dz = nodeCz - mz;
        double distSq = dx * dx + dz * dz;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestI = i;
        }
    }
    size_t j = (bestI + 1) % ve.size();
    double p0x = ve[bestI].x, p0z = ve[bestI].z;
    double p1x = ve[j].x, p1z = ve[j].z;

    double dx = edgeCx - nodeCx, dz = edgeCz - nodeCz;
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

bool NavMeshPathfinder::getGenericFallbackPortal(const NavPolygon &a, const NavPolygon &b,
                                                 PathPoint &outLeft, PathPoint &outRight) {
    auto [aCx, aCz] = a.getCenter();
    auto [bCx, bCz] = b.getCenter();
    const auto &vb = b.getVertices();
    if (vb.size() < 2)
        return false;

    size_t bestI = 0;
    double bestDistSq = std::numeric_limits<double>::max();
    for (size_t i = 0; i < vb.size(); ++i) {
        size_t j = (i + 1) % vb.size();
        double mx = (vb[i].x + vb[j].x) * 0.5;
        double mz = (vb[i].z + vb[j].z) * 0.5;
        double dx = aCx - mx, dz = aCz - mz;
        double distSq = dx * dx + dz * dz;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestI = i;
        }
    }
    size_t j = (bestI + 1) % vb.size();
    double p0x = vb[bestI].x, p0z = vb[bestI].z;
    double p1x = vb[j].x, p1z = vb[j].z;

    double dx = bCx - aCx, dz = bCz - aCz;
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

/**
 * @brief Refines a polygon-based path into a smoothed point-based path using the Funnel Algorithm.
 * * This method takes a sequence of navigation mesh polygons and calculates the shortest
 * geometric path (string-pulling) through the portals shared by those polygons.
 *
 * @param polygonPath   A vector of polygon IDs representing the raw path from a search algorithm.
 * @param navmesh       A reference to the NavMesh containing geometry and connectivity data.
 * @param startX        The X-coordinate of the starting position.
 * @param startZ        The Z-coordinate of the starting position.
 * @param endX          The X-coordinate of the target position.
 * @param endZ          The Z-coordinate of the target position.
 * * @return std::vector<PathPoint> A sequence of 2D points representing the smoothed path.
 * * @note This implementation assumes navigation occurs on the XZ plane.
 * @see Simple Stupid Funnel Algorithm (SSFA)
 */
std::vector<NavMeshPathfinder::PathPoint>
NavMeshPathfinder::smoothPath(const std::vector<int> &polygonPath, const NavMesh &navmesh,
                              double startX, double startZ, double endX, double endZ) {

    // Handle trivial case: no polygons in path
    if (polygonPath.empty()) {
        return {{startX, startZ}, {endX, endZ}};
    }

    std::vector<PathPoint> path;
    path.emplace_back(startX, startZ);

    // --- CONFIGURATION: AGENT BUFFER ---
    // Keep path away from portal edges so waypoints stay clear of shelf obstacles at junctions.
    // The navmesh is now eroded by agent radius (0.35 m) so the polygon boundary is
    // already the valid agent-centre boundary.  The PATH_BUFFER here is an additional
    // inset BEYOND that erosion — just enough to keep waypoints slightly interior to
    // the polygon rather than exactly on its edge.
    const double AGENT_RADIUS     = 0.35;
    const double JUNCTION_BUFFER  = AGENT_RADIUS * 0.6; // 0.21 m — small extra at junction corners
    const double EDGE_BUFFER      = AGENT_RADIUS * 0.4; // 0.14 m — minimal inset mid-aisle

    // Step 1: Extract portal boundaries (left and right edges) between polygons
    std::vector<PathPoint> leftBoundary, rightBoundary;
    int portalsFound = 0;
    for (size_t i = 0; i + 1 < polygonPath.size(); ++i) {
        const NavPolygon &pa = navmesh.getPolygon(polygonPath[i]);
        const NavPolygon &pb = navmesh.getPolygon(polygonPath[i + 1]);
        PathPoint left, right;

        // Find the shared edge between two adjacent polygons (exact match).
        // If that fails, use node-edge fallback, then generic fallback so we always get a portal.
        if (!getPortalBetween(pa, pb, left, right) &&
            !getFallbackPortalBetween(pa, pb, left, right) &&
            !getGenericFallbackPortal(pa, pb, left, right))
            continue;
        ++portalsFound;

        // Use a larger buffer when exactly one side of the portal is a node polygon — these
        // are the junction corners where shelf ends are closest to the walkable corridor.
        bool aIsNode = pa.getAssociatedNodeId() >= 0;
        bool bIsNode = pb.getAssociatedNodeId() >= 0;
        double PATH_BUFFER = (aIsNode != bIsNode) ? JUNCTION_BUFFER : EDGE_BUFFER;

        // We artificially narrow the portal so the "string" wraps around a buffer zone
        // rather than the physical vertex of the wall.
        double dx = right.x - left.x;
        double dz = right.z - left.z;
        double len = std::sqrt(dx * dx + dz * dz);

        // Only modify valid portals
        if (len > 1e-6) {
            // Normalize direction vector along the portal edge
            double ndx = dx / len;
            double ndz = dz / len;

            // Calculate shrink amount: keep path away from portal edges (junctions / shelf corners).
            // Clamp to 45% of portal width to avoid crossing in narrow hallways.
            double shrinkAmount = std::min(PATH_BUFFER, len * 0.45);

            // Move Left point inward (towards Right)
            left.x += ndx * shrinkAmount;
            left.z += ndz * shrinkAmount;

            // Move Right point inward (towards Left)
            right.x -= ndx * shrinkAmount;
            right.z -= ndz * shrinkAmount;
        }

        leftBoundary.push_back(left);
        rightBoundary.push_back(right);
    }

    // Handle case where no valid portals were found
    if (leftBoundary.empty() || rightBoundary.empty()) {
        path.emplace_back(endX, endZ);
        return path;
    }

    // Step 2: Initialize funnel variables
    double apexX = startX, apexZ = startZ;
    size_t leftIdx = 0, rightIdx = 0;
    const size_t numPortals = leftBoundary.size();

    // Step 3: Iterate through portals and tighten the funnel
    for (size_t k = 0; k < numPortals; ++k) {
        const double lx = leftBoundary[k].x, lz = leftBoundary[k].z;
        const double rx = rightBoundary[k].x, rz = rightBoundary[k].z;

        // Tighten the right side of the funnel
        // If the new right point crosses the left side, the old left point becomes the new apex
        while (rightIdx < k && cross(apexX, apexZ, rightBoundary[rightIdx].x,
                                     rightBoundary[rightIdx].z, rx, rz) < 0.0) {
            path.emplace_back(rightBoundary[rightIdx].x, rightBoundary[rightIdx].z);
            apexX = rightBoundary[rightIdx].x;
            apexZ = rightBoundary[rightIdx].z;
            ++rightIdx;
        }

        // Tighten the left side of the funnel
        // If the new left point crosses the right side, the old right point becomes the new apex
        while (leftIdx < k && cross(apexX, apexZ, leftBoundary[leftIdx].x, leftBoundary[leftIdx].z,
                                    lx, lz) > 0.0) {
            path.emplace_back(leftBoundary[leftIdx].x, leftBoundary[leftIdx].z);
            apexX = leftBoundary[leftIdx].x;
            apexZ = leftBoundary[leftIdx].z;
            ++leftIdx;
        }
    }

    // Finalize the path at the destination
    path.emplace_back(endX, endZ);

    return path;
}

} // namespace priceriot
