#include "navmesh_visualizer.h"
#include <cmath>

namespace priceriot {

sf::Vector2f NavMeshVisualizer::worldToScreen(double x, double z) const {
    return sf::Vector2f(
        static_cast<float>(x) * pixelsPerMeter + offsetX,
        static_cast<float>(z) * pixelsPerMeter + offsetY
    );
}

void NavMeshVisualizer::drawPolygons(sf::RenderWindow& window, const NavMesh& navmesh) {
    if (!showPolygons) return;
    
    int polygonCount = navmesh.getPolygonCount();
    for (int i = 0; i < polygonCount; ++i) {
        const NavPolygon& polygon = navmesh.getPolygon(i);
        if (!polygon.isWalkable()) continue;
        
        const auto& vertices = polygon.getVertices();
        if (vertices.size() < 3) continue;
        
        // Create convex shape from polygon vertices
        sf::ConvexShape shape;
        shape.setPointCount(vertices.size());
        
        for (size_t j = 0; j < vertices.size(); ++j) {
            sf::Vector2f screenPos = worldToScreen(vertices[j].x, vertices[j].z);
            shape.setPoint(j, screenPos);
        }
        
        // Color based on whether it's associated with a node or edge
        if (polygon.getAssociatedNodeId() >= 0) {
            shape.setFillColor(nodePolygonColor);
            shape.setOutlineColor(sf::Color(nodePolygonColor.r, nodePolygonColor.g, nodePolygonColor.b, 255));
        } else if (polygon.getAssociatedEdgeId() >= 0) {
            shape.setFillColor(edgePolygonColor);
            shape.setOutlineColor(sf::Color(edgePolygonColor.r, edgePolygonColor.g, edgePolygonColor.b, 255));
        } else {
            // Default color for unassociated polygons
            shape.setFillColor(sf::Color(200, 200, 200, 100));
            shape.setOutlineColor(sf::Color(200, 200, 200, 255));
        }
        
        shape.setOutlineThickness(1.0f);
        window.draw(shape);
    }
}

void NavMeshVisualizer::drawConnections(sf::RenderWindow& window, const NavMesh& navmesh) {
    if (!showConnections) return;
    
    int polygonCount = navmesh.getPolygonCount();
    for (int i = 0; i < polygonCount; ++i) {
        const NavPolygon& polygon = navmesh.getPolygon(i);
        if (!polygon.isWalkable()) continue;
        
        auto [centerX, centerZ] = polygon.getCenter();
        sf::Vector2f centerScreen = worldToScreen(centerX, centerZ);
        
        // Draw lines to all neighbors
        const auto& neighbors = polygon.getNeighbors();
        for (int neighborIdx : neighbors) {
            if (neighborIdx < 0 || neighborIdx >= polygonCount) continue;
            
            const NavPolygon& neighbor = navmesh.getPolygon(neighborIdx);
            auto [neighborX, neighborZ] = neighbor.getCenter();
            sf::Vector2f neighborScreen = worldToScreen(neighborX, neighborZ);
            
            // Draw line
            sf::Vertex line[2] = {
                sf::Vertex(centerScreen, connectionColor),
                sf::Vertex(neighborScreen, connectionColor)
            };
            window.draw(line, 2, sf::Lines);
        }
    }
}

void NavMeshVisualizer::drawPath(sf::RenderWindow& window, const std::vector<std::pair<double, double>>& waypoints) {
    if (!showPaths || waypoints.size() < 2) return;
    
    // Draw lines between waypoints
    for (size_t i = 0; i < waypoints.size() - 1; ++i) {
        sf::Vector2f start = worldToScreen(waypoints[i].first, waypoints[i].second);
        sf::Vector2f end = worldToScreen(waypoints[i + 1].first, waypoints[i + 1].second);
        
        sf::Vertex line[2] = {
            sf::Vertex(start, pathColor),
            sf::Vertex(end, pathColor)
        };
        window.draw(line, 2, sf::Lines);
    }
    
    // Draw waypoint markers
    for (size_t i = 0; i < waypoints.size(); ++i) {
        sf::Vector2f pos = worldToScreen(waypoints[i].first, waypoints[i].second);
        
        sf::CircleShape waypointMarker(4.0f);
        waypointMarker.setOrigin(4.0f, 4.0f);
        waypointMarker.setPosition(pos);
        waypointMarker.setFillColor(waypointColor);
        waypointMarker.setOutlineColor(sf::Color::White);
        waypointMarker.setOutlineThickness(1.0f);
        window.draw(waypointMarker);
    }
}

void NavMeshVisualizer::drawCenters(sf::RenderWindow& window, const NavMesh& navmesh) {
    if (!showCenters) return;
    
    int polygonCount = navmesh.getPolygonCount();
    for (int i = 0; i < polygonCount; ++i) {
        const NavPolygon& polygon = navmesh.getPolygon(i);
        if (!polygon.isWalkable()) continue;
        
        auto [centerX, centerZ] = polygon.getCenter();
        sf::Vector2f centerScreen = worldToScreen(centerX, centerZ);
        
        sf::CircleShape centerMarker(2.0f);
        centerMarker.setOrigin(2.0f, 2.0f);
        centerMarker.setPosition(centerScreen);
        centerMarker.setFillColor(centerColor);
        window.draw(centerMarker);
    }
}

} // namespace priceriot
