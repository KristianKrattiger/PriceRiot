#include "navmesh_generator.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <vector>

namespace priceriot {

// Minimum node hub size for navmesh (avoids degenerate polygons when YAML has 0 width/length)
static constexpr float kNodeMinWidth = 0.5f;
static constexpr float kNodeMinLength = 0.5f;
// Inset from each side so node walkable area stays clear of shelf obstacles at junctions
// CHANGED: Increased from 0.3f to 0.6f to prevent shelf corner overlap
static constexpr float kNodeInset = 0.6f;

NavMesh NavMeshGenerator::generate(const StoreGraph &graph, const StoreLayout &layout) {
    NavMesh navmesh;

    // Defensive: layout must be built from same graph before buildNavMesh(layout)
    const auto &nodes = graph.getNodes();
    if (layout.nodeGeoms.empty() && !nodes.empty()) {
        std::cerr << "NavMeshGenerator: warning - layout.nodeGeoms is empty but graph has "
                  << nodes.size() << " nodes. Call layout.buildGeometry(store) before buildNavMesh.\n";
    }

    // Maps: nodeId -> polygon index, edgeId -> polygon indices
    std::map<int, int> nodePolygonMap;
    std::map<int, std::vector<int>> edgePolygonMap;

    // Step 1: Generate polygons for all nodes
    int nodesWithGeom = 0;
    for (const auto &node : nodes) {
        int nodeId = node->getNodeId();
        if (layout.nodeGeoms.count(nodeId)) {
            ++nodesWithGeom;
            generateNodePolygons(*node, layout.nodeGeoms.at(nodeId), navmesh, nodePolygonMap);
        }
    }

    // Step 2: Generate polygons for all edges
    const auto &edges = graph.getEdges();
    for (const auto &edge : edges) {
        int edgeId = edge->getEdgeId();
        if (layout.edgeGeoms.count(edgeId)) {
            generateEdgePolygons(*edge, layout.edgeGeoms.at(edgeId), navmesh, edgePolygonMap);
        }
    }

    // Step 3: Connect node polygons to edge polygons
    connectNodeEdgePolygons(graph, nodePolygonMap, edgePolygonMap, navmesh);

    // Diagnostic: log node polygon count when layout was incomplete
    if (nodesWithGeom < static_cast<int>(nodes.size()) && !nodes.empty()) {
        std::cerr << "NavMeshGenerator: " << nodesWithGeom << "/" << nodes.size()
                  << " nodes had layout geometry; " << nodePolygonMap.size()
                  << " node polygons created.\n";
    }

    return navmesh;
}

void NavMeshGenerator::generateNodePolygons(const Node &node, const NodeGeometry &geom,
                                            NavMesh &navmesh, std::map<int, int> &nodePolygonMap) {
    // Use minimum size so node polygon is non-degenerate and visible (avoids zero width/length)
    NodeGeometry geomClamped = geom;
    geomClamped.width = std::max(geom.width, kNodeMinWidth);
    geomClamped.length = std::max(geom.length, kNodeMinLength);

    // Inset hub toward center so walkable node area stays clear of shelf obstacles at junctions.
    // Keep half-sizes at least kNodeMin* so the polygon remains valid and portals still overlap.
    float halfW = geomClamped.width / 2.f;
    float halfL = geomClamped.length / 2.f;
    halfW = std::max(kNodeMinWidth / 2.f, halfW - kNodeInset);
    halfL = std::max(kNodeMinLength / 2.f, halfL - kNodeInset);

    // Build inset rectangle corners (same order as NodeGeometry::getCorners: TL, TR, BR, BL)
    std::vector<std::pair<float, float>> corners = {
        {geomClamped.x - halfW, geomClamped.z - halfL},
        {geomClamped.x + halfW, geomClamped.z - halfL},
        {geomClamped.x + halfW, geomClamped.z + halfL},
        {geomClamped.x - halfW, geomClamped.z + halfL},
    };

    std::vector<NavPolygon::Vertex> vertices;
    for (const auto &c : corners) {
        vertices.emplace_back(static_cast<double>(c.first), static_cast<double>(c.second));
    }

    // Create polygon
    int polygonId = navmesh.getPolygonCount();
    NavPolygon polygon(polygonId, vertices);
    polygon.setAssociatedNodeId(node.getNodeId());
    polygon.setWalkable(true);

    int polygonIdx = navmesh.addPolygon(polygon);
    nodePolygonMap[node.getNodeId()] = polygonIdx;
}

void NavMeshGenerator::generateEdgePolygons(const Edge &edge, const EdgeGeometry &geom,
                                            NavMesh &navmesh,
                                            std::map<int, std::vector<int>> &edgePolygonMap) {
    // Use the edge's calculated clear width (already accounts for shelf protrusions and blocked
    // areas)
    double clearWidth = static_cast<double>(edge.getClearWidth());
    clearWidth = std::max(0.1, clearWidth); // Minimum walkable width

    // Get walkable corners (accounting for shelf protrusions)
    auto walkableCorners = getEdgeWalkableCorners(geom, clearWidth);

    if (walkableCorners.size() >= 3) {
        // Create a single polygon for the edge
        std::vector<NavPolygon::Vertex> vertices;
        for (const auto &corner : walkableCorners) {
            vertices.emplace_back(corner.first, corner.second);
        }

        int polygonId = navmesh.getPolygonCount();
        NavPolygon polygon(polygonId, vertices);
        polygon.setAssociatedEdgeId(edge.getEdgeId());
        polygon.setWalkable(true);

        int polygonIdx = navmesh.addPolygon(polygon);
        edgePolygonMap[edge.getEdgeId()].push_back(polygonIdx);
    }
}

void NavMeshGenerator::connectNodeEdgePolygons(
    const StoreGraph &graph, const std::map<int, int> &nodePolygonMap,
    const std::map<int, std::vector<int>> &edgePolygonMap, NavMesh &navmesh) {
    // For each edge, connect its polygons to the polygons of its from/to nodes
    const auto &edges = graph.getEdges();
    for (const auto &edge : edges) {
        int fromNodeId = graph.nodeAt(edge->getFromNode()).getNodeId();
        int toNodeId = graph.nodeAt(edge->getToNode()).getNodeId();
        int edgeId = edge->getEdgeId();

        // Connect to from node
        if (nodePolygonMap.count(fromNodeId) && edgePolygonMap.count(edgeId)) {
            int nodePolyIdx = nodePolygonMap.at(fromNodeId);
            for (int edgePolyIdx : edgePolygonMap.at(edgeId)) {
                navmesh.connectPolygons(nodePolyIdx, edgePolyIdx);
            }
        }

        // Connect to to node
        if (nodePolygonMap.count(toNodeId) && edgePolygonMap.count(edgeId)) {
            int nodePolyIdx = nodePolygonMap.at(toNodeId);
            for (int edgePolyIdx : edgePolygonMap.at(edgeId)) {
                navmesh.connectPolygons(nodePolyIdx, edgePolyIdx);
            }
        }

        // Connect edge polygons to each other (for multi-segment edges)
        if (edgePolygonMap.count(edgeId)) {
            const auto &edgePolys = edgePolygonMap.at(edgeId);
            for (size_t i = 0; i < edgePolys.size(); ++i) {
                for (size_t j = i + 1; j < edgePolys.size(); ++j) {
                    navmesh.connectPolygons(edgePolys[i], edgePolys[j]);
                }
            }
        }
    }
}

NavPolygon
NavMeshGenerator::createRectPolygon(int id, const std::vector<std::pair<double, double>> &corners) {
    std::vector<NavPolygon::Vertex> vertices;
    for (const auto &corner : corners) {
        vertices.emplace_back(corner.first, corner.second);
    }
    return NavPolygon(id, vertices);
}

std::vector<std::pair<double, double>>
NavMeshGenerator::getEdgeWalkableCorners(const EdgeGeometry &geom, double clearWidth) {

    std::vector<std::pair<double, double>> corners;

    // Calculate perpendicular direction (90 degrees from edge direction)
    double perpAngle = geom.angle + 1.5708; // +90 degrees in radians
    double perpX = std::cos(perpAngle);
    double perpZ = std::sin(perpAngle);

    // Erode walkable corridor by agent radius so the polygon boundary represents
    // valid positions for the agent's CENTER, not the physical shelf face.
    // Without erosion an agent whose centre is at the polygon edge would have their
    // body (radius 0.35 m) overlapping the shelf.
    static constexpr double kAgentErosion = 0.35;
    double halfWidth = std::max(kAgentErosion * 0.5, clearWidth / 2.0 - kAgentErosion);

    // Calculate offset
    double offsetX = perpX * halfWidth;
    double offsetZ = perpZ * halfWidth;

    // Create corners of walkable rectangle
    // Start points
    corners.emplace_back(geom.startX - offsetX, geom.startZ - offsetZ);
    corners.emplace_back(geom.startX + offsetX, geom.startZ + offsetZ);

    // End points
    corners.emplace_back(geom.endX + offsetX, geom.endZ + offsetZ);
    corners.emplace_back(geom.endX - offsetX, geom.endZ - offsetZ);

    return corners;
}

} // namespace priceriot