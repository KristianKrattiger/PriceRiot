/**
 * @file checkout_queue.h
 * @brief Global checkout queue manager for FIFO queuing at registers.
 *
 * Manages multiple checkout lanes, queue positions, and event-driven
 * advancement when front customers finish. Customers select lanes based
 * on hybrid scoring (distance + queue length + patience/crowd sensitivity).
 */
#ifndef CHECKOUT_QUEUE_H
#define CHECKOUT_QUEUE_H

#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace YAML {
class Node;
}

namespace priceriot {

/**
 * Manages checkout queues for all register lanes.
 * - Customers join queues via selectLane() + joinQueue()
 * - Queue positions are defined in YAML as waypoints
 * - Event callbacks notify customers when queue advances
 */
class CheckoutQueueManager {
  public:
    /** A queue position in world coordinates. */
    struct QueueWaypoint {
        double x = 0.0;
        double z = 0.0;
    };

    /** Configuration for a single checkout lane. */
    struct LaneConfig {
        int registerId = -1;           // Node ID of the register
        double processingTime = 5.0;   // Average checkout time in seconds
        std::vector<QueueWaypoint> waypoints; // [0] = counter, [1..N] = queue positions
    };

    CheckoutQueueManager() = default;

    /**
     * Load lane configurations from YAML.
     * Expected format:
     *   checkout_queues:
     *     - register_id: 7
     *       processing_time: 5.0
     *       waypoints:
     *         - { x: 30.0, z: 0.0 }
     *         - { x: 28.5, z: 0.0 }
     */
    void loadFromYaml(const YAML::Node &queueConfig);

    /** Get number of configured lanes. */
    [[nodiscard]] size_t getLaneCount() const { return lanes_.size(); }

    /** Get lane config by index. */
    [[nodiscard]] const LaneConfig &getLane(size_t idx) const { return lanes_[idx]; }

    /** Get queue length for a lane. */
    [[nodiscard]] size_t getQueueLength(int laneId) const;

    // --- Customer Interface ---

    /**
     * Select the best lane for a customer based on hybrid scoring.
     * @param customerX, customerZ Customer world position
     * @param patience 0-1: higher = more willing to walk farther
     * @param crowdSensitivity 0-1: higher = more averse to long queues
     * @return Lane index (0 to getLaneCount()-1), or -1 if no lanes
     */
    int selectLane(double customerX, double customerZ, double patience, double crowdSensitivity);

    /**
     * Join the queue for a specific lane.
     * @return true if successfully joined
     */
    bool joinQueue(int laneId, int customerId);

    /**
     * Get the current queue position waypoint for a customer.
     * Returns the waypoint at their position index, or the last waypoint
     * if position exceeds defined waypoints.
     */
    [[nodiscard]] QueueWaypoint getQueueWaypoint(int laneId, int customerId) const;

    /**
     * Get customer's position in queue (0 = at counter, 1 = first behind, etc.)
     * @return Position index, or -1 if not in queue
     */
    [[nodiscard]] int getPositionInQueue(int laneId, int customerId) const;

    /**
     * Check if customer is at the front of the queue (position 0).
     */
    [[nodiscard]] bool isAtFront(int laneId, int customerId) const;

    /**
     * Remove customer from their queue (when they finish or leave).
     */
    void leaveQueue(int laneId, int customerId);

    // --- Register Interface ---

    /**
     * Advance the queue when front customer finishes checkout.
     * Removes front customer and notifies all others via callbacks.
     */
    void advanceQueue(int laneId);

    /**
     * Get the customer ID at the front of a queue.
     * @return Customer ID, or -1 if queue is empty
     */
    [[nodiscard]] int getFrontCustomer(int laneId) const;

    // --- Event Callbacks ---

    /**
     * Callback signature: (customerId, newPosition)
     * Called when a customer's queue position changes.
     */
    using AdvanceCallback = std::function<void(int customerId, int newPosition)>;

    /**
     * Register a callback for when a customer's position changes.
     */
    void registerAdvanceCallback(int customerId, AdvanceCallback cb);

    /**
     * Unregister a customer's callback.
     */
    void unregisterCallback(int customerId);

    /**
     * Find which lane a customer is in.
     * @return Lane index, or -1 if not in any queue
     */
    [[nodiscard]] int findCustomerLane(int customerId) const;

  private:
    std::vector<LaneConfig> lanes_;
    std::map<int, std::deque<int>> queues_; // laneId -> queue of customerIds
    std::map<int, AdvanceCallback> callbacks_;

    /** Helper: Euclidean distance. */
    static double distance(double x1, double z1, double x2, double z2) {
        double dx = x2 - x1;
        double dz = z2 - z1;
        return std::sqrt(dx * dx + dz * dz);
    }
};

} // namespace priceriot

#endif // CHECKOUT_QUEUE_H
