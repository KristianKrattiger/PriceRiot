#include "environment.h"
#include "store_init.h"
#include "store_inventory.h"
#include "products.h"
#include "shelf.h"
#include "cell.h"
#include "storeLayout.h"
#include "navmesh_generator.h"
#include "physics_generator.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <yaml-cpp/yaml.h>

// --- Helper Functions ---
template <class T>
static size_t safe_size(const T* ptr) { return ptr ? ptr->size() : 0; }

static inline std::string or_empty(const YAML::Node& n, const char* key) {
    const auto v = n[key];
    return v ? v.as<std::string>() : std::string{};
}

namespace {
    static void attach_inventory_to_cell_from_yaml(
        const YAML::Node& planRoot,
        priceriot::Catalog& catalog,
        priceriot::EdgeCell& cell,
        std::unordered_map<std::uint32_t,int>& on_shelf_sum,
        int edgeId,
        int cellIndex)
    {
        using namespace priceriot;
        ShelfSide left{}, right{};
        if (auto pog = planRoot["planogram"]) {
            if (auto edgesY = pog["edges"]) {
                if (auto edgeY = edgesY[std::to_string(edgeId)]) {
                    if (auto cellsY = edgeY["cells"]) {
                        if (cellIndex >= 0 && cellIndex < static_cast<int>(cellsY.size())) {
                            const auto& cellY = cellsY[cellIndex];
                            if (auto L = cellY["left_side"]) apply_planogram_to_side(L,  catalog, left,  on_shelf_sum);
                            if (auto R = cellY["right_side"]) apply_planogram_to_side(R, catalog, right, on_shelf_sum);
                            cell.set_left_inventory(left);
                            cell.set_right_inventory(right);
                            return;
                        }
                    }
                }
            }
        }
        if (auto L = planRoot["left_side"])  apply_planogram_to_side(L,  catalog, left,  on_shelf_sum);
        if (auto R = planRoot["right_side"]) apply_planogram_to_side(R, catalog, right, on_shelf_sum);
        cell.set_left_inventory(left);
        cell.set_right_inventory(right);
    }
}

