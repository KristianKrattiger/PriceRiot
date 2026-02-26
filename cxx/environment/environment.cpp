#include "environment.h"
#include "cell.h"
#include "navmesh_generator.h"
#include "physics_generator.h"
#include "products.h"
#include "shelf.h"
#include "store_layout.h"
#include "store_init.h"
#include "store_inventory.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <yaml-cpp/yaml.h>
#include <type_traits>
#include <chrono>

// --- Helper Functions ---
template <class T> static size_t safe_size(const T *ptr) {
    return ptr ? ptr->size() : 0;
}

static inline std::string or_empty(const YAML::Node &n, const char *key) {
    const auto v = n[key];
    return v ? v.as<std::string>() : std::string{};
}

namespace {
constexpr bool kAgentLogEnabled = true;
struct NullLogStream {
    NullLogStream(const char *, std::ios_base::openmode) {}
    explicit operator bool() const { return false; }
    template <typename T> NullLogStream &operator<<(const T &) { return *this; }
};
using AgentLogStream = std::conditional_t<kAgentLogEnabled, std::ofstream, NullLogStream>;

static void attach_inventory_to_cell_from_yaml(const YAML::Node &planRoot,
                                               priceriot::Catalog &catalog,
                                               priceriot::EdgeCell &cell,
                                               std::unordered_map<std::uint32_t, int> &onShelfQuantitySum,
                                               int edgeId, int cellIndex,
                                               bool skipDefaultPlanogram) {
    using namespace priceriot;
    ShelfSide left{}, right{};
    // #region agent log
    static bool firstLog = true;
    if (firstLog) {
        AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app);
        if(lf) lf << "{\"hypothesisId\":\"C\",\"location\":\"environment.cpp:attach_inventory\",\"message\":\"Planogram loading started\",\"data\":{\"hasRoot\":true},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
        firstLog = false;
    }
    // #endregion
    if (auto pog = planRoot["planogram"]) {
        if (auto edgesY = pog["edges"]) {
            if (auto edgeY = edgesY[std::to_string(edgeId)]) {
                // #region agent log
                { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"C\",\"location\":\"environment.cpp:planogram_found\",\"message\":\"Found planogram for edge\",\"data\":{\"edgeId\":" << edgeId << ",\"cellIndex\":" << cellIndex << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                // #endregion
                if (auto cellsY = edgeY["cells"]) {
                    if (cellIndex >= 0 && cellIndex < static_cast<int>(cellsY.size())) {
                        const auto &cellY = cellsY[cellIndex];
                        if (auto L = cellY["left_side"])
                            apply_planogram_to_side(L, catalog, left, onShelfQuantitySum);
                        if (auto R = cellY["right_side"])
                            apply_planogram_to_side(R, catalog, right, onShelfQuantitySum);
                        cell.set_left_inventory(left);
                        cell.set_right_inventory(right);
                        return;
                    }
                }
                // Edge-level planogram (no cells): apply to all cells of this edge
                if (auto L = edgeY["left_side"])
                    apply_planogram_to_side(L, catalog, left, onShelfQuantitySum);
                if (auto R = edgeY["right_side"])
                    apply_planogram_to_side(R, catalog, right, onShelfQuantitySum);
                cell.set_left_inventory(left);
                cell.set_right_inventory(right);
                return;
            } else {
                // #region agent log
                { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"C\",\"location\":\"environment.cpp:planogram_not_found\",\"message\":\"No planogram for edge - using fallback\",\"data\":{\"edgeId\":" << edgeId << ",\"cellIndex\":" << cellIndex << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                // #endregion
            }
        }
    }
    if (skipDefaultPlanogram) {
        cell.set_left_inventory(left);
        cell.set_right_inventory(right);
        return;
    }
    if (auto L = planRoot["left_side"])
        apply_planogram_to_side(L, catalog, left, onShelfQuantitySum);
    if (auto R = planRoot["right_side"])
        apply_planogram_to_side(R, catalog, right, onShelfQuantitySum);
    // Fallback: if no planogram from YAML, put first catalog product on left so cells have stock
    const auto &pm = catalog.getProductsMap();
    if (!pm.empty() && !planRoot["left_side"] && !planRoot["right_side"]) {
        const int firstSku = pm.begin()->first;
        left.bay_count = 1;
        left.bays[0].face_count = 1;
        left.bays[0].faces[0].slot_count = 1;
        left.bays[0].faces[0].slots[0].sku_id = static_cast<std::uint32_t>(firstSku);
        left.bays[0].faces[0].slots[0].qty_on_face = 10;
        onShelfQuantitySum[static_cast<std::uint32_t>(firstSku)] += 10;
    }
    cell.set_left_inventory(left);
    cell.set_right_inventory(right);
}
} // namespace

namespace priceriot {

// ========================= Node =========================
Node::Node()
    : nodeId(-1), type(Node::NodeType::Junction), x(0.0), z(0.0), length(0.0), width(0.0),
      shelfProtrusion_left(0.0), shelfProtrusion_right(0.0), blockedFraction(0.0),
      PersonalSpaceArea(0.0), jamDensity(0.0), avgDwellTime(0.0), serviceRate(0.0),
      agentsPresent(0), entryRate(0.0), exitRate(0.0) {}

Node::Node(int nodeId_, NodeType type_, double x_, double z_,
           double length_, double width_, double shelfProtrusion_left_,
           double shelfProtrusion_right_, double blockedFraction_, double personalSpaceArea_,
           double jamDensity_, double avgDwellTime_, double serviceRate_, int agentsPresent_,
           double entryRate_, double exitRate_)
    : nodeId(nodeId_), type(type_), x(x_), z(z_),
      length(length_), width(width_), shelfProtrusion_left(shelfProtrusion_left_),
      shelfProtrusion_right(shelfProtrusion_right_),
      blockedFraction(std::clamp(blockedFraction_, 0.0, 1.0)),
      PersonalSpaceArea(personalSpaceArea_), jamDensity(std::max(0.0, jamDensity_)),
      avgDwellTime(std::max(0.0, avgDwellTime_)), serviceRate(std::max(0.0, serviceRate_)),
      agentsPresent(std::max(0, agentsPresent_)), entryRate(entryRate_), exitRate(exitRate_) {}

void Node::setNodeType(Node::NodeType t) noexcept {
    type = t;
}
void Node::setBlockedFraction(double f) noexcept {
    blockedFraction = std::clamp(f, 0.0, 1.0);
}
void Node::setPersonalSpaceArea(double a) noexcept {
    PersonalSpaceArea = a;
}
void Node::setJamDensity(double d) noexcept {
    jamDensity = (d < 0.0) ? 0.0 : d;
}
void Node::setAvgDwellTime(double s) noexcept {
    avgDwellTime = (s < 0.0) ? 0.0 : s;
}
void Node::setServiceRate(double r) noexcept {
    serviceRate = (r < 0.0) ? 0.0 : r;
}
void Node::setAgentsPresent(int n) noexcept {
    agentsPresent = (n < 0) ? 0 : n;
}

// --- Section: Edge policy and StoreGraph YAML loading ---

void Edge::setAllowedAgentsMask(AgentMask m) noexcept {
    edgePolicy.allowed = m;
}
void Edge::allowCustomers(bool on) noexcept { /*...*/ }
void Edge::allowStaff(bool on) noexcept { /*...*/ }

StoreGraph::StoreGraph() : nodes(), edges(), adj(), nodeIdToIndex(), edgeIdToIndex() {}
StoreGraph::StoreGraph(std::vector<std::unique_ptr<Node>> nodes_,
                       std::vector<std::unique_ptr<Edge>> edges_,
                       std::vector<std::vector<Neighbor>> adj_,
                       std::unordered_map<int, int> nodeIdToIndex_,
                       std::unordered_map<int, int> edgeIdToIndex_)
    : nodes(std::move(nodes_)), edges(std::move(edges_)), adj(std::move(adj_)),
      nodeIdToIndex(std::move(nodeIdToIndex_)), edgeIdToIndex(std::move(edgeIdToIndex_)) {}

double StoreGraph::densityAtEdge(int e) const { /*...*/
    return 0.0;
}                                                     // Stubbed for brevity
double StoreGraph::densityAtNode(int nodeIdx) const { /*...*/
    return 0.0;
}
double StoreGraph::traversalTime(int fromNode, int toNode) const { /*...*/
    return 0.0;
}
void StoreGraph::buildAdjacency() { /*...*/ }
bool StoreGraph::validateTopology(bool verbose) const { /*...*/
    return true;
}

void StoreGraph::loadFromYaml(const std::string &path) {
    nodes.clear();
    edges.clear();
    adj.clear();
    nodeIdToIndex.clear();
    edgeIdToIndex.clear();
    catalog = priceriot::Products();
    
    // #region agent log
    { 
        std::filesystem::path absPath = std::filesystem::absolute(path);
        AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); 
        if(lf) lf << "{\"location\":\"environment.cpp:loadFromYaml\",\"message\":\"Loading YAML file\",\"data\":{\"path\":\"" << path << "\",\"absolutePath\":\"" << absPath.string() << "\"},\"timestamp\":0}\n"; 
    }
    // #endregion

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
        if (!root)
            throw std::runtime_error("Failed to load YAML: " + path);

