/**
 * @file physics.h
 * @brief Physics world: obstacles (AABBs) and boundaries for collision.
 *
 * PhysicsWorld holds shelf obstacles and store boundaries. CollisionManager uses it
 * for agent-agent separation and obstacle avoidance.
 */
#ifndef PHYSICS_H
#define PHYSICS_H

#include <algorithm>
#include <cmath>
#include <vector>

namespace priceriot {

/**
 * 2D Circle for agent collision representation
 */
struct Circle {
    double x, z;
    double radius;

    Circle(double x_, double z_, double r) : x(x_), z(z_), radius(r) {}

    bool contains(double px, double pz) const {
        double dx = px - x;
        double dz = pz - z;
        return (dx * dx + dz * dz) <= (radius * radius);
    }

    double distanceSquared(double px, double pz) const {
        double dx = px - x;
        double dz = pz - z;
        return dx * dx + dz * dz;
    }

    double distance(double px, double pz) const {
        return std::sqrt(distanceSquared(px, pz));
    }
};

/**
 * 2D Axis-Aligned Bounding Box for obstacles
 */
struct AABB {
    double minX, maxX;
    double minZ, maxZ;

    AABB(double x1, double z1, double x2, double z2) {
        minX = std::min(x1, x2);
        maxX = std::max(x1, x2);
        minZ = std::min(z1, z2);
        maxZ = std::max(z1, z2);
    }

    bool contains(double px, double pz) const {
        return px >= minX && px <= maxX && pz >= minZ && pz <= maxZ;
    }

    bool intersects(const Circle &circle) const {
        // Find closest point on AABB to circle center
        double closestX = std::clamp(circle.x, minX, maxX);
        double closestZ = std::clamp(circle.z, minZ, maxZ);

        // Check if closest point is within circle
        double dx = circle.x - closestX;
        double dz = circle.z - closestZ;
        return (dx * dx + dz * dz) <= (circle.radius * circle.radius);
    }
};

/**
 * Physical world representation with obstacles and boundaries
 */
class PhysicsWorld {
  public:
    PhysicsWorld() = default;

    // Obstacle management
    void addObstacle(const AABB &obstacle);
    void addBoundary(const AABB &boundary); // Store boundaries (walls)
    void clear();

    // Collision queries
    bool checkCollision(const Circle &agent) const;
    bool checkCollision(double x, double z, double radius) const;

    // Get collision normal and penetration depth for resolution
    bool getCollisionInfo(const Circle &agent, double &normalX, double &normalZ,
                          double &penetration) const;

    // Find nearest valid position (push out of obstacles)
    void resolveCollision(Circle &agent) const;

    // Check if position is valid (not colliding)
    bool isValidPosition(double x, double z, double radius) const;

    // Get all obstacles (for visualization/debugging)
    const std::vector<AABB> &getObstacles() const {
        return obstacles;
    }
    const std::vector<AABB> &getBoundaries() const {
        return boundaries;
    }

  private:
    std::vector<AABB> obstacles;  // Store fixtures (shelves, displays, etc.)
    std::vector<AABB> boundaries; // Store walls/boundaries

    bool checkCollisionWithObstacle(const Circle &agent, const AABB &obstacle, double &normalX,
                                    double &normalZ, double &penetration) const;
};

} // namespace priceriot

#endif // PHYSICS_H
