#include "task_manager.h"
#include "../agents/worker.h"

namespace priceriot {

TaskId TaskManager::createTask(const Task &tTemplate, double createdAtSeconds) {
    Task t = tTemplate;
    t.id        = nextId_++;
    t.createdAt = createdAtSeconds;
    t.consecutiveHighPrioTaken = 0;

    tasks_[t.id] = t;
    pending_.push(t);
    return t.id;
}

const Task *TaskManager::getTask(TaskId id) const {
    auto it = tasks_.find(id);
    if (it == tasks_.end())
        return nullptr;
    return &it->second;
}

bool TaskManager::isTaskCompatibleWithWorker(const Task &t, const Worker &w) const {
    switch (t.type) {
        case TaskType::StockShelves:    return w.canStock();
        case TaskType::ProcessRegister: return w.canServe();
        case TaskType::AssistCustomer:  return true; // any worker can assist for now
    }
    return true;
}

std::optional<Task> TaskManager::requestTaskForWorker(const Worker &worker) {
    if (pending_.empty())
        return std::nullopt;

    // To respect starvation and compatibility, inspect a small window of tasks.
    std::vector<Task> buffer;
    buffer.reserve(8);

    Task *best = nullptr;
    size_t bestIdx = 0;

    for (size_t i = 0; i < 8 && !pending_.empty(); ++i) {
        Task top = pending_.top();
        pending_.pop();
        buffer.push_back(top);

        if (!isTaskCompatibleWithWorker(top, worker))
            continue;

        if (!best) {
            best    = &buffer.back();
            bestIdx = buffer.size() - 1;
            continue;
        }
        if (TaskComparator{}( *best, buffer.back())) {
            best    = &buffer.back();
            bestIdx = buffer.size() - 1;
        }
    }

    // Push everything back except the chosen one (if any).
    std::optional<Task> result;
    for (size_t i = 0; i < buffer.size(); ++i) {
        if (best && i == bestIdx) {
            result = buffer[i];
            continue;
        }
        pending_.push(buffer[i]);
    }

    if (!result.has_value())
        return std::nullopt;

    // Update starvation bookkeeping.
    int pri = result->priority;
    if (pri >= lastPriority_) {
        ++highPriorityStreak_;
    } else {
        highPriorityStreak_ = 0;
    }
    lastPriority_ = pri;

    // Remove from tasks_ map; ownership moves to worker side.
    tasks_.erase(result->id);

    return result;
}

void TaskManager::releaseTask(TaskId id) {
    // Used for re-queuing or cancelling; for now we simply erase.
    tasks_.erase(id);
}

} // namespace priceriot

