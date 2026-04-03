/**
 * @file simulator.h
 * @brief Headless simulation core: Simulator class and CustomerVisit struct.
 *
 * Extracts all simulation state and logic from the monolithic sim.cpp/runSim(),
 * so the engine can be driven either by the SFML visualiser (sim.cpp) or by
 * Python via pybind11 (priceriot_bindings.cpp) without any SFML dependency.
 *
 * Usage (headless / Python):
 *   Simulator sim("store.yaml");
 *   sim.run(3600.0);                        // blocking headless run
 *   auto txns = sim.getTransactions();
 *
 * Usage (visualiser):
 *   Simulator sim("store.yaml");
 *   // each SFML frame:
 *   sim.step(dt);
 *   // then render via sim.getAgents(), sim.getStore(), sim.getLayout() …
 */
#pragma once
#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "../agents/basket.h"
#include "../agents/customer.h"
#include "../agents/customer_behavior.h"
#include "../agents/worker.h"
#include "../environment/checkout_queue.h"
#include "../environment/collision_manager.h"
#include "../environment/environment.h"
#include "../environment/products.h"
#include "../environment/store_layout.h"
#include "task_manager.h"
#include "transaction.h"

#include <cstdint>
#include <deque>
#include <algorithm>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace priceriot {

/** Snapshot of a mission customer at checkout (captured before basket is cleared). */
struct MissionCheckoutSnapshot {
    float simTime = 0.0f;
    int customerId = -1;
    std::vector<std::string> missionItems;
    std::vector<std::string> basketItems;
    double basketTotal = 0.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// CustomerVisit
// Customer + Basket pair that lives for one store visit.
// Moved here from the anonymous scope in sim.cpp so both the visualiser and
// the headless Simulator can share the same definition.
// ─────────────────────────────────────────────────────────────────────────────
struct CustomerVisit {
    std::shared_ptr<Customer> cust;
    Basket basket;
    bool hasPaid = false;

    /**
     * @param missionProbability Probability in [0,1] that a browsing customer
     *        is upgraded to MissionBehavior regardless of their TripPurpose.
     */
    CustomerVisit(std::shared_ptr<Customer> c, Basket b, float missionProbability,
                  std::default_random_engine &engine);

    /** Tick the visit. Returns false when the agent should be despawned. */
    bool update(float dt, const StoreGraph &store,
                CheckoutQueueManager *queueManager = nullptr,
                CollisionManager *collisionManager = nullptr,
                std::default_random_engine *rng = nullptr);
};

/** Per-worker efficiency sample for time-series analytics. */
struct WorkerMoodSample {
    float  time           = 0.0f;
    int    workerId       = 0;
    double taskEfficiency = 1.0;
};

/** Lightweight snapshot of a Worker for UI / analytics. */
struct WorkerSnapshot {
    int      id            = 0;
    double   posX          = 0.0;
    double   posZ          = 0.0;
    bool     canStock      = false;
    bool     canServe      = false;
    double   taskEfficiency= 1.0;
    bool     hasTask       = false;
    TaskType taskType      = TaskType::StockShelves;
    int      taskTargetId  = -1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Simulator
// ─────────────────────────────────────────────────────────────────────────────
class Simulator {
  public:
    // ── Construction / Lifecycle ────────────────────────────────────────────

    /**
     * @param yamlPath          Path to store.yaml.
     * @param spawnInterval     Seconds of sim-time between customer spawns.
     * @param missionProbability Fraction of spawned customers assigned
     *        MissionBehavior (0 = all Default, 1 = all Mission).
     * @param seed              Random seed; 0 = use std::random_device (non-deterministic).
     *        Non-zero gives reproducible runs (e.g. same seed after reset()).
     */
    explicit Simulator(const std::string &yamlPath,
                       float spawnInterval     = 5.0f,
                       float missionProbability = 0.5f,
                       std::uint32_t seed      = 0);