namespace priceriot {

// ========================= Node =========================
Node::Node()
: nodeId(-1), type(Node::NodeType::Junction), x(0.0), z(0.0), length(0.0), width(0.0),
  shelfProtrusion_left(0.0), shelfProtrusion_right(0.0), blockedFraction(0.0),
  PersonalSpaceArea(0.0), jamDensity(0.0), avgDwellTime(0.0), serviceRate(0.0),
  agentsPresent(0), entryRate(0.0), exitRate(0.0)
{}

Node::Node(int nodeId_, NodeType type_,
           double x_, double z_, // <--- NEW ARGS
           double length_, double width_,
           double shelfProtrusion_left_, double shelfProtrusion_right_,
           double blockedFraction_, double personalSpaceArea_, double jamDensity_,
           double avgDwellTime_, double serviceRate_, int agentsPresent_,
           double entryRate_, double exitRate_)
: nodeId(nodeId_), type(type_),
  x(x_), z(z_), // <--- INIT
  length(length_), width(width_),
  shelfProtrusion_left(shelfProtrusion_left_), shelfProtrusion_right(shelfProtrusion_right_),
  blockedFraction(std::clamp(blockedFraction_, 0.0, 1.0)),
  PersonalSpaceArea(personalSpaceArea_), jamDensity(std::max(0.0, jamDensity_)),
  avgDwellTime(std::max(0.0, avgDwellTime_)), serviceRate(std::max(0.0, serviceRate_)),
  agentsPresent(std::max(0, agentsPresent_)), entryRate(entryRate_), exitRate(exitRate_)
{}

void Node::setNodeType(Node::NodeType t) noexcept { type = t; }
void Node::setBlockedFraction(double f) noexcept { blockedFraction = std::clamp(f, 0.0, 1.0); }
void Node::setPersonalSpaceArea(double a) noexcept { PersonalSpaceArea = a; }
void Node::setJamDensity(double d) noexcept { jamDensity = (d < 0.0) ? 0.0 : d; }
void Node::setAvgDwellTime(double s) noexcept { avgDwellTime = (s < 0.0) ? 0.0 : s; }
void Node::setServiceRate(double r) noexcept { serviceRate = (r < 0.0) ? 0.0 : r; }
void Node::setAgentsPresent(int n) noexcept { agentsPresent = (n < 0) ? 0 : n; }

// ====================== Edge & StoreGraph ======================
// (Methods omitted for brevity unless changed)

void Edge::setAllowedAgentsMask(AgentMask m) noexcept { edgePolicy.allowed = m; }
void Edge::allowCustomers(bool on) noexcept { /*...*/ }
void Edge::allowStaff(bool on) noexcept { /*...*/ }

StoreGraph::StoreGraph() : nodes(), edges(), adj(), nodeIdToIndex(), edgeIdToIndex() {}
StoreGraph::StoreGraph(std::vector<std::unique_ptr<Node>> nodes_, std::vector<std::unique_ptr<Edge>> edges_,
                       std::vector<std::vector<Neighbor>> adj_, std::unordered_map<int,int> nodeIdToIndex_,
                       std::unordered_map<int,int> edgeIdToIndex_)
: nodes(std::move(nodes_)), edges(std::move(edges_)), adj(std::move(adj_)),
  nodeIdToIndex(std::move(nodeIdToIndex_)), edgeIdToIndex(std::move(edgeIdToIndex_)) {}

double StoreGraph::densityAtEdge(int e) const { /*...*/ return 0.0; } // Stubbed for brevity
double StoreGraph::densityAtNode(int nodeIdx) const { /*...*/ return 0.0; }
double StoreGraph::traversalTime(int fromNode, int toNode) const { /*...*/ return 0.0; }
void StoreGraph::buildAdjacency() { /*...*/ }
bool StoreGraph::validateTopology(bool verbose) const { /*...*/ return true; }

void StoreGraph::loadFromYaml(const std::string& path) {
    nodes.clear(); edges.clear(); adj.clear();
    nodeIdToIndex.clear(); edgeIdToIndex.clear();
    catalog = priceriot::Products();

    YAML::Node root;
    try {
        std::cout << "Loading store YAML: " << path << "\n";
        root = YAML::LoadFile(path);
        if (!root) throw std::runtime_error("Failed to load YAML: " + path);

        // ----- NODES -----
        const YAML::Node nodesY = root["nodes"];
        if (!nodesY || !nodesY.IsSequence()) throw std::runtime_error("'nodes' must be a sequence");

        nodes.reserve(nodesY.size());
        std::unordered_set<int> seenNodeIds;

        for (const auto& n : nodesY) {
            const int id = n["id"].as<int>();
            const auto typeStr = n["type"].as<std::string>();

            if (!seenNodeIds.insert(id).second) throw std::runtime_error("Duplicate node id");

            const int idx = static_cast<int>(nodes.size());
            nodeIdToIndex[id] = idx;

            nodes.push_back(std::make_unique<Node>(
                id,
                (typeStr == "Entrance") ? Node::NodeType::Entrance :
                (typeStr == "Exit")     ? Node::NodeType::Exit :
                (typeStr == "Register") ? Node::NodeType::Register :
                (typeStr == "Stockroom")? Node::NodeType::Stockroom :
                                           Node::NodeType::Junction,
                n["x"] ? n["x"].as<double>() : 0.0, // <--- LOAD X
                n["z"] ? n["z"].as<double>() : 0.0, // <--- LOAD Z (Mapped to Y in YAML if user prefers, but Z in code)
                n["length"] ? n["length"].as<double>() : 0.0,
                n["width"]  ? n["width"].as<double>()  : 0.0,
                n["shelf_left"] ? n["shelf_left"].as<double>() : 0.0,
                n["shelf_right"]? n["shelf_right"].as<double>() : 0.0,
                std::clamp(n["blocked"] ? n["blocked"].as<double>() : 0.0, 0.0, 1.0),
                n["personal_space"] ? n["personal_space"].as<double>() : 1.0,
                n["jam_density"] ? n["jam_density"].as<double>() : 3.5,
                n["dwell_s"] ? n["dwell_s"].as<double>() : 0.0,
                n["service_rate"] ? n["service_rate"].as<double>() : 0.0,
                n["agents"] ? n["agents"].as<int>() : 0,
                n["entry_rate"] ? n["entry_rate"].as<double>() : 0.0,
                n["exit_rate"]  ? n["exit_rate"].as<double>()  : 0.0
            ));
        }

        // ----- EDGES -----
        const YAML::Node edgesY = root["edges"];
        edges.reserve(edgesY.size());
        std::unordered_set<int> seenEdgeIds;

        for (const auto& e : edgesY) {
            const int id = e["id"].as<int>();
            const int fromId = e["from"].as<int>();
            const int toId = e["to"].as<int>();
            // ... (rest of edge loading same as before)
            if (!seenEdgeIds.insert(id).second) throw std::runtime_error("Duplicate edge id");
            auto itFrom = nodeIdToIndex.find(fromId);
            auto itTo = nodeIdToIndex.find(toId);
            if (itFrom == nodeIdToIndex.end() || itTo == nodeIdToIndex.end()) throw std::runtime_error("Edge references unknown node id");

            const int fromIdx = itFrom->second;
            const int toIdx = itTo->second;
            std::string flowStr = e["flow"] ? e["flow"].as<std::string>() : "bi";
            std::string orntStr = e["orientation"] ? e["orientation"].as<std::string>() : "fwd";
            Edge::Flow flow = (flowStr == "bi") ? Edge::Flow::bi : Edge::Flow::uni;
            Edge::Direction ornt = (orntStr == "fwd") ? Edge::Direction::fwd : Edge::Direction::bwd;
            Edge::EdgePolicy policy{flow, ornt, Edge::AgentMask::Both};

            auto edgePtr = std::make_unique<Edge>(
                id, fromIdx, toIdx,
                e["length"] ? e["length"].as<double>() : 0.0,
                e["width"] ? e["width"].as<double>() : 0.0,
                e["free_speed"] ? e["free_speed"].as<double>() : 1.2,
                e["jam_density"] ? e["jam_density"].as<double>() : 3.5,
                std::clamp(e["blocked"] ? e["blocked"].as<double>() : 0.0, 0.0, 1.0),
                policy
            );
            edgePtr->setShelfLeft(e["shelf_left"] ? e["shelf_left"].as<double>() : 0.0);
            edgePtr->setShelfRight(e["shelf_right"] ? e["shelf_right"].as<double>() : 0.0);
            const int eidx = static_cast<int>(edges.size());
            edgeIdToIndex[id] = eidx;
            edges.push_back(std::move(edgePtr));
        }
        buildAdjacency();
        validateTopology(true);
    } catch (const YAML::BadFile& e) {
        throw std::runtime_error(std::string("YAML bad file: ") + e.what());
    } catch (const YAML::ParserException& e) {
        throw std::runtime_error(std::string("YAML parse error: ") + e.what());
    }

    // Inventory Loading (Unchanged)
    priceriot::InventoryPool backroom;
    std::filesystem::path storePath(path);
    std::filesystem::path baseDir = storePath.has_parent_path() ? storePath.parent_path() : std::filesystem::current_path();
    std::string productsFile = "store_products.yaml";
    if (auto pf = root["products_file"]; pf && pf.IsScalar()) productsFile = pf.as<std::string>();
    std::filesystem::path productsPath = baseDir / productsFile;
    YAML::Node productsRoot = YAML::LoadFile(productsPath.string());
    std::unordered_map<std::string,int> totals_by_sku = load_products_from_yaml(productsRoot, this->catalog);
    std::unordered_map<std::uint32_t,int> on_shelf_sum;

    for (int e = 0; e < static_cast<int>(edges.size()); ++e) {
        Edge& E = *edges[e];
        const int nCells = E.getCellCount();
        const double d_cell = E.getCellLength();
        const double r_pers = E.getPersonalRadius();
        E.cells.clear();
        E.cells.reserve(nCells);
        for (int c = 0; c < nCells; ++c) {
            priceriot::EdgeCell cell(E.getEdgeId()*1000 + c, d_cell, r_pers);
            attach_inventory_to_cell_from_yaml(root, this->catalog, cell, on_shelf_sum, E.getEdgeId(), c);
            E.cells.push_back(std::move(cell));
        }
    }
    compute_and_load_backstock(root, this->catalog, totals_by_sku, on_shelf_sum, backroom);
}

// Queries/Prints (Omitted for brevity, unchanged)
std::vector<int> StoreGraph::neighbors(const int nodeIdx) const { return {}; }
std::vector<StoreGraph::Neighbor> StoreGraph::outgoingEdges(int nodeIdx) const { return {}; }
void StoreGraph::printNodes() const {}
void StoreGraph::printEdges() const {}
void StoreGraph::printAdj() const {}
void StoreGraph::printEdgeCells(int edgeIdx) const {}
void StoreGraph::printAllEdgeCells() const {}

void StoreGraph::buildNavMesh(const StoreLayout& layout) {
    navmesh = NavMeshGenerator::generate(*this, layout);
    navmeshBuilt = true;
}

void StoreGraph::buildPhysicsWorld(const StoreLayout& layout) {
    physicsWorld = PhysicsGenerator::generate(*this, layout);
    physicsWorldBuilt = true;
}
void StoreGraph::printAllEdgeCellsWithShelves(const YAML::Node& storeRoot, priceriot::Products& catalog) const {}

} // namespace priceriot