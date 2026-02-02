#ifndef PHYSICS_GENERATOR_H
#define PHYSICS_GENERATOR_H

#include "environment.h"
#include "physics.h"
#include "store_layout.h"

namespace priceriot {

/**
 * Generates physical world obstacles from store layout.
 * Creates AABB obstacles for shelves and boundaries.
 */
class PhysicsGenerator {
  public:
    /**
     * Generate physics world from store graph and layout.
     * Creates obstacles from shelf protrusions and store boundaries.
     */
    static PhysicsWorld generate(const StoreGraph &graph, const StoreLayout &layout);

  private:
    /**
     * Generate obstacles from edge shelf protrusions.
     * Creates AABB obstacles along edges where shelves protrude.
     */
    static void generateShelfObstacles(const StoreGraph &graph, const StoreLayout &layout,
                                       PhysicsWorld &world);

    /**
     * Generate store boundaries (walls) from layout bounding box.
     */
    static void generateBoundaries(const StoreLayout &layout, PhysicsWorld &world,
                                   double wallThickness = 0.5);
};

} // namespace priceriot

#endif // PHYSICS_GENERATOR_H
