#include "customer_behavior.h"
#include "customer.h"
// CRITICAL FIX: Include Environment so StoreGraph and Node are defined
#include "../environment/environment.h"
#include "basket.h"
#include "shelf.h"
#include <random>
#include <algorithm>
#include <queue>
#include <map>
#include <iostream>

namespace priceriot {

// --- Helper: BFS Navigator ---
class Navigator {
public:
    static int findNextEdge(int startNode, int targetNode, const StoreGraph& store) {
        if (startNode == targetNode) return -1;

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
            if (current == targetNode) { found = true; break; }

            for (int i = 0; i < store.numEdges(); ++i) {
                // store is fully defined now
                if (const auto& edge = store.edgeAt(i); edge.getFromNode() == current) {
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
            while (parent[curr] != startNode) { curr = parent[curr]; }
            return cameFrom[curr];
        }
        return -1;
    }

    // Now Node::NodeType is visible
    static int findNextEdgeToType(int startNode, Node::NodeType targetType, const StoreGraph& store) {
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
                const auto& edge = store.edgeAt(i);
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
            if (parent[foundNode] == -1) return -1;
            int curr = foundNode;
            while (parent[curr] != startNode) { curr = parent[curr]; }
            return cameFrom[curr];
        }
        return -1;
    }
};

// --- Default Behavior Implementation ---

DefaultBehavior::DefaultBehavior() : state(Entering) {}

void DefaultBehavior::onEnterStore(Customer& c, const ICustomerBehaviorContext& ctx) const {
    c.setSpawning(true);
    c.setDwellTicks(0);
}

static std::vector<int> getSkusInCell(const priceriot::EdgeCell& cell) {
    std::vector<int> skus;
    auto scan = [&](const priceriot::ShelfSide& s) {
        for(int b=0; b<s.bay_count; ++b)
            for(int f=0; f<s.bays[b].face_count; ++f)
                for(int sl=0; sl<s.bays[b].faces[f].slot_count; ++sl)
                    if (s.bays[b].faces[f].slots[sl].qty_on_face > 0)
                        skus.push_back(static_cast<int>(s.bays[b].faces[f].slots[sl].sku_id));
    };
    scan(cell.get_left()); scan(cell.get_right());
    return skus;
}

Decision DefaultBehavior::decide(Customer& c, const ICustomerBehaviorContext& ctx) {
    if (c.getDwellTicks() > 0) return {Decision::Wait};
    if (c.currentEdgeIndex == -1) return {Decision::Despawn};

    const auto& currentEdge = ctx.store.edgeAt(c.currentEdgeIndex);
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

                    // High chance to check shelf if we are here
                    if (rng() % 100 < 80) {
                        if (const auto skus = getSkusInCell(currentEdge.cells[cellIdx]); !skus.empty()) {
                            // Debug: Confirm inventory is found
                            // std::cout << "Agent saw products: " << skus.size() << " SKUs.\n";
                            return {Decision::PickProduct, skus[rng() % skus.size()], 1.0f};
                        }
                    }
                }
            }
        }
    }

    // --- STATE MACHINE: MOVEMENT ---
    if (c.distOnEdge >= currentEdge.getLength()) {
        const auto& arrivalNode = ctx.store.nodeAt(currentNode);
        const int arrivalNodeId = arrivalNode.getNodeId();

        // 1. Transition Logic (Enter -> Browse)
        if (state == Entering) state = Browsing;

        // 2. Browse -> Checkout Logic
        if (state == Browsing) {
             if (ctx.basket.getSize() >= 5) state = HeadingToCheckout;

             // Random chance to finish shopping
             static std::mt19937 rng(std::random_device{}());
             if (rng() % 100 < 10) state = HeadingToCheckout;
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

        // 4. Navigation (Find next edge)
        int nextEdge = -1;

        if (state == HeadingToCheckout) {
            nextEdge = Navigator::findNextEdgeToType(currentNode, Node::NodeType::Register, ctx.store);
        }
        else if (state == HeadingToExit) {
             nextEdge = Navigator::findNextEdgeToType(currentNode, Node::NodeType::Exit, ctx.store);
        }
        else if (state == Browsing) {
            // Wander: Pick random edge that isn't reverse
            std::vector<int> candidates;
            for(int i=0; i<ctx.store.numEdges(); ++i) {
                if (ctx.store.edgeAt(i).getFromNode() == currentNode) candidates.push_back(i);
            }
            if (!candidates.empty()) {
                static std::mt19937 rng(std::random_device{}());
                nextEdge = candidates[rng() % candidates.size()];
            }
        }

        // Failsafe: If stuck or no path found, just pick any outgoing edge
        if (nextEdge == -1) {
            for(int i=0; i<ctx.store.numEdges(); ++i) {
                if (ctx.store.edgeAt(i).getFromNode() == currentNode) {
                     nextEdge = i;
                     break;
                }
            }
            if (nextEdge == -1) return {Decision::Despawn};
        }

        return {Decision::SwitchEdge, nextEdge};
    }

    return {Decision::Move};
}

// Helpers for the header stubs (if any)
int DefaultBehavior::getNextEdgeToNode(int currentNodeId, int targetNodeId, const StoreGraph& store) const {
    return Navigator::findNextEdge(currentNodeId, targetNodeId, store);
}
int DefaultBehavior::getNextEdgeToNodeType(int currentNodeId, int targetType, const StoreGraph& store) const {
    // Cast int to NodeType safely because we know it's an enum under the hood
    return Navigator::findNextEdgeToType(currentNodeId, static_cast<Node::NodeType>(targetType), store);
}

} // namespace priceriot