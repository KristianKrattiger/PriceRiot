#include "collision_manager.h"
#include "../agents/customer.h"
#include "../agents/customer_behavior.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

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

/** If PRICERIOT_DEBUG_COLLISION_LOG is set, return its value as the log path; else nullptr. */
static const char *getCollisionDebugLogPath() {
    const char *p = std::getenv("PRICERIOT_DEBUG_COLLISION_LOG");
    return (p && *p) ? p : nullptr;
}

/**
 * Helper: Determine if an agent is in a "stationary picking" state that should resist displacement.
 * Returns true if agent is browsing/shopping at a shelf (high resistance to being pushed).
 */
static bool isAgentPicking(const Customer *agent) {
    if (!agent || !agent->getBehavior())
        return false;

    // Check behavior state name
    const char *stateName = agent->getBehavior()->getStateName();

    // Agent is picking if in "Browsing" or "MissionBrowse" state AND has dwell ticks
    // (dwellTicks > 0 means they're standing at a shelf picking items)
    if ((strcmp(stateName, "Browsing") == 0 || strcmp(stateName, "MissionBrowse") == 0)
        && agent->getDwellTicks() > 0) {
        return true;
    }

    return false;
}

void CollisionManager::resolveCollisions(Customer *agent, double radius) {
    if (!agent)
        return;

    double agentX = agent->getPosX();
    double agentZ = agent->getPosZ();

    // Resolve obstacle collisions first so agent-agent separation never pushes into shelves.
    // (Previously agent-agent ran first and could push an agent into an obstacle.)
    if (physicsWorld) {
        Circle agentCircle(agentX, agentZ, radius);
        if (physicsWorld->checkCollision(agentCircle)) {
            physicsWorld->resolveCollision(agentCircle);
            if (const char *logPath = getCollisionDebugLogPath()) {
                std::ofstream lf(logPath, std::ios::app);
                if (lf)
                    lf << "{\"hypothesisId\":\"H2\",\"location\":\"collision_manager.cpp:obstacle_push\",\"message\":\"Agent pushed out of obstacle\",\"data\":{\"customerId\":"
                       << agent->getId() << ",\"posBeforeX\":" << agentX << ",\"posBeforeZ\":" << agentZ
                       << ",\"posAfterX\":" << agentCircle.x << ",\"posAfterZ\":" << agentCircle.z
                       << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
            }
            agent->setPosition(agentCircle.x, agentCircle.z);
            agentX = agentCircle.x;
            agentZ = agentCircle.z;
        }
    }

    // Check if this agent is picking (should resist being pushed)
    bool agentIsPicking = isAgentPicking(agent);

    // Check collisions with other agents (using position after obstacle resolve)
    int overlapCount = 0;
    for (const auto &other : registeredAgents) {
        if (other.agent == agent)
            continue;

        double otherX = other.agent->getPosX();
        double otherZ = other.agent->getPosZ();

        if (circlesIntersect(agentX, agentZ, radius, otherX, otherZ, other.radius)) {
            overlapCount++;
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

                // *** KEY FIX: State-based mass ratio ***
                // Picking agents get much higher "mass" - they barely move
                // Moving agents take most of the displacement
                bool otherIsPicking = isAgentPicking(other.agent);

                double agentMassRatio;
                if (agentIsPicking && otherIsPicking) {
                    // Both picking: equal split (rare case)
                    agentMassRatio = 0.5;
                } else if (agentIsPicking) {
                    // This agent is picking: it barely moves (10% of displacement)
                    agentMassRatio = 0.1;
                } else if (otherIsPicking) {
                    // Other agent is picking: this agent takes most displacement (90%)
                    agentMassRatio = 0.9;
                } else {
                    // Neither picking: equal split
                    agentMassRatio = 0.5;
                }

                // Push both agents apart with mass consideration
                double pushAmount = overlap / 2.0;

                // Clamp push so neither ends up inside an obstacle
                if (physicsWorld) {
                    double lo = 0.0, hi = pushAmount;
                    for (int iter = 0; iter < 16; ++iter) {
                        double mid = (lo + hi) * 0.5;
                        double newAx = agentX + nx * mid * (2.0 * agentMassRatio);
                        double newAz = agentZ + nz * mid * (2.0 * agentMassRatio);
                        double newOx = otherX - nx * mid * (2.0 * (1.0 - agentMassRatio));
                        double newOz = otherZ - nz * mid * (2.0 * (1.0 - agentMassRatio));
                        bool aOk = physicsWorld->isValidPosition(newAx, newAz, radius);
                        bool oOk = physicsWorld->isValidPosition(newOx, newOz, other.radius);
                        if (aOk && oOk)
                            lo = mid;
                        else
                            hi = mid;
                    }
                    pushAmount = lo;
                }

                // Apply displacement with mass ratio
                agent->setPosition(agentX + nx * pushAmount * (2.0 * agentMassRatio),
                                  agentZ + nz * pushAmount * (2.0 * agentMassRatio));
                other.agent->setPosition(otherX - nx * pushAmount * (2.0 * (1.0 - agentMassRatio)),
                                        otherZ - nz * pushAmount * (2.0 * (1.0 - agentMassRatio)));
            }
        }
    }

    // #region agent log
    if (overlapCount > 0 && (agent->getId() % 25 == 0)) {
        const char *logPath = std::getenv("PRICERIOT_DEBUG_LOG");
        if (!logPath || !logPath[0]) logPath = "C:/Users/krist/Projects/PriceRiot/debug-01e413.log";
        std::ofstream lf(logPath, std::ios::app);
        if (lf) {
            auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            lf << "{\"sessionId\":\"01e413\",\"hypothesisId\":\"H4\",\"location\":\"collision_manager.cpp:resolveCollisions\",\"message\":\"agent-agent overlaps resolved\",\"data\":{\"customerId\":" << agent->getId() << ",\"overlapCount\":" << overlapCount << "},\"timestamp\":" << ts << "}\n";
        }
    }
    // #endregion

    // After agent-agent separation, current agent may have been pushed into an obstacle.
    // Resolve again so we never end the frame inside a shelf.
    if (physicsWorld) {
        agentX = agent->getPosX();
        agentZ = agent->getPosZ();
        Circle agentCircle(agentX, agentZ, radius);
        if (physicsWorld->checkCollision(agentCircle)) {
            physicsWorld->resolveCollision(agentCircle);
            if (const char *logPath = getCollisionDebugLogPath()) {
                std::ofstream lf(logPath, std::ios::app);
                if (lf)
                    lf << "{\"hypothesisId\":\"H2\",\"location\":\"collision_manager.cpp:obstacle_push_after_agent\",\"message\":\"Agent pushed out of obstacle after agent-agent\",\"data\":{\"customerId\":"
                       << agent->getId() << ",\"posBeforeX\":" << agentX << ",\"posBeforeZ\":" << agentZ
                       << ",\"posAfterX\":" << agentCircle.x << ",\"posAfterZ\":" << agentCircle.z
                       << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
            }
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

        // *** FIX: Increase avoidance radius for picking agents ***
        // Other agents should steer around picking agents earlier
        double effectiveRadius = other.radius;
        if (isAgentPicking(other.agent)) {
            effectiveRadius *= 2.0; // Double the avoidance radius for picking agents
        }

        // Calculate repulsion strength (stronger when closer)
        double combinedRadius = radius + effectiveRadius;
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