#include "customer_behavior.h"
#include "customer.h"
#include "../environment/checkout_queue.h"
#include "../environment/collision_manager.h"
#include "../environment/environment.h"
#include "../environment/navmesh_pathfinder.h"
#include "../environment/physics.h"
#include "../environment/store_layout.h"
#include "basket.h"
#include "shelf.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <string>
#include <type_traits>

namespace priceriot {

namespace {
/// Behavioural personal space radius (m) for separation forces.
/// Kept tight so the avoidance bubble isn't visible at normal walking distances.
constexpr double kPersonalSpaceRadius = 0.9;
/// Extra multiplier applied to the averaged separation force.
constexpr double kSeparationAmplify = 1.5;
/// Time-to-live for cached navmesh paths in seconds.
constexpr double kNavmeshPathCacheTtlSeconds = 2.5;

// Jitter generator for symmetry breaking in crowd flow.
// Fixed seed makes movement deterministic per-run; the simulator's seeded RNG
// drives all decision-making randomness, so this only affects path micro-jitter.
static std::mt19937 &jitterRng() {
    thread_local std::mt19937 rng(12345u);
    return rng;
}
static std::uniform_real_distribution<double> &jitterDist() {
    static std::uniform_real_distribution<double> dist(-0.1, 0.1);
    return dist;
}

/**
 * @brief Compute exponential separation force from nearby agents using the spatial hash.
 *
 * Uses inverse-square falloff: strength = (personal_space / distance)^2.
 * Forces from all neighbors are averaged and then amplified.
 *
 * Only dynamic agents are considered; static obstacles are handled separately via PhysicsWorld.
 */
static void computeSeparationVector(const Customer &self,
                                    const StoreGraph &store,
                                    double &outX,
                                    double &outZ) {
    outX = 0.0;
    outZ = 0.0;

    // Spatial hash is owned by StoreGraph and rebuilt each tick by the simulator.
    const SpatialHash &hash = store.getSpatialHash();

    const double px = self.getPosX();
    const double pz = self.getPosZ();

    auto neighbors = hash.query(px, pz, kPersonalSpaceRadius);
    if (neighbors.empty())
        return;

    double sumX = 0.0;
    double sumZ = 0.0;
    int count = 0;

    for (Customer *other : neighbors) {
        if (!other || other == &self)
            continue;

        const double ox = other->getPosX();
        const double oz = other->getPosZ();
        double dx = px - ox;
        double dz = pz - oz;
        double distSq = dx * dx + dz * dz;
        if (distSq < 1e-6)
            continue;

        double dist = std::sqrt(distSq);
        if (dist <= 0.0)
            continue;

        // Quartic falloff: (1 - dist/R)^4
        // Zero at the boundary, ramps up sharply only when agents are truly close.
        // This makes the personal-space edge invisible — there's no sudden jump.
        double t = dist / kPersonalSpaceRadius;
        double one_mt = 1.0 - t;
        double strength = one_mt * one_mt * one_mt * one_mt;

        dx /= dist;
        dz /= dist;
        sumX += dx * strength;
        sumZ += dz * strength;
        ++count;
    }

    if (count == 0)
        return;

    // Average then amplify.
    sumX /= static_cast<double>(count);
    sumZ /= static_cast<double>(count);
    sumX *= kSeparationAmplify;
    sumZ *= kSeparationAmplify;

    outX = sumX;
    outZ = sumZ;
}

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

    // Second pass: push waypoints that are too close to obstacles away from them.
    // The funnel places waypoints at portal edges (corridor corners near shelf ends).
    // Nudging them into clearer corridor space prevents path segments from clipping shelves.
    const double safeRadius = radius + 0.12;
    for (size_t i = 1; i + 1 < waypoints.size(); ++i) { // skip start and final goal
        Circle wpCircle(waypoints[i].first, waypoints[i].second, safeRadius);
        if (pw.checkCollision(wpCircle)) {
            pw.resolveCollision(wpCircle);
            waypoints[i].first = wpCircle.x;
            waypoints[i].second = wpCircle.z;
        }
    }
}

/**
 * @brief Pull corner waypoints toward the inner bisector of each turn.
 *
 * The funnel algorithm places corner waypoints at tight portal edges near
 * shelf ends. Pulling them toward the corridor centre reduces the chance an
 * agent clips the shelf when approaching from a deflected position.
 */
static void smoothCornerWaypoints(std::vector<std::pair<double, double>> &waypoints,
                                   const priceriot::StoreGraph &store) {
    if (waypoints.size() < 3 || !store.hasPhysicsWorld())
        return;
    const PhysicsWorld &pw = store.getPhysicsWorld();
    constexpr double kAgentRadius = 0.35;
    constexpr double kMaxPull = 0.5; // metres — cap so we don't overshoot corridor centre

    for (size_t i = 1; i + 1 < waypoints.size(); ++i) {
        double ax = waypoints[i - 1].first,  az = waypoints[i - 1].second;
        double bx = waypoints[i].first,      bz = waypoints[i].second;
        double cx = waypoints[i + 1].first,  cz = waypoints[i + 1].second;

        // Incoming direction (a→b) and outgoing direction (b→c)
        double inX = bx - ax, inZ = bz - az;
        double inLen = std::sqrt(inX * inX + inZ * inZ);
        double outX = cx - bx, outZ = cz - bz;
        double outLen = std::sqrt(outX * outX + outZ * outZ);
        if (inLen < 1e-6 || outLen < 1e-6)
            continue;

        inX /= inLen;  inZ /= inLen;
        outX /= outLen; outZ /= outLen;

        double dot = inX * outX + inZ * outZ; // 1 = straight, -1 = U-turn
        if (dot > 0.98)
            continue; // Nearly straight — nothing to smooth

        // Bisector direction pointing toward the inside of the turn
        double bisX = inX + outX, bisZ = inZ + outZ;
        double bisLen = std::sqrt(bisX * bisX + bisZ * bisZ);
        if (bisLen < 1e-6)
            continue;
        bisX /= bisLen; bisZ /= bisLen;

        // Pull distance scales with sharpness of the turn: max pull at 90°, zero at straight
        double pullDist = std::min(kMaxPull, (1.0 - dot) * 0.4);

        double nx = bx + bisX * pullDist;
        double nz = bz + bisZ * pullDist;

        if (pw.isValidPosition(nx, nz, kAgentRadius)) {
            waypoints[i].first  = nx;
            waypoints[i].second = nz;
        }
    }
}

/**
 * @brief Check static line-of-sight between two points using PhysicsWorld.
 *
 * Samples along the segment [a,b] at fixed intervals and returns false as
 * soon as a sample enters an obstacle or boundary.
 */
static bool hasLineOfSight(const priceriot::StoreGraph &store,
                           double ax, double az,
                           double bx, double bz,
                           double agentRadius) {
    if (!store.hasPhysicsWorld())
        return true;

    const PhysicsWorld &pw = store.getPhysicsWorld();

    double dx = bx - ax;
    double dz = bz - az;
    double dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 1e-6)
        return true;

    const double step = 0.1; // meters between samples — fine enough to catch narrow shelf protrusions
    int steps = static_cast<int>(std::ceil(dist / step));
    if (steps <= 1)
        return true;

    double stepX = dx / static_cast<double>(steps);
    double stepZ = dz / static_cast<double>(steps);

    // Skip the exact endpoints; they are assumed to be valid or already snapped.
    // Use a slightly padded radius so paths don't clip shelf corners even when
    // the funnel places a waypoint just barely inside the clear corridor.
    const double checkRadius = agentRadius + 0.05;
    for (int i = 1; i < steps; ++i) {
        double sx = ax + stepX * static_cast<double>(i);
        double sz = az + stepZ * static_cast<double>(i);
        if (!pw.isValidPosition(sx, sz, checkRadius))
            return false;
    }
    return true;
}

