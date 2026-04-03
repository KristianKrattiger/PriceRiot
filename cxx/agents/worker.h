/**
 * @file worker.h
 * @brief Store staff agent that executes Tasks (stocking, registers, assistance).
 */
#ifndef PRICERIOT_WORKER_H
#define PRICERIOT_WORKER_H

#include "agent.h"
#include "task.h"
#include "../environment/environment.h"
#include "../environment/navmesh_pathfinder.h"

#include <queue>
#include <vector>

namespace priceriot {

class Worker : public Agent {
  public:
    enum class State : std::uint8_t {
        Idle,
        MovingToTask,
        ExecutingTask
    };

    Worker(int id,
           bool canStockShelves,
           bool canServeRegister,
           double happiness      = 1.0,
           double taskEfficiency = 1.0);

    // Capabilities
    [[nodiscard]] bool canStock() const noexcept { return canStockShelves_; }
    [[nodiscard]] bool canServe() const noexcept { return canServeRegister_; }

    // Performance — efficiency in [0.1, 2.0]; 1.0 is the neutral baseline.
    [[nodiscard]] double getTaskEfficiency() const noexcept { return taskEfficiency_; }
    void setTaskEfficiency(double e) noexcept;

    /** POD event emitted when a task finishes; cleared after one pop. */
    struct CompletedTask {
        bool     valid    = false;
        TaskType type     = TaskType::StockShelves;
        int      targetId = -1;
    };

    // Task introspection
    void addTask(const Task &task);
    [[nodiscard]] bool hasTasks() const noexcept;
    [[nodiscard]] const Task *currentTask() const noexcept;

    /**
     * Consume and return the most recently completed task (if any).
     * Resets the internal event to invalid after the call.
     */
    [[nodiscard]] CompletedTask popCompletedTask() noexcept;

    // Agent interface
    bool update(float dt,
                const StoreGraph &store,
                CheckoutQueueManager *queueManager,
                CollisionManager *collisionManager) override;

  private:
    bool   canStockShelves_{false};
    bool   canServeRegister_{false};
    double taskEfficiency_{1.0}; // current operative efficiency; range [0.1, 2.0]

    State state_{State::Idle};

    std::priority_queue<Task, std::vector<Task>, TaskComparator> taskQueue_;
    bool   hasCurrentTask_{false};
    Task   currentTask_{};
    double remainingWorkSeconds_{0.0};

    // Navmesh travel state
    std::vector<NavMeshPathfinder::PathPoint> waypoints_;
    int waypointIdx_{0};

    // Pending completion event (consumed by Simulator::updateWorkers)
    CompletedTask completedTask_{};

    // Internal helpers
    void maybePickNextTask();
    void startExecutingCurrentTask();
    void tickExecuting(double dt);
    double effectiveSpeed() const noexcept;
};

} // namespace priceriot

#endif // PRICERIOT_WORKER_H

