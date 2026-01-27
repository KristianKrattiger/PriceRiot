#include "navmesh.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace priceriot {

// ==================== NavPolygon ====================

bool NavPolygon::containsPoint(double x, double z) const {
    return NavPolygon::pointInPolygon(vertices, x, z);
}

std::pair<double, double> NavPolygon::getCenter() const {
    if (vertices.empty()) {
        return {0.0, 0.0};
    }
    
    double sumX = 0.0, sumZ = 0.0;
    for (const auto& v : vertices) {
        sumX += v.x;
        sumZ += v.z;
    }
    
    const auto count = static_cast<double>(vertices.size());
    return {sumX / count, sumZ / count};
}

double NavPolygon::getArea() const {
    if (vertices.size() < 3) return 0.0;
    
    double area = 0.0;
    for (size_t i = 0; i < vertices.size(); ++i) {
        size_t j = (i + 1) % vertices.size();
        area += vertices[i].x * vertices[j].z;
        area -= vertices[j].x * vertices[i].z;
    }
    return std::abs(area) / 2.0;
}

// ==================== NavMesh ====================

int NavMesh::addPolygon(const NavPolygon& polygon) {
    int index = static_cast<int>(polygons.size());
    polygons.push_back(polygon);
    return index;
}

std::optional<int> NavMesh::findPolygonContaining(double x, double z) const {
    for (size_t i = 0; i < polygons.size(); ++i) {
        if (polygons[i].isWalkable() && polygons[i].containsPoint(x, z)) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

std::optional<int> NavMesh::findNearestPolygon(double x, double z) const {
    if (polygons.empty()) return std::nullopt;
    
    int nearestIdx = -1;
    double minDistSq = std::numeric_limits<double>::max();
    
    for (size_t i = 0; i < polygons.size(); ++i) {
        if (!polygons[i].isWalkable()) continue;
        
        auto [centerX, centerZ] = polygons[i].getCenter();
        double distSq = distanceSquared(x, z, centerX, centerZ);
        
        if (distSq < minDistSq) {
            minDistSq = distSq;
            nearestIdx = static_cast<int>(i);
        }
    }
    
    return (nearestIdx >= 0) ? std::optional<int>(nearestIdx) : std::nullopt;
}

void NavMesh::connectPolygons(int polyIdx1, int polyIdx2) {
    if (polyIdx1 < 0 || polyIdx1 >= static_cast<int>(polygons.size()) ||
        polyIdx2 < 0 || polyIdx2 >= static_cast<int>(polygons.size())) {
        return;
    }
    
    polygons[static_cast<size_t>(polyIdx1)].addNeighbor(polyIdx2);
    polygons[static_cast<size_t>(polyIdx2)].addNeighbor(polyIdx1);
}

bool NavMesh::validate() const {
    // Check that all polygons have at least 3 vertices
    for (const auto& poly : polygons) {
        if (poly.getVertices().size() < 3) {
            return false;
        }
    }
    
    // Check neighbor connections are bidirectional
    for (size_t i = 0; i < polygons.size(); ++i) {
        const auto& neighbors = polygons[i].getNeighbors();
        for (int neighborIdx : neighbors) {
            if (neighborIdx < 0 || neighborIdx >= static_cast<int>(polygons.size())) {
                return false;
            }
            // Check reverse connection exists
            const auto& neighborNeighbors = polygons[static_cast<size_t>(neighborIdx)].getNeighbors();
            if (std::find(neighborNeighbors.begin(), neighborNeighbors.end(), static_cast<int>(i)) == neighborNeighbors.end()) {
                return false;
            }
        }
    }
    
    return true;
}

// ==================== Static Helpers ====================

bool NavPolygon::pointInPolygon(const std::vector<NavPolygon::Vertex>& vertices, double x, double z) {
    if (vertices.size() < 3) return false;
    
    bool inside = false;
    for (size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++) {
        const auto& vi = vertices[i];
        const auto& vj = vertices[j];
        
        if (((vi.z > z) != (vj.z > z)) &&
            (x < (vj.x - vi.x) * (z - vi.z) / (vj.z - vi.z) + vi.x)) {
            inside = !inside;
        }
    }
    return inside;
}

double NavMesh::distanceSquared(double x1, double z1, double x2, double z2) {
    double dx = x2 - x1;
    double dz = z2 - z1;
    return dx * dx + dz * dz;
}

} // namespace priceriot
