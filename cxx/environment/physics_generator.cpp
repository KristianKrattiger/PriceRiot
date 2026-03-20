#include "physics_generator.h"
#include <algorithm>
#include <cmath>

namespace priceriot {

PhysicsWorld PhysicsGenerator::generate(const StoreGraph &graph, const StoreLayout &layout) {
    PhysicsWorld world;

    // Generate shelf obstacles
    generateShelfObstacles(graph, layout, world);

    // Generate store boundaries
    generateBoundaries(layout, world);

    return world;
}

void PhysicsGenerator::generateShelfObstacles(const StoreGraph &graph, const StoreLayout &layout,
                                              PhysicsWorld &world) {
    const auto &edges = graph.getEdges();

    for (const auto &edge : edges) {
        int edgeId = edge->getEdgeId();
        if (!layout.edgeGeoms.count(edgeId))
            continue;

        const auto &edgeGeo = layout.edgeGeoms.at(edgeId);

        // Skip if no shelf protrusions
        if (edgeGeo.shelfLeft < 0.01 && edgeGeo.shelfRight < 0.01)
            continue;

        // Calculate edge direction
        double dx = edgeGeo.endX - edgeGeo.startX;
        double dz = edgeGeo.endZ - edgeGeo.startZ;
        double edgeLength = std::sqrt(dx * dx + dz * dz);
        if (edgeLength < 0.01)
            continue;

        double dirX = dx / edgeLength;
        double dirZ = dz / edgeLength;

        // Perpendicular direction (for shelf placement)
        double perpX = -dirZ; // 90 degree rotation
        double perpZ = dirX;

        // Trim shelf ends inward so they don't protrude into junction/node areas.
        // Previously the AABB was expanded ±shelfWidth/2 in ALL directions, causing a 0.15 m
        // overhang past each edge endpoint that agents would clip when turning at junctions.
        // We now trim by kNodeInset (matching the navmesh node polygon inset) so the shelf
        // runs only through the actual aisle corridor, not into the crossing area.
        static constexpr double kShelfDepth   = 0.3;  // shelf unit depth (metres)
        static constexpr double kShelfEndTrim = 1.0;  // > kNodeInset(0.6): extra clearance at junctions

        // Trimmed edge endpoints (shelf starts/ends kShelfEndTrim inside each node).
        const double trimSx = edgeGeo.startX + dirX * kShelfEndTrim;
        const double trimSz = edgeGeo.startZ + dirZ * kShelfEndTrim;
        const double trimEx = edgeGeo.endX   - dirX * kShelfEndTrim;
        const double trimEz = edgeGeo.endZ   - dirZ * kShelfEndTrim;

        // Only create a shelf if there is still length left after trimming.
        const bool hasTrimmedLength = (edgeLength > 2.0 * kShelfEndTrim + 0.1);

        // Helper lambda: build an AABB from the 4 corners of a shelf strip so the
        // rectangle is correct for any aisle angle (no spurious end-cap expansion).
        auto makeShelfAABB = [&](double side) -> AABB {
            // side = +1 for left (+perp), -1 for right (-perp)
            double p0x = trimSx + perpX * side * (edgeGeo.width / 2.0);
            double p0z = trimSz + perpZ * side * (edgeGeo.width / 2.0);
            double p1x = trimSx + perpX * side * (edgeGeo.width / 2.0 + kShelfDepth);
            double p1z = trimSz + perpZ * side * (edgeGeo.width / 2.0 + kShelfDepth);
            double p2x = trimEx + perpX * side * (edgeGeo.width / 2.0);
            double p2z = trimEz + perpZ * side * (edgeGeo.width / 2.0);
            double p3x = trimEx + perpX * side * (edgeGeo.width / 2.0 + kShelfDepth);
            double p3z = trimEz + perpZ * side * (edgeGeo.width / 2.0 + kShelfDepth);
            return AABB(std::min({p0x, p1x, p2x, p3x}), std::min({p0z, p1z, p2z, p3z}),
                        std::max({p0x, p1x, p2x, p3x}), std::max({p0z, p1z, p2z, p3z}));
        };

        if (hasTrimmedLength) {
            if (edgeGeo.shelfLeft  > 0.01) world.addObstacle(makeShelfAABB(+1.0));
            if (edgeGeo.shelfRight > 0.01) world.addObstacle(makeShelfAABB(-1.0));
        }
    }
}

void PhysicsGenerator::generateBoundaries(const StoreLayout &layout, PhysicsWorld &world,
                                          double wallThickness) {
    float minX, maxX, minZ, maxZ;
    layout.getBoundingBox(minX, maxX, minZ, maxZ);

    // Add padding for walls
    double padding = 2.0;
    minX -= padding;
    maxX += padding;
    minZ -= padding;
    maxZ += padding;

    // Create four boundary walls
    // Top wall
    world.addBoundary(AABB(minX, minZ, maxX, minZ + wallThickness));
    // Bottom wall
    world.addBoundary(AABB(minX, maxZ - wallThickness, maxX, maxZ));
    // Left wall
    world.addBoundary(AABB(minX, minZ, minX + wallThickness, maxZ));
    // Right wall
    world.addBoundary(AABB(maxX - wallThickness, minZ, maxX, maxZ));
}

} // namespace priceriot
