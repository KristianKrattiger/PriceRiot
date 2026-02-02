#include "collision_manager.h"
#include "../agents/customer.h"
#include <algorithm>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace priceriot {

void CollisionManager::registerAgent(Customer *agent, double radius) {
    if (!agent)
        return;

    // Check if already registered
    auto it = std::find_if(registeredAgents.begin(), registeredAgents.end(),
                           [agent](const AgentInfo &info) { return info.agent == agent; });

    if (it == registeredAgents.end()) {
        registeredAgents.emplace_back(agent, radius);
    } else {
        it->radius = radius; // Update radius
    }
}

void CollisionManager::unregisterAgent(Customer *agent) {
    registeredAgents.erase(
        std::remove_if(registeredAgents.begin(), registeredAgents.end(),
                       [agent](const AgentInfo &info) { return info.agent == agent; }),
        registeredAgents.end());
}

void CollisionManager::clear() {
    registeredAgents.clear();
}

void CollisionManager::resolveCollisions(Customer *agent, double radius) {
    if (!agent)
        return;

    double agentX = agent->getPosX();
    double agentZ = agent->getPosZ();

    // Check collisions with other agents
    for (const auto &other : registeredAgents) {
        if (other.agent == agent)
            continue;

        double otherX = other.agent->getPosX();
        double otherZ = other.agent->getPosZ();

        if (circlesIntersect(agentX, agentZ, radius, otherX, otherZ, other.radius)) {
            // Calculate separation vector
            double dx = agentX - otherX;
            double dz = agentZ - otherZ;
            double distSq = dx * dx + dz * dz;

            if (distSq < 1e-6) {
                // Agents are exactly on top of each other, push apart randomly
                dx = 0.1;
                dz = 0.1;
                distSq = 0.02;
            }

            double dist = std::sqrt(distSq);
            double overlap = (radius + other.radius) - dist;

            if (overlap > 0) {
                // Normalize direction
                double nx = dx / dist;
                double nz = dz / dist;

                // Push both agents apart (equal and opposite)
                double pushAmount = overlap / 2.0;
                agent->setPosition(agentX + nx * pushAmount, agentZ + nz * pushAmount);
                other.agent->setPosition(otherX - nx * pushAmount, otherZ - nz * pushAmount);
            }
        }
    }

    // Check collisions with obstacles
    if (physicsWorld) {
        Circle agentCircle(agentX, agentZ, radius);
        if (physicsWorld->checkCollision(agentCircle)) {
            physicsWorld->resolveCollision(agentCircle);
            agent->setPosition(agentCircle.x, agentCircle.z);
        }
    }
}