        // ----- NODES -----
        const YAML::Node nodesY = root["nodes"];
        if (!nodesY || !nodesY.IsSequence())
            throw std::runtime_error("'nodes' must be a sequence");

        nodes.reserve(nodesY.size());
        std::unordered_set<int> seenNodeIds;

        for (const auto &n : nodesY) {
            const int id = n["id"].as<int>();
            const auto typeStr = n["type"].as<std::string>();

            if (!seenNodeIds.insert(id).second)
                throw std::runtime_error("Duplicate node id");

            const int idx = static_cast<int>(nodes.size());
            nodeIdToIndex[id] = idx;

            nodes.push_back(std::make_unique<Node>(
                id,
                (typeStr == "Entrance")    ? Node::NodeType::Entrance
                : (typeStr == "Exit")      ? Node::NodeType::Exit
                : (typeStr == "Register")  ? Node::NodeType::Register
                : (typeStr == "Stockroom") ? Node::NodeType::Stockroom
                                           : Node::NodeType::Junction,
                n["x"] ? n["x"].as<double>() : 0.0,
                n["z"] ? n["z"].as<double>()
                       : 0.0,
                n["length"] ? n["length"].as<double>() : 0.0,
                n["width"] ? n["width"].as<double>() : 0.0,
                n["shelf_left"] ? n["shelf_left"].as<double>() : 0.0,
                n["shelf_right"] ? n["shelf_right"].as<double>() : 0.0,
                std::clamp(n["blocked"] ? n["blocked"].as<double>() : 0.0, 0.0, 1.0),
                n["personal_space"] ? n["personal_space"].as<double>() : 1.0,
                n["jam_density"] ? n["jam_density"].as<double>() : 3.5,
                n["dwell_s"] ? n["dwell_s"].as<double>() : 0.0,
                n["service_rate"] ? n["service_rate"].as<double>() : 0.0,
                n["agents"] ? n["agents"].as<int>() : 0,
                n["entry_rate"] ? n["entry_rate"].as<double>() : 0.0,
                n["exit_rate"] ? n["exit_rate"].as<double>() : 0.0));
        }

