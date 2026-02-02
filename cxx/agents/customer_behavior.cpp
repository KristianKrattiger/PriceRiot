#include "customer_behavior.h"
#include "customer.h"
#include "../environment/environment.h"
#include "../environment/navmesh_pathfinder.h"
#include "../environment/physics.h"
#include "../environment/store_layout.h"
#include "basket.h"
#include "shelf.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <queue>
#include <random>

namespace priceriot {

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

Decision DefaultBehavior::decide(Customer &c, const ICustomerBehaviorContext &ctx) {
    if (c.getDwellTicks() > 0)
        return {Decision::Wait};
    if (c.currentEdgeIndex == -1)
        return {Decision::Despawn};

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

        // Velocity-based waypoint threshold: use distance we can travel in one frame.
        // Funnel-smoothed paths avoid corner-grazing, so no extra turn-aware scaling needed.
        double waypointThreshold =
            std::max(0.2, c.speed * ctx.dt * 1.5); // At least 0.2m, or 1.5x frame movement

        // If reached waypoint, move to next
        if (dist < waypointThreshold) {
            c.incrementWaypointIndex();
            if (c.getCurrentWaypointIndex() >= c.getNavmeshPath().size()) {
                // Reached end of path, clear it
                c.setNavmeshPath({});
                c.setUsingNavmesh(false);
                if (state == HeadingToCheckout) {
                    state = HeadingToExit;
                    return {Decision::Checkout,
                            ctx.store.nodeAt(ctx.store.edgeAt(c.currentEdgeIndex).getToNode())
                                .getNodeId(),
                            5.0f};
                }
                if (state == HeadingToExit)
                    return {Decision::Despawn};
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
                                        // Completely blocked, wait
                                        return {Decision::Wait};
                                    }
                                } else {
                                    // No collision info, wait
                                    return {Decision::Wait};
                                }
                            }
                        }
                    }
                }
            }

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
    if (state == Browsing || state == Entering) {
        // Are we on an aisle with shelves?
        if (currentEdge.getCellCount() > 0 && ctx.basket.getSize() < 8) {
            state = Browsing;
            if (const int cellIdx = static_cast<int>(c.distOnEdge / currentEdge.getCellLength());
                cellIdx >= 0 && cellIdx < static_cast<int>(currentEdge.cells.size())) {
                if (c.getLastShopCell() != cellIdx) {
                    c.setLastShopCell(cellIdx);
                    static std::mt19937 rng(std::random_device{}());
                    int pickChance = 80;
                    if (c.getImpulsivity() > 0.4)
                        pickChance = 90;
                    if (rng() % 100 < pickChance) {
                        if (const auto skus = getSkusInCell(currentEdge.cells[cellIdx]);
                            !skus.empty()) {
                            return {Decision::PickProduct, skus[rng() % skus.size()], 1.0f};
                        }
                    }
                }
            }
        }
    }

    // --- STATE MACHINE: MOVEMENT ---
    if (c.distOnEdge >= currentEdge.getLength()) {
        const auto &arrivalNode = ctx.store.nodeAt(currentNode);
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

        // 3. Node Interactions
        if (arrivalNode.getNodeType() == Node::NodeType::Register) {
            // If basket is empty, skip checkout, go to exit.
            if (ctx.basket.getSize() > 0) {
                if (state == HeadingToCheckout) {
                    state = HeadingToExit;
                    return {Decision::Checkout, arrivalNodeId, 5.0f};
                }
            } else {
                state = HeadingToExit; // Just leave if empty
            }
        }

        if (arrivalNode.getNodeType() == Node::NodeType::Exit) {
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
                int toId = ctx.store.nodeAt(ctx.store.edgeAt(i).getToNode()).getNodeId();
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
        }

        // Failsafe: If stuck or no path found, just pick any outgoing edge
        if (nextEdge == -1) {
            for (int i = 0; i < ctx.store.numEdges(); ++i) {
                if (ctx.store.edgeAt(i).getFromNode() == currentNode) {
                    nextEdge = i;
                    targetNodeId = ctx.store.nodeAt(ctx.store.edgeAt(i).getToNode()).getNodeId();
                    break;
                }
            }
            if (nextEdge == -1)
                return {Decision::Despawn};
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
                c.setNavmeshPath(waypoints);
                c.setUsingNavmesh(true);

                // Update current edge index for compatibility
                c.currentEdgeIndex = nextEdge;
                c.distOnEdge = 0.0;

                return {Decision::Move};
            }
        }

        // Fall back to edge-based navigation
        return {Decision::SwitchEdge, nextEdge};
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

void MissionBehavior::onEnterStore(Customer &c, const ICustomerBehaviorContext &ctx) const {
    c.setSpawning(true);
    c.setDwellTicks(0);
}

Decision MissionBehavior::decide(Customer &c, const ICustomerBehaviorContext &ctx) {
    if (c.getDwellTicks() > 0)
        return {Decision::Wait};
    if (c.currentEdgeIndex == -1)
        return {Decision::Despawn};

    if (state == Entering) {
        state = MissionBrowse;
        missionSkus.clear();
        missionIndex = 0;
        const auto &catalog = ctx.store.catalog.getProductsMap();
        if (!catalog.empty()) {
            std::vector<int> skuList;
            skuList.reserve(catalog.size());
            for (const auto &kv : catalog)
                skuList.push_back(kv.first);
            static std::mt19937 rngMission(std::random_device{}());
            std::shuffle(skuList.begin(), skuList.end(), rngMission);
            size_t n = std::min(skuList.size(), static_cast<size_t>(2 + rngMission() % 3));
            for (size_t i = 0; i < n; ++i)
                missionSkus.push_back(skuList[i]);
        }
    }

    bool useNavmesh = ctx.store.hasNavMesh();
    if (useNavmesh && c.isUsingNavmesh() && !c.getNavmeshPath().empty() &&
        c.getCurrentWaypointIndex() < c.getNavmeshPath().size()) {
        const auto &waypoint = c.getNavmeshPath()[c.getCurrentWaypointIndex()];
        double dx = waypoint.first - c.posX, dz = waypoint.second - c.posZ;
        double dist = std::sqrt(dx * dx + dz * dz);
        double waypointThreshold = std::max(0.2, c.speed * ctx.dt * 1.5);
        if (dist < waypointThreshold) {
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
                if (state == HeadingToExit)
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
                if (!ctx.store.getPhysicsWorld().isValidPosition(newX, newZ, 0.35))
                    return {Decision::Wait};
            }
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
            auto [tx, tz] = ctx.store.getCellCenter(e, cell);
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
                c.setNavmeshPath(waypoints);
                c.setUsingNavmesh(true);
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
            if (arr.getNodeType() == Node::NodeType::Register && ctx.basket.getSize() > 0 &&
                state == HeadingToCheckout) {
                state = HeadingToExit;
                return {Decision::Checkout, arr.getNodeId(), 5.0f};
            }
            if (arr.getNodeType() == Node::NodeType::Exit)
                return {Decision::Despawn};
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
                    c.setNavmeshPath(waypoints);
                    c.setUsingNavmesh(true);
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