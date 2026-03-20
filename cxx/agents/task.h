/**
 * @file task.h
 * @brief Shared task primitives for in-store workers (stocking, registers, assistance).
 */
#ifndef PRICERIOT_TASK_H
#define PRICERIOT_TASK_H

#include <cstdint>
#include <chrono>

namespace priceriot {

enum class TaskType : std::uint8_t {
    StockShelves,
    ProcessRegister,
    AssistCustomer
};

using TaskId = std::uint64_t;

struct Task {
    TaskId   id        = 0;
    TaskType type      = TaskType::StockShelves;
    int      priority  = 0;      // Higher value = higher priority
    int      targetId  = -1;     // Shelf, lane, or customer identifier
    double   createdAt = 0.0;    // Simulation time seconds when created
    int      consecutiveHighPrioTaken = 0; // For starvation mitigation bookkeeping
};

/**
 * @brief Comparator for priority_queue: higher priority first, then older tasks first.
 *
 * std::priority_queue puts the "largest" element on top. We define "larger" as:
 *  - higher priority
 *  - if equal priority, smaller createdAt (older)
 */
struct TaskComparator {
    bool operator()(const Task &a, const Task &b) const noexcept {
        if (a.priority != b.priority)
            return a.priority < b.priority; // higher priority first
        return a.createdAt > b.createdAt;   // older (smaller createdAt) first
    }
};

} // namespace priceriot

#endif // PRICERIOT_TASK_H

