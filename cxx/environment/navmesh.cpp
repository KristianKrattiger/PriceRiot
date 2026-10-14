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
    for (const auto &v : vertices) {
        sumX += v.x;
        sumZ += v.z;
    }

    const auto count = static_cast<double>(vertices.size());
    return {sumX / count, sumZ / count};
}

double NavPolygon::getArea() const {
    if (vertices.size() < 3)
        return 0.0;

    double area = 0.0;
    for (size_t i = 0; i < vertices.size(); ++i) {
        size_t j = (i + 1) % vertices.size();
        area += vertices[i].x * vertices[j].z;
        area -= vertices[j].x * vertices[i].z;
    }
    return std::abs(area) / 2.0;
}

// ==================== NavMesh ====================

int NavMesh::addPolygon(const NavPolygon &polygon) {
    int index = static_cast<int>(polygons.size());
    polygons.push_back(polygon);
    // Invalidate spatial index when new polygons are added.
    spatialIndexBuilt_ = false;
    return index;
}

void NavMesh::buildSpatialIndex() const {
    spatialGrid_.clear();
    for (int i = 0; i < static_cast<int>(polygons.size()); ++i) {
        if (!polygons[i].isWalkable())
            continue;
        const auto &verts = polygons[i].getVertices();
        if (verts.empty())
            continue;
        double minX = verts[0].x, maxX = verts[0].x;
        double minZ = verts[0].z, maxZ = verts[0].z;
        for (const auto &v : verts) {
            if (v.x < minX) minX = v.x;
            if (v.x > maxX) maxX = v.x;
            if (v.z < minZ) minZ = v.z;
            if (v.z > maxZ) maxZ = v.z;
        }
        int cxMin = static_cast<int>(std::floor(minX / kGridCellSize));
        int czMin = static_cast<int>(std::floor(minZ / kGridCellSize));
        int cxMax = static_cast<int>(std::floor(maxX / kGridCellSize));
        int czMax = static_cast<int>(std::floor(maxZ / kGridCellSize));
        for (int cx = cxMin; cx <= cxMax; ++cx) {
            for (int cz = czMin; cz <= czMax; ++cz) {
                spatialGrid_[cellKey(cx, cz)].push_back(i);
            }
        }
    }
    spatialIndexBuilt_ = true;
}

std::optional<int> NavMesh::findPolygonContaining(double x, double z) const {
    if (!spatialIndexBuilt_)
        buildSpatialIndex();

    int cx = static_cast<int>(std::floor(x / kGridCellSize));
    int cz = static_cast<int>(std::floor(z / kGridCellSize));
    auto it = spatialGrid_.find(cellKey(cx, cz));
    if (it != spatialGrid_.end()) {
        for (int idx : it->second) {
            if (polygons[idx].isWalkable() && polygons[idx].containsPoint(x, z))
                return idx;
        }
    }
    return std::nullopt;
}

std::optional<int> NavMesh::findNearestPolygon(double x, double z) const {
    if (polygons.empty())
        return std::nullopt;
    if (!spatialIndexBuilt_)
        buildSpatialIndex();

    int cx = static_cast<int>(std::floor(x / kGridCellSize));
    int cz = static_cast<int>(std::floor(z / kGridCellSize));

    int nearestIdx = -1;
    double minDistSq = std::numeric_limits<double>::max();

    // Search expanding rings of grid cells until we find at least one candidate.
    for (int radius = 0; radius <= 4; ++radius) {
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                if (std::abs(dx) != radius && std::abs(dz) != radius)
                    continue; // Only the outer ring of this radius
                auto it = spatialGrid_.find(cellKey(cx + dx, cz + dz));
                if (it == spatialGrid_.end())
                    continue;
                for (int idx : it->second) {
                    if (!polygons[idx].isWalkable())
                        continue;
                    auto [cxP, czP] = polygons[idx].getCenter();
                    double distSq = distanceSquared(x, z, cxP, czP);
                    if (distSq < minDistSq) {
                        minDistSq = distSq;
                        nearestIdx = idx;
                    }
                }
            }
        }
        if (nearestIdx >= 0)
            break;
    }

    // Full linear fallback if nothing found nearby (should be rare).
    if (nearestIdx < 0) {
        for (int i = 0; i < static_cast<int>(polygons.size()); ++i) {
            if (!polygons[i].isWalkable())
                continue;
            auto [cxP, czP] = polygons[i].getCenter();
            double distSq = distanceSquared(x, z, cxP, czP);
            if (distSq < minDistSq) {
                minDistSq = distSq;
                nearestIdx = i;
            }
        }
    }

    return (nearestIdx >= 0) ? std::optional<int>(nearestIdx) : std::nullopt;
}

void NavMesh::connectPolygons(int polyIdx1, int polyIdx2) {
    if (polyIdx1 < 0 || polyIdx1 >= static_cast<int>(polygons.size()) || polyIdx2 < 0 ||
        polyIdx2 >= static_cast<int>(polygons.size())) {
        return;
    }

    polygons[static_cast<size_t>(polyIdx1)].addNeighbor(polyIdx2);
    polygons[static_cast<size_t>(polyIdx2)].addNeighbor(polyIdx1);
}

bool NavMesh::validate() const {
    // Check that all polygons have at least 3 vertices
    for (const auto &poly : polygons) {
        if (poly.getVertices().size() < 3) {
            return false;
        }
    }

    // Check neighbor connections are bidirectional
    for (size_t i = 0; i < polygons.size(); ++i) {
        const auto &neighbors = polygons[i].getNeighbors();
        for (int neighborIdx : neighbors) {
            if (neighborIdx < 0 || neighborIdx >= static_cast<int>(polygons.size())) {
                return false;
            }
            // Check reverse connection exists
            const auto &neighborNeighbors =
                polygons[static_cast<size_t>(neighborIdx)].getNeighbors();
            if (std::find(neighborNeighbors.begin(), neighborNeighbors.end(),
                          static_cast<int>(i)) == neighborNeighbors.end()) {
                return false;
            }
        }
    }

    return true;
}

// ==================== Static Helpers ====================

bool NavPolygon::pointInPolygon(const std::vector<NavPolygon::Vertex> &vertices, double x,
                                double z) {
    if (vertices.size() < 3)
        return false;

    bool inside = false;
    for (size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++) {
        const auto &vi = vertices[i];
        const auto &vj = vertices[j];

        if (((vi.z > z) != (vj.z > z)) && (x < (vj.x - vi.x) * (z - vi.z) / (vj.z - vi.z) + vi.x)) {
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
