#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <yaml-cpp/yaml.h>

#include "cell.h"
#include "products.h"

namespace priceriot {

// ------------------ Node ------------------
class Node {
public:
    enum class NodeType { Entrance, Exit, Junction, Register, Stockroom };
    Node();
    Node(int nodeId, NodeType type,
         double x, double z, // <--- NEW: World Coordinates (Meters)
         double length, double width,
         double shelfProtrusion_left, double shelfProtrusion_right,
         double blockedFraction, double personalSpaceArea, double jamDensity,
         double avgDwellTime, double serviceRate, int agentsPresent,
         double entryRate, double exitRate);

    int        getNodeId() const noexcept { return nodeId; }
    NodeType   getNodeType() const noexcept { return type; }

    // Position Getters
    double getX() const noexcept { return x; }
    double getZ() const noexcept { return z; }

    double getLength() const noexcept { return length; }
    double getWidth()  const noexcept { return width;  }
    double getShelfProtrusionLeft()  const noexcept { return shelfProtrusion_left;  }
    double getShelfProtrusionRight() const noexcept { return shelfProtrusion_right; }
    double getBlockedFraction() const noexcept { return blockedFraction; }
    double getPersonalSpaceArea() const noexcept { return PersonalSpaceArea; }
    double getJamDensity()        const noexcept { return jamDensity; }
    double getAvgDwellTime() const noexcept { return avgDwellTime; }
    double getServiceRate()  const noexcept { return serviceRate;  }
    int    getAgentsPresent() const noexcept { return agentsPresent; }
    double getEntryRate()     const noexcept { return entryRate; }
    double getExitRate()      const noexcept { return exitRate;  }

    void setNodeType(NodeType t) noexcept;
    void setBlockedFraction(double f) noexcept;
    void setPersonalSpaceArea(double a) noexcept;
    void setJamDensity(double d) noexcept;
    void setAvgDwellTime(double s) noexcept;
    void setServiceRate(double r) noexcept;
    void setAgentsPresent(int n) noexcept;

private:
    const int nodeId;
    NodeType  type;
    double x{}, z{}; // <--- NEW
    double length{}, width{};
    double shelfProtrusion_left{}, shelfProtrusion_right{};
    double blockedFraction{};
    double PersonalSpaceArea{};
    double jamDensity{};
    double avgDwellTime{};
    double serviceRate{};
    int    agentsPresent{};
    const double entryRate{}, exitRate{};
};

// ------------------ Edge ------------------
// (Unchanged, provided for context)
class Edge {
public:
    enum class Direction : std::uint8_t { fwd, bwd, both };
    enum class Flow : std::uint8_t { uni, bi };
    enum class AgentMask : std::uint8_t { None = 0, Customer = 1, Staff = 2, Both = 3 };

    struct EdgePolicy {
        Flow      flow{Flow::bi};
        Direction orientation{Direction::fwd};
        AgentMask allowed{AgentMask::Both};
    };

    std::vector<priceriot::EdgeCell> cells{};

    inline Edge() : edgeId(-1), fromNode(-1), toNode(-1) { rebuildDiscretization(0.35, 0.75, 0.7); }

    inline Edge(int edgeId_, int fromNode_, int toNode_, double length_, double width_,
                double freeSpeed_, double jamDensity_, double blockedFraction_, EdgePolicy edgePolicy_)
        : edgeId(edgeId_), fromNode(fromNode_), toNode(toNode_), length(length_), width(width_),
          freeSpeed(freeSpeed_), jamDensity(jamDensity_), blockedFraction(blockedFraction_), edgePolicy(edgePolicy_) {
        rebuildDiscretization(0.35, 0.75, 0.7);
    }

    int    getEdgeId()   const noexcept { return edgeId; }
    int    getFromNode() const noexcept { return fromNode; }
    int    getToNode()   const noexcept { return toNode;   }
    double getLength() const noexcept { return length; }
    double getWidth()  const noexcept { return width;  }
    double getFreeSpeed()       const noexcept { return freeSpeed; }
    double getJamDensity()      const noexcept { return jamDensity; }
    int    getAgentsOnEdge()    const noexcept { return agentsOnEdge; }
    double getBlockedFraction() const noexcept { return blockedFraction; }
    Flow      getFlow()        const noexcept { return edgePolicy.flow; }
    Direction getOrientation() const noexcept { return edgePolicy.orientation; }
    AgentMask getAllowedAgentsMask() const noexcept { return edgePolicy.allowed;  }
    double getShelfLeft()   const noexcept { return shelfProtrusion_left;  }
    double getShelfRight()  const noexcept { return shelfProtrusion_right; }
    double getClearWidth()  const noexcept { return w_clear; }
    int    getLaneCount()   const noexcept { return usableLanes; }
    int    getCellCount()   const noexcept { return cellCount; }
    double getCellLength()  const noexcept { return d_cell; }
    double getPersonalRadius() const noexcept { return r_personal; }