    // Non-copyable (owns unique_ptrs, mutex, large graph)
    Simulator(const Simulator &)            = delete;
    Simulator &operator=(const Simulator &) = delete;

    // ── Core Tick API ───────────────────────────────────────────────────────

    /**
     * Advance simulation by dt seconds (one tick).
     * Thread-safe with respect to getTransactions() / exportTransactions().
     */
    void step(float dt);

    /**
     * Blocking headless run for durationSeconds of sim-time.
     * @param dt Fixed timestep per tick (default 1/60 s).
     */
    void run(double durationSeconds, float dt = 1.0f / 60.0f);

    /**
     * Tear down all agents and transactions, then reload the store from YAML.
     * Useful for running multiple independent trials.
     */
    void reset();

    // ── Data Access ─────────────────────────────────────────────────────────

    /** Snapshot of all completed transactions (thread-safe copy). */
    [[nodiscard]] std::vector<Transaction> getTransactions() const;

    /** All customers ever spawned, including those still active. */
    [[nodiscard]] std::vector<std::shared_ptr<Customer>> getCustomers() const;

    /** Number of completed transactions (cheap, no copy). */
    [[nodiscard]] size_t getTransactionCount() const;

    /**
     * Write completed transactions to a CSV file.
     * Columns: transaction_id, customer_id, timestamp, satisfaction,
     *          total_spent, item_id, item_name, quantity, price_per_unit,
     *          item_total
     * @throws std::runtime_error if the file cannot be opened.
     */
    void exportTransactions(const std::string &path) const;

    // ── Config Setters (take effect on next step()) ──────────────────────────

    void setSpawnInterval(float seconds)      { spawnInterval_      = seconds; }
    void setMissionProbability(float p)       { missionProbability_ = p;       }

    /** Set RNG seed for reproducible runs. Call before run() or after reset(). */
    void setSeed(std::uint32_t seed)          { rng_.seed(seed);               }

    /**
     * Configure staff pool and automatic task generation.
     * Immediately re-spawns the worker pool so the new counts take effect
     * without needing a separate reset() call.
     */
    void setWorkerConfig(int numStockers,
                          int numCashiers,
                          bool autoStockTasks,
                          bool autoRegisterTasks) {
        numStockers_       = std::max(0, numStockers);
        numCashiers_       = std::max(0, numCashiers);
        autoStockTasks_    = autoStockTasks;
        autoRegisterTasks_ = autoRegisterTasks;
        respawnWorkers();
    }

    // ── Accessors for the SFML Visualiser (sim.cpp) ─────────────────────────

    [[nodiscard]] const StoreGraph           &getStore()          const { return store_;          }
    [[nodiscard]] StoreGraph                 &getMutableStore()         { return store_;          }
    [[nodiscard]] const StoreLayout          &getLayout()         const { return layout_;         }
    [[nodiscard]] const CheckoutQueueManager &getQueueManager()   const { return queueManager_;  }
    [[nodiscard]] CheckoutQueueManager       &getMutableQueueManager()  { return queueManager_;  }
    [[nodiscard]] const CollisionManager     &getCollisionManager()const { return collisionManager_; }
    [[nodiscard]] const std::vector<std::unique_ptr<CustomerVisit>> &getAgents() const { return agents_; }
    [[nodiscard]] float getElapsedTime()  const { return elapsedTime_;  }
    [[nodiscard]] float getSpawnInterval() const { return spawnInterval_; }
    [[nodiscard]] float getMissionProbability() const { return missionProbability_; }
    [[nodiscard]] std::vector<std::vector<std::uint64_t>> getCellVisitCounts() const {
        return cellVisitCounts_;
    }
    [[nodiscard]] std::vector<float> getQueueSampleTimes() const {
        return queueSampleTimes_;
    }
    [[nodiscard]] std::vector<std::vector<int>> getQueueLengthsHistory() const {
        return queueLengthsHistory_;
    }

    /** Access to workers for visualiser / bindings. */
    [[nodiscard]] const std::vector<std::unique_ptr<Worker>> &getWorkers() const {
        return workers_;
    }

