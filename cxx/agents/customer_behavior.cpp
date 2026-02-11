#include "customer_behavior.h"
#include "customer.h"
#include "../environment/checkout_queue.h"
#include "../environment/environment.h"
#include "../environment/navmesh_pathfinder.h"
#include "../environment/physics.h"
#include "../environment/store_layout.h"
#include "basket.h"
#include "shelf.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <type_traits>

namespace priceriot {

namespace {
constexpr bool kAgentLogEnabled = true;
struct NullLogStream {
    NullLogStream(const char *, std::ios_base::openmode) {}
    explicit operator bool() const { return false; }
    template <typename T> NullLogStream &operator<<(const T &) { return *this; }
};
using AgentLogStream = std::conditional_t<kAgentLogEnabled, std::ofstream, NullLogStream>;

// Helper: Snap waypoints out of obstacles
// Iterates through the path. If a waypoint is invalid (inside an obstacle),
// it backtracks along the segment from the previous waypoint until it finds a valid spot.
static void validateAndSnapPath(std::vector<std::pair<double, double>>& waypoints,
                                const priceriot::StoreGraph& store) {
    if (!store.hasPhysicsWorld() || waypoints.empty()) return;

    const auto& pw = store.getPhysicsWorld();
    const double radius = 0.35; // Standard agent radius
    const double stepSize = 0.1; // 10cm steps for backtracking

    // Start from index 1 (skip start point as agent is already there)
    for (size_t i = 1; i < waypoints.size(); ++i) {
        double wx = waypoints[i].first;
        double wz = waypoints[i].second;

        // If this waypoint is inside an obstacle
        if (!pw.isValidPosition(wx, wz, radius)) {
            // Backtrack toward previous waypoint
            double prevX = waypoints[i-1].first;
            double prevZ = waypoints[i-1].second;

            double dx = wx - prevX;
            double dz = wz - prevZ;
            double dist = std::sqrt(dx*dx + dz*dz);

            if (dist > 0.001) {
                double dirX = dx / dist;
                double dirZ = dz / dist;

                // Walk backward from current waypoint
                bool found = false;
                for (double d = 0; d < dist; d += stepSize) {
                    double testX = wx - dirX * d;
                    double testZ = wz - dirZ * d;

                    if (pw.isValidPosition(testX, testZ, radius)) {
                        waypoints[i].first = testX;
                        waypoints[i].second = testZ;
                        found = true;
                        break;
                    }
                }

                // If backtracking didn't fix it, snap to previous point
                if (!found) {
                    waypoints[i] = waypoints[i-1];
                }
            }
        }
    }
}
}

// --- Pathfinding tuning constants (waypoint follow and goal arrival) ---
static constexpr double WAYPOINT_THRESHOLD_MIN = 0.15;       // Min distance to consider waypoint reached (m)
static constexpr double WAYPOINT_THRESHOLD_FRAME_FACTOR = 1.2; // Scale for speed*dt (avoid overshoot)
static constexpr double GOAL_RADIUS = 0.3;                    // Larger radius for final waypoint (m)

// --- Helper: BFS Navigator ---
class Navigator {
  public:
    static int findNextEdge(int startNode, int targetNode, const StoreGraph &store) {
        if (startNode == targetNode)
            return -1;

        std::queue<int> frontier;
        frontier.push(startNode);
        std::map<int, int> cameFrom;
        std::map<int, int> parent;
        std::vector<bool> visited(store.numNodes(), false);

        visited[startNode] = true;
        parent[startNode] = -1;
        bool found = false;

        while (!frontier.empty()) {
            int current = frontier.front();
            frontier.pop();
            if (current == targetNode) {
                found = true;
                break;
            }

            for (int i = 0; i < store.numEdges(); ++i) {
                // store is fully defined now
                if (const auto &edge = store.edgeAt(i); edge.getFromNode() == current) {
                    if (int neighbor = edge.getToNode(); !visited[neighbor]) {
                        visited[neighbor] = true;
                        parent[neighbor] = current;
                        cameFrom[neighbor] = i;
                        frontier.push(neighbor);
                    }
                }
            }
        }

        if (found) {
            int curr = targetNode;
            while (parent[curr] != startNode) {
                curr = parent[curr];
            }
            return cameFrom[curr];
        }
        return -1;
    }

