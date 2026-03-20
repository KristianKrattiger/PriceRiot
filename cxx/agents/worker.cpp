#include "worker.h"

namespace priceriot {

Worker::Worker(int id,
               bool canStockShelves,
               bool canServeRegister,
               double happiness,
               double taskEfficiency)
    : Agent(id),
      canStockShelves_(canStockShelves),
      canServeRegister_(canServeRegister),
      happiness_(happiness),
      taskEfficiency_(taskEfficiency) {}

void Worker::setHappiness(double h) noexcept {
    happiness_ = std::clamp(h, 0.0, 1.0);
    // Simple coupling: lower happiness slightly reduces efficiency.
    const double baseEff = std::clamp(taskEfficiency_, 0.1, 2.0);
    taskEfficiency_ = baseEff * (0.5 + 0.5 * happiness_);
}

void Worker::setTaskEfficiency(double e) noexcept {
    taskEfficiency_ = std::clamp(e, 0.1, 2.0);
}

double Worker::effectiveSpeed() const noexcept {
    // Scale base speed by task efficiency; keep within reasonable bounds.
    const double base = getSpeed();
    return std::clamp(base * taskEfficiency_, 0.1, 3.0);
}

void Worker::addTask(const Task &task) {
    taskQueue_.push(task);
}

bool Worker::hasTasks() const noexcept {
    return hasCurrentTask_ || !taskQueue_.empty();
}

const Task *Worker::currentTask() const noexcept {
    return hasCurrentTask_ ? &currentTask_ : nullptr;
}

void Worker::maybePickNextTask() {
    if (hasCurrentTask_ || taskQueue_.empty())
        return;

    // For now, just pop the highest priority/oldest task and start executing in place.
    currentTask_ = taskQueue_.top();
    taskQueue_.pop();
    hasCurrentTask_ = true;
    startExecutingCurrentTask();
}

void Worker::startExecutingCurrentTask() {
    state_ = State::ExecutingTask;

    // Simple per-task base durations in seconds, scaled by efficiency.
    double baseDuration = 5.0;
    switch (currentTask_.type) {
        case TaskType::StockShelves:    baseDuration = 8.0; break;
        case TaskType::ProcessRegister: baseDuration = 6.0; break;
        case TaskType::AssistCustomer:  baseDuration = 4.0; break;
    }

    const double eff = std::max(taskEfficiency_, 0.1);
    remainingWorkSeconds_ = baseDuration / eff;
}

void Worker::tickExecuting(double dt) {
    if (!hasCurrentTask_)
        return;
    if (state_ != State::ExecutingTask)
        return;

    remainingWorkSeconds_ -= dt;
    if (remainingWorkSeconds_ <= 0.0) {
        // Task complete; in a later wiring step, TaskManager will be notified.
        hasCurrentTask_ = false;
        state_ = State::Idle;
        remainingWorkSeconds_ = 0.0;
    }
}

bool Worker::update(float dt,
                    const StoreGraph & /*store*/,
                    CheckoutQueueManager * /*queueManager*/,
                    CollisionManager * /*collisionManager*/) {
    switch (state_) {
        case State::Idle:
            if (!hasCurrentTask_ && !taskQueue_.empty())
                maybePickNextTask();
            break;

        case State::MovingToTask:
            // Placeholder: once wired to spatial targets, move toward them here
            // using StoreGraph/navmesh utilities, then transition to ExecutingTask.
            // For the first iteration, we skip movement and execute in place.
            state_ = State::ExecutingTask;
            break;

        case State::ExecutingTask:
            tickExecuting(dt);
            break;
    }

    // Workers currently do not despawn; always return true.
    return true;
}

} // namespace priceriot

