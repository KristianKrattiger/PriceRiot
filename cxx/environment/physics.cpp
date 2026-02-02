#include "physics.h"
#include <limits>

namespace priceriot {

void PhysicsWorld::addObstacle(const AABB &obstacle) {
    obstacles.push_back(obstacle);
}

void PhysicsWorld::addBoundary(const AABB &boundary) {
    boundaries.push_back(boundary);
}

void PhysicsWorld::clear() {
    obstacles.clear();
    boundaries.clear();
}

bool PhysicsWorld::checkCollision(const Circle &agent) const {
    // Check against obstacles
    for (const auto &obstacle : obstacles) {
        if (obstacle.intersects(agent)) {
            return true;
        }
    }

    // Check against boundaries
    for (const auto &boundary : boundaries) {
        if (boundary.intersects(agent)) {
            return true;
        }
    }

    return false;
}

bool PhysicsWorld::checkCollision(double x, double z, double radius) const {
    return checkCollision(Circle(x, z, radius));
}

bool PhysicsWorld::getCollisionInfo(const Circle &agent, double &normalX, double &normalZ,
                                    double &penetration) const {
    double bestPenetration = std::numeric_limits<double>::max();
    double bestNormalX = 0.0, bestNormalZ = 0.0;
    bool foundCollision = false;

    // Check obstacles
    for (const auto &obstacle : obstacles) {
        double nx, nz, pen;
        if (checkCollisionWithObstacle(agent, obstacle, nx, nz, pen)) {
            if (pen < bestPenetration) {
                bestPenetration = pen;
                bestNormalX = nx;
                bestNormalZ = nz;
                foundCollision = true;
            }
        }
    }

    // Check boundaries
    for (const auto &boundary : boundaries) {
        double nx, nz, pen;
        if (checkCollisionWithObstacle(agent, boundary, nx, nz, pen)) {
            if (pen < bestPenetration) {
                bestPenetration = pen;
                bestNormalX = nx;
                bestNormalZ = nz;
                foundCollision = true;
            }
        }
    }

    if (foundCollision) {
        normalX = bestNormalX;
        normalZ = bestNormalZ;
        penetration = bestPenetration;
    }

    return foundCollision;
}

bool PhysicsWorld::checkCollisionWithObstacle(const Circle &agent, const AABB &obstacle,
                                              double &normalX, double &normalZ,
                                              double &penetration) const {
    if (!obstacle.intersects(agent)) {
        return false;
    }

    // Find closest point on AABB to circle center
    double closestX = std::clamp(agent.x, obstacle.minX, obstacle.maxX);
    double closestZ = std::clamp(agent.z, obstacle.minZ, obstacle.maxZ);

    // Calculate vector from closest point to circle center
    double dx = agent.x - closestX;
    double dz = agent.z - closestZ;
    double distSq = dx * dx + dz * dz;

    if (distSq > agent.radius * agent.radius) {
        return false; // Not actually colliding
    }

    // Calculate penetration and normal
    double dist = std::sqrt(distSq);
    if (dist < 1e-6) {
        // Circle center is inside AABB, push out in shortest direction
        double distToMinX = agent.x - obstacle.minX;
        double distToMaxX = obstacle.maxX - agent.x;
        double distToMinZ = agent.z - obstacle.minZ;
        double distToMaxZ = obstacle.maxZ - agent.z;

        double minDist = std::min({distToMinX, distToMaxX, distToMinZ, distToMaxZ});

        if (minDist == distToMinX) {
            normalX = -1.0;
            normalZ = 0.0;
            penetration = agent.radius + distToMinX;
        } else if (minDist == distToMaxX) {
            normalX = 1.0;
            normalZ = 0.0;
            penetration = agent.radius + distToMaxX;
        } else if (minDist == distToMinZ) {
            normalX = 0.0;
            normalZ = -1.0;
            penetration = agent.radius + distToMinZ;
        } else {
            normalX = 0.0;
            normalZ = 1.0;
            penetration = agent.radius + distToMaxZ;
        }
    } else {
        // Normalize direction
        normalX = dx / dist;
        normalZ = dz / dist;
        penetration = agent.radius - dist;
    }

    return true;
}

void PhysicsWorld::resolveCollision(Circle &agent) const {
    double normalX, normalZ, penetration;
    if (getCollisionInfo(agent, normalX, normalZ, penetration)) {
        // Push agent out of collision
        agent.x += normalX * penetration;
        agent.z += normalZ * penetration;
    }
}

bool PhysicsWorld::isValidPosition(double x, double z, double radius) const {
    return !checkCollision(x, z, radius);
}

} // namespace priceriot