        // ----- EDGES -----
        const YAML::Node edgesY = root["edges"];
        edges.reserve(edgesY.size());
        std::unordered_set<int> seenEdgeIds;
        
        // #region agent log
        { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"location\":\"environment.cpp:loadEdges\",\"message\":\"Loading edges\",\"data\":{\"edgesYSize\":" << edgesY.size() << "},\"timestamp\":0}\n"; }
        // #endregion

        for (const auto &e : edgesY) {
            const int id = e["id"].as<int>();
            const int fromId = e["from"].as<int>();
            const int toId = e["to"].as<int>();
            
            // #region agent log
            { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"location\":\"environment.cpp:loadEdge\",\"message\":\"Loading edge\",\"data\":{\"id\":" << id << ",\"from\":" << fromId << ",\"to\":" << toId << "},\"timestamp\":0}\n"; }
            // #endregion
            
            // ... (rest of edge loading same as before)
            if (!seenEdgeIds.insert(id).second)
                throw std::runtime_error("Duplicate edge id");
            auto itFrom = nodeIdToIndex.find(fromId);
            auto itTo = nodeIdToIndex.find(toId);
            if (itFrom == nodeIdToIndex.end() || itTo == nodeIdToIndex.end())
                throw std::runtime_error("Edge references unknown node id");

            const int fromIdx = itFrom->second;
            const int toIdx = itTo->second;
            std::string flowStr = e["flow"] ? e["flow"].as<std::string>() : "bi";
            std::string orntStr = e["orientation"] ? e["orientation"].as<std::string>() : "fwd";
            Edge::Flow flow = (flowStr == "bi") ? Edge::Flow::bi : Edge::Flow::uni;
            Edge::Direction ornt = (orntStr == "fwd") ? Edge::Direction::fwd : Edge::Direction::bwd;
            Edge::EdgePolicy policy{flow, ornt, Edge::AgentMask::Both};

            auto edgePtr = std::make_unique<Edge>(
                id, fromIdx, toIdx, e["length"] ? e["length"].as<double>() : 0.0,
                e["width"] ? e["width"].as<double>() : 0.0,
                e["free_speed"] ? e["free_speed"].as<double>() : 1.2,
                e["jam_density"] ? e["jam_density"].as<double>() : 3.5,
                std::clamp(e["blocked"] ? e["blocked"].as<double>() : 0.0, 0.0, 1.0), policy);
            edgePtr->setShelfLeft(e["shelf_left"] ? e["shelf_left"].as<double>() : 0.0);
            edgePtr->setShelfRight(e["shelf_right"] ? e["shelf_right"].as<double>() : 0.0);
            const int eidx = static_cast<int>(edges.size());
            edgeIdToIndex[id] = eidx;
            edges.push_back(std::move(edgePtr));
        }
        buildAdjacency();
        validateTopology(true);
    } catch (const YAML::BadFile &e) {
        throw std::runtime_error(std::string("YAML bad file: ") + e.what());
    } catch (const YAML::ParserException &e) {
        throw std::runtime_error(std::string("YAML parse error: ") + e.what());
    }