void CollisionManager::getAvoidanceVector(double x, double z, double radius, double &avoidX,
                                          double &avoidZ, double maxDistance) const {
    avoidX = 0.0;
    avoidZ = 0.0;

    double totalWeight = 0.0;

    // Avoid other agents
    for (const auto &other : registeredAgents) {
        double otherX = other.agent->getPosX();
        double otherZ = other.agent->getPosZ();

        double dx = x - otherX;
        double dz = z - otherZ;
        double distSq = dx * dx + dz * dz;
        double dist = std::sqrt(distSq);

        if (dist < 1e-6 || dist > maxDistance)
            continue;

        // Calculate repulsion strength (stronger when closer)
        double combinedRadius = radius + other.radius;
        if (dist < combinedRadius) {
            // Already colliding, strong repulsion
            double weight = 1.0 / (dist + 0.1);
            avoidX += (dx / dist) * weight;
            avoidZ += (dz / dist) * weight;
            totalWeight += weight;
        } else {
            // Within avoidance range
            double avoidanceRange = maxDistance;
            double weight = (avoidanceRange - dist) / avoidanceRange;
            weight = weight * weight; // Quadratic falloff
            avoidX += (dx / dist) * weight;
            avoidZ += (dz / dist) * weight;
            totalWeight += weight;
        }
    }

    // Avoid obstacles from physics world
    if (physicsWorld) {
        Circle agentCircle(x, z, radius);

        // Check nearby obstacles and boundaries
        const auto &obstacles = physicsWorld->getObstacles();
        const auto &boundaries = physicsWorld->getBoundaries();

        // Sample points around agent to detect nearby obstacles
        const int samples = 8;
        const double sampleRadius = radius + 0.5; // Check slightly beyond agent radius

        for (int i = 0; i < samples; ++i) {
            double angle = (2.0 * M_PI * i) / samples;
            double sampleX = x + std::cos(angle) * sampleRadius;
            double sampleZ = z + std::sin(angle) * sampleRadius;

            Circle sampleCircle(sampleX, sampleZ, radius * 0.5);

            // Check obstacles
            for (const auto &obstacle : obstacles) {
                if (obstacle.intersects(sampleCircle)) {
                    // Calculate avoidance vector away from obstacle
                    double closestX = std::clamp(x, obstacle.minX, obstacle.maxX);
                    double closestZ = std::clamp(z, obstacle.minZ, obstacle.maxZ);
                    double dx = x - closestX;
                    double dz = z - closestZ;
                    double distSq = dx * dx + dz * dz;

                    if (distSq < 1e-6) {
                        // Agent is inside obstacle, push out
                        dx = std::cos(angle);
                        dz = std::sin(angle);
                        distSq = 1.0;
                    }

                    double dist = std::sqrt(distSq);
                    double weight = 2.0 / (dist + 0.1); // Stronger weight for obstacles
                    avoidX += (dx / dist) * weight;
                    avoidZ += (dz / dist) * weight;
                    totalWeight += weight;
                    break; // Only count each obstacle once per sample
                }
            }

            // Check boundaries
            for (const auto &boundary : boundaries) {
                if (boundary.intersects(sampleCircle)) {
                    double closestX = std::clamp(x, boundary.minX, boundary.maxX);
                    double closestZ = std::clamp(z, boundary.minZ, boundary.maxZ);
                    double dx = x - closestX;
                    double dz = z - closestZ;
                    double distSq = dx * dx + dz * dz;

                    if (distSq < 1e-6) {
                        dx = std::cos(angle);
                        dz = std::sin(angle);
                        distSq = 1.0;
                    }

                    double dist = std::sqrt(distSq);
                    double weight = 2.0 / (dist + 0.1);
                    avoidX += (dx / dist) * weight;
                    avoidZ += (dz / dist) * weight;
                    totalWeight += weight;
                    break;
                }
            }
        }
    }

    // Normalize
    if (totalWeight > 0) {
        double mag = std::sqrt(avoidX * avoidX + avoidZ * avoidZ);
        if (mag > 1e-6) {
            avoidX /= mag;
            avoidZ /= mag;
        }
    }
}

bool CollisionManager::wouldCollideWithAgents(double x, double z, double radius,
                                              const Customer *excludeAgent) const {
    for (const auto &other : registeredAgents) {
        if (other.agent == excludeAgent)
            continue;

        if (circlesIntersect(x, z, radius, other.agent->getPosX(), other.agent->getPosZ(),
                             other.radius)) {
            return true;
        }
    }
    return false;
}

std::vector<Customer *> CollisionManager::getNearbyAgents(double x, double z, double radius) const {
    std::vector<Customer *> nearby;

    for (const auto &other : registeredAgents) {
        double dx = other.agent->getPosX() - x;
        double dz = other.agent->getPosZ() - z;
        double distSq = dx * dx + dz * dz;
        double maxDist = radius * 2.0; // Detection radius

        if (distSq <= maxDist * maxDist) {
            nearby.push_back(other.agent);
        }
    }

    return nearby;
}

bool CollisionManager::circlesIntersect(double x1, double z1, double r1, double x2, double z2,
                                        double r2) const {
    double dx = x2 - x1;
    double dz = z2 - z1;
    double distSq = dx * dx + dz * dz;
    double combinedRadius = r1 + r2;
    return distSq <= (combinedRadius * combinedRadius);
}

} // namespace priceriot