    inline void setFreeSpeed(double v) noexcept { freeSpeed = v; }
    inline void setJamDensity(double v) noexcept { jamDensity = v; }
    inline void setAgentsOnEdge(int n) noexcept { agentsOnEdge = n; }
    inline void setBlockedFraction(double f) noexcept {
        blockedFraction = (f < 0.0) ? 0.0 : (f > 1.0 ? 1.0 : f);
        rebuildDiscretization(r_personal, w_lane, d_cell);
    }
    inline void setFlow(Flow f) noexcept { edgePolicy.flow = f; }
    inline void setOrientation(Direction d) noexcept { edgePolicy.orientation = d; }
    inline void setDirection(Direction d) noexcept { edgePolicy.orientation = d; }

    void setAllowedAgentsMask(AgentMask m) noexcept;
    void allowCustomers(bool on = true) noexcept;
    void allowStaff(bool on = true) noexcept;

    void setShelfLeft(double w) { shelfProtrusion_left = w; rebuildDiscretization(r_personal, w_lane, d_cell); }
    void setShelfRight(double w) { shelfProtrusion_right = w; rebuildDiscretization(r_personal, w_lane, d_cell); }

    inline void rebuildDiscretization(double r_personal_in, double w_lane_in, double d_cell_in) noexcept {
        r_personal = r_personal_in;
        d_cell     = d_cell_in;
        w_lane     = w_lane_in;
        w_lane = std::max(w_lane, 2.0 * r_personal);
        d_cell = std::max(d_cell, 2.0 * r_personal);
        const double shelves = shelfProtrusion_left + shelfProtrusion_right;
        const double blocked = std::clamp(blockedFraction, 0.0, 1.0) * width;
        w_clear = std::max(0.0, width - shelves - blocked);
        usableLanes = std::max(1, static_cast<int>(std::floor(w_clear / w_lane)));
        cellCount   = std::max(1, static_cast<int>(std::floor(length  / d_cell)));
    }

private:
    const int edgeId;
    const int fromNode;
    const int toNode;
    double length{}, width{};
    double shelfProtrusion_left{0.0};
    double shelfProtrusion_right{0.0};
    double w_clear{0.0};
    double w_lane{0.75};
    int    cellCount{1};
    int    usableLanes{1};
    double d_cell{0.7};
    double r_personal{0.35};
    double freeSpeed{1.0};
    double jamDensity{1.0};
    int    agentsOnEdge{0};
    double blockedFraction{0.0};
    EdgePolicy edgePolicy;
};

// ------------------ StoreGraph ------------------
class StoreGraph {
public:
    struct Neighbor { int nodeIdx; int edgeIdx; };
    StoreGraph();
    StoreGraph(std::vector<std::unique_ptr<Node>> nodes,
               std::vector<std::unique_ptr<Edge>> edges,
               std::vector<std::vector<Neighbor>> adj,
               std::unordered_map<int,int> nodeIdToIndex,
               std::unordered_map<int,int> edgeIdToIndex);

    void loadFromYaml(const std::string& path);
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
    int numNodes() const noexcept { return static_cast<int>(nodes.size()); }
    int numEdges() const noexcept { return static_cast<int>(edges.size()); }
    const std::vector<std::unique_ptr<Node>>& getNodes() const noexcept { return nodes; }
    const std::vector<std::unique_ptr<Edge>>& getEdges() const noexcept { return edges; }
    const std::vector<std::vector<Neighbor>>& getAdj()   const noexcept { return adj; }
    const std::unordered_map<int,int>& getNodeIdToIndex() const noexcept { return nodeIdToIndex; }
    const std::unordered_map<int,int>& getEdgeIdToIndex() const noexcept { return edgeIdToIndex; }
    const Node& nodeAt(int idx) const { return *nodes.at(static_cast<size_t>(idx)); }
    const Edge& edgeAt(int idx) const { return *edges.at(static_cast<size_t>(idx)); }
    int nodeIndexById(int id) const { return nodeIdToIndex.at(id); }
    int edgeIndexById(int id) const { return edgeIdToIndex.at(id); }
    bool validateTopology(bool verbose = true) const;
    void printAllEdgeCellsWithShelves(const YAML::Node& storeRoot, priceriot::Products& catalog) const;

    priceriot::Products catalog;

private:
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<std::unique_ptr<Edge>> edges;
    std::vector<std::vector<Neighbor>> adj;
    std::unordered_map<int,int> nodeIdToIndex;
    std::unordered_map<int,int> edgeIdToIndex;
    void buildAdjacency();
};

} // namespace priceriot

#endif // ENVIRONMENT_H