    // --- Section: Inventory and planogram loading ---
    priceriot::InventoryPool backroom;
    std::filesystem::path storePath(path);
    std::filesystem::path baseDir =
        storePath.has_parent_path() ? storePath.parent_path() : std::filesystem::current_path();
    std::string productsFile = "store_products.yaml";
    if (auto pf = root["products_file"]; pf && pf.IsScalar())
        productsFile = pf.as<std::string>();
    std::filesystem::path productsPath = baseDir / productsFile;
    std::unordered_map<std::string, int> totals_by_sku;
    if (std::filesystem::exists(productsPath)) {
        YAML::Node productsRoot = YAML::LoadFile(productsPath.string());
        totals_by_sku = load_products_from_yaml(productsRoot, this->catalog);
    } else {
        this->catalog.addProduct(1, "Placeholder", 0.0, "misc", 0.0);
    }
    std::unordered_map<std::uint32_t, int> onShelfQuantitySum;

    for (int e = 0; e < static_cast<int>(edges.size()); ++e) {
        Edge &E = *edges[e];
        const int fromNodeIdx = E.getFromNode();
        const int toNodeIdx = E.getToNode();
        const bool skipDefaultPlanogram =
            (nodeAt(fromNodeIdx).getNodeType() == Node::NodeType::Entrance ||
             nodeAt(toNodeIdx).getNodeType() == Node::NodeType::Exit);
        const int nCells = E.getCellCount();
        const double cellLength_m = E.getCellLength();
        const double personalRadius_m = E.getPersonalRadius();
        E.cells.clear();
        E.cells.reserve(nCells);
        for (int c = 0; c < nCells; ++c) {
            priceriot::EdgeCell cell(E.getEdgeId() * 1000 + c, cellLength_m, personalRadius_m);
            attach_inventory_to_cell_from_yaml(root, this->catalog, cell, onShelfQuantitySum,
                                               E.getEdgeId(), c, skipDefaultPlanogram);

            // #region agent log
            if ((E.getShelfLeft() > 0.0 || E.getShelfRight() > 0.0) && c == 0) {
                int leftBays = cell.get_left().bay_count;
                int rightBays = cell.get_right().bay_count;
                int leftQty = 0;
                int rightQty = 0;
                for (std::uint8_t b = 0; b < cell.get_left().bay_count; ++b) {
                    const auto &bay = cell.get_left().bays[b];
                    for (std::uint8_t f = 0; f < bay.face_count; ++f) {
                        const auto &face = bay.faces[f];
                        for (std::uint8_t sl = 0; sl < face.slot_count; ++sl) {
                            leftQty += face.slots[sl].qty_on_face;
                        }
                    }
                }
                for (std::uint8_t b = 0; b < cell.get_right().bay_count; ++b) {
                    const auto &bay = cell.get_right().bays[b];
                    for (std::uint8_t f = 0; f < bay.face_count; ++f) {
                        const auto &face = bay.faces[f];
                        for (std::uint8_t sl = 0; sl < face.slot_count; ++sl) {
                            rightQty += face.slots[sl].qty_on_face;
                        }
                    }
                }
                AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app);
                if (lf) {
                    lf << "{\"hypothesisId\":\"SHELF\",\"location\":\"environment.cpp:edge_inventory\","
                          "\"message\":\"Edge inventory summary (first cell)\","
                          "\"data\":{\"edgeId\":" << E.getEdgeId()
                       << ",\"cellIndex\":" << c
                       << ",\"cellCount\":" << nCells
                       << ",\"leftBays\":" << leftBays
                       << ",\"rightBays\":" << rightBays
                       << ",\"leftQty\":" << leftQty
                       << ",\"rightQty\":" << rightQty
                       << "},\"timestamp\":"
                       << std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count()
                       << "}\n";
                }
            }
            // #endregion

            E.cells.push_back(std::move(cell));
        }
    }
    compute_and_load_backstock(root, this->catalog, totals_by_sku, onShelfQuantitySum, backroom);
}