/**
 * @brief Aggressively reduce waypoints using static line-of-sight.
 *
 * Keeps only waypoints where a turn is necessary. Starting from the first
 * waypoint, greedily jumps to the furthest later waypoint that is still
 * visible without intersecting static obstacles.
 */
static void reduceWaypointsByLineOfSight(std::vector<std::pair<double, double>> &waypoints,
                                         const priceriot::StoreGraph &store) {
    if (waypoints.size() <= 2 || !store.hasPhysicsWorld())
        return;

    constexpr double kAgentRadius = 0.35;

    std::vector<std::pair<double, double>> reduced;
    reduced.reserve(waypoints.size());

    std::size_t current = 0;
    reduced.push_back(waypoints[current]);

    while (current + 1 < waypoints.size()) {
        std::size_t next = current + 1;

        // Find furthest reachable waypoint with static LOS from current.
        for (std::size_t j = waypoints.size() - 1; j > current + 1; --j) {
            if (hasLineOfSight(store,
                               waypoints[current].first, waypoints[current].second,
                               waypoints[j].first,      waypoints[j].second,
                               kAgentRadius)) {
                next = j;
                break;
            }
        }

        reduced.push_back(waypoints[next]);
        current = next;
    }

    waypoints.swap(reduced);
}
}

// --- Pathfinding tuning constants (waypoint follow and goal arrival) ---
static constexpr double WAYPOINT_THRESHOLD_MIN = 0.15;       // Min distance to consider waypoint reached (m)
static constexpr double WAYPOINT_THRESHOLD_FRAME_FACTOR = 1.2; // Scale for speed*dt (avoid overshoot)
static constexpr double GOAL_RADIUS = 0.3;                    // Larger radius for final waypoint (m)

// --- Graph step: which edge to take and whether we walk YAML from→to or to→from (bi only) ---
struct NavStep {
    int edgeIndex = -1;
    bool forwardAlongYaml = true;
};

// --- Helper: BFS Navigator (bi edges are traversable in both directions) ---
class Navigator {
  public:
    static NavStep findNextEdge(int startNode, int targetNode, const StoreGraph &store) {
        if (startNode == targetNode)
            return {-1, true};

        std::queue<int> frontier;
        frontier.push(startNode);
        std::map<int, int> parent;
        std::map<int, NavStep> stepInto;
        std::vector<bool> visited(store.numNodes(), false);

        visited[startNode] = true;
        parent[startNode] = -1;

        while (!frontier.empty()) {
            int current = frontier.front();
            frontier.pop();
            if (current == targetNode)
                break;

            for (int i = 0; i < store.numEdges(); ++i) {
                const auto &edge = store.edgeAt(i);
                const bool bi = (edge.getFlow() == Edge::Flow::bi);

                if (edge.getFromNode() == current) {
                    int neighbor = edge.getToNode();
                    if (!visited[neighbor]) {
                        visited[neighbor] = true;
                        parent[neighbor] = current;
                        stepInto[neighbor] = {i, true};
                        frontier.push(neighbor);
                    }
                } else if (bi && edge.getToNode() == current) {
                    int neighbor = edge.getFromNode();
                    if (!visited[neighbor]) {
                        visited[neighbor] = true;
                        parent[neighbor] = current;
                        stepInto[neighbor] = {i, false};
                        frontier.push(neighbor);
                    }
                }
            }
        }

        if (!visited[static_cast<size_t>(targetNode)])
            return {-1, true};

        int curr = targetNode;
        while (parent[curr] != startNode)
            curr = parent[curr];
        return stepInto[curr];
    }

    static NavStep findNextEdgeToType(int startNode, Node::NodeType targetType,
                                      const StoreGraph &store) {
        std::queue<int> frontier;
        frontier.push(startNode);
        std::map<int, int> parent;
        std::map<int, NavStep> stepInto;
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
                const bool bi = (edge.getFlow() == Edge::Flow::bi);

                if (edge.getFromNode() == current) {
                    int neighbor = edge.getToNode();
                    if (!visited[neighbor]) {
                        visited[neighbor] = true;
                        parent[neighbor] = current;
                        stepInto[neighbor] = {i, true};
                        frontier.push(neighbor);
                    }
                } else if (bi && edge.getToNode() == current) {
                    int neighbor = edge.getFromNode();
                    if (!visited[neighbor]) {
                        visited[neighbor] = true;
                        parent[neighbor] = current;
                        stepInto[neighbor] = {i, false};
                        frontier.push(neighbor);
                    }
                }
            }
        }

        if (foundNode != -1) {
            if (parent[foundNode] == -1)
                return {-1, true};
            int curr = foundNode;
            while (parent[curr] != startNode)
                curr = parent[curr];
            return stepInto[curr];
        }
        return {-1, true};
    }
};

/** Graph index of the Register endpoint for this edge, if any; else to-node (legacy). */
static int registerEndpointNodeIdx(const StoreGraph &store, int edgeIdx) {
    if (edgeIdx < 0)
        return -1;
    const auto &e = store.edgeAt(edgeIdx);
    if (store.nodeAt(e.getFromNode()).getNodeType() == Node::NodeType::Register)
        return e.getFromNode();
    if (store.nodeAt(e.getToNode()).getNodeType() == Node::NodeType::Register)
        return e.getToNode();
    return e.getToNode();
}

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
static bool chooseInteractionSide(const priceriot::EdgeCell &cell,
                                  std::default_random_engine *rng) {
    bool leftHas = sideHasInventory(cell.get_left());
    bool rightHas = sideHasInventory(cell.get_right());
    if (leftHas && !rightHas) return true;  // Left side
    if (rightHas && !leftHas) return false; // Right side
    // Both or neither: pick randomly
    if (rng) return (*rng)() % 2 == 0;
    return true; // deterministic fallback if no RNG provided
}

