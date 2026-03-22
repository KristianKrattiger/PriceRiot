#include "worker.h"
#include "../environment/checkout_queue.h"

#include <cmath>

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

    currentTask_ = taskQueue_.top();
    taskQueue_.pop();
    hasCurrentTask_ = true;
    startExecutingCurrentTask();
}

void Worker::startExecutingCurrentTask() {
    // Transition to movement first; pathfinding happens on the first MovingToTask tick.
    state_       = State::MovingToTask;
    waypoints_.clear();
    waypointIdx_ = 0;

    // Pre-compute work duration now so it's ready when we arrive.
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
    if (!hasCurrentTask_ || state_ != State::ExecutingTask)
        return;

    remainingWorkSeconds_ -= dt;
    if (remainingWorkSeconds_ <= 0.0) {
        // Emit a completion event for Simulator::updateWorkers to apply world effects.
        completedTask_.valid    = true;
        completedTask_.type     = currentTask_.type;
        completedTask_.targetId = currentTask_.targetId;

        happiness_ = std::min(1.0, happiness_ + 0.05);

        hasCurrentTask_       = false;
        state_                = State::Idle;
        remainingWorkSeconds_ = 0.0;
    }
}

Worker::CompletedTask Worker::popCompletedTask() noexcept {
    CompletedTask out  = completedTask_;
    completedTask_.valid = false;
    return out;
}

bool Worker::update(float dt,
                    const StoreGraph &store,
                    CheckoutQueueManager *queueManager,
                    CollisionManager * /*collisionManager*/) {
    switch (state_) {
        case State::Idle:
            if (!hasCurrentTask_ && !taskQueue_.empty())
                maybePickNextTask();
            break;

        case State::MovingToTask: {
            // On the first tick of a new task: resolve target world position and compute path.
            if (waypoints_.empty()) {
                double tgtX = posX_, tgtZ = posZ_;

                if (hasCurrentTask_) {
                    if (currentTask_.type == TaskType::StockShelves) {
                        const int edgeIdx = currentTask_.targetId;
                        if (edgeIdx >= 0 && edgeIdx < store.numEdges()) {
                            const int midCell = std::max(0, store.edgeAt(edgeIdx).getCellCount() / 2);
                            auto [cx, cz]     = store.getCellCenter(edgeIdx, midCell);
                            tgtX = cx;
                            tgtZ = cz;
                        }
                    } else if (currentTask_.type == TaskType::ProcessRegister && queueManager) {
                        const int lane = currentTask_.targetId;
                        if (lane >= 0 && static_cast<size_t>(lane) < queueManager->getLaneCount()) {
                            const auto &wp = queueManager->getLane(static_cast<size_t>(lane)).waypoints;
                            if (!wp.empty()) {
                                tgtX = wp[0].x;
                                tgtZ = wp[0].z;
                            }
                        }
                    } else {
                        // AssistCustomer or unknown: execute in place immediately.
                        state_ = State::ExecutingTask;
                        break;
                    }
                }

                if (store.hasNavMesh()) {
                    waypoints_ = NavMeshPathfinder::findPath(store.getNavMesh(),
                                                             posX_, posZ_, tgtX, tgtZ);
                }
                waypointIdx_ = 0;

                if (waypoints_.empty()) {
                    // No navmesh or unreachable: teleport to target and begin work.
                    posX_ = tgtX;
                    posZ_ = tgtZ;
                    state_ = State::ExecutingTask;
                    break;
                }
            }

            // Advance along the computed path.
            if (waypointIdx_ < static_cast<int>(waypoints_.size())) {
                const auto &wp  = waypoints_[static_cast<size_t>(waypointIdx_)];
                const double dx = wp.x - posX_;
                const double dz = wp.z - posZ_;
                const double dist = std::sqrt(dx * dx + dz * dz);
                const double step = effectiveSpeed() * static_cast<double>(dt);

                if (dist <= step) {
                    posX_ = wp.x;
                    posZ_ = wp.z;
                    ++waypointIdx_;
                } else {
                    posX_ += dx / dist * step;
                    posZ_ += dz / dist * step;
                }
            } else {
                // Reached the end of the path.
                waypoints_.clear();
                waypointIdx_ = 0;
                state_       = State::ExecutingTask;
            }
            break;
        }

        case State::ExecutingTask:
            tickExecuting(dt);
            break;
    }

    return true;
}

} // namespace priceriot