// Queries/Prints (Omitted for brevity, unchanged)
std::vector<int> StoreGraph::neighbors(const int nodeIdx) const {
    return {};
}
std::vector<StoreGraph::Neighbor> StoreGraph::outgoingEdges(int nodeIdx) const {
    return {};
}
void StoreGraph::printNodes() const {}
void StoreGraph::printEdges() const {}
void StoreGraph::printAdj() const {}
void StoreGraph::printEdgeCells(int edgeIdx) const {}
void StoreGraph::printAllEdgeCells() const {}

void StoreGraph::buildNavMesh(const StoreLayout &layout) {
    navmesh = NavMeshGenerator::generate(*this, layout);
    navmeshBuilt = true;
}

void StoreGraph::buildPhysicsWorld(const StoreLayout &layout) {
    physicsWorld = PhysicsGenerator::generate(*this, layout);
    physicsWorldBuilt = true;
}
void StoreGraph::printAllEdgeCellsWithShelves(const YAML::Node &storeRoot,
                                              priceriot::Products &catalog) const {}

std::vector<std::pair<int, int>> StoreGraph::findEdgesContainingSku(int sku) const {
    std::vector<std::pair<int, int>> out;
    const auto skuU = static_cast<std::uint32_t>(sku);
    for (int e = 0; e < numEdges(); ++e) {
        const Edge &edge = edgeAt(e);
        for (int c = 0; c < static_cast<int>(edge.cells.size()); ++c) {
            if (edge.cells[static_cast<size_t>(c)].containsSku(skuU))
                out.emplace_back(e, c);
        }
    }
    return out;
}

