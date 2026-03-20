/**
 * @file worker.h
 * @brief Store staff agent that executes Tasks (stocking, registers, assistance).
 */
#ifndef PRICERIOT_WORKER_H
#define PRICERIOT_WORKER_H

#include "agent.h"
#include "task.h"
#include "../environment/environment.h"

#include <queue>

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

    // Wellbeing / performance
    [[nodiscard]] double getHappiness() const noexcept { return happiness_; }
    void setHappiness(double h) noexcept;

    [[nodiscard]] double getTaskEfficiency() const noexcept { return taskEfficiency_; }
    void setTaskEfficiency(double e) noexcept;

    // Task introspection
    void addTask(const Task &task);
    [[nodiscard]] bool hasTasks() const noexcept;
    [[nodiscard]] const Task *currentTask() const noexcept;

    // Agent interface
    bool update(float dt,
                const StoreGraph &store,
                CheckoutQueueManager *queueManager,
                CollisionManager *collisionManager) override;

  private:
    bool   canStockShelves_{false};
    bool   canServeRegister_{false};
    double happiness_{1.0};
    double taskEfficiency_{1.0}; // 0-2 range typically; scales speed / work rate

    State state_{State::Idle};

    std::priority_queue<Task, std::vector<Task>, TaskComparator> taskQueue_;
    bool   hasCurrentTask_{false};
    Task   currentTask_{};
    double remainingWorkSeconds_{0.0};

    // Internal helpers
    void maybePickNextTask();
    void startExecutingCurrentTask();
    void tickExecuting(double dt);
    double effectiveSpeed() const noexcept;
};

} // namespace priceriot

#endif // PRICERIOT_WORKER_H

