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

        // Create obstacles for left shelf
        if (edgeGeo.shelfLeft > 0.01) {
            // Shelf extends perpendicular from edge
            double shelfWidth = 0.3; // Shelf depth (meters)
            double shelfStartX = edgeGeo.startX + perpX * (edgeGeo.width / 2.0 + shelfWidth / 2.0);
            double shelfStartZ = edgeGeo.startZ + perpZ * (edgeGeo.width / 2.0 + shelfWidth / 2.0);
            double shelfEndX = edgeGeo.endX + perpX * (edgeGeo.width / 2.0 + shelfWidth / 2.0);
            double shelfEndZ = edgeGeo.endZ + perpZ * (edgeGeo.width / 2.0 + shelfWidth / 2.0);

            // Create AABB for shelf obstacle
            AABB shelfObstacle(shelfStartX - shelfWidth / 2.0, shelfStartZ - shelfWidth / 2.0,
                               shelfEndX + shelfWidth / 2.0, shelfEndZ + shelfWidth / 2.0);
            world.addObstacle(shelfObstacle);
        }

        // Create obstacles for right shelf
        if (edgeGeo.shelfRight > 0.01) {
            double shelfWidth = 0.3;
            double shelfStartX = edgeGeo.startX - perpX * (edgeGeo.width / 2.0 + shelfWidth / 2.0);
            double shelfStartZ = edgeGeo.startZ - perpZ * (edgeGeo.width / 2.0 + shelfWidth / 2.0);
            double shelfEndX = edgeGeo.endX - perpX * (edgeGeo.width / 2.0 + shelfWidth / 2.0);
            double shelfEndZ = edgeGeo.endZ - perpZ * (edgeGeo.width / 2.0 + shelfWidth / 2.0);

            AABB shelfObstacle(shelfStartX - shelfWidth / 2.0, shelfStartZ - shelfWidth / 2.0,
                               shelfEndX + shelfWidth / 2.0, shelfEndZ + shelfWidth / 2.0);
            world.addObstacle(shelfObstacle);
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
