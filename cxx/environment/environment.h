/**
 * @file environment.h
 * @brief Core store graph model: Node, Edge, and StoreGraph.
 *
 * The store is represented as a directed graph where:
 * - **Nodes** are spatial hubs (entrance, exit, junctions, registers, stockrooms) with world coordinates (x,z).
 * - **Edges** are aisles connecting nodes, subdivided into cells for traffic flow and shelf inventory.
 * - **StoreGraph** holds the graph structure, adjacency, navmesh, and physics world.
 *
 * See docs/ARCHITECTURE.md for data flow and layering.
 */
#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "cell.h"
#include "navmesh.h"
#include "physics.h"
#include "products.h"

namespace priceriot {

// Forward declaration to avoid circular dependency
class StoreLayout;

/**
 * Spatial hub in the store graph. Represents entrance, exit, junction, register, or stockroom.
 * Has world coordinates (x,z) in meters and dimensions for traffic flow (personal space, dwell time).
 */
class Node {
  public:
    /** Node role in the store topology. */
    enum class NodeType { Entrance, Exit, Junction, Register, Stockroom };
    Node();
    /** @param x,z World coordinates in meters */
    Node(int nodeId, NodeType type, double x, double z,
         double length, double width, double shelfProtrusion_left, double shelfProtrusion_right,
         double blockedFraction, double personalSpaceArea, double jamDensity, double avgDwellTime,
         double serviceRate, int agentsPresent, double entryRate, double exitRate);

    int getNodeId() const noexcept {
        return nodeId;
    }
    NodeType getNodeType() const noexcept {
        return type;
    }

    // Position Getters
    double getX() const noexcept {
        return x;
    }
    double getZ() const noexcept {
        return z;
    }

    double getLength() const noexcept {
        return length;
    }
    double getWidth() const noexcept {
        return width;
    }
    double getShelfProtrusionLeft() const noexcept {
        return shelfProtrusion_left;
    }
    double getShelfProtrusionRight() const noexcept {
        return shelfProtrusion_right;
    }
    double getBlockedFraction() const noexcept {
        return blockedFraction;
    }
    double getPersonalSpaceArea() const noexcept {
        return PersonalSpaceArea;
    }
    double getJamDensity() const noexcept {
        return jamDensity;
    }
    double getAvgDwellTime() const noexcept {
        return avgDwellTime;
    }
    double getServiceRate() const noexcept {
        return serviceRate;
    }
    int getAgentsPresent() const noexcept {
        return agentsPresent;
    }
    double getEntryRate() const noexcept {
        return entryRate;
    }
    double getExitRate() const noexcept {
        return exitRate;
    }

    void setNodeType(NodeType t) noexcept;
    void setBlockedFraction(double f) noexcept;
    void setPersonalSpaceArea(double a) noexcept;
    void setJamDensity(double d) noexcept;
    void setAvgDwellTime(double s) noexcept;
    void setServiceRate(double r) noexcept;
    void setAgentsPresent(int n) noexcept;

  private:
    const int nodeId;
    NodeType type;
    double x{}, z{}; ///< World coordinates in meters
    double length{}, width{};
    double shelfProtrusion_left{}, shelfProtrusion_right{};
    double blockedFraction{};
    double PersonalSpaceArea{}; ///< Personal space area in m^2 (PascalCase retained for YAML parity)
    double jamDensity{};
    double avgDwellTime{};
    double serviceRate{};
    int agentsPresent{};
    const double entryRate{}, exitRate{};
};

/**
 * Aisle connecting two nodes. Subdivided into EdgeCells for traffic and shelf inventory.
 * Discretization (lanes, cell count) is computed from width, shelf protrusion, and personal space.
 */
class Edge {
  public:
    enum class Direction : std::uint8_t { fwd, bwd, both };
    enum class Flow : std::uint8_t { uni, bi };
    enum class AgentMask : std::uint8_t { None = 0, Customer = 1, Staff = 2, Both = 3 };

    struct EdgePolicy {
        Flow flow{Flow::bi};
        Direction orientation{Direction::fwd};
        AgentMask allowed{AgentMask::Both};
    };

    std::vector<priceriot::EdgeCell> cells{};

    inline Edge() : edgeId(-1), fromNode(-1), toNode(-1) {
        rebuildDiscretization(0.35, 0.75, 0.7);
    }

