/**
 * @file checkout_queue.cpp
 * @brief Implementation of CheckoutQueueManager.
 */
#include "checkout_queue.h"

#include <algorithm>
#include <limits>
#include <yaml-cpp/yaml.h>

namespace priceriot {

void CheckoutQueueManager::loadFromYaml(const YAML::Node &queueConfig) {
    lanes_.clear();
    queues_.clear();

    if (!queueConfig || !queueConfig.IsSequence())
        return;

    for (size_t i = 0; i < queueConfig.size(); ++i) {
        const auto &laneY = queueConfig[i];
        LaneConfig lane;

        if (auto regId = laneY["register_id"])
            lane.registerId = regId.as<int>();
        if (auto procTime = laneY["processing_time"])
            lane.processingTime = procTime.as<double>();

        if (auto waypoints = laneY["waypoints"]) {
            for (size_t w = 0; w < waypoints.size(); ++w) {
                QueueWaypoint wp;
                if (auto x = waypoints[w]["x"])
                    wp.x = x.as<double>();
                if (auto z = waypoints[w]["z"])
                    wp.z = z.as<double>();
                lane.waypoints.push_back(wp);
            }
        }

        lanes_.push_back(lane);
        queues_[static_cast<int>(i)] = std::deque<int>();
    }
}

size_t CheckoutQueueManager::getQueueLength(int laneId) const {
    auto it = queues_.find(laneId);
    return it != queues_.end() ? it->second.size() : 0;
}

int CheckoutQueueManager::selectLane(double customerX, double customerZ,
                                      double patience, double crowdSensitivity) {
    if (lanes_.empty())
        return -1;

    int bestLane = 0;
    double bestScore = std::numeric_limits<double>::max();

    for (size_t i = 0; i < lanes_.size(); ++i) {
        const auto &lane = lanes_[i];
        if (lane.waypoints.empty())
            continue;

        // Distance to the counter (first waypoint)
        double dist = distance(customerX, customerZ, lane.waypoints[0].x, lane.waypoints[0].z);

        // Queue length
        auto qIt = queues_.find(static_cast<int>(i));
        int queueLen = qIt != queues_.end() ? static_cast<int>(qIt->second.size()) : 0;

        // Hybrid score:
        // - patience (0-1): higher = more willing to walk farther for shorter queue
        // - crowdSensitivity (0-1): higher = more averse to long queues
        // Formula: score = dist * distWeight + queueLen * queueWeight
        double distWeight = 1.0 - patience * 0.5;  // Range: 0.5 to 1.0
        double queueWeight = 0.5 + crowdSensitivity * 1.5;  // Range: 0.5 to 2.0

        double score = dist * distWeight + queueLen * queueWeight * 2.0;

        if (score < bestScore) {
            bestScore = score;
            bestLane = static_cast<int>(i);
        }
    }

    return bestLane;
}

bool CheckoutQueueManager::joinQueue(int laneId, int customerId) {
    if (laneId < 0 || laneId >= static_cast<int>(lanes_.size()))
        return false;

    auto &queue = queues_[laneId];

    // Check if already in queue
    for (int id : queue) {
        if (id == customerId)
            return true; // Already in queue
    }

    queue.push_back(customerId);
    return true;
}

CheckoutQueueManager::QueueWaypoint CheckoutQueueManager::getQueueWaypoint(int laneId,
                                                                            int customerId) const {
    if (laneId < 0 || laneId >= static_cast<int>(lanes_.size()))
        return {};

    const auto &lane = lanes_[static_cast<size_t>(laneId)];
    if (lane.waypoints.empty())
        return {};

    int pos = getPositionInQueue(laneId, customerId);
    if (pos < 0)
        return lane.waypoints.back(); // Not in queue, return last position

    // Return waypoint at position, or last waypoint if position exceeds array
    size_t wpIdx = std::min(static_cast<size_t>(pos), lane.waypoints.size() - 1);
    return lane.waypoints[wpIdx];
}

int CheckoutQueueManager::getPositionInQueue(int laneId, int customerId) const {
    auto qIt = queues_.find(laneId);
    if (qIt == queues_.end())
        return -1;

    const auto &queue = qIt->second;
    for (size_t i = 0; i < queue.size(); ++i) {
        if (queue[i] == customerId)
            return static_cast<int>(i);
    }
    return -1;
}

bool CheckoutQueueManager::isAtFront(int laneId, int customerId) const {
    return getPositionInQueue(laneId, customerId) == 0;
}

void CheckoutQueueManager::leaveQueue(int laneId, int customerId) {
    auto qIt = queues_.find(laneId);
    if (qIt == queues_.end())
        return;

    auto &queue = qIt->second;
    auto it = std::find(queue.begin(), queue.end(), customerId);
    if (it == queue.end())
        return;

    bool wasAtFront = (it == queue.begin());
    queue.erase(it);

    // Unregister callback
    callbacks_.erase(customerId);

    // If the leaving customer was at front, notify remaining customers
    if (wasAtFront) {
        for (size_t i = 0; i < queue.size(); ++i) {
            int custId = queue[i];
            auto cbIt = callbacks_.find(custId);
            if (cbIt != callbacks_.end()) {
                cbIt->second(custId, static_cast<int>(i));
            }
        }
    }
}

void CheckoutQueueManager::advanceQueue(int laneId) {
    auto qIt = queues_.find(laneId);
    if (qIt == queues_.end() || qIt->second.empty())
        return;

    auto &queue = qIt->second;

    // Remove front customer
    int frontId = queue.front();
    queue.pop_front();

    // Unregister their callback
    callbacks_.erase(frontId);

    // Notify all remaining customers of their new position
    for (size_t i = 0; i < queue.size(); ++i) {
        int custId = queue[i];
        auto cbIt = callbacks_.find(custId);
        if (cbIt != callbacks_.end()) {
            cbIt->second(custId, static_cast<int>(i));
        }
    }
}

int CheckoutQueueManager::getFrontCustomer(int laneId) const {
    auto qIt = queues_.find(laneId);
    if (qIt == queues_.end() || qIt->second.empty())
        return -1;
    return qIt->second.front();
}

void CheckoutQueueManager::registerAdvanceCallback(int customerId, AdvanceCallback cb) {
    callbacks_[customerId] = std::move(cb);
}

void CheckoutQueueManager::unregisterCallback(int customerId) {
    callbacks_.erase(customerId);
}

int CheckoutQueueManager::findCustomerLane(int customerId) const {
    for (const auto &[laneId, queue] : queues_) {
        for (int id : queue) {
            if (id == customerId)
                return laneId;
        }
    }
    return -1;
}

} // namespace priceriot
