#ifndef NAVMESH_GENERATOR_H
#define NAVMESH_GENERATOR_H

#include "navmesh.h"
#include "storeLayout.h"
#include "environment.h"

namespace priceriot {

/**
 * Generates a navigation mesh from a StoreGraph.
 * Converts nodes and edges into walkable polygons.
 */
class NavMeshGenerator {
public:
    /**
     * Generate a navmesh from the store graph and layout.
     * @param graph The store graph containing nodes and edges
     * @param layout The pre-built geometry layout
     * @return The generated navmesh
     */
    static NavMesh generate(const StoreGraph& graph, const StoreLayout& layout);

private:
    /**
     * Generate polygons for a node hub.
     * Creates a rectangular polygon from the node's corners.
     */
    static void generateNodePolygons(const Node& node, const NodeGeometry& geom,
                                     NavMesh& navmesh, std::map<int, int>& nodePolygonMap);
    
    /**
     * Generate polygons for an edge (aisle).
     * Creates polygons along the edge accounting for shelf protrusions and clear width.
     */
    static void generateEdgePolygons(const Edge& edge, const EdgeGeometry& geom,
                                      NavMesh& navmesh, std::map<int, std::vector<int>>& edgePolygonMap);
    
    /**
     * Connect node polygons to adjacent edge polygons.
     * Ensures continuous walkable surface at junctions.
     */
    static void connectNodeEdgePolygons(const StoreGraph& graph,
                                        const std::map<int, int>& nodePolygonMap,
                                        const std::map<int, std::vector<int>>& edgePolygonMap,
                                        NavMesh& navmesh);
    
    /**
     * Create a rectangular polygon from corners.
     * Helper function for creating node and edge polygons.
     */
    static NavPolygon createRectPolygon(int id, const std::vector<std::pair<double, double>>& corners);
    
    /**
     * Create edge polygons accounting for clear width (shelf protrusions).
     * Returns corners of the walkable area.
     */
    static std::vector<std::pair<double, double>> getEdgeWalkableCorners(
        const EdgeGeometry& geom, double clearWidth);
};

} // namespace priceriot

#endif // NAVMESH_GENERATOR_H