    inline Edge(int edgeId_, int fromNode_, int toNode_, double length_, double width_,
                double freeSpeed_, double jamDensity_, double blockedFraction_,
                EdgePolicy edgePolicy_)
        : edgeId(edgeId_), fromNode(fromNode_), toNode(toNode_), length(length_), width(width_),
          freeSpeed(freeSpeed_), jamDensity(jamDensity_), blockedFraction(blockedFraction_),
          edgePolicy(edgePolicy_) {
        rebuildDiscretization(0.35, 0.75, 0.7);
    }

    int getEdgeId() const noexcept {
        return edgeId;
    }
    int getFromNode() const noexcept {
        return fromNode;
    }
    int getToNode() const noexcept {
        return toNode;
    }
    double getLength() const noexcept {
        return length;
    }
    double getWidth() const noexcept {
        return width;
    }
    double getFreeSpeed() const noexcept {
        return freeSpeed;
    }
    double getJamDensity() const noexcept {
        return jamDensity;
    }
    int getAgentsOnEdge() const noexcept {
        return agentsOnEdge;
    }
    double getBlockedFraction() const noexcept {
        return blockedFraction;
    }
    Flow getFlow() const noexcept {
        return edgePolicy.flow;
    }
    Direction getOrientation() const noexcept {
        return edgePolicy.orientation;
    }
    AgentMask getAllowedAgentsMask() const noexcept {
        return edgePolicy.allowed;
    }
    double getShelfLeft() const noexcept {
        return shelfProtrusion_left;
    }
    double getShelfRight() const noexcept {
        return shelfProtrusion_right;
    }
    double getClearWidth() const noexcept {
        return clearAisleWidth_m;
    }
    int getLaneCount() const noexcept {
        return usableLanes;
    }
    int getCellCount() const noexcept {
        return cellCount;
    }
    double getCellLength() const noexcept {
        return cellLength_m;
    }
    double getPersonalRadius() const noexcept {
        return personalSpaceRadius_m;
    }

    inline void setFreeSpeed(double v) noexcept {
        freeSpeed = v;
    }
    inline void setJamDensity(double v) noexcept {
        jamDensity = v;
    }
    inline void setAgentsOnEdge(int n) noexcept {
        agentsOnEdge = n;
    }
    inline void setBlockedFraction(double f) noexcept {
        blockedFraction = (f < 0.0) ? 0.0 : (f > 1.0 ? 1.0 : f);
        rebuildDiscretization(personalSpaceRadius_m, laneWidth_m, cellLength_m);
    }
    inline void setFlow(Flow f) noexcept {
        edgePolicy.flow = f;
    }
    inline void setOrientation(Direction d) noexcept {
        edgePolicy.orientation = d;
    }
    inline void setDirection(Direction d) noexcept {
        edgePolicy.orientation = d;
    }

    void setAllowedAgentsMask(AgentMask m) noexcept;
    void allowCustomers(bool on = true) noexcept;
    void allowStaff(bool on = true) noexcept;

    void setShelfLeft(double w) {
        shelfProtrusion_left = w;
        rebuildDiscretization(personalSpaceRadius_m, laneWidth_m, cellLength_m);
    }
    void setShelfRight(double w) {
        shelfProtrusion_right = w;
        rebuildDiscretization(personalSpaceRadius_m, laneWidth_m, cellLength_m);
    }

    inline void rebuildDiscretization(double personalSpaceRadius_m_in, double laneWidth_m_in,
                                      double cellLength_m_in) noexcept {
        personalSpaceRadius_m = personalSpaceRadius_m_in;
        cellLength_m = cellLength_m_in;
        laneWidth_m = laneWidth_m_in;
        laneWidth_m = std::max(laneWidth_m, 2.0 * personalSpaceRadius_m);
        cellLength_m = std::max(cellLength_m, 2.0 * personalSpaceRadius_m);
        const double shelves = shelfProtrusion_left + shelfProtrusion_right;
        const double blocked = std::clamp(blockedFraction, 0.0, 1.0) * width;
        clearAisleWidth_m = std::max(0.0, width - shelves - blocked);
        usableLanes =
            std::max(1, static_cast<int>(std::floor(clearAisleWidth_m / laneWidth_m)));
        cellCount = std::max(1, static_cast<int>(std::floor(length / cellLength_m)));
    }