    // Now Node::NodeType is visible
    static int findNextEdgeToType(int startNode, Node::NodeType targetType,
                                  const StoreGraph &store) {
        std::queue<int> frontier;
        frontier.push(startNode);
        std::map<int, int> cameFrom;
        std::map<int, int> parent;
        std::vector<bool> visited(store.numNodes(), false);
        visited[startNode] = true;
        parent[startNode] = -1;

        int foundNode = -1;

        while (!frontier.empty()) {
            int current = frontier.front();
            frontier.pop();

            if (store.nodeAt(current).getNodeType() == targetType) {
                foundNode = current;
                break;
            }

            for (int i = 0; i < store.numEdges(); ++i) {
                const auto &edge = store.edgeAt(i);
                if (edge.getFromNode() == current) {
                    int neighbor = edge.getToNode();
                    if (!visited[neighbor]) {
                        visited[neighbor] = true;
                        parent[neighbor] = current;
                        cameFrom[neighbor] = i;
                        frontier.push(neighbor);
                    }
                }
            }
        }

        if (foundNode != -1) {
            if (parent[foundNode] == -1)
                return -1;
            int curr = foundNode;
            while (parent[curr] != startNode) {
                curr = parent[curr];
            }
            return cameFrom[curr];
        }
        return -1;
    }
};

// --- Section: DefaultBehavior (browsing) implementation ---

DefaultBehavior::DefaultBehavior() : state(Entering) {}

const char *DefaultBehavior::getStateName() const {
    switch (state) {
        case Entering: return "Entering";
        case Browsing: return "Browsing";
        case HeadingToCheckout: return "HeadingToCheckout";
        case InQueue: return "InQueue";
        case HeadingToExit: return "HeadingToExit";
        case Exiting: return "Exiting";
        case Done: return "Done";
        default: return "Unknown";
    }
}

void DefaultBehavior::onEnterStore(Customer &c, const ICustomerBehaviorContext &ctx) const {
    c.setSpawning(true);
    c.setDwellTicks(0);
}

static std::vector<int> getSkusInCell(const priceriot::EdgeCell &cell) {
    std::vector<int> skus;
    auto scan = [&](const priceriot::ShelfSide &s) {
        for (int b = 0; b < s.bay_count; ++b)
            for (int f = 0; f < s.bays[b].face_count; ++f)
                for (int sl = 0; sl < s.bays[b].faces[f].slot_count; ++sl)
                    if (s.bays[b].faces[f].slots[sl].qty_on_face > 0)
                        skus.push_back(static_cast<int>(s.bays[b].faces[f].slots[sl].sku_id));
    };
    scan(cell.get_left());
    scan(cell.get_right());
    return skus;
}

/** Returns true if the given shelf side has any inventory. */
static bool sideHasInventory(const priceriot::ShelfSide &side) {
    for (int b = 0; b < side.bay_count; ++b)
        for (int f = 0; f < side.bays[b].face_count; ++f)
            for (int sl = 0; sl < side.bays[b].faces[f].slot_count; ++sl)
                if (side.bays[b].faces[f].slots[sl].qty_on_face > 0)
                    return true;
    return false;
}

/** Determine which side to interact with based on inventory availability. */
static bool chooseInteractionSide(const priceriot::EdgeCell &cell) {
    bool leftHas = sideHasInventory(cell.get_left());
    bool rightHas = sideHasInventory(cell.get_right());
    if (leftHas && !rightHas) return true;  // Left side
    if (rightHas && !leftHas) return false; // Right side
    // Both or neither: pick randomly
    static std::mt19937 sideRng(std::random_device{}());
    return sideRng() % 2 == 0;
}

Decision DefaultBehavior::decide(Customer &c, const ICustomerBehaviorContext &ctx) {
    // #region agent log
    { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"A\",\"location\":\"customer_behavior.cpp:decide_entry\",\"message\":\"decide() called\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\",\"edgeIdx\":" << c.currentEdgeIndex << ",\"usingNavmesh\":" << (c.isUsingNavmesh()?"true":"false") << ",\"pathSize\":" << c.getNavmeshPath().size() << ",\"waypointIdx\":" << c.getCurrentWaypointIndex() << ",\"dwellTicks\":" << c.getDwellTicks() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
    // #endregion
    if (c.getDwellTicks() > 0)
        return {Decision::Wait};
    if (c.currentEdgeIndex == -1) {
        // #region agent log
        { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"D\",\"location\":\"customer_behavior.cpp:161\",\"message\":\"DESPAWN: edgeIndex==-1\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\"},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
        // #endregion
        return {Decision::Despawn};
    }

    int exitNodeIdx = -1;
    auto getExitNodeIdx = [&]() -> int {
        if (exitNodeIdx >= 0)
            return exitNodeIdx;
        for (int i = 0; i < ctx.store.numNodes(); ++i) {
            if (ctx.store.nodeAt(i).getNodeType() == Node::NodeType::Exit) {
                exitNodeIdx = i;
                break;
            }
        }
        return exitNodeIdx;
    };
    auto isNearExit = [&]() -> bool {
        const int idx = getExitNodeIdx();
        if (idx < 0)
            return false;
        const auto &exitNode = ctx.store.nodeAt(idx);
        const double dx = c.posX - exitNode.getX();
        const double dz = c.posZ - exitNode.getZ();
        return (dx * dx + dz * dz) <= (GOAL_RADIUS * GOAL_RADIUS);
    };

    if (state == HeadingToExit && isNearExit()) {
        if (c.isUsingNavmesh()) {
            c.setNavmeshPath({});
            c.setUsingNavmesh(false);
        }
        state = Exiting;
        return {Decision::Despawn};
    }
    if (state == Exiting && isNearExit())
        return {Decision::Despawn};

    // InQueue walking: move toward queue waypoint position
    if (state == InQueue && c.isWalkingToQueuePos()) {
        double dx = c.getQueueTargetX() - c.posX;
        double dz = c.getQueueTargetZ() - c.posZ;
        double dist = std::sqrt(dx * dx + dz * dz);

        if (dist < 0.3) { // Arrived at waypoint
            c.setPosition(c.getQueueTargetX(), c.getQueueTargetZ()); // Snap final
            c.setWalkingToQueuePos(false);
            // If at front of queue, start service timer
            if (ctx.queueManager && ctx.queueManager->isAtFront(c.getQueueLaneId(), c.getId())) {
                const auto &regNode = ctx.store.nodeAt(ctx.store.edgeAt(c.currentEdgeIndex).getToNode());
                double rate = regNode.getServiceRate();
                int serviceTicks = (rate > 0.01) ? static_cast<int>((1.0 / rate) * 60.0) : 300;
                c.setDwellTicks(serviceTicks);
            } else {
                c.setDwellTicks(30); // Wait for queue to advance
            }
            return {Decision::Wait};
        }

        // Walk toward target
        double walkSpeed = c.speed * ctx.dt;
        double dirX = dx / dist;
        double dirZ = dz / dist;
        c.setPosition(c.posX + dirX * walkSpeed, c.posZ + dirZ * walkSpeed);
        return {Decision::Move};
    }

    // Check if navmesh is available and use hybrid navigation
    bool useNavmesh = ctx.store.hasNavMesh();

    // If using navmesh and have a path, follow it
    if (useNavmesh && c.isUsingNavmesh() && !c.getNavmeshPath().empty() &&
        c.getCurrentWaypointIndex() < c.getNavmeshPath().size()) {
        // Get current waypoint
        const auto &waypoint = c.getNavmeshPath()[c.getCurrentWaypointIndex()];
        double dx = waypoint.first - c.posX;
        double dz = waypoint.second - c.posZ;
        double dist = std::sqrt(dx * dx + dz * dz);

        size_t pathSize = c.getNavmeshPath().size();
        bool isLastWaypoint =
            (c.getCurrentWaypointIndex() == pathSize - 1);
        double reachRadius = isLastWaypoint
                                 ? GOAL_RADIUS
                                 : std::max(WAYPOINT_THRESHOLD_MIN,
                                            c.speed * ctx.dt * WAYPOINT_THRESHOLD_FRAME_FACTOR);

        // If reached waypoint, move to next
        if (dist < reachRadius) {
            c.incrementWaypointIndex();
            if (c.getCurrentWaypointIndex() >= c.getNavmeshPath().size()) {
                // #region agent log
                { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"A\",\"location\":\"customer_behavior.cpp:186\",\"message\":\"Navmesh path complete\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\",\"edgeIdx\":" << c.currentEdgeIndex << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                // #endregion
                // Reached end of path, clear it
                c.setNavmeshPath({});
                c.setUsingNavmesh(false);
                if (state == HeadingToCheckout) {
                    state = InQueue;
                    const auto &regNode =
                        ctx.store.nodeAt(ctx.store.edgeAt(c.currentEdgeIndex).getToNode());
                    double rate = regNode.getServiceRate();
                    int serviceTicks = (rate > 0.01) ? static_cast<int>((1.0 / rate) * 60.0) : 300;
                    c.setDwellTicks(serviceTicks);
                    return {Decision::Wait};
                }
                if (state == HeadingToExit) {
                    state = Exiting;
                    return {Decision::Despawn};
                }
                if (state == Exiting)
                    return {Decision::Despawn};
                // #region agent log
                { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"C\",\"location\":\"customer_behavior.cpp:path_complete_fallthrough\",\"message\":\"Path complete but state not handled\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\",\"edgeIdx\":" << c.currentEdgeIndex << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                // #endregion
                // FIX: When navmesh path completes in Browsing state, find the closest graph node
                // and update currentEdgeIndex to an edge starting from that node
                // IMPORTANT: For Browsing state, avoid routing toward Register/Exit - stay in shopping area
                if (state == Browsing || state == Entering) {
                    double px = c.posX, pz = c.posZ;

                    // For Browsing customers, find the best edge that keeps them in the shopping area
                    // (avoid edges leading to Register or Exit)
                    double bestDistSq = 1e99;
                    int bestEdgeIdx = -1;
                    int bestFromNode = -1;

                    for (int ei = 0; ei < ctx.store.numEdges(); ++ei) {
                        const auto &edge = ctx.store.edgeAt(ei);
                        int toNodeIdx = edge.getToNode();
                        Node::NodeType toType = ctx.store.nodeAt(toNodeIdx).getNodeType();

                        // Skip edges leading to Register or Exit when Browsing
                        if (state == Browsing &&
                            (toType == Node::NodeType::Register || toType == Node::NodeType::Exit)) {
                            continue;
                        }

                        int fromNodeIdx = edge.getFromNode();
                        const auto &fromNode = ctx.store.nodeAt(fromNodeIdx);
                        double dx = fromNode.getX() - px, dz = fromNode.getZ() - pz;
                        double d2 = dx * dx + dz * dz;
                        if (d2 < bestDistSq) {
                            bestDistSq = d2;
                            bestFromNode = fromNodeIdx;
                            bestEdgeIdx = ei;
                        }
                    }

                    // Fallback: if no suitable edge found (all lead to Register/Exit), just pick any
                    if (bestEdgeIdx < 0) {
                        for (int ei = 0; ei < ctx.store.numEdges(); ++ei) {
                            int fromNodeIdx = ctx.store.edgeAt(ei).getFromNode();
                            const auto &fromNode = ctx.store.nodeAt(fromNodeIdx);
                            double dx = fromNode.getX() - px, dz = fromNode.getZ() - pz;
                            double d2 = dx * dx + dz * dz;
                            if (d2 < bestDistSq) {
                                bestDistSq = d2;
                                bestFromNode = fromNodeIdx;
                                bestEdgeIdx = ei;
                            }
                        }
                    }

                    if (bestEdgeIdx >= 0) {
                        c.currentEdgeIndex = bestEdgeIdx;
                        // FIX: Set distOnEdge to 0 to allow browsing through the edge cells
                        // Previously set to edgeLength which skipped all cell interactions
                        c.distOnEdge = 0.0;
                        // Sync position to edge start node for consistent cell positioning
                        const auto &fromNode = ctx.store.nodeAt(ctx.store.edgeAt(bestEdgeIdx).getFromNode());
                        c.setPosition(fromNode.getX(), fromNode.getZ());
                        // Also reset shop cell tracking for the new edge
                        c.setLastShopCell(-1);
                        c.setLastPickAttemptCell(-1);
                        // #region agent log
                        { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"FIX\",\"location\":\"customer_behavior.cpp:path_complete_fix\",\"message\":\"Updated edge after navmesh path complete (distOnEdge=0 for browsing)\",\"data\":{\"customerId\":" << c.getId() << ",\"posX\":" << px << ",\"posZ\":" << pz << ",\"bestFromNode\":" << bestFromNode << ",\"newEdgeIdx\":" << bestEdgeIdx << ",\"state\":\"" << getStateName() << "\"},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                        // #endregion
                    }

                    // Continue with Move to let edge-based navigation take over
                    return {Decision::Move};
                }
            } else {
                // Continue to next waypoint
                return {Decision::Move};
            }
        } else {
            // Move toward waypoint with velocity-based movement
            double moveDist = c.speed * ctx.dt;

            // Normalize direction
            double dirX = (dist > 1e-6) ? dx / dist : 0.0;
            double dirZ = (dist > 1e-6) ? dz / dist : 0.0;

            // Don't overshoot waypoint
            if (moveDist > dist) {
                moveDist = dist;
            }

            double moveX = dirX * moveDist;
            double moveZ = dirZ * moveDist;

            // Check if new position would be valid (obstacle check)
            double newX = c.posX + moveX;
            double newZ = c.posZ + moveZ;
            double agentRadius = 0.35;

            if (ctx.store.hasPhysicsWorld()) {
                const PhysicsWorld &physics = ctx.store.getPhysicsWorld();

                // Check if position is valid
                if (!physics.isValidPosition(newX, newZ, agentRadius)) {
                    // Try reduced movement (half speed)
                    newX = c.posX + moveX * 0.5;
                    newZ = c.posZ + moveZ * 0.5;
                    if (!physics.isValidPosition(newX, newZ, agentRadius)) {
                        // Try perpendicular movement (slide along obstacle)
                        double perpX = -dirZ;
                        double perpZ = dirX;
                        newX = c.posX + perpX * moveDist * 0.3;
                        newZ = c.posZ + perpZ * moveDist * 0.3;
                        if (!physics.isValidPosition(newX, newZ, agentRadius)) {
                            // Try opposite perpendicular
                            newX = c.posX - perpX * moveDist * 0.3;
                            newZ = c.posZ - perpZ * moveDist * 0.3;
                            if (!physics.isValidPosition(newX, newZ, agentRadius)) {
                                // Still blocked, try to push away from obstacle
                                Circle agentCircle(c.posX, c.posZ, agentRadius);
                                double normalX, normalZ, penetration;
                                if (physics.getCollisionInfo(agentCircle, normalX, normalZ,
                                                             penetration)) {
                                    // Push away from obstacle
                                    newX = c.posX + normalX * moveDist * 0.5;
                                    newZ = c.posZ + normalZ * moveDist * 0.5;
                                    if (!physics.isValidPosition(newX, newZ, agentRadius)) {
                                        // Completely blocked; fall back to edge-based navigation.
                                        // #region debug log
                                        { std::ofstream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if (lf) lf << "{\"hypothesisId\":\"H5\",\"location\":\"customer_behavior.cpp:navmesh_blocked_fallback\",\"message\":\"Completely blocked, clear path\",\"data\":{\"customerId\":" << c.getId() << ",\"posX\":" << c.posX << ",\"posZ\":" << c.posZ << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                                        // #endregion
                                        c.setNavmeshPath({});
                                        c.setUsingNavmesh(false);
                                        return {Decision::Move};
                                    }
                                } else {
                                    // No collision info; fall back to edge-based navigation.
                                    // #region debug log
                                    { std::ofstream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if (lf) lf << "{\"hypothesisId\":\"H5\",\"location\":\"customer_behavior.cpp:navmesh_nocollinfo_fallback\",\"message\":\"No collision info, clear path\",\"data\":{\"customerId\":" << c.getId() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                                    // #endregion
                                    c.setNavmeshPath({});
                                    c.setUsingNavmesh(false);
                                    return {Decision::Move};
                                }
                            }
                        }
                    }
                }
            }

            // #region debug log
            {
                bool inObst = ctx.store.hasPhysicsWorld() && !ctx.store.getPhysicsWorld().isValidPosition(newX, newZ, agentRadius);
                std::ofstream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app);
                if (lf) lf << "{\"hypothesisId\":\"H4\",\"location\":\"customer_behavior.cpp:navmesh_move\",\"message\":\"Navmesh step toward waypoint\",\"data\":{\"customerId\":" << c.getId() << ",\"posBeforeX\":" << c.posX << ",\"posBeforeZ\":" << c.posZ << ",\"waypointX\":" << waypoint.first << ",\"waypointZ\":" << waypoint.second << ",\"newPosX\":" << newX << ",\"newPosZ\":" << newZ << ",\"inObstacleAfter\":" << (inObst ? "true" : "false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
            }
            // #endregion

            // Update position using setter
            c.setPosition(newX, newZ);

            // Update distOnEdge for backward compatibility (approximate)
            if (c.currentEdgeIndex >= 0) {
                const auto &edge = ctx.store.edgeAt(c.currentEdgeIndex);
                // Approximate distance along edge based on position
                c.distOnEdge = std::min(c.distOnEdge + moveDist, edge.getLength());
            }

            return {Decision::Move};
        }
    }

    const auto &currentEdge = ctx.store.edgeAt(c.currentEdgeIndex);
    const int currentNode = currentEdge.getToNode();

    // --- STATE MACHINE: SHOPPING ---
    // Only allow picking when Browsing; never pick on entrance corridor (from-node is Entrance).
    const bool fromEntrance =
        ctx.store.nodeAt(currentEdge.getFromNode()).getNodeType() == Node::NodeType::Entrance;
    // #region agent log
    { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"C\",\"location\":\"customer_behavior.cpp:browsing_check\",\"message\":\"Browsing pick logic entry\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\",\"fromEntrance\":" << (fromEntrance?"true":"false") << ",\"distOnEdge\":" << c.distOnEdge << ",\"edgeLength\":" << currentEdge.getLength() << ",\"cellCount\":" << currentEdge.getCellCount() << ",\"basketSize\":" << ctx.basket.getSize() << ",\"usingNavmesh\":" << (c.isUsingNavmesh()?"true":"false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
    // #endregion
    if (state == Browsing && !fromEntrance) {
        // Are we on an aisle with shelves?
        if (currentEdge.getCellCount() > 0 && ctx.basket.getSize() < 8) {
            if (const int cellIdx = static_cast<int>(c.distOnEdge / currentEdge.getCellLength());
                cellIdx >= 0 && cellIdx < static_cast<int>(currentEdge.cells.size())) {
                // #region agent log
                { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"D\",\"location\":\"customer_behavior.cpp:cell_calc\",\"message\":\"Cell index calculated\",\"data\":{\"customerId\":" << c.getId() << ",\"cellIdx\":" << cellIdx << ",\"cellCount\":" << currentEdge.getCellCount() << ",\"distOnEdge\":" << c.distOnEdge << ",\"cellLength\":" << currentEdge.getCellLength() << ",\"lastShopCell\":" << c.getLastShopCell() << ",\"lastPickAttemptCell\":" << c.getLastPickAttemptCell() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                // #endregion
                // New cell - dwell to simulate browsing
                if (c.getLastShopCell() != cellIdx) {
                    c.setLastShopCell(cellIdx);
                    // Check if cell has inventory - if so, dwell first
                    if (const auto skus = getSkusInCell(currentEdge.cells[cellIdx]);
                        !skus.empty()) {
                        // Set sideband interaction state
                        c.setTargetCellIdx(cellIdx);
                        c.setInteractingLeftSide(chooseInteractionSide(currentEdge.cells[cellIdx]));
                        // Randomized dwell: 2-5 seconds at 60 ticks/sec = 120-300 ticks
                        static std::mt19937 dwellRng(std::random_device{}());
                        int dwellTicks = 120 + static_cast<int>(dwellRng() % 181);
                        c.setDwellTicks(dwellTicks);
                        // #region agent log
                        { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"D\",\"location\":\"customer_behavior.cpp:dwell_start\",\"message\":\"Starting dwell at new cell\",\"data\":{\"customerId\":" << c.getId() << ",\"cellIdx\":" << cellIdx << ",\"skuCount\":" << skus.size() << ",\"dwellTicks\":" << dwellTicks << ",\"leftSide\":" << (c.isInteractingLeftSide()?"true":"false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                        // #endregion
                        return {Decision::Wait};
                    }
                }
                // Same cell, dwell complete - attempt pick once per cell
                else if (c.getLastPickAttemptCell() != cellIdx) {
                    c.setLastPickAttemptCell(cellIdx);
                    static std::mt19937 rng(std::random_device{}());
                    int pickChance = 80;
                    if (c.getImpulsivity() > 0.4)
                        pickChance = 90;
                    if (rng() % 100 < pickChance) {
                        if (const auto skus = getSkusInCell(currentEdge.cells[cellIdx]);
                            !skus.empty()) {
                            int chosenSku = skus[rng() % skus.size()];
                            if (c.getPriceSensitivity() > 0.4) {
                                std::vector<std::pair<int, double>> skuPrices;
                                for (int sku : skus) {
                                    double price = ctx.store.catalog.productExists(sku)
                                                       ? ctx.store.catalog.getProductPrice(sku)
                                                       : 1e9;
                                    skuPrices.emplace_back(sku, price);
                                }
                                std::sort(skuPrices.begin(), skuPrices.end(),
                                          [](const auto &a, const auto &b) { return a.second < b.second; });
                                size_t n = skuPrices.size();
                                std::vector<double> weights(n);
                                for (size_t i = 0; i < n; ++i)
                                    weights[i] = static_cast<double>(n - i);
                                std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
                                chosenSku = skuPrices[dist(rng)].first;
                            }
                            // #region agent log
                            {
                                // Calculate expected cell center position for comparison
                                double cellLen = currentEdge.getCellLength();
                                double cellCenterDist = (cellIdx + 0.5) * cellLen;
                                double frac = cellCenterDist / currentEdge.getLength();
                                int fromNodeIdx = currentEdge.getFromNode();
                                int toNodeIdx = currentEdge.getToNode();
                                double fromX = ctx.store.nodeAt(fromNodeIdx).getX();
                                double fromZ = ctx.store.nodeAt(fromNodeIdx).getZ();
                                double toX = ctx.store.nodeAt(toNodeIdx).getX();
                                double toZ = ctx.store.nodeAt(toNodeIdx).getZ();
                                double expectedX = fromX + (toX - fromX) * frac;
                                double expectedZ = fromZ + (toZ - fromZ) * frac;
                                double actualX = c.getPosX();
                                double actualZ = c.getPosZ();
                                double distFromExpected = std::sqrt((actualX - expectedX) * (actualX - expectedX) + (actualZ - expectedZ) * (actualZ - expectedZ));
                                AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app);
                                if(lf) lf << "{\"hypothesisId\":\"G\",\"location\":\"customer_behavior.cpp:pick_product\",\"message\":\"PICK PRODUCT DECISION\",\"data\":{\"customerId\":" << c.getId() << ",\"chosenSku\":" << chosenSku << ",\"cellIdx\":" << cellIdx << ",\"edgeIdx\":" << c.currentEdgeIndex << ",\"actualPos\":[" << actualX << "," << actualZ << "],\"expectedCellPos\":[" << expectedX << "," << expectedZ << "],\"distFromExpected\":" << distFromExpected << ",\"usingNavmesh\":" << (c.isUsingNavmesh()?"true":"false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
                            }
                            // #endregion
                            return {Decision::PickProduct, chosenSku, 1.0f};
                        }
                    }
                }
            }
        }
    }

    // --- STATE MACHINE: MOVEMENT ---
    // #region agent log
    { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app);
        if(lf) lf << "{\"hypothesisId\":\"C\",\"location\":\"customer_behavior.cpp:movement_check\",\"message\":\"Movement check\",\"data\":{\"customerId\":" << c.getId() << ",\"distOnEdge\":" << c.distOnEdge << ",\"edgeLength\":" << currentEdge.getLength() << ",\"atNodeBoundary\":" << (c.distOnEdge >= currentEdge.getLength() ? "true" : "false") << ",\"browsingSkipped\":" << ((c.distOnEdge >= currentEdge.getLength() && state == Browsing) ? "true" : "false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
    // #endregion
    if (c.distOnEdge >= currentEdge.getLength()) {
        // #region agent log
        { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app);
            if(lf) lf << "{\"hypothesisId\":\"E\",\"location\":\"customer_behavior.cpp:348\",\"message\":\"Arrived at node (edge-based)\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\",\"currentNode\":" << currentNode << ",\"distOnEdge\":" << c.distOnEdge << ",\"edgeLength\":" << currentEdge.getLength() << ",\"usingNavmesh\":" << (c.isUsingNavmesh()?"true":"false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
        // #endregion
        const auto &arrivalNode = ctx.store.nodeAt(currentNode);
        const int arrivalNodeId = arrivalNode.getNodeId();

        // 1. Transition Logic (Enter -> Browse)
        if (state == Entering) {
            state = Browsing;
            // #region agent log
            { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"A\",\"location\":\"customer_behavior.cpp:state_transition\",\"message\":\"State: Entering->Browsing\",\"data\":{\"customerId\":" << c.getId() << ",\"nodeId\":" << arrivalNodeId << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
            // #endregion
            preferredAisleNodes.clear();
            std::vector<int> junctionIds;
            for (int i = 0; i < ctx.store.numNodes(); ++i) {
                if (ctx.store.nodeAt(i).getNodeType() == Node::NodeType::Junction)
                    junctionIds.push_back(ctx.store.nodeAt(i).getNodeId());
            }
            static std::mt19937 rngPreferred(std::random_device{}());
            std::shuffle(junctionIds.begin(), junctionIds.end(), rngPreferred);
            for (size_t i = 0; i < std::min(size_t(3), junctionIds.size()); ++i)
                preferredAisleNodes.push_back(junctionIds[i]);
        }

        // 2. Browse -> Checkout Logic
        if (state == Browsing) {
            if (ctx.basket.getSize() >= 5)
                state = HeadingToCheckout;
            // Random chance to finish shopping
            static std::mt19937 rng(std::random_device{}());
            if (rng() % 100 < 10)
                state = HeadingToCheckout;
        }

        // 3. Node Interactions - Register queue handling
        if (arrivalNode.getNodeType() == Node::NodeType::Register) {
            // If basket is empty, skip checkout, go to exit.
            if (ctx.basket.getSize() > 0) {
                if (state == HeadingToCheckout && !c.isInQueue()) {
                    // Join checkout queue
                    if (ctx.queueManager) {
                        int laneId = ctx.queueManager->selectLane(
                            c.posX, c.posZ, c.getPatience(), c.getCrowdSensitivity());
                        if (laneId >= 0) {
                            ctx.queueManager->joinQueue(laneId, c.getId());
                            c.setQueueLaneId(laneId);
                            c.setInQueue(true);
                            // Set queue walk target
                            auto wp = ctx.queueManager->getQueueWaypoint(laneId, c.getId());
                            c.setQueueTarget(wp.x, wp.z);
                            c.setWalkingToQueuePos(true);
                            // Register callback for queue position updates and walk-to-target
                            CheckoutQueueManager* qmPtr = ctx.queueManager;
                            int custId = c.getId();
                            ctx.queueManager->registerAdvanceCallback(c.getId(),
                                [&c, qmPtr, laneId, custId](int, int newPos) {
                                    c.setQueuePosition(newPos);
                                    // Update walk target when queue advances
                                    if (qmPtr) {
                                        auto newWp = qmPtr->getQueueWaypoint(laneId, custId);
                                        c.setQueueTarget(newWp.x, newWp.z);
                                        c.setWalkingToQueuePos(true);
                                    }
                                });
                        }
                    }
                    state = InQueue;
                    // Start walking to queue position
                    return {Decision::Move};
                }
                if (state == InQueue) {
                    // Check if at front of queue
                    bool atFront = true;
                    if (ctx.queueManager && c.isInQueue()) {
                        atFront = ctx.queueManager->isAtFront(c.getQueueLaneId(), c.getId());
                    }
                    if (atFront) {
                        // Leave queue and checkout
                        if (ctx.queueManager && c.isInQueue()) {
                            ctx.queueManager->advanceQueue(c.getQueueLaneId());
                            c.setInQueue(false);
                            c.setQueueLaneId(-1);
                        }
                        state = HeadingToExit;
                        return {Decision::Checkout, arrivalNodeId, 5.0f};
                    } else {
                        // Still waiting in queue
                        c.setDwellTicks(30); // Keep waiting
                        return {Decision::Wait};
                    }
                }
            } else {
                state = HeadingToExit; // Just leave if empty
            }
        }

        if (arrivalNode.getNodeType() == Node::NodeType::Exit) {
            if (c.isUsingNavmesh()) {
                c.setNavmeshPath({});
                c.setUsingNavmesh(false);
            }
            // #region agent log
            { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"A\",\"location\":\"customer_behavior.cpp:exit_arrival\",\"message\":\"Agent arrived at Exit node\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << ",\"distOnEdge\":" << c.distOnEdge << ",\"edgeLength\":" << currentEdge.getLength() << "\",\"expectedStates\":\"HeadingToExit or Exiting\",\"willDespawn\":" << (state == Exiting ? "true" : "false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
            // #endregion
            state = Exiting;
            return {Decision::Despawn};
        }

        // 4. Navigation (Find next edge or waypoint)
        int nextEdge = -1;
        int targetNodeId = -1;

        if (state == HeadingToCheckout) {
            // Find waypoint node (Register)
            for (int i = 0; i < ctx.store.numNodes(); ++i) {
                if (ctx.store.nodeAt(i).getNodeType() == Node::NodeType::Register) {
                    targetNodeId = ctx.store.nodeAt(i).getNodeId();
                    break;
                }
            }
            nextEdge =
                Navigator::findNextEdgeToType(currentNode, Node::NodeType::Register, ctx.store);
        } else if (state == HeadingToExit) {
            // Find waypoint node (Exit)
            for (int i = 0; i < ctx.store.numNodes(); ++i) {
                if (ctx.store.nodeAt(i).getNodeType() == Node::NodeType::Exit) {
                    targetNodeId = ctx.store.nodeAt(i).getNodeId();
                    break;
                }
            }
            nextEdge = Navigator::findNextEdgeToType(currentNode, Node::NodeType::Exit, ctx.store);
        } else if (state == Browsing) {
            std::vector<int> preferred, other;
            for (int i = 0; i < ctx.store.numEdges(); ++i) {
                if (ctx.store.edgeAt(i).getFromNode() != currentNode)
                    continue;
                int toNodeIdx = ctx.store.edgeAt(i).getToNode();
                Node::NodeType toType = ctx.store.nodeAt(toNodeIdx).getNodeType();
                // BUG FIX: Skip edges leading to Register or Exit when Browsing
                if (toType == Node::NodeType::Register || toType == Node::NodeType::Exit) {
                    // #region agent log
                    { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"A\",\"location\":\"customer_behavior.cpp:browsing_skip_exit\",\"message\":\"Skipping edge to Exit/Register in Browsing\",\"data\":{\"customerId\":" << c.getId() << ",\"edgeIdx\":" << i << ",\"toNodeType\":\"" << (toType == Node::NodeType::Exit ? "Exit" : "Register") << "\"},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                    // #endregion
                    continue;
                }
                int toId = ctx.store.nodeAt(toNodeIdx).getNodeId();
                if (std::find(preferredAisleNodes.begin(), preferredAisleNodes.end(), toId) !=
                    preferredAisleNodes.end())
                    preferred.push_back(i);
                else
                    other.push_back(i);
            }
            static std::mt19937 rng(std::random_device{}());
            std::vector<int> *choice =
                !preferred.empty() && (other.empty() || rng() % 100 < 70) ? &preferred : &other;
            if (!choice->empty()) {
                nextEdge = (*choice)[static_cast<size_t>(rng()) % choice->size()];
                targetNodeId = ctx.store.nodeAt(ctx.store.edgeAt(nextEdge).getToNode()).getNodeId();
            }
            // #region agent log
            if (preferred.empty() && other.empty()) {
                { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"BROWSE_NAV\",\"location\":\"customer_behavior.cpp:browsing_nav\",\"message\":\"No edges found in Browsing nav\",\"data\":{\"customerId\":" << c.getId() << ",\"currentNode\":" << currentNode << ",\"numEdges\":" << ctx.store.numEdges() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
            }
            // #endregion
        }

        // Failsafe: If stuck or no path found, just pick any outgoing edge
        if (nextEdge == -1) {
            // #region agent log
            { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"B\",\"location\":\"customer_behavior.cpp:failsafe\",\"message\":\"No edge found, trying failsafe\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\",\"currentNode\":" << currentNode << ",\"numEdges\":" << ctx.store.numEdges() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
            // #endregion
            // Build a list of all edges for debugging (idx:id:from->to)
            std::string edgeList;
            for (int i = 0; i < ctx.store.numEdges(); ++i) {
                edgeList += std::to_string(i) + "(id" + std::to_string(ctx.store.edgeAt(i).getEdgeId()) + "):" +
                           std::to_string(ctx.store.edgeAt(i).getFromNode()) + "->" +
                           std::to_string(ctx.store.edgeAt(i).getToNode()) + " ";
                if (ctx.store.edgeAt(i).getFromNode() == currentNode) {
                    nextEdge = i;
                    targetNodeId = ctx.store.nodeAt(ctx.store.edgeAt(i).getToNode()).getNodeId();
                    // #region agent log
                    { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"B\",\"location\":\"customer_behavior.cpp:failsafe_found\",\"message\":\"Failsafe found edge\",\"data\":{\"customerId\":" << c.getId() << ",\"edgeIdx\":" << i << ",\"edgeId\":" << ctx.store.edgeAt(i).getEdgeId() << ",\"fromNode\":" << ctx.store.edgeAt(i).getFromNode() << ",\"toNode\":" << ctx.store.edgeAt(i).getToNode() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                    // #endregion
                    break;
                }
            }
            if (nextEdge == -1) {
                // #region agent log
                { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"B\",\"location\":\"customer_behavior.cpp:failsafe_despawn\",\"message\":\"DESPAWN: no outgoing edge\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\",\"currentNode\":" << currentNode << ",\"currentNodeId\":" << ctx.store.nodeAt(currentNode).getNodeId() << ",\"edgeList\":\"" << edgeList << "\"},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                // #endregion
                return {Decision::Despawn};
            }
        }

        // If navmesh is available, use hybrid navigation
        bool useNavmesh = ctx.store.hasNavMesh();
        if (useNavmesh && targetNodeId >= 0) {
            // Get current position (use node center if world pos not set)
            double startX = c.posX;
            double startZ = c.posZ;
            if (startX == 0.0 && startZ == 0.0) {
                // Initialize from current node center
                const auto &currentNodeObj = ctx.store.nodeAt(currentNode);
                startX = currentNodeObj.getX();
                startZ = currentNodeObj.getZ();
                c.setPosition(startX, startZ);
            }

            // Get target position (node center)
            const auto &targetNode = ctx.store.nodeAt(ctx.store.nodeIndexById(targetNodeId));
            double endX = targetNode.getX();
            double endZ = targetNode.getZ();

            // Find navmesh path
            auto path =
                NavMeshPathfinder::findPath(ctx.store.getNavMesh(), startX, startZ, endX, endZ);

            if (!path.empty()) {
                // Convert path to waypoints
                std::vector<std::pair<double, double>> waypoints;
                for (const auto &point : path) {
                    waypoints.emplace_back(point.x, point.z);
                }

                // --- VALIDATE AND SNAP PATH ---
                validateAndSnapPath(waypoints, ctx.store);
                // ------------------------------

                c.setNavmeshPath(waypoints);
                c.setUsingNavmesh(true);

                // #region debug log
                if (ctx.store.hasPhysicsWorld()) {
                    const PhysicsWorld &pw = ctx.store.getPhysicsWorld();
                    int inObst = 0;
                    for (const auto &w : waypoints)
                        if (!pw.isValidPosition(w.first, w.second, 0.35)) ++inObst;
                    double fx = waypoints.empty() ? 0 : waypoints.front().first, fz = waypoints.empty() ? 0 : waypoints.front().second;
                    double lx = waypoints.empty() ? 0 : waypoints.back().first, lz = waypoints.empty() ? 0 : waypoints.back().second;
                    { std::ofstream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if (lf) lf << "{\"hypothesisId\":\"H1\",\"location\":\"customer_behavior.cpp:navmesh_path_set\",\"message\":\"Path waypoints obstacle check\",\"data\":{\"customerId\":" << c.getId() << ",\"pathSize\":" << waypoints.size() << ",\"waypointsInObstacle\":" << inObst << ",\"firstWpX\":" << fx << ",\"firstWpZ\":" << fz << ",\"lastWpX\":" << lx << ",\"lastWpZ\":" << lz << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                }
                // #endregion

                // Update current edge index for compatibility
                c.currentEdgeIndex = nextEdge;
                c.distOnEdge = 0.0;

                // #region agent log
                { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"A\",\"location\":\"customer_behavior.cpp:navmesh_set\",\"message\":\"Navmesh path set\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\",\"pathSize\":" << waypoints.size() << ",\"nextEdge\":" << nextEdge << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                // #endregion
                return {Decision::Move};
            }
        }

        // Fall back to edge-based navigation
        // #region agent log
        { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"A\",\"location\":\"customer_behavior.cpp:SwitchEdge\",\"message\":\"SwitchEdge decision\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\",\"nextEdge\":" << nextEdge << ",\"targetNodeId\":" << targetNodeId << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
        // #endregion
        return {Decision::SwitchEdge, nextEdge};
    }

    // #region agent log
    if (state == Exiting) {
        { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"H\",\"location\":\"customer_behavior.cpp:exiting_fallthrough\",\"message\":\"STUCK: Exiting state fell through to Move\",\"data\":{\"customerId\":" << c.getId() << ",\"distOnEdge\":" << c.distOnEdge << ",\"edgeLength\":" << currentEdge.getLength() << ",\"edgeIdx\":" << c.currentEdgeIndex << ",\"posX\":" << c.getPosX() << ",\"posZ\":" << c.getPosZ() << ",\"usingNavmesh\":" << (c.isUsingNavmesh()?"true":"false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
    }
    if (state == HeadingToExit) {
        { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"H\",\"location\":\"customer_behavior.cpp:headingtoexit_fallthrough\",\"message\":\"HeadingToExit state fell through to Move\",\"data\":{\"customerId\":" << c.getId() << ",\"distOnEdge\":" << c.distOnEdge << ",\"edgeLength\":" << currentEdge.getLength() << ",\"edgeIdx\":" << c.currentEdgeIndex << ",\"posX\":" << c.getPosX() << ",\"posZ\":" << c.getPosZ() << ",\"usingNavmesh\":" << (c.isUsingNavmesh()?"true":"false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
    }
    if (state == HeadingToExit || state == Exiting) {
        const int exitIdx = getExitNodeIdx();
        if (exitIdx >= 0) {
            const auto &exitNode = ctx.store.nodeAt(exitIdx);
            const double dx = c.getPosX() - exitNode.getX();
            const double dz = c.getPosZ() - exitNode.getZ();
            const double distToExit = std::sqrt(dx * dx + dz * dz);
            { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"EXIT_POS\",\"location\":\"customer_behavior.cpp:exit_distance\",\"message\":\"Exit distance check\",\"data\":{\"customerId\":" << c.getId() << ",\"state\":\"" << getStateName() << "\",\"distToExit\":" << distToExit << ",\"exitPos\":[" << exitNode.getX() << "," << exitNode.getZ() << "],\"pos\":[" << c.getPosX() << "," << c.getPosZ() << "],\"distOnEdge\":" << c.distOnEdge << ",\"edgeLength\":" << currentEdge.getLength() << ",\"edgeIdx\":" << c.currentEdgeIndex << ",\"usingNavmesh\":" << (c.isUsingNavmesh()?"true":"false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
        }
    }
    // #endregion
    if (state == Exiting && isNearExit())
        return {Decision::Despawn};

    // Cell attraction: steer browsing agents toward stall positions (offset from shelf)
    if (state == Browsing && c.currentEdgeIndex >= 0 && !c.isUsingNavmesh()) {
        const auto &edge = ctx.store.edgeAt(c.currentEdgeIndex);
        if (edge.getCellCount() > 0) {
            int cellIdx = static_cast<int>(c.distOnEdge / edge.getCellLength());
            cellIdx = std::clamp(cellIdx, 0, edge.getCellCount() - 1);

            // Use stall position if interacting with a cell, otherwise use cell center
            double targetX, targetZ;
            if (c.getTargetCellIdx() == cellIdx) {
                // Steer toward stall position (offset toward shelf side)
                auto [stallX, stallZ] = ctx.store.getStallPosition(
                    c.currentEdgeIndex, cellIdx, c.isInteractingLeftSide());
                targetX = stallX;
                targetZ = stallZ;
            } else {
                // Just passing through - steer toward cell center
                auto [cellX, cellZ] = ctx.store.getCellCenter(c.currentEdgeIndex, cellIdx);
                targetX = cellX;
                targetZ = cellZ;
            }

            double dx = targetX - c.posX;
            double dz = targetZ - c.posZ;
            double dist = std::sqrt(dx * dx + dz * dz);

            if (dist > 0.1) { // Only correct if >10cm off
                double correctionRate = 0.15; // Blend factor per tick
                c.setPosition(c.posX + dx * correctionRate, c.posZ + dz * correctionRate);
            }
        }
    }

    return {Decision::Move};
}

int DefaultBehavior::getNextEdgeToNode(int currentNodeId, int targetNodeId,
                                       const StoreGraph &store) const {
    return Navigator::findNextEdge(currentNodeId, targetNodeId, store);
}
int DefaultBehavior::getNextEdgeToNodeType(int currentNodeId, int targetType,
                                           const StoreGraph &store) const {
    return Navigator::findNextEdgeToType(currentNodeId, static_cast<Node::NodeType>(targetType),
                                         store);
}

// --- Section: MissionBehavior (targeted SKU list) implementation ---

MissionBehavior::MissionBehavior() : state(Entering) {}

const char *MissionBehavior::getStateName() const {
    switch (state) {
        case Entering: return "Entering";
        case MissionBrowse: return "MissionBrowse";
        case HeadingToCheckout: return "HeadingToCheckout";
        case InQueue: return "InQueue";
        case HeadingToExit: return "HeadingToExit";
        case Exiting: return "Exiting";
        case Done: return "Done";
        default: return "Unknown";
    }
}

void MissionBehavior::onEnterStore(Customer &c, const ICustomerBehaviorContext &ctx) const {
    c.setSpawning(true);
    c.setDwellTicks(0);
}

Decision MissionBehavior::decide(Customer &c, const ICustomerBehaviorContext &ctx) {
    if (c.getDwellTicks() > 0)
        return {Decision::Wait};
    if (c.currentEdgeIndex == -1)
        return {Decision::Despawn};

    // InQueue walking: move toward queue waypoint position
    if (state == InQueue && c.isWalkingToQueuePos()) {
        double dx = c.getQueueTargetX() - c.posX;
        double dz = c.getQueueTargetZ() - c.posZ;
        double dist = std::sqrt(dx * dx + dz * dz);

        if (dist < 0.3) { // Arrived at waypoint
            c.setPosition(c.getQueueTargetX(), c.getQueueTargetZ()); // Snap final
            c.setWalkingToQueuePos(false);
            // If at front of queue, start service timer
            if (ctx.queueManager && ctx.queueManager->isAtFront(c.getQueueLaneId(), c.getId())) {
                const auto &regNode = ctx.store.nodeAt(ctx.store.edgeAt(c.currentEdgeIndex).getToNode());
                double rate = regNode.getServiceRate();
                int serviceTicks = (rate > 0.01) ? static_cast<int>((1.0 / rate) * 60.0) : 300;
                c.setDwellTicks(serviceTicks);
            } else {
                c.setDwellTicks(30); // Wait for queue to advance
            }
            return {Decision::Wait};
        }

        // Walk toward target
        double walkSpeed = c.speed * ctx.dt;
        double dirX = dx / dist;
        double dirZ = dz / dist;
        c.setPosition(c.posX + dirX * walkSpeed, c.posZ + dirZ * walkSpeed);
        return {Decision::Move};
    }

    if (state == Entering) {
        state = MissionBrowse;
        missionSkus.clear();
        missionIndex = 0;

        // FIX: Only select SKUs that are actually available on shelves
        // First, collect all SKUs that exist on shelves (have at least one location)
        std::vector<int> availableSkus;
        const auto &catalog = ctx.store.catalog.getProductsMap();
        // #region agent log
        { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"B\",\"location\":\"customer_behavior.cpp:mission_init\",\"message\":\"Mission init - catalog products\",\"data\":{\"customerId\":" << c.getId() << ",\"catalogSize\":" << catalog.size() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
        // #endregion
        for (const auto &kv : catalog) {
            int sku = kv.first;
            auto locations = ctx.store.findEdgesContainingSku(sku);
            // #region agent log
            { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"B\",\"location\":\"customer_behavior.cpp:sku_check\",\"message\":\"Checking SKU for shelf location\",\"data\":{\"customerId\":" << c.getId() << ",\"sku\":" << sku << ",\"productName\":\"" << kv.second.name << "\",\"productCategory\":\"" << kv.second.category << "\",\"locationCount\":" << locations.size() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
            // #endregion
            if (!locations.empty()) {
                availableSkus.push_back(sku);
            }
        }

        // #region agent log
        { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"B\",\"location\":\"customer_behavior.cpp:available_skus\",\"message\":\"Available SKUs after filtering\",\"data\":{\"customerId\":" << c.getId() << ",\"availableCount\":" << availableSkus.size() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
        // #endregion

        // Now randomly select from available SKUs only
        // Mission size based on customer demographics (family size and income)
        if (!availableSkus.empty()) {
            static std::mt19937 rngMission(std::random_device{}());
            std::shuffle(availableSkus.begin(), availableSkus.end(), rngMission);

            // Base mission size from family size (larger families need more items)
            int baseMission = std::max(2, std::min(6, c.getFamilySize() + 1));

            // Income modifier: higher income = slight increase
            // Normalize income: mean ~40k, so income/60 gives ~0.67 for average
            double incomeMultiplier = 0.8 + 0.4 * std::min(c.getAnnualIncome() / 60.0, 1.5);

            // Final mission size with some randomness (+/- 1 item)
            int targetSize = static_cast<int>(baseMission * incomeMultiplier);
            std::uniform_int_distribution<int> variance(-1, 1);
            size_t n = static_cast<size_t>(std::max(2, std::min(8, targetSize + variance(rngMission))));
            n = std::min(availableSkus.size(), n);

            for (size_t i = 0; i < n; ++i)
                missionSkus.push_back(availableSkus[i]);

            // #region agent log
            {
                std::string missionStr;
                for (size_t i = 0; i < missionSkus.size(); ++i) {
                    if (i > 0) missionStr += ",";
                    missionStr += std::to_string(missionSkus[i]);
                }
                AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app);
                if(lf) lf << "{\"hypothesisId\":\"B\",\"location\":\"customer_behavior.cpp:mission_selected\",\"message\":\"Mission SKUs selected\",\"data\":{\"customerId\":" << c.getId() << ",\"missionSkus\":\"" << missionStr << "\",\"missionCount\":" << missionSkus.size() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
            }
            // #endregion
        }
    }

    bool useNavmesh = ctx.store.hasNavMesh();
    if (useNavmesh && c.isUsingNavmesh() && !c.getNavmeshPath().empty() &&
        c.getCurrentWaypointIndex() < c.getNavmeshPath().size()) {
        const auto &waypoint = c.getNavmeshPath()[c.getCurrentWaypointIndex()];
        double dx = waypoint.first - c.posX, dz = waypoint.second - c.posZ;
        double dist = std::sqrt(dx * dx + dz * dz);
        size_t pathSizeMb = c.getNavmeshPath().size();
        bool isLastWp = (c.getCurrentWaypointIndex() == pathSizeMb - 1);
        double reachRadiusMb = isLastWp ? GOAL_RADIUS
                                        : std::max(WAYPOINT_THRESHOLD_MIN,
                                                   c.speed * ctx.dt * WAYPOINT_THRESHOLD_FRAME_FACTOR);
        if (dist < reachRadiusMb) {
            c.incrementWaypointIndex();
            if (c.getCurrentWaypointIndex() >= c.getNavmeshPath().size()) {
                c.setNavmeshPath({});
                c.setUsingNavmesh(false);
                if (state == HeadingToCheckout) {
                    state = HeadingToExit;
                    return {Decision::Checkout,
                            ctx.store.nodeAt(ctx.store.edgeAt(c.currentEdgeIndex).getToNode())
                                .getNodeId(),
                            5.0f};
                }
                if (state == HeadingToExit) {
                    state = Exiting;
                    return {Decision::Despawn};
                }
                if (state == Exiting)
                    return {Decision::Despawn};
                if (state == MissionBrowse && missionTargetEdgeIdx >= 0) {
                    int sku = missionSkus[missionIndex];
                    missionTargetEdgeIdx = missionTargetCellIdx = -1;
                    ++missionIndex;
                    if (missionIndex >= missionSkus.size())
                        state = HeadingToCheckout;
                    return {Decision::PickProduct, sku, 1.0f};
                }
            } else {
                return {Decision::Move};
            }
        } else {
            double moveDist = std::min(c.speed * ctx.dt, dist);
            double dirX = (dist > 1e-6) ? dx / dist : 0.0, dirZ = (dist > 1e-6) ? dz / dist : 0.0;
            double newX = c.posX + dirX * moveDist, newZ = c.posZ + dirZ * moveDist;
            if (ctx.store.hasPhysicsWorld() &&
                !ctx.store.getPhysicsWorld().isValidPosition(newX, newZ, 0.35)) {
                newX = c.posX + dirX * moveDist * 0.5;
                newZ = c.posZ + dirZ * moveDist * 0.5;
                if (!ctx.store.getPhysicsWorld().isValidPosition(newX, newZ, 0.35)) {
                    c.setNavmeshPath({});
                    c.setUsingNavmesh(false);
                    return {Decision::Move};
                }
            }
            // #region debug log
            { bool inObst = ctx.store.hasPhysicsWorld() && !ctx.store.getPhysicsWorld().isValidPosition(newX, newZ, 0.35); std::ofstream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if (lf) lf << "{\"hypothesisId\":\"H4\",\"location\":\"customer_behavior.cpp:mission_navmesh_move\",\"message\":\"Mission navmesh step\",\"data\":{\"customerId\":" << c.getId() << ",\"posBeforeX\":" << c.posX << ",\"posBeforeZ\":" << c.posZ << ",\"waypointX\":" << waypoint.first << ",\"waypointZ\":" << waypoint.second << ",\"newPosX\":" << newX << ",\"newPosZ\":" << newZ << ",\"inObstacleAfter\":" << (inObst ? "true" : "false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
            // #endregion
            c.setPosition(newX, newZ);
            if (c.currentEdgeIndex >= 0)
                c.distOnEdge = std::min(c.distOnEdge + moveDist,
                                        ctx.store.edgeAt(c.currentEdgeIndex).getLength());
            return {Decision::Move};
        }
    }

    if (state == MissionBrowse && missionIndex < missionSkus.size()) {
        int sku = missionSkus[missionIndex];
        auto loci = ctx.store.findEdgesContainingSku(sku);
        if (loci.empty()) {
            ++missionIndex; /* skip */
        } else {
            static std::mt19937 rngMb(std::random_device{}());
            size_t idx = static_cast<size_t>(rngMb()) % loci.size();
            int e = loci[idx].first, cell = loci[idx].second;
            // Determine which side has the SKU and path to stall position
            const auto &targetCell = ctx.store.edgeAt(e).cells[static_cast<size_t>(cell)];
            bool leftSide = chooseInteractionSide(targetCell);
            c.setTargetCellIdx(cell);
            c.setInteractingLeftSide(leftSide);
            auto [tx, tz] = ctx.store.getStallPosition(e, cell, leftSide);
            double sx = c.posX, sz = c.posZ;
            if (sx == 0.0 && sz == 0.0) {
                int nodeIdx = ctx.store.edgeAt(e).getFromNode();
                sx = ctx.store.nodeAt(nodeIdx).getX();
                sz = ctx.store.nodeAt(nodeIdx).getZ();
                c.setPosition(sx, sz);
            }
            auto path = NavMeshPathfinder::findPath(ctx.store.getNavMesh(), sx, sz, tx, tz);
            if (!path.empty()) {
                std::vector<std::pair<double, double>> waypoints;
                for (const auto &p : path)
                    waypoints.emplace_back(p.x, p.z);

                // --- VALIDATE AND SNAP PATH ---
                validateAndSnapPath(waypoints, ctx.store);
                // ------------------------------

                c.setNavmeshPath(waypoints);
                c.setUsingNavmesh(true);
                // #region debug log
                if (ctx.store.hasPhysicsWorld()) {
                    const PhysicsWorld &pw = ctx.store.getPhysicsWorld();
                    int inObst = 0;
                    for (const auto &w : waypoints)
                        if (!pw.isValidPosition(w.first, w.second, 0.35)) ++inObst;
                    { std::ofstream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if (lf) lf << "{\"hypothesisId\":\"H1\",\"location\":\"customer_behavior.cpp:mission_path_set\",\"message\":\"Mission path waypoints obstacle check\",\"data\":{\"customerId\":" << c.getId() << ",\"pathSize\":" << waypoints.size() << ",\"waypointsInObstacle\":" << inObst << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                }
                // #endregion
                c.currentEdgeIndex = e;
                c.distOnEdge = 0.0;
                missionTargetEdgeIdx = e;
                missionTargetCellIdx = cell;
                return {Decision::Move};
            }
            ++missionIndex;
        }
        return {Decision::Move};
    }

    if (state == MissionBrowse && missionIndex >= missionSkus.size())
        state = HeadingToCheckout;

    int currentNode = -1;
    if (c.currentEdgeIndex >= 0) {
        currentNode = ctx.store.edgeAt(c.currentEdgeIndex).getToNode();
        const auto &edge = ctx.store.edgeAt(c.currentEdgeIndex);
        if (c.distOnEdge >= edge.getLength()) {
            const auto &arr = ctx.store.nodeAt(currentNode);
            if (arr.getNodeType() == Node::NodeType::Register && ctx.basket.getSize() > 0) {
                if (state == HeadingToCheckout && !c.isInQueue()) {
                    // Join checkout queue
                    if (ctx.queueManager) {
                        int laneId = ctx.queueManager->selectLane(
                            c.posX, c.posZ, c.getPatience(), c.getCrowdSensitivity());
                        if (laneId >= 0) {
                            ctx.queueManager->joinQueue(laneId, c.getId());
                            c.setQueueLaneId(laneId);
                            c.setInQueue(true);
                            // Set queue walk target
                            auto wp = ctx.queueManager->getQueueWaypoint(laneId, c.getId());
                            c.setQueueTarget(wp.x, wp.z);
                            c.setWalkingToQueuePos(true);
                            // Register callback for queue position updates and walk-to-target
                            CheckoutQueueManager* qmPtr = ctx.queueManager;
                            int custId = c.getId();
                            ctx.queueManager->registerAdvanceCallback(c.getId(),
                                [&c, qmPtr, laneId, custId](int, int newPos) {
                                    c.setQueuePosition(newPos);
                                    // Update walk target when queue advances
                                    if (qmPtr) {
                                        auto newWp = qmPtr->getQueueWaypoint(laneId, custId);
                                        c.setQueueTarget(newWp.x, newWp.z);
                                        c.setWalkingToQueuePos(true);
                                    }
                                });
                        }
                    }
                    state = InQueue;
                    // Start walking to queue position
                    return {Decision::Move};
                }
                if (state == InQueue && !c.isWalkingToQueuePos()) {
                    // Check if at front of queue and done waiting
                    bool atFront = !ctx.queueManager || ctx.queueManager->isAtFront(c.getQueueLaneId(), c.getId());
                    if (atFront) {
                        // Leave queue and proceed to exit
                        if (ctx.queueManager) {
                            ctx.queueManager->leaveQueue(c.getQueueLaneId(), c.getId());
                            ctx.queueManager->unregisterCallback(c.getId());
                        }
                        c.setInQueue(false);
                        state = HeadingToExit;
                        return {Decision::Checkout, arr.getNodeId(), 5.0f};
                    }
                }
            }
            if (arr.getNodeType() == Node::NodeType::Exit) {
                if (c.isUsingNavmesh()) {
                    c.setNavmeshPath({});
                    c.setUsingNavmesh(false);
                }
                state = Exiting;
                return {Decision::Despawn};
            }
        }
    }

    if (state == HeadingToCheckout || state == HeadingToExit) {
        if (currentNode < 0 && c.currentEdgeIndex >= 0)
            currentNode = ctx.store.edgeAt(c.currentEdgeIndex).getToNode();
        if (currentNode >= 0 && useNavmesh) {
            int targetNodeId = -1;
            for (int i = 0; i < ctx.store.numNodes(); ++i) {
                if (ctx.store.nodeAt(i).getNodeType() == (state == HeadingToCheckout
                                                              ? Node::NodeType::Register
                                                              : Node::NodeType::Exit)) {
                    targetNodeId = ctx.store.nodeAt(i).getNodeId();
                    break;
                }
            }
            if (targetNodeId >= 0) {
                double sx = c.posX, sz = c.posZ;
                if (sx == 0.0 && sz == 0.0 && c.currentEdgeIndex >= 0) {
                    int n = ctx.store.edgeAt(c.currentEdgeIndex).getFromNode();
                    sx = ctx.store.nodeAt(n).getX();
                    sz = ctx.store.nodeAt(n).getZ();
                    c.setPosition(sx, sz);
                }
                const auto &tn = ctx.store.nodeAt(ctx.store.nodeIndexById(targetNodeId));
                auto path = NavMeshPathfinder::findPath(ctx.store.getNavMesh(), sx, sz, tn.getX(),
                                                        tn.getZ());
                if (!path.empty()) {
                    std::vector<std::pair<double, double>> waypoints;
                    for (const auto &p : path)
                        waypoints.emplace_back(p.x, p.z);

                    // --- VALIDATE AND SNAP PATH ---
                    validateAndSnapPath(waypoints, ctx.store);
                    // ------------------------------

                    c.setNavmeshPath(waypoints);
                    c.setUsingNavmesh(true);
                    // #region debug log
                    if (ctx.store.hasPhysicsWorld()) {
                        const PhysicsWorld &pw = ctx.store.getPhysicsWorld();
                        int inObst = 0;
                        for (const auto &w : waypoints)
                            if (!pw.isValidPosition(w.first, w.second, 0.35)) ++inObst;
                        { std::ofstream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if (lf) lf << "{\"hypothesisId\":\"H1\",\"location\":\"customer_behavior.cpp:mission_checkout_exit_path_set\",\"message\":\"Checkout/exit path waypoints obstacle check\",\"data\":{\"customerId\":" << c.getId() << ",\"pathSize\":" << waypoints.size() << ",\"waypointsInObstacle\":" << inObst << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                    }
                    // #endregion
                    int nextEdge = Navigator::findNextEdgeToType(currentNode,
                                                                 state == HeadingToCheckout
                                                                     ? Node::NodeType::Register
                                                                     : Node::NodeType::Exit,
                                                                 ctx.store);
                    c.currentEdgeIndex = nextEdge >= 0 ? nextEdge : c.currentEdgeIndex;
                    c.distOnEdge = 0.0;
                    return {Decision::Move};
                }
            }
        }
    }

    return {Decision::Move};
}

} // namespace priceriot