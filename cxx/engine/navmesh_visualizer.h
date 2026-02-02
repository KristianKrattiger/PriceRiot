#ifndef NAVMESH_VISUALIZER_H
#define NAVMESH_VISUALIZER_H

#include "../environment/environment.h"
#include "../environment/navmesh.h"
#include <SFML/Graphics.hpp>

namespace priceriot {

/**
 * Visualizer for navigation mesh debugging and visualization.
 * Renders polygons, connections, paths, and debug information.
 */
struct NavMeshVisualizer {
    // Visualization flags
    bool showPolygons = false;
    bool showConnections = false;
    bool showPaths = false;
    bool showCenters = false;

    // Colors
    sf::Color nodePolygonColor = sf::Color(100, 150, 255, 100); // Semi-transparent blue
    sf::Color edgePolygonColor = sf::Color(255, 200, 100, 100); // Semi-transparent orange
    sf::Color connectionColor = sf::Color(150, 150, 150, 150);  // Gray
    sf::Color pathColor = sf::Color(0, 255, 255, 200);          // Cyan
    sf::Color waypointColor = sf::Color(255, 0, 255, 255);      // Magenta
    sf::Color centerColor = sf::Color(255, 255, 0, 200);        // Yellow

    // Rendering constants (will be set from sim.cpp)
    float pixelsPerMeter = 20.0f;
    float offsetX = 640.0f;
    float offsetY = 400.0f;

    /**
     * Draw all navmesh polygons.
     */
    void drawPolygons(sf::RenderWindow &window, const NavMesh &navmesh);

    /**
     * Draw connections between neighboring polygons.
     */
    void drawConnections(sf::RenderWindow &window, const NavMesh &navmesh);

    /**
     * Draw a specific path (waypoints and connections).
     */
    void drawPath(sf::RenderWindow &window,
                  const std::vector<std::pair<double, double>> &waypoints);

    /**
     * Draw polygon center points.
     */
    void drawCenters(sf::RenderWindow &window, const NavMesh &navmesh);

    /**
     * Convert world coordinates to screen coordinates.
     */
    sf::Vector2f worldToScreen(double x, double z) const;
};

} // namespace priceriot

#endif // NAVMESH_VISUALIZER_H
