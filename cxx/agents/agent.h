/**
 * @file agent.h
 * @brief Abstract base class for all in-store agents (customers, workers, etc.).
 *
 * Encapsulates identity, kinematics, and navigation state that are common across
 * different agent types. Game-logic classes (Simulator, behaviors, collision
 * manager) should prefer this interface instead of concrete types where possible.
 */
#ifndef PRICERIOT_AGENT_H
#define PRICERIOT_AGENT_H

#include <cstddef>

namespace priceriot {

// Forward declarations (defined in other headers).
class StoreGraph;
class CheckoutQueueManager;
class CollisionManager;

class Agent {
  public:
    Agent() = default;
    explicit Agent(int id) : id_(id) {}
    virtual ~Agent() = default;

    // --- Identity ---
    [[nodiscard]] int getId() const noexcept { return id_; }
    void setId(int id) noexcept { id_ = id; }

    // --- World position ---
    [[nodiscard]] double getPosX() const noexcept { return posX_; }
    [[nodiscard]] double getPosZ() const noexcept { return posZ_; }
    void setPosition(double x, double z) noexcept {
        posX_ = x;
        posZ_ = z;
    }

    // --- Edge-based navigation state ---
    [[nodiscard]] int getCurrentEdgeIndex() const noexcept { return currentEdgeIndex_; }
    void setCurrentEdgeIndex(int edgeIdx) noexcept { currentEdgeIndex_ = edgeIdx; }

    [[nodiscard]] double getDistOnEdge() const noexcept { return distOnEdge_; }
    void setDistOnEdge(double dist) noexcept { distOnEdge_ = dist; }

    [[nodiscard]] double getSpeed() const noexcept { return speed_; }
    void setSpeed(double s) noexcept { speed_ = s; }

    // Collision radius for avoidance/physics. Default matches existing usage.
    [[nodiscard]] virtual double getCollisionRadius() const noexcept { return 0.35; }

    /**
     * Advance the agent by dt seconds.
     *
     * Note: In this codebase not every derived agent type uses the same update
     * signature (e.g. Customer update additionally needs a Basket).
     * Making this a non-pure virtual keeps Customer non-abstract while still
     * allowing Worker to override it.
     */
    virtual bool update(float dt,
                        const StoreGraph &store,
                        CheckoutQueueManager *queueManager,
                        CollisionManager *collisionManager) {
        (void)dt;
        (void)store;
        (void)queueManager;
        (void)collisionManager;
        return true;
    }

  protected:
    int    id_               = 0;
    double posX_             = 0.0;
    double posZ_             = 0.0;
    int    currentEdgeIndex_ = -1;
    double distOnEdge_       = 0.0;
    double speed_            = 1.0;
};

} // namespace priceriot

#endif // PRICERIOT_AGENT_H