  private:
    const int edgeId;
    const int fromNode;
    const int toNode;
    double length{}, width{};
    double shelfProtrusion_left{0.0};
    double shelfProtrusion_right{0.0};
    double clearAisleWidth_m{0.0};
    double laneWidth_m{0.75};
    int cellCount{1};
    int usableLanes{1};
    double cellLength_m{0.7};
    double personalSpaceRadius_m{0.35};
    double freeSpeed{1.0};
    double jamDensity{1.0};
    int agentsOnEdge{0};
    double blockedFraction{0.0};
    EdgePolicy edgePolicy;
};

/**
 * Complete store graph: nodes, edges, adjacency, navmesh, and physics world.
 * Load from YAML via loadFromYaml(). Build navmesh/physics via buildNavMesh() and buildPhysicsWorld().
 */
class StoreGraph {
  public:
    struct Neighbor {
        int nodeIdx;
        int edgeIdx;
    };
    StoreGraph();
    StoreGraph(std::vector<std::unique_ptr<Node>> nodes, std::vector<std::unique_ptr<Edge>> edges,
               std::vector<std::vector<Neighbor>> adj, std::unordered_map<int, int> nodeIdToIndex,
               std::unordered_map<int, int> edgeIdToIndex);

    void loadFromYaml(const std::string &path);
    std::vector<int> neighbors(int nodeIdx) const;
    std::vector<Neighbor> outgoingEdges(int nodeIdx) const;
    double densityAtNode(int nodeIdx) const;
    double densityAtEdge(int edgeIdx) const;
    double traversalTime(int fromNode, int toNode) const;
    void printNodes() const;
    void printEdges() const;
    void printAdj() const;
    void printEdgeCells(int edgeIdx) const;
    void printAllEdgeCells() const;
    int numNodes() const noexcept {
        return static_cast<int>(nodes.size());
    }
    int numEdges() const noexcept {
        return static_cast<int>(edges.size());
    }
    const std::vector<std::unique_ptr<Node>> &getNodes() const noexcept {
        return nodes;
    }
    const std::vector<std::unique_ptr<Edge>> &getEdges() const noexcept {
        return edges;
    }
    const std::vector<std::vector<Neighbor>> &getAdj() const noexcept {
        return adj;
    }
    const std::unordered_map<int, int> &getNodeIdToIndex() const noexcept {
        return nodeIdToIndex;
    }
    const std::unordered_map<int, int> &getEdgeIdToIndex() const noexcept {
        return edgeIdToIndex;
    }
    const Node &nodeAt(int idx) const {
        return *nodes.at(static_cast<size_t>(idx));
    }
    const Edge &edgeAt(int idx) const {
        return *edges.at(static_cast<size_t>(idx));
    }
    /** Mutable edge access for shelf inventory updates (e.g. takeOneBySku). */
    Edge &mutableEdgeAt(int idx) {
        return *edges.at(static_cast<size_t>(idx));
    }
    int nodeIndexById(int id) const {
        return nodeIdToIndex.at(id);
    }
    int edgeIndexById(int id) const {
        return edgeIdToIndex.at(id);
    }
    bool validateTopology(bool verbose = true) const;
    void printAllEdgeCellsWithShelves(const YAML::Node &storeRoot,
                                      priceriot::Products &catalog) const;

    /** (edgeIdx, cellIdx) for every cell containing the given SKU. */
    std::vector<std::pair<int, int>> findEdgesContainingSku(int sku) const;
    /** World (x, z) of cell center; interpolates along edge from node to node. */
    std::pair<double, double> getCellCenter(int edgeIdx, int cellIdx) const;
    /** (edgeIdx, cellIdx) of the cell whose center is closest to (x,z); (-1,-1) if no cells. */
    std::pair<int, int> findClosestCell(double x, double z) const;

    // Navmesh support
    void buildNavMesh(const StoreLayout &layout);
    const NavMesh &getNavMesh() const noexcept {
        return navmesh;
    }
    bool hasNavMesh() const noexcept {
        return navmeshBuilt;
    }

    // Physics world support
    void buildPhysicsWorld(const StoreLayout &layout);
    const PhysicsWorld &getPhysicsWorld() const noexcept {
        return physicsWorld;
    }
    bool hasPhysicsWorld() const noexcept {
        return physicsWorldBuilt;
    }

    priceriot::Products catalog;

  private:
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<std::unique_ptr<Edge>> edges;
    std::vector<std::vector<Neighbor>> adj;
    std::unordered_map<int, int> nodeIdToIndex;
    std::unordered_map<int, int> edgeIdToIndex;
    void buildAdjacency();

    NavMesh navmesh;
    bool navmeshBuilt = false;

    PhysicsWorld physicsWorld;
    bool physicsWorldBuilt = false;
};

} // namespace priceriot

#endif // ENVIRONMENT_H