    /** Snapshot workers into simple POD structs for analytics/UI. */
    [[nodiscard]] std::vector<WorkerSnapshot> getWorkerSnapshots() const;

    /** Per-worker happiness/efficiency samples collected during the run. */
    [[nodiscard]] const std::vector<WorkerMoodSample> &getWorkerMoodSamples() const {
        return workerMoodSamples_;
    }

    /** Mission vs basket at checkout (captured before basket clear). Max 50 entries. */
    [[nodiscard]] const std::deque<MissionCheckoutSnapshot> &getMissionCheckoutLog() const {
        return missionCheckoutLog_;
    }

  private:
    // ── Initialisation ───────────────────────────────────────────────────────
    void loadStore();
    /** Clear and re-spawn the worker pool based on current numStockers_/numCashiers_. */
    void respawnWorkers();

    // ── Per-Tick Helpers ────────────────────────────────────────────────────
    void spawnAgent(float dt);
    void updateAgents(float dt);
    void updateWorkers(float dt);
    void updateWorkerMoods();
    void generateTasks(float dt);
    void finalizeCheckout(CustomerVisit &agent);
    void removeDeadAgents();
    void sampleQueues();

    /**
     * Resolve the best (edgeIdx, cellIdx) for a shelf-pick event.
     * Tries layout-geometry first, then StoreGraph::findClosestCell, then
     * falls back to distOnEdge-based indexing.
     */
    [[nodiscard]] std::pair<int, int> resolvePickCell(double px, double pz,
                                                      int    edgeIdx,
                                                      double distOnEdge,
                                                      int    sku) const;

    // ── Owned State ──────────────────────────────────────────────────────────
    std::string yamlPath_;

    StoreGraph            store_;
    StoreLayout           layout_;
    CheckoutQueueManager  queueManager_;
    CollisionManager      collisionManager_;

    std::vector<std::unique_ptr<CustomerVisit>> agents_;
    TaskManager                                       taskManager_;
    std::vector<std::unique_ptr<Worker>>             workers_;
    std::vector<std::shared_ptr<Customer>>     customerPool_;

    mutable std::mutex          transactionMutex_;
    std::vector<Transaction>    completedTransactions_;

    std::default_random_engine rng_;

    float  spawnTimer_        = 0.0f;
    float  spawnInterval_     = 5.0f;
    float  missionProbability_= 0.5f;
    float  elapsedTime_       = 0.0f;
    int    nextTransactionId_ = 1;

    // Simple worker/task tuning knobs.
    int   numStockers_          = 2;
    int   numCashiers_          = 1;
    bool  autoStockTasks_       = true;
    bool  autoRegisterTasks_    = true;
    float taskScanTimer_        = 0.0f;
    float taskScanInterval_     = 1.0f; // seconds between heuristic task scans

    // Aggregated traffic metrics: per-edge, per-cell visit counts.
    // Incremented once per tick for each agent based on its current edge/cell.
    std::vector<std::vector<std::uint64_t>> cellVisitCounts_;

    // Queue metrics: sampled once per tick after step().
    // queueSampleTimes_[i] is the simulation time (seconds) at which the ith
    // sample was taken. queueLengthsHistory_[lane][i] is the length of that
    // lane's queue at the same sample.
    std::vector<float>                queueSampleTimes_;
    std::vector<std::vector<int>>     queueLengthsHistory_;

    // Worker mood time-series (sampled every kMoodUpdateInterval sim-seconds).
    static constexpr float kMoodUpdateInterval = 10.0f;
    float moodUpdateTimer_ = 0.0f;
    std::vector<WorkerMoodSample> workerMoodSamples_;

    // Mission checkout log: snapshot (mission + basket) before basket clear. For UI.
    static constexpr size_t MISSION_CHECKOUT_LOG_MAX = 50;
    std::deque<MissionCheckoutSnapshot> missionCheckoutLog_;
};

} // namespace priceriot

#endif // SIMULATOR_H