std::pair<double, double> StoreGraph::getCellCenter(int edgeIdx, int cellIdx) const {
    const Edge &edge = edgeAt(edgeIdx);
    const int n = edge.getCellCount();
    const double cellLength_m = edge.getCellLength();
    const double len = edge.getLength();
    if (n <= 0 || len <= 0.0)
        return {0.0, 0.0};
    const int c = std::max(0, std::min(cellIdx, n - 1));
    const double frac = ((c + 0.5) * cellLength_m) / len;
    const Node &from = nodeAt(edge.getFromNode());
    const Node &to = nodeAt(edge.getToNode());
    const double x = from.getX() + (to.getX() - from.getX()) * frac;
    const double z = from.getZ() + (to.getZ() - from.getZ()) * frac;
    return {x, z};
}

std::pair<double, double> StoreGraph::getStallPosition(int edgeIdx, int cellIdx, bool leftSide) const {
    auto [cx, cz] = getCellCenter(edgeIdx, cellIdx);
    if (cx == 0.0 && cz == 0.0)
        return {0.0, 0.0};

    const Edge &edge = edgeAt(edgeIdx);
    const Node &from = nodeAt(edge.getFromNode());
    const Node &to = nodeAt(edge.getToNode());

    // Edge direction (normalized)
    double dx = to.getX() - from.getX();
    double dz = to.getZ() - from.getZ();
    double len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-9)
        return {cx, cz};
    dx /= len;
    dz /= len;

    // Perpendicular direction: left = (-dz, dx), right = (dz, -dx)
    double perpX = leftSide ? -dz : dz;
    double perpZ = leftSide ? dx : -dx;

    // Offset from centerline toward the shelf side.
    // Each side's correct offset is: halfWidth - thatSide'sProtrusion - personalRadius.
    // Using the average clearWidth/2 would be wrong when shelf protrusions are asymmetric.
    double personalRadius = edge.getPersonalRadius();
    double halfWidth = edge.getWidth() / 2.0;
    double offset = leftSide
        ? halfWidth - edge.getShelfLeft()  - personalRadius
        : halfWidth - edge.getShelfRight() - personalRadius;
    if (offset < 0.1)
        offset = 0.1; // Minimum offset to avoid centerline overlap

    double stallX = cx + perpX * offset;
    double stallZ = cz + perpZ * offset;
    return {stallX, stallZ};
}

std::pair<int, int> StoreGraph::findClosestCell(double x, double z) const {
    int bestEdge = -1;
    int bestCell = -1;
    double bestDistSq = 1e99;
    for (int e = 0; e < numEdges(); ++e) {
        const Edge &edge = edgeAt(e);
        const int n = edge.getCellCount();
        if (n <= 0)
            continue;
        for (int c = 0; c < n; ++c) {
            auto [cx, cz] = getCellCenter(e, c);
            double dx = x - cx, dz = z - cz;
            double d2 = dx * dx + dz * dz;
            if (d2 < bestDistSq) {
                bestDistSq = d2;
                bestEdge = e;
                bestCell = c;
            }
        }
    }
    return {bestEdge, bestCell};
}

} // namespace priceriot