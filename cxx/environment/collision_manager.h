#ifndef COLLISION_MANAGER_H
#define COLLISION_MANAGER_H

#include "physics.h"
#include <vector>
#include <memory>

namespace priceriot {

// Forward declaration
class Customer;

/**
 * Manages agent-to-agent and agent-to-obstacle collisions.
 * Provides collision avoidance and resolution.
 */
class CollisionManager {
public:
    CollisionManager() = default;
    
    // Register agents for collision checking
    void registerAgent(Customer* agent, double radius = 0.35);
    void unregisterAgent(Customer* agent);
    void clear();
    
    // Set physics world for obstacle checking
    void setPhysicsWorld(const PhysicsWorld* world) { physicsWorld = world; }
    
    // Check and resolve collisions for a single agent
    void resolveCollisions(Customer* agent, double radius);
    
    // Get avoidance vector (steering away from nearby agents)
    void getAvoidanceVector(double x, double z, double radius, double& avoidX, double& avoidZ, double maxDistance = 2.0) const;
    
    // Check if position would collide with other agents
    bool wouldCollideWithAgents(double x, double z, double radius, const Customer* excludeAgent = nullptr) const;
    
    // Get all nearby agents within radius
    std::vector<Customer*> getNearbyAgents(double x, double z, double radius) const;

private:
    struct AgentInfo {
        Customer* agent;
        double radius;
        
        AgentInfo(Customer* a, double r) : agent(a), radius(r) {}
    };
    
    std::vector<AgentInfo> registeredAgents;
    const PhysicsWorld* physicsWorld = nullptr;
    
    bool circlesIntersect(double x1, double z1, double r1, double x2, double z2, double r2) const;
};

} // namespace priceriot

#endif // COLLISION_MANAGER_H
