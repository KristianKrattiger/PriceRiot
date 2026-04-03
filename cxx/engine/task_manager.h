/**
 * @file task_manager.h
 * @brief Central TaskManager coordinating tasks across workers.
 */
#ifndef PRICERIOT_TASK_MANAGER_H
#define PRICERIOT_TASK_MANAGER_H

#include "../agents/task.h"

#include <queue>
#include <unordered_map>
#include <optional>
#include <vector>

namespace priceriot {

class Worker;

class TaskManager {
  public:
    TaskManager() = default;

    TaskId createTask(const Task &tTemplate, double createdAtSeconds);

    [[nodiscard]] const Task *getTask(TaskId id) const;

    /**
     * @brief Request the next suitable task for a worker, according to priority and age.
     *
     * This function does not transfer ownership into the worker directly; instead,
     * the caller should pass the returned Task to Worker::addTask. Internally,
     * the Task is removed from the global pending queue and task map.
     */
    [[nodiscard]] std::optional<Task> requestTaskForWorker(const Worker &worker);

    void releaseTask(TaskId id);

    /** Number of tasks currently waiting to be assigned to a worker. */
    [[nodiscard]] int pendingTaskCount() const noexcept {
        return static_cast<int>(pending_.size());
    }

  private:
    TaskId nextId_{1};

    std::unordered_map<TaskId, Task> tasks_; // all known tasks, indexed by id
    std::priority_queue<Task, std::vector<Task>, TaskComparator> pending_; // tasks not yet owned

    int  highPriorityStreak_{0};
    int  highPriorityThreshold_{5};
    int  lastPriority_{0};

    bool isTaskCompatibleWithWorker(const Task &t, const Worker &w) const;
};

} // namespace priceriot

#endif // PRICERIOT_TASK_MANAGER_H