Decision DefaultBehavior::decide(Customer &c, const ICustomerBehaviorContext &ctx) {
    if (c.getDwellTicks() > 0)
        return {Decision::Wait};
    if (c.getCurrentEdgeIndex() == -1) {
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
        const double dx = c.getPosX() - exitNode.getX();
        const double dz = c.getPosZ() - exitNode.getZ();
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
        double dx = c.getQueueTargetX() - c.getPosX();
        double dz = c.getQueueTargetZ() - c.getPosZ();
        double dist = std::sqrt(dx * dx + dz * dz);

        if (dist < 0.3) { // Arrived at waypoint
            c.setPosition(c.getQueueTargetX(), c.getQueueTargetZ()); // Snap final
            c.setWalkingToQueuePos(false);
            // If at front of queue, start service timer
            if (ctx.queueManager && ctx.queueManager->isAtFront(c.getQueueLaneId(), c.getId())) {
                const auto &regNode =
                    ctx.store.nodeAt(registerEndpointNodeIdx(ctx.store, c.getCurrentEdgeIndex()));
                double rate = regNode.getServiceRate();
                int serviceTicks = (rate > 0.01) ? static_cast<int>((1.0 / rate) * 60.0) : 300;
                c.setDwellTicks(serviceTicks);
            } else {
                c.setDwellTicks(30); // Wait for queue to advance
            }
            return {Decision::Wait};
        }

        // Walk toward target
        double walkSpeed = c.getSpeed() * ctx.dt;
        double dirX = dx / dist;
        double dirZ = dz / dist;
        c.setPosition(c.getPosX() + dirX * walkSpeed, c.getPosZ() + dirZ * walkSpeed);
        return {Decision::Move};
    }

    // InQueue settled: dwell expired and not walking — check queue position.
    // This runs before any navmesh/navigation code so checkout fires regardless
    // of distOnEdge, preventing the navigation failsafe from routing the customer
    // to the exit before they've paid.
    if (state == InQueue && !c.isWalkingToQueuePos()) {
        bool atFront = true;
        if (ctx.queueManager && c.isInQueue()) {
            atFront = ctx.queueManager->isAtFront(c.getQueueLaneId(), c.getId());
        }
        if (atFront) {
            if (ctx.queueManager && c.isInQueue()) {
                ctx.queueManager->advanceQueue(c.getQueueLaneId());
                c.setInQueue(false);
                c.setQueueLaneId(-1);
            }
            state = HeadingToExit;
            return {Decision::Checkout, -1, 5.0f};
        }
        c.setDwellTicks(30);
        return {Decision::Wait};
    }

    // Check if navmesh is available and use hybrid navigation
    bool useNavmesh = ctx.store.hasNavMesh();

    // If using navmesh and have a path, follow it
    if (useNavmesh && c.isUsingNavmesh() && !c.getNavmeshPath().empty() &&
        c.getCurrentWaypointIndex() < c.getNavmeshPath().size()) {
        // Get current waypoint
        const auto &waypoint = c.getNavmeshPath()[c.getCurrentWaypointIndex()];
        double dx = waypoint.first - c.getPosX();
        double dz = waypoint.second - c.getPosZ();
        double dist = std::sqrt(dx * dx + dz * dz);

        size_t pathSize = c.getNavmeshPath().size();
        bool isLastWaypoint =
            (c.getCurrentWaypointIndex() == pathSize - 1);
        double reachRadius = isLastWaypoint
                                 ? GOAL_RADIUS
                                 : std::max(WAYPOINT_THRESHOLD_MIN,
                                            c.getSpeed() * ctx.dt * WAYPOINT_THRESHOLD_FRAME_FACTOR);

        // Stuck-waypoint detection: if the same waypoint has been targeted for >3 sim-seconds
        // without being reached, skip it (intermediate) or clear the path (goal). This replaces
        // the per-tick hasLineOfSight checks that were causing oscillation at shelf corners.
        {
            size_t curWpIdx = c.getCurrentWaypointIndex();
            if (curWpIdx != lastNavmeshWaypointIdx) {
                waypointStuckSeconds = 0.0f;
                lastNavmeshWaypointIdx = curWpIdx;
            } else {
                waypointStuckSeconds += static_cast<float>(ctx.dt);
            }
            constexpr float kMaxStuckSeconds = 3.0f; // 3 sim-seconds
            if (waypointStuckSeconds > kMaxStuckSeconds) {
                waypointStuckSeconds = 0.0f;
                if (!isLastWaypoint) {
                    // Skip to next intermediate waypoint
                    c.incrementWaypointIndex();
                    lastNavmeshWaypointIdx = c.getCurrentWaypointIndex();
                } else {
                    // Stuck at goal — clear path and fall back to graph nav
                    c.setNavmeshPath({});
                    c.setUsingNavmesh(false);
                }
                return {Decision::Move};
            }
        }

        // If reached waypoint, move to next
        if (dist < reachRadius) {
            c.incrementWaypointIndex();
            if (c.getCurrentWaypointIndex() >= c.getNavmeshPath().size()) {
                // Reached end of path, clear it
                c.setNavmeshPath({});
                c.setUsingNavmesh(false);
                if (state == HeadingToCheckout) {
                    if (ctx.queueManager) {
                        int laneId = ctx.queueManager->selectLane(
                            c.getPosX(), c.getPosZ(), c.getPatience(), c.getCrowdSensitivity());
                        if (laneId >= 0) {
                            CheckoutQueueManager* qmPtr = ctx.queueManager;
                            int custId = c.getId();
                            ctx.queueManager->joinQueue(laneId, custId);
                            c.setQueueLaneId(laneId);
                            c.setInQueue(true);
                            ctx.queueManager->registerAdvanceCallback(custId,
                                [&c, qmPtr, laneId, custId](int, int newPos) {
                                    c.setQueuePosition(newPos);
                                    if (qmPtr) {
                                        auto newWp = qmPtr->getQueueWaypoint(laneId, custId);
                                        c.setQueueTarget(newWp.x, newWp.z);
                                        c.setWalkingToQueuePos(true);
                                    }
                                });
                        }
                    }
                    state = InQueue;
                    // Fix: navmesh resets distOnEdge to 0 at path start; snap to edge end so
                    // node-arrival / queue logic can run when facing forward or backward.
                    {
                        const double el = ctx.store.edgeAt(c.getCurrentEdgeIndex()).getLength();
                        c.setDistOnEdge(c.isEdgeTraverseForward() ? el : 0.0);
                    }
                    // Only start the service timer if this customer is already at the
                    // front of the queue. Non-front customers get a short initial dwell
                    // then fall into the 30-tick polling loop via the early InQueue check.
                    if (ctx.queueManager && c.getQueueLaneId() >= 0 &&
                        ctx.queueManager->isAtFront(c.getQueueLaneId(), c.getId())) {
                        const auto &regNode = ctx.store.nodeAt(
                            registerEndpointNodeIdx(ctx.store, c.getCurrentEdgeIndex()));
                        double rate = regNode.getServiceRate();
                        int serviceTicks = (rate > 0.01) ? static_cast<int>((1.0 / rate) * 60.0) : 300;
                        c.setDwellTicks(serviceTicks);
                    }
                    return {Decision::Wait};
                }
                if (state == HeadingToExit) {
                    state = Exiting;
                    return {Decision::Despawn};
                }
                if (state == Exiting)
                    return {Decision::Despawn};
                // FIX: When navmesh path completes in Browsing state, find the closest graph node
                // and update currentEdgeIndex to an edge starting from that node
                // IMPORTANT: For Browsing state, avoid routing toward Register/Exit - stay in shopping area
                if (state == Browsing || state == Entering) {
                    double px = c.getPosX(), pz = c.getPosZ();

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
                        c.setCurrentEdgeIndex(bestEdgeIdx);
                        c.setEdgeTraverseForward(true);
                        // FIX: Set distOnEdge to 0 to allow browsing through the edge cells
                        // Previously set to edgeLength which skipped all cell interactions
                        c.setDistOnEdge(0.0);
                        // Sync position to edge start node for consistent cell positioning
                        const auto &fromNode = ctx.store.nodeAt(ctx.store.edgeAt(bestEdgeIdx).getFromNode());
                        c.setPosition(fromNode.getX(), fromNode.getZ());
                        // Also reset shop cell tracking for the new edge
                        c.setLastShopCell(-1);
                        c.setLastPickAttemptCell(-1);
                    }

                    // Continue with Move to let edge-based navigation take over
                    return {Decision::Move};
                }
            } else {
                // Continue to next waypoint
                return {Decision::Move};
            }
        } else {
            // Move toward waypoint using blended global path + local separation steering
            double moveDist = c.getSpeed() * ctx.dt;

            // Normalize global path direction
            double dirX = (dist > 1e-6) ? dx / dist : 0.0;
            double dirZ = (dist > 1e-6) ? dz / dist : 0.0;

            if (moveDist > dist)
                moveDist = dist;

            // Local steering from nearby agents via spatial hash (dynamic obstacles only)
            double sepX = 0.0;
            double sepZ = 0.0;
            computeSeparationVector(c, ctx.store, sepX, sepZ);

            // Blend global path and local steering.
            // Weight scales with separation magnitude so distant agents cause no wobble.
            double velX = dirX;
            double velZ = dirZ;
            double sepMag = std::sqrt(sepX * sepX + sepZ * sepZ);
            if (sepMag > 0.05) {
                // Normalise sep direction, then blend proportionally (max 30%).
                double invSepMag = 1.0 / sepMag;
                double blendW = std::min(sepMag * 0.35, 0.30);
                velX = dirX * (1.0 - blendW) + (sepX * invSepMag) * blendW;
                velZ = dirZ * (1.0 - blendW) + (sepZ * invSepMag) * blendW;
            }

            // Symmetry-breaking jitter (use seeded RNG for determinism; fall back to jitterRng)
            double jx = ctx.rng ? jitterDist()(*ctx.rng) : jitterDist()(jitterRng());
            double jz = ctx.rng ? jitterDist()(*ctx.rng) : jitterDist()(jitterRng());
            velX += jx;
            velZ += jz;

            // Normalize final velocity direction to keep speed bounded
            double vMag = std::sqrt(velX * velX + velZ * velZ);
            if (vMag > 1e-6) {
                velX /= vMag;
                velZ /= vMag;
            } else {
                velX = dirX;
                velZ = dirZ;
            }

            double moveX = velX * moveDist;
            double moveZ = velZ * moveDist;

            // Check if new position would be valid (obstacle check)
            double newX = c.getPosX() + moveX;
            double newZ = c.getPosZ() + moveZ;

            // If stepping would collide with another agent, take a smaller step.
            // Use the spatial hash (O(k) nearby agents) rather than the full O(N) scan.
            const double agentRadius = 0.35;
            {
                const SpatialHash &hash = ctx.store.getSpatialHash();
                auto nearby = hash.query(newX, newZ, agentRadius * 2.0);
                for (Customer *other : nearby) {
                    if (!other || other == &c) continue;
                    double ox = other->getPosX(), oz = other->getPosZ();
                    double ddx = newX - ox, ddz = newZ - oz;
                    if (ddx * ddx + ddz * ddz < (agentRadius * 2.0) * (agentRadius * 2.0)) {
                        moveX *= 0.5;
                        moveZ *= 0.5;
                        newX = c.getPosX() + moveX;
                        newZ = c.getPosZ() + moveZ;
                        break;
                    }
                }
            }

            if (ctx.store.hasPhysicsWorld()) {
                const PhysicsWorld &physics = ctx.store.getPhysicsWorld();

                // Check if position is valid
                if (!physics.isValidPosition(newX, newZ, agentRadius)) {
                    // Try reduced movement (half speed)
                    newX = c.getPosX() + moveX * 0.5;
                    newZ = c.getPosZ() + moveZ * 0.5;
                    if (!physics.isValidPosition(newX, newZ, agentRadius)) {
                        // Try perpendicular movement (slide along obstacle)
                        double perpX = -dirZ;
                        double perpZ = dirX;
                        newX = c.getPosX() + perpX * moveDist * 0.3;
                        newZ = c.getPosZ() + perpZ * moveDist * 0.3;
                        if (!physics.isValidPosition(newX, newZ, agentRadius)) {
                            // Try opposite perpendicular
                            newX = c.getPosX() - perpX * moveDist * 0.3;
                            newZ = c.getPosZ() - perpZ * moveDist * 0.3;
                            if (!physics.isValidPosition(newX, newZ, agentRadius)) {
                                // Still blocked, try to push away from obstacle
                                Circle agentCircle(c.getPosX(), c.getPosZ(), agentRadius);
                                double normalX, normalZ, penetration;
                                if (physics.getCollisionInfo(agentCircle, normalX, normalZ,
                                                             penetration)) {
                                    // Push away from obstacle
                                    newX = c.getPosX() + normalX * moveDist * 0.5;
                                    newZ = c.getPosZ() + normalZ * moveDist * 0.5;
                                    if (!physics.isValidPosition(newX, newZ, agentRadius)) {
                                        // Completely blocked — snap the agent clearly away from
                                        // the shelf using the collision normal, then clear the
                                        // path but keep usingNavmesh=true so the cooldown check
                                        // prevents an immediate bad recalculation.
                                        {
                                            double snapX = c.getPosX() + normalX * (penetration + agentRadius * 0.5 + 0.1);
                                            double snapZ = c.getPosZ() + normalZ * (penetration + agentRadius * 0.5 + 0.1);
                                            if (physics.isValidPosition(snapX, snapZ, agentRadius))
                                                c.setPosition(snapX, snapZ);
                                        }
                                        c.setNavmeshPath({});
                                        c.setUsingNavmesh(false);
                                        return {Decision::Move};
                                    }
                                } else {
                                    // No collision info — generic snap then cooldown.
                                    {
                                        Circle snapCircle(c.getPosX(), c.getPosZ(), agentRadius);
                                        if (physics.checkCollision(snapCircle)) {
                                            physics.resolveCollision(snapCircle);
                                            c.setPosition(snapCircle.x, snapCircle.z);
                                        }
                                    }
                                    c.setNavmeshPath({});
                                    c.setUsingNavmesh(false);
                                    return {Decision::Move};
                                }
                            }
                        }
                    }
                }
            }

            // Update position using setter
            c.setPosition(newX, newZ);

            // Update distOnEdge for backward compatibility (approximate)
            if (c.getCurrentEdgeIndex() >= 0) {
                const auto &edge = ctx.store.edgeAt(c.getCurrentEdgeIndex());
                if (c.isEdgeTraverseForward())
                    c.setDistOnEdge(std::min(c.getDistOnEdge() + moveDist, edge.getLength()));
                else
                    c.setDistOnEdge(std::max(c.getDistOnEdge() - moveDist, 0.0));
            }

            return {Decision::Move};
        }
    }

    const auto &currentEdge = ctx.store.edgeAt(c.getCurrentEdgeIndex());
    const bool trFwd = c.isEdgeTraverseForward();
    const int approachNodeIdx = trFwd ? currentEdge.getToNode() : currentEdge.getFromNode();

    // --- STATE MACHINE: SHOPPING ---
    // Do not pick on any edge segment that touches the entrance (either YAML endpoint).
    const bool fromEntrance =
        ctx.store.nodeAt(currentEdge.getFromNode()).getNodeType() == Node::NodeType::Entrance ||
        ctx.store.nodeAt(currentEdge.getToNode()).getNodeType() == Node::NodeType::Entrance;
    if (state == Browsing && !fromEntrance) {
        // Are we on an aisle with shelves?
        if (currentEdge.getCellCount() > 0 && ctx.basket.getSize() < 8) {
            if (const int cellIdx = static_cast<int>(c.getDistOnEdge() / currentEdge.getCellLength());
                cellIdx >= 0 && cellIdx < static_cast<int>(currentEdge.cells.size())) {
                // New cell - dwell to simulate browsing
                if (c.getLastShopCell() != cellIdx) {
                    c.setLastShopCell(cellIdx);
                    // Check if cell has inventory - if so, dwell first
                    if (const auto skus = getSkusInCell(currentEdge.cells[cellIdx]);
                        !skus.empty()) {
                        // Set sideband interaction state
                        c.setTargetCellIdx(cellIdx);
                        c.setInteractingLeftSide(chooseInteractionSide(currentEdge.cells[cellIdx], ctx.rng));
                        // Randomized dwell: 2-5 seconds at 60 ticks/sec = 120-300 ticks
                        int dwellTicks = ctx.rng ? 120 + static_cast<int>((*ctx.rng)() % 181) : 210;
                        c.setDwellTicks(dwellTicks);
                        return {Decision::Wait};
                    }
                }
                // Same cell, dwell complete - attempt pick once per cell
                else if (c.getLastPickAttemptCell() != cellIdx) {
                    c.setLastPickAttemptCell(cellIdx);
                    int pickChance = 80;
                    if (c.getImpulsivity() > 0.4)
                        pickChance = 90;
                    auto pickRoll = [&]() -> unsigned { return ctx.rng ? (*ctx.rng)() : 50u; };
                    if (pickRoll() % 100 < static_cast<unsigned>(pickChance)) {
                        if (const auto skus = getSkusInCell(currentEdge.cells[cellIdx]);
                            !skus.empty()) {
                            int chosenSku = skus[pickRoll() % skus.size()];
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
                                chosenSku = skuPrices[ctx.rng ? dist(*ctx.rng) : 0].first;
                            }
                            return {Decision::PickProduct, chosenSku, 1.0f};
                        }
                    }
                }
            }
        }
    }

    // --- STATE MACHINE: MOVEMENT ---
    {
        const double elen = currentEdge.getLength();
        const bool arrived =
            (trFwd && c.getDistOnEdge() >= elen) || (!trFwd && c.getDistOnEdge() <= 0.0);
        if (arrived) {

        if (trFwd)
            c.setDistOnEdge(elen);
        else
            c.setDistOnEdge(0.0);

        const auto &arrivalNode = ctx.store.nodeAt(approachNodeIdx);
        const int arrivalNodeId = arrivalNode.getNodeId();

        // 1. Transition Logic (Enter -> Browse)
        if (state == Entering) {
            state = Browsing;
            preferredAisleNodes.clear();
            std::vector<int> junctionIds;
            for (int i = 0; i < ctx.store.numNodes(); ++i) {
                if (ctx.store.nodeAt(i).getNodeType() == Node::NodeType::Junction)
                    junctionIds.push_back(ctx.store.nodeAt(i).getNodeId());
            }
            if (ctx.rng)
                std::shuffle(junctionIds.begin(), junctionIds.end(), *ctx.rng);
            for (size_t i = 0; i < std::min(size_t(3), junctionIds.size()); ++i)
                preferredAisleNodes.push_back(junctionIds[i]);
        }

        // 2. Browse -> Checkout Logic
        if (state == Browsing) {
            if (ctx.basket.getSize() >= 5)
                state = HeadingToCheckout;
            // Random chance to finish shopping
            if (ctx.rng && (*ctx.rng)() % 100 < 10)
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
                            c.getPosX(), c.getPosZ(), c.getPatience(), c.getCrowdSensitivity());
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
            state = Exiting;
            return {Decision::Despawn};
        }

        // 4. Navigation (Find next edge or waypoint)
        NavStep nextStep{-1, true};
        int targetNodeId = -1;

        if (state == HeadingToCheckout) {
            // Find waypoint node (Register)
            for (int i = 0; i < ctx.store.numNodes(); ++i) {
                if (ctx.store.nodeAt(i).getNodeType() == Node::NodeType::Register) {
                    targetNodeId = ctx.store.nodeAt(i).getNodeId();
                    break;
                }
            }
            nextStep = Navigator::findNextEdgeToType(approachNodeIdx, Node::NodeType::Register, ctx.store);
        } else if (state == HeadingToExit) {
            // Find waypoint node (Exit)
            for (int i = 0; i < ctx.store.numNodes(); ++i) {
                if (ctx.store.nodeAt(i).getNodeType() == Node::NodeType::Exit) {
                    targetNodeId = ctx.store.nodeAt(i).getNodeId();
                    break;
                }
            }
            nextStep = Navigator::findNextEdgeToType(approachNodeIdx, Node::NodeType::Exit, ctx.store);
        } else if (state == Browsing) {
            struct Choice {
                int idx;
                bool fwd;
            };
            std::vector<Choice> preferred, other;
            auto tryBrowseEdge = [&](int ei, bool fwdAlong, int nextNodeGraphIdx) {
                const Node &nextN = ctx.store.nodeAt(nextNodeGraphIdx);
                Node::NodeType toType = nextN.getNodeType();
                if (!nextN.canEnter(c.getId()))
                    return;
                if (toType == Node::NodeType::Register || toType == Node::NodeType::Exit) {
                    return;
                }
                int toId = nextN.getNodeId();
                Choice ch{ei, fwdAlong};
                if (std::find(preferredAisleNodes.begin(), preferredAisleNodes.end(), toId) !=
                    preferredAisleNodes.end())
                    preferred.push_back(ch);
                else
                    other.push_back(ch);
            };
            for (int i = 0; i < ctx.store.numEdges(); ++i) {
                const auto &e = ctx.store.edgeAt(i);
                if (e.getFromNode() == approachNodeIdx)
                    tryBrowseEdge(i, true, e.getToNode());
                if (e.getFlow() == Edge::Flow::bi && e.getToNode() == approachNodeIdx)
                    tryBrowseEdge(i, false, e.getFromNode());
            }
            auto navRoll = [&]() -> unsigned { return ctx.rng ? (*ctx.rng)() : jitterRng()(); };
            std::vector<Choice> *choice =
                !preferred.empty() && (other.empty() || navRoll() % 100 < 70) ? &preferred : &other;
            if (!choice->empty()) {
                const Choice &ch = (*choice)[static_cast<size_t>(navRoll()) % choice->size()];
                nextStep.edgeIndex = ch.idx;
                nextStep.forwardAlongYaml = ch.fwd;
                const auto &e = ctx.store.edgeAt(ch.idx);
                const int nextNi = ch.fwd ? e.getToNode() : e.getFromNode();
                targetNodeId = ctx.store.nodeAt(nextNi).getNodeId();
            }
        }

        int nextEdge = nextStep.edgeIndex;
        bool nextEdgeForward = nextStep.forwardAlongYaml;

        // Failsafe: If stuck or no path found, just pick any outgoing edge
        if (nextEdge == -1) {
            // Build a list of all edges for debugging (idx:id:from->to)
            std::string edgeList;
            for (int i = 0; i < ctx.store.numEdges(); ++i) {
                const auto &e = ctx.store.edgeAt(i);
                edgeList += std::to_string(i) + "(id" + std::to_string(e.getEdgeId()) + "):" +
                           std::to_string(e.getFromNode()) + "->" +
                           std::to_string(e.getToNode()) + " ";
                if (e.getFromNode() == approachNodeIdx) {
                    nextEdge = i;
                    nextEdgeForward = true;
                    targetNodeId = ctx.store.nodeAt(e.getToNode()).getNodeId();
                    break;
                }
                if (e.getFlow() == Edge::Flow::bi && e.getToNode() == approachNodeIdx) {
                    nextEdge = i;
                    nextEdgeForward = false;
                    targetNodeId = ctx.store.nodeAt(e.getFromNode()).getNodeId();
                    break;
                }
            }
            if (nextEdge == -1) {
                return {Decision::Despawn};
            }
        }

        // If navmesh is available, use hybrid navigation
        bool useNavmesh = ctx.store.hasNavMesh();
        if (useNavmesh && targetNodeId >= 0) {
            // Get current position (use node center if world pos not set)
            double startX = c.getPosX();
            double startZ = c.getPosZ();
            if (startX == 0.0 && startZ == 0.0) {
                // Initialize from current node center
                const auto &currentNodeObj = ctx.store.nodeAt(approachNodeIdx);
                startX = currentNodeObj.getX();
                startZ = currentNodeObj.getZ();
                c.setPosition(startX, startZ);
            }

            // Get target position (node center)
            const auto &targetNode = ctx.store.nodeAt(ctx.store.nodeIndexById(targetNodeId));
            double endX = targetNode.getX();
            double endZ = targetNode.getZ();

            // Path caching: if we already have a recent path to (endX,endZ), reuse it.
            {
                const double dxg = endX - c.getCachedGoal().first;
                const double dzg = endZ - c.getCachedGoal().second;
                const double goalDiffSq = dxg * dxg + dzg * dzg;
                constexpr double kGoalEpsSq = 0.25; // 0.5 m
                // Failure cooldown: path was just cleared due to an obstacle
                // (usingNavmesh=true, path empty, age near zero). Hold off 0.75 s so the
                // agent drifts away from the shelf via edge-based movement first.
                if (c.isUsingNavmesh() && c.getNavmeshPath().empty() && c.hasCachedGoal() &&
                    goalDiffSq < kGoalEpsSq && c.getNavmeshPathAge() < 0.75) {
                    return {Decision::Move};
                }
                if (c.isUsingNavmesh() && !c.getNavmeshPath().empty() && c.hasCachedGoal() &&
                    goalDiffSq < kGoalEpsSq &&
                    c.getNavmeshPathAge() < kNavmeshPathCacheTtlSeconds) {
                    return {Decision::Move};
                }
            }

            // Snap start position away from obstacles before path calculation.
            // An agent pressed against a shelf boundary would otherwise produce a first
            // path segment that immediately re-enters the same obstacle.
            if (ctx.store.hasPhysicsWorld()) {
                Circle startSnap(startX, startZ, 0.40);
                if (ctx.store.getPhysicsWorld().checkCollision(startSnap)) {
                    ctx.store.getPhysicsWorld().resolveCollision(startSnap);
                    startX = startSnap.x;
                    startZ = startSnap.z;
                    c.setPosition(startX, startZ);
                }
            }

            // Find navmesh path
            auto path =
                NavMeshPathfinder::findPath(ctx.store.getNavMesh(), startX, startZ, endX, endZ);

            if (!path.empty()) {
                // Convert path to waypoints
                std::vector<std::pair<double, double>> waypoints;
                for (const auto &point : path) {
                    waypoints.emplace_back(point.x, point.z);
                }

                // --- Aggressive LOS-based waypoint reduction then snap out of obstacles ---
                reduceWaypointsByLineOfSight(waypoints, ctx.store);
                // --- VALIDATE AND SNAP PATH ---
                validateAndSnapPath(waypoints, ctx.store);
                smoothCornerWaypoints(waypoints, ctx.store);
                // ------------------------------

                c.setNavmeshPath(waypoints);
                c.setUsingNavmesh(true);
                c.setCachedGoal(endX, endZ);

                // Update current edge index for compatibility
                c.setCurrentEdgeIndex(nextEdge);
                c.setEdgeTraverseForward(nextEdgeForward);
                const double nlen = nextEdge >= 0 ? ctx.store.edgeAt(nextEdge).getLength() : 0.0;
                c.setDistOnEdge(nextEdgeForward ? 0.0 : nlen);

                return {Decision::Move};
            }
        }

        // Fall back to edge-based navigation
        return {Decision::SwitchEdge, nextEdge, 0.0f, nextEdgeForward};
        }
    }

    if (state == Exiting && isNearExit())
        return {Decision::Despawn};

    // Cell attraction: steer browsing agents toward stall positions (offset from shelf)
    if (state == Browsing && c.getCurrentEdgeIndex() >= 0 && !c.isUsingNavmesh()) {
        const auto &edge = ctx.store.edgeAt(c.getCurrentEdgeIndex());
        if (edge.getCellCount() > 0) {
            int cellIdx = static_cast<int>(c.getDistOnEdge() / edge.getCellLength());
            cellIdx = std::clamp(cellIdx, 0, edge.getCellCount() - 1);

            // Use stall position if interacting with a cell, otherwise use cell center
            double targetX, targetZ;
            if (c.getTargetCellIdx() == cellIdx) {
                // Steer toward stall position (offset toward shelf side)
                auto [stallX, stallZ] = ctx.store.getStallPosition(
                    c.getCurrentEdgeIndex(), cellIdx, c.isInteractingLeftSide());
                targetX = stallX;
                targetZ = stallZ;
            } else {
                // Just passing through - steer toward cell center
                auto [cellX, cellZ] = ctx.store.getCellCenter(c.getCurrentEdgeIndex(), cellIdx);
                targetX = cellX;
                targetZ = cellZ;
            }

            double dx = targetX - c.getPosX();
            double dz = targetZ - c.getPosZ();
            double dist = std::sqrt(dx * dx + dz * dz);

            if (dist > 0.1) { // Only correct if >10cm off
                double correctionRate = 0.15; // Blend factor per tick
                c.setPosition(c.getPosX() + dx * correctionRate, c.getPosZ() + dz * correctionRate);
            }
        }
    }

    return {Decision::Move};
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
    if (c.getCurrentEdgeIndex() == -1)
        return {Decision::Despawn};

    // InQueue walking: move toward queue waypoint position
    if (state == InQueue && c.isWalkingToQueuePos()) {
        double dx = c.getQueueTargetX() - c.getPosX();
        double dz = c.getQueueTargetZ() - c.getPosZ();
        double dist = std::sqrt(dx * dx + dz * dz);

        if (dist < 0.3) { // Arrived at waypoint
            c.setPosition(c.getQueueTargetX(), c.getQueueTargetZ()); // Snap final
            c.setWalkingToQueuePos(false);
            // If at front of queue, start service timer
            if (ctx.queueManager && ctx.queueManager->isAtFront(c.getQueueLaneId(), c.getId())) {
                const auto &regNode =
                    ctx.store.nodeAt(registerEndpointNodeIdx(ctx.store, c.getCurrentEdgeIndex()));
                double rate = regNode.getServiceRate();
                int serviceTicks = (rate > 0.01) ? static_cast<int>((1.0 / rate) * 60.0) : 300;
                c.setDwellTicks(serviceTicks);
            } else {
                c.setDwellTicks(30); // Wait for queue to advance
            }
            return {Decision::Wait};
        }

        // Walk toward target
        double walkSpeed = c.getSpeed() * ctx.dt;
        double dirX = dx / dist;
        double dirZ = dz / dist;
        c.setPosition(c.getPosX() + dirX * walkSpeed, c.getPosZ() + dirZ * walkSpeed);
        return {Decision::Move};
    }

    // InQueue settled: same early-exit checkout as DefaultBehavior — ensures front
    // customers check out and non-front customers poll every 30 ticks, without
    // depending on the distOnEdge gate or the node-arrival block being reached.
    if (state == InQueue && !c.isWalkingToQueuePos()) {
        bool atFront = true;
        if (ctx.queueManager && c.isInQueue()) {
            atFront = ctx.queueManager->isAtFront(c.getQueueLaneId(), c.getId());
        }
        if (atFront) {
            if (ctx.queueManager && c.isInQueue()) {
                ctx.queueManager->leaveQueue(c.getQueueLaneId(), c.getId());
                ctx.queueManager->unregisterCallback(c.getId());
            }
            c.setInQueue(false);
            state = HeadingToExit;
            return {Decision::Checkout, -1, 5.0f};
        }
        c.setDwellTicks(30);
        return {Decision::Wait};
    }

    if (state == Entering) {
        state = MissionBrowse;
        missionSkus.clear();
        missionIndex = 0;

        // FIX: Only select SKUs that are actually available on shelves
        // First, collect all SKUs that exist on shelves (have at least one location)
        std::vector<int> availableSkus;
        const auto &catalog = ctx.store.catalog.getProductsMap();
        for (const auto &kv : catalog) {
            int sku = kv.first;
            auto locations = ctx.store.findEdgesContainingSku(sku);
            if (!locations.empty()) {
                availableSkus.push_back(sku);
            }
        }


        // Now randomly select from available SKUs only
        // Mission size based on customer demographics (family size and income)
        if (!availableSkus.empty()) {
            if (ctx.rng)
                std::shuffle(availableSkus.begin(), availableSkus.end(), *ctx.rng);

            // Base mission size from family size (larger families need more items)
            int baseMission = std::max(2, std::min(6, c.getFamilySize() + 1));

            // Income modifier: higher income = slight increase
            // Normalize income: mean ~40k, so income/60 gives ~0.67 for average
            double incomeMultiplier = 0.8 + 0.4 * std::min(c.getAnnualIncome() / 60.0, 1.5);

            // Final mission size with some randomness (+/- 1 item)
            int targetSize = static_cast<int>(baseMission * incomeMultiplier);
            std::uniform_int_distribution<int> variance(-1, 1);
            size_t n = static_cast<size_t>(std::max(2, std::min(8,
                ctx.rng ? targetSize + variance(*ctx.rng) : targetSize)));
            n = std::min(availableSkus.size(), n);

            for (size_t i = 0; i < n; ++i)
                missionSkus.push_back(availableSkus[i]);

        }
    }

    bool useNavmesh = ctx.store.hasNavMesh();
    if (useNavmesh && c.isUsingNavmesh() && !c.getNavmeshPath().empty() &&
        c.getCurrentWaypointIndex() < c.getNavmeshPath().size()) {
        const auto &waypoint = c.getNavmeshPath()[c.getCurrentWaypointIndex()];
        double dx = waypoint.first - c.getPosX(), dz = waypoint.second - c.getPosZ();
        double dist = std::sqrt(dx * dx + dz * dz);
        size_t pathSizeMb = c.getNavmeshPath().size();
        bool isLastWp = (c.getCurrentWaypointIndex() == pathSizeMb - 1);
        double reachRadiusMb = isLastWp ? GOAL_RADIUS
                                        : std::max(WAYPOINT_THRESHOLD_MIN,
                                                   c.getSpeed() * ctx.dt * WAYPOINT_THRESHOLD_FRAME_FACTOR);

        // Stuck-waypoint detection (see DefaultBehavior for rationale).
        {
            size_t curWpIdxMb = c.getCurrentWaypointIndex();
            if (curWpIdxMb != lastNavmeshWaypointIdx) {
                waypointStuckSeconds = 0.0f;
                lastNavmeshWaypointIdx = curWpIdxMb;
            } else {
                waypointStuckSeconds += static_cast<float>(ctx.dt);
            }
            constexpr float kMaxStuckSecondsMb = 3.0f;
            if (waypointStuckSeconds > kMaxStuckSecondsMb) {
                waypointStuckSeconds = 0.0f;
                if (!isLastWp) {
                    c.incrementWaypointIndex();
                    lastNavmeshWaypointIdx = c.getCurrentWaypointIndex();
                } else {
                    c.setNavmeshPath({});
                    c.setUsingNavmesh(false);
                }
                return {Decision::Move};
            }
        }

        if (dist < reachRadiusMb) {
            c.incrementWaypointIndex();
            if (c.getCurrentWaypointIndex() >= c.getNavmeshPath().size()) {
                c.setNavmeshPath({});
                c.setUsingNavmesh(false);
                if (state == HeadingToCheckout) {
                    if (ctx.queueManager) {
                        int laneId = ctx.queueManager->selectLane(
                            c.getPosX(), c.getPosZ(), c.getPatience(), c.getCrowdSensitivity());
                        if (laneId >= 0) {
                            CheckoutQueueManager* qmPtr = ctx.queueManager;
                            int custId = c.getId();
                            ctx.queueManager->joinQueue(laneId, custId);
                            c.setQueueLaneId(laneId);
                            c.setInQueue(true);
                            auto wp = ctx.queueManager->getQueueWaypoint(laneId, custId);
                            c.setQueueTarget(wp.x, wp.z);
                            c.setWalkingToQueuePos(true);
                            ctx.queueManager->registerAdvanceCallback(custId,
                                [&c, qmPtr, laneId, custId](int, int newPos) {
                                    c.setQueuePosition(newPos);
                                    if (qmPtr) {
                                        auto newWp = qmPtr->getQueueWaypoint(laneId, custId);
                                        c.setQueueTarget(newWp.x, newWp.z);
                                        c.setWalkingToQueuePos(true);
                                    }
                                });
                        }
                    }
                    state = InQueue;
                    {
                        const double el = ctx.store.edgeAt(c.getCurrentEdgeIndex()).getLength();
                        c.setDistOnEdge(c.isEdgeTraverseForward() ? el : 0.0);
                    }
                    return {Decision::Move};
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
            double moveDist = std::min(c.getSpeed() * ctx.dt, dist);
            double dirX = (dist > 1e-6) ? dx / dist : 0.0;
            double dirZ = (dist > 1e-6) ? dz / dist : 0.0;

            // Local steering from nearby agents via spatial hash
            double sepX = 0.0;
            double sepZ = 0.0;
            computeSeparationVector(c, ctx.store, sepX, sepZ);

            double velX = dirX;
            double velZ = dirZ;
            double sepMag = std::sqrt(sepX * sepX + sepZ * sepZ);
            if (sepMag > 0.05) {
                double invSepMag = 1.0 / sepMag;
                double blendW = std::min(sepMag * 0.35, 0.30);
                velX = dirX * (1.0 - blendW) + (sepX * invSepMag) * blendW;
                velZ = dirZ * (1.0 - blendW) + (sepZ * invSepMag) * blendW;
            }

            double jx = ctx.rng ? jitterDist()(*ctx.rng) : jitterDist()(jitterRng());
            double jz = ctx.rng ? jitterDist()(*ctx.rng) : jitterDist()(jitterRng());
            velX += jx;
            velZ += jz;

            double vMag = std::sqrt(velX * velX + velZ * velZ);
            if (vMag > 1e-6) {
                velX /= vMag;
                velZ /= vMag;
            } else {
                velX = dirX;
                velZ = dirZ;
            }

            double moveX = velX * moveDist;
            double moveZ = velZ * moveDist;

            double newX = c.getPosX() + moveX;
            double newZ = c.getPosZ() + moveZ;
            const double agentRadius = 0.35;
            {
                const SpatialHash &hash = ctx.store.getSpatialHash();
                auto nearby = hash.query(newX, newZ, agentRadius * 2.0);
                for (Customer *other : nearby) {
                    if (!other || other == &c) continue;
                    double ox = other->getPosX(), oz = other->getPosZ();
                    double ddx = newX - ox, ddz = newZ - oz;
                    if (ddx * ddx + ddz * ddz < (agentRadius * 2.0) * (agentRadius * 2.0)) {
                        moveX *= 0.5;
                        moveZ *= 0.5;
                        newX = c.getPosX() + moveX;
                        newZ = c.getPosZ() + moveZ;
                        break;
                    }
                }
            }

            if (ctx.store.hasPhysicsWorld() &&
                !ctx.store.getPhysicsWorld().isValidPosition(newX, newZ, 0.35)) {
                newX = c.getPosX() + moveX * 0.5;
                newZ = c.getPosZ() + moveZ * 0.5;
                if (!ctx.store.getPhysicsWorld().isValidPosition(newX, newZ, 0.35)) {
                    // Snap agent away from shelf before clearing — cooldown stays active.
                    {
                        Circle snapCircle(c.getPosX(), c.getPosZ(), 0.35);
                        if (ctx.store.getPhysicsWorld().checkCollision(snapCircle)) {
                            ctx.store.getPhysicsWorld().resolveCollision(snapCircle);
                            c.setPosition(snapCircle.x, snapCircle.z);
                        }
                    }
                    c.setNavmeshPath({});
                    c.setUsingNavmesh(false);
                    return {Decision::Move};
                }
            }
            c.setPosition(newX, newZ);
            if (c.getCurrentEdgeIndex() >= 0) {
                const auto &me = ctx.store.edgeAt(c.getCurrentEdgeIndex());
                if (c.isEdgeTraverseForward())
                    c.setDistOnEdge(std::min(c.getDistOnEdge() + moveDist, me.getLength()));
                else
                    c.setDistOnEdge(std::max(c.getDistOnEdge() - moveDist, 0.0));
            }
            return {Decision::Move};
        }
    }

    if (state == MissionBrowse && missionIndex < missionSkus.size()) {
        int sku = missionSkus[missionIndex];
        auto loci = ctx.store.findEdgesContainingSku(sku);

        // Filter out cells whose center is too close to a Junction or Entrance node.
        // getCellCenter interpolates between node *centers*, so cell 0 of an edge can
        // sit right on top of a junction hub — causing agents to stop at junctions to pick.
        // 1.5m radius (2.25 = 1.5^2) keeps picks inside the aisle body.
        loci.erase(
            std::remove_if(loci.begin(), loci.end(),
                [&](const std::pair<int,int> &loc) {
                    auto [cx, cz] = ctx.store.getCellCenter(loc.first, loc.second);
                    for (int n = 0; n < ctx.store.numNodes(); ++n) {
                        const auto &nd = ctx.store.nodeAt(n);
                        auto t = nd.getNodeType();
                        if (t == Node::NodeType::Junction || t == Node::NodeType::Entrance) {
                            double ddx = cx - nd.getX(), ddz = cz - nd.getZ();
                            if (ddx * ddx + ddz * ddz < 2.25) // 1.5m
                                return true;
                        }
                    }
                    return false;
                }),
            loci.end());

        if (loci.empty()) {
            ++missionIndex; /* skip — no valid stall away from junctions */
        } else {
            size_t idx = ctx.rng ? static_cast<size_t>((*ctx.rng)()) % loci.size() : 0;
            int e = loci[idx].first, cell = loci[idx].second;
            // Determine which side has the SKU and path to stall position
            const auto &targetCell = ctx.store.edgeAt(e).cells[static_cast<size_t>(cell)];
            bool leftSide = chooseInteractionSide(targetCell, ctx.rng);
            c.setTargetCellIdx(cell);
            c.setInteractingLeftSide(leftSide);
            auto [tx, tz] = ctx.store.getStallPosition(e, cell, leftSide);
            double sx = c.getPosX(), sz = c.getPosZ();
            if (sx == 0.0 && sz == 0.0) {
                int nodeIdx = ctx.store.edgeAt(e).getFromNode();
                sx = ctx.store.nodeAt(nodeIdx).getX();
                sz = ctx.store.nodeAt(nodeIdx).getZ();
                c.setPosition(sx, sz);
            }
            if (ctx.store.hasPhysicsWorld()) {
                Circle startSnap(sx, sz, 0.40);
                if (ctx.store.getPhysicsWorld().checkCollision(startSnap)) {
                    ctx.store.getPhysicsWorld().resolveCollision(startSnap);
                    sx = startSnap.x;
                    sz = startSnap.z;
                    c.setPosition(sx, sz);
                }
            }
            auto path = NavMeshPathfinder::findPath(ctx.store.getNavMesh(), sx, sz, tx, tz);
            if (!path.empty()) {
                std::vector<std::pair<double, double>> waypoints;
                for (const auto &p : path)
                    waypoints.emplace_back(p.x, p.z);

                // --- Aggressive LOS-based waypoint reduction then snap out of obstacles ---
                reduceWaypointsByLineOfSight(waypoints, ctx.store);
                // --- VALIDATE AND SNAP PATH ---
                validateAndSnapPath(waypoints, ctx.store);
                smoothCornerWaypoints(waypoints, ctx.store);
                // ------------------------------

                c.setNavmeshPath(waypoints);
                c.setUsingNavmesh(true);
                c.setCachedGoal(tx, tz);
                c.setCurrentEdgeIndex(e);
                c.setEdgeTraverseForward(true);
                c.setDistOnEdge(0.0);
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

    int graphStartNode = -1;
    if (c.getCurrentEdgeIndex() >= 0) {
        const auto &edge = ctx.store.edgeAt(c.getCurrentEdgeIndex());
        const bool mtrFwd = c.isEdgeTraverseForward();
        const double melen = edge.getLength();
        const bool marrived =
            (mtrFwd && c.getDistOnEdge() >= melen) || (!mtrFwd && c.getDistOnEdge() <= 0.0);
        if (marrived) {
            graphStartNode = mtrFwd ? edge.getToNode() : edge.getFromNode();
            if (mtrFwd)
                c.setDistOnEdge(melen);
            else
                c.setDistOnEdge(0.0);
            const auto &arr = ctx.store.nodeAt(graphStartNode);
            if (arr.getNodeType() == Node::NodeType::Register && ctx.basket.getSize() > 0) {
                if (state == HeadingToCheckout && !c.isInQueue()) {
                    // Join checkout queue
                    if (ctx.queueManager) {
                        int laneId = ctx.queueManager->selectLane(
                            c.getPosX(), c.getPosZ(), c.getPatience(), c.getCrowdSensitivity());
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
        } else {
            graphStartNode = mtrFwd ? edge.getFromNode() : edge.getToNode();
        }
    }

    if (state == HeadingToCheckout || state == HeadingToExit) {
        if (graphStartNode < 0 && c.getCurrentEdgeIndex() >= 0) {
            const auto &edge = ctx.store.edgeAt(c.getCurrentEdgeIndex());
            const bool mtrFwd = c.isEdgeTraverseForward();
            const double melen = edge.getLength();
            const bool marrived =
                (mtrFwd && c.getDistOnEdge() >= melen) || (!mtrFwd && c.getDistOnEdge() <= 0.0);
            graphStartNode =
                marrived ? (mtrFwd ? edge.getToNode() : edge.getFromNode())
                         : (mtrFwd ? edge.getFromNode() : edge.getToNode());
        }
        if (graphStartNode >= 0 && useNavmesh) {
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
                double sx = c.getPosX(), sz = c.getPosZ();
                if (sx == 0.0 && sz == 0.0 && c.getCurrentEdgeIndex() >= 0) {
                    const auto &ce = ctx.store.edgeAt(c.getCurrentEdgeIndex());
                    const int n = c.isEdgeTraverseForward() ? ce.getFromNode() : ce.getToNode();
                    sx = ctx.store.nodeAt(n).getX();
                    sz = ctx.store.nodeAt(n).getZ();
                    c.setPosition(sx, sz);
                }
                const auto &tn = ctx.store.nodeAt(ctx.store.nodeIndexById(targetNodeId));
                if (ctx.store.hasPhysicsWorld()) {
                    Circle startSnap(sx, sz, 0.40);
                    if (ctx.store.getPhysicsWorld().checkCollision(startSnap)) {
                        ctx.store.getPhysicsWorld().resolveCollision(startSnap);
                        sx = startSnap.x;
                        sz = startSnap.z;
                        c.setPosition(sx, sz);
                    }
                }
                auto path = NavMeshPathfinder::findPath(ctx.store.getNavMesh(), sx, sz, tn.getX(),
                                                        tn.getZ());
                if (!path.empty()) {
                    std::vector<std::pair<double, double>> waypoints;
                    for (const auto &p : path)
                        waypoints.emplace_back(p.x, p.z);

                    // --- Aggressive LOS-based waypoint reduction then snap out of obstacles ---
                    reduceWaypointsByLineOfSight(waypoints, ctx.store);
                    // --- VALIDATE AND SNAP PATH ---
                    validateAndSnapPath(waypoints, ctx.store);
                    smoothCornerWaypoints(waypoints, ctx.store);
                    // ------------------------------

                    c.setNavmeshPath(waypoints);
                    c.setUsingNavmesh(true);
                    c.setCachedGoal(tn.getX(), tn.getZ());
                    NavStep ns = Navigator::findNextEdgeToType(
                        graphStartNode,
                        state == HeadingToCheckout ? Node::NodeType::Register : Node::NodeType::Exit,
                        ctx.store);
                    if (ns.edgeIndex >= 0) {
                        c.setCurrentEdgeIndex(ns.edgeIndex);
                        c.setEdgeTraverseForward(ns.forwardAlongYaml);
                        const double nlen = ctx.store.edgeAt(ns.edgeIndex).getLength();
                        c.setDistOnEdge(ns.forwardAlongYaml ? 0.0 : nlen);
                    }
                    return {Decision::Move};
                }
            }
        }
    }

    return {Decision::Move};
}

} // namespace priceriot