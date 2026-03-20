/**
 * @file simulator.cpp
 * @brief Headless Simulator implementation.
 *
 * All simulation logic (spawning, agent updates, collision avoidance, shelf picking,
 * transaction generation) lives here. No SFML, no ImGui, no rendering.
 */
#include "simulator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace priceriot {

// ═════════════════════════════════════════════════════════════════════════════
// CustomerVisit
// ═════════════════════════════════════════════════════════════════════════════

CustomerVisit::CustomerVisit(std::shared_ptr<Customer> c, Basket b, float missionProbability,
                             std::default_random_engine &engine)
    : cust(std::move(c)), basket(std::move(b)) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    if (cust->getTripPurpose() == Customer::TripPurpose::Mission ||
        u(engine) < static_cast<double>(missionProbability))
        cust->setBehavior(new MissionBehavior());
    else
        cust->setBehavior(new DefaultBehavior());
}

bool CustomerVisit::update(float dt, const StoreGraph &store,
                           CheckoutQueueManager *queueManager,
                           CollisionManager *collisionManager) {
    bool alive = cust->update(dt, store, basket, queueManager, collisionManager);
    // Legacy flag: set if basket was already cleared externally.
    if (basket.getSize() == 0 && cust->getTotalSpent() > 0)
        hasPaid = true;
    return alive;
}

// ═════════════════════════════════════════════════════════════════════════════
// Simulator – Construction
// ═════════════════════════════════════════════════════════════════════════════

Simulator::Simulator(const std::string &yamlPath,
                     float spawnInterval,
                     float missionProbability,
                     std::uint32_t seed)
    : yamlPath_(yamlPath),
      spawnInterval_(spawnInterval),
      missionProbability_(missionProbability) {
    if (seed != 0u)
        rng_.seed(seed);
    else {
        std::random_device rd;
        rng_.seed(static_cast<std::default_random_engine::result_type>(rd()));
    }
    loadStore();
}

void Simulator::loadStore() {
    store_  = StoreGraph{};
    layout_ = StoreLayout{};

    store_.loadFromYaml(yamlPath_);
    layout_.buildGeometry(store_);
    store_.buildNavMesh(layout_);
    store_.buildPhysicsWorld(layout_);

    if (store_.hasPhysicsWorld())
        collisionManager_.setPhysicsWorld(&store_.getPhysicsWorld());

    YAML::Node yaml = YAML::LoadFile(yamlPath_);
    if (yaml["checkout_queues"])
        queueManager_.loadFromYaml(yaml["checkout_queues"]);

    // Initialise traffic heatmap storage to match current store geometry.
    cellVisitCounts_.clear();
    cellVisitCounts_.resize(store_.numEdges());
    for (int e = 0; e < store_.numEdges(); ++e) {
        const Edge &edge = store_.edgeAt(e);
        cellVisitCounts_[static_cast<size_t>(e)].assign(edge.cells.size(), 0);
    }

    // Spawn initial workers: simple fixed pool near stockroom/register nodes.
    workers_.clear();
    int nextWorkerId = 1;

    // Helper lambdas to find representative nodes.
    auto findFirstNodeOfType = [this](Node::NodeType type) -> int {
        for (int i = 0; i < store_.numNodes(); ++i) {
            if (store_.nodeAt(i).getNodeType() == type)
                return i;
        }
        return -1;
    };

    const int stockroomNodeIdx = findFirstNodeOfType(Node::NodeType::Stockroom);
    const int registerNodeIdx  = findFirstNodeOfType(Node::NodeType::Register);

    // Spawn stockers
    for (int i = 0; i < numStockers_; ++i) {
        auto w = std::make_unique<Worker>(nextWorkerId++, true, false);
        if (stockroomNodeIdx >= 0) {
            const Node &n = store_.nodeAt(stockroomNodeIdx);
            w->setPosition(n.getX(), n.getZ());
        }
        w->setSpeed(0.9);
        workers_.push_back(std::move(w));
    }

    // Spawn cashiers
    for (int i = 0; i < numCashiers_; ++i) {
        auto w = std::make_unique<Worker>(nextWorkerId++, false, true);
        if (registerNodeIdx >= 0) {
            const Node &n = store_.nodeAt(registerNodeIdx);
            w->setPosition(n.getX(), n.getZ());
        }
        w->setSpeed(0.9);
        workers_.push_back(std::move(w));
    }

}

// ═════════════════════════════════════════════════════════════════════════════
// Simulator – Public API
// ═════════════════════════════════════════════════════════════════════════════

void Simulator::run(double durationSeconds, float dt) {
    const float target = static_cast<float>(durationSeconds);
    while (elapsedTime_ < target)
        step(dt);
}

void Simulator::step(float dt) {
    spawnAgent(dt);
    updateAgents(dt);
    generateTasks(dt);
    updateWorkers(dt);
    removeDeadAgents();
    elapsedTime_ += dt;
    sampleQueues();
}

void Simulator::reset() {
    agents_.clear();
    customerPool_.clear();
    workers_.clear();

    {
        std::lock_guard<std::mutex> lk(transactionMutex_);
        completedTransactions_.clear();
    }

    collisionManager_ = CollisionManager{};
    queueManager_     = CheckoutQueueManager{};

    spawnTimer_        = 0.0f;
    elapsedTime_       = 0.0f;
    nextTransactionId_ = 1;
    taskScanTimer_     = 0.0f;

    queueSampleTimes_.clear();
    queueLengthsHistory_.clear();
    missionCheckoutLog_.clear();

    loadStore();
}

std::vector<Transaction> Simulator::getTransactions() const {
    std::lock_guard<std::mutex> lk(transactionMutex_);
    return completedTransactions_;
}

std::vector<std::shared_ptr<Customer>> Simulator::getCustomers() const {
    return customerPool_;
}

size_t Simulator::getTransactionCount() const {
    std::lock_guard<std::mutex> lk(transactionMutex_);
    return completedTransactions_.size();
}

void Simulator::exportTransactions(const std::string &path) const {
    std::lock_guard<std::mutex> lk(transactionMutex_);

    std::ofstream f(path);
    if (!f)
        throw std::runtime_error("Simulator::exportTransactions – cannot open: " + path);

    f << "transaction_id,customer_id,timestamp,satisfaction,total_spent,"
         "item_id,item_name,quantity,price_per_unit,item_total\n";

    for (const auto &tx : completedTransactions_) {
        if (tx.getItems().empty()) {
            // Header row only (no line items)
            f << tx.getTransID()    << ','
              << tx.getCustID()     << ','
              << tx.getTimestamp()  << ','
              << tx.getSatisfaction() << ','
              << std::fixed << std::setprecision(2) << tx.getTotalSpent()
              << ",,,,,\n";
        } else {
            for (const auto &item : tx.getItems()) {
                f << tx.getTransID()    << ','
                  << tx.getCustID()     << ','
                  << tx.getTimestamp()  << ','
                  << tx.getSatisfaction() << ','
                  << std::fixed << std::setprecision(2) << tx.getTotalSpent() << ','
                  << item.id            << ','
                  << item.name          << ','
                  << item.quantity      << ','
                  << item.pricePerUnit  << ','
                  << item.total         << '\n';
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Simulator – Private tick helpers
// ═════════════════════════════════════════════════════════════════════════════

void Simulator::spawnAgent(float dt) {
    if (store_.numEdges() == 0)
        return;

    spawnTimer_ += dt;
    if (spawnTimer_ < spawnInterval_)
        return;
    spawnTimer_ = 0.0f;

    // Create customer + basket
    auto [custPtr, basket] = newCustomer(customerPool_, rng_);
    auto ag = std::make_unique<CustomerVisit>(custPtr, std::move(basket),
                                              missionProbability_, rng_);

    // Find the first entrance edge
    int startEdgeIdx = -1;
    for (int i = 0; i < store_.numEdges(); ++i) {
        if (store_.nodeAt(store_.edgeAt(i).getFromNode()).getNodeType() ==
            Node::NodeType::Entrance) {
            startEdgeIdx = i;
            break;
        }
    }
    if (startEdgeIdx == -1) {
        // No entrance found – drop this spawn silently
        return;
    }

    ag->cust->setCurrentEdgeIndex(startEdgeIdx);
    ag->cust->setDistOnEdge(0.0);
    ag->cust->setSpeed(0.75);

    // Set world position to the entrance node
    for (int i = 0; i < store_.numNodes(); ++i) {
        if (store_.nodeAt(i).getNodeType() == Node::NodeType::Entrance) {
            ag->cust->setPosition(store_.nodeAt(i).getX(),
                                  store_.nodeAt(i).getZ());
            break;
        }
    }

    collisionManager_.registerAgent(ag->cust.get(), 0.35);
    agents_.push_back(std::move(ag));
}

void Simulator::updateAgents(float dt) {
    // Rebuild spatial hash for this tick using agent positions from the previous frame.
    // This is used for fast nearby-agent queries (e.g. separation forces).
    {
        SpatialHash &spatialHash = store_.getSpatialHash();
        spatialHash.clear();
        for (auto &ag : agents_) {
            if (!ag || !ag->cust)
                continue;
            spatialHash.insert(ag->cust.get(), ag->cust->getPosX(), ag->cust->getPosZ());
        }
    }

    // Recompute node occupancy based on current agent positions.
    {
        const int nodeCount = store_.numNodes();
        for (int i = 0; i < nodeCount; ++i) {
            store_.mutableNodeAt(i).clearOccupants();
        }

        // Associate each agent with the nearest node within a small radius.
        constexpr double kMaxNodeRadius = 2.0;
        const double maxDistSq = kMaxNodeRadius * kMaxNodeRadius;
        for (auto &ag : agents_) {
            if (!ag || !ag->cust)
                continue;
            double px = ag->cust->getPosX();
            double pz = ag->cust->getPosZ();
            int bestIdx = -1;
            double bestD2 = maxDistSq;
            for (int n = 0; n < nodeCount; ++n) {
                const Node &node = store_.nodeAt(n);
                double dx = px - node.getX();
                double dz = pz - node.getZ();
                double d2 = dx * dx + dz * dz;
                if (d2 < bestD2) {
                    bestD2 = d2;
                    bestIdx = n;
                }
            }
            if (bestIdx >= 0) {
                store_.mutableNodeAt(bestIdx).registerOccupant(ag->cust->getId());
            }
        }
    }

    // Re-register agents in case any were added this tick
    for (auto &ag : agents_)
        collisionManager_.registerAgent(ag->cust.get(), 0.35);

    bool hasPhysics = store_.hasPhysicsWorld();
    CollisionManager *cmPtr = hasPhysics ? &collisionManager_ : nullptr;
    // #region agent log
    {
        static int tickCount = 0;
        if (agents_.size() > 0 && (tickCount++ % 60 == 0)) {
            const char *logPath = std::getenv("PRICERIOT_DEBUG_LOG");
            if (!logPath || !logPath[0]) logPath = "C:/Users/krist/Projects/PriceRiot/debug-01e413.log";
            std::ofstream lf(logPath, std::ios::app);
            if (lf) {
                auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                lf << "{\"sessionId\":\"01e413\",\"hypothesisId\":\"H1\",\"location\":\"simulator.cpp:updateAgents\",\"message\":\"collision manager wiring\",\"data\":{\"hasPhysicsWorld\":" << (hasPhysics ? "true" : "false") << ",\"cmPassed\":" << (cmPtr != nullptr ? "true" : "false") << ",\"agentCount\":" << agents_.size() << "},\"timestamp\":" << ts << "}\n";
            }
        }
    }
    // #endregion

    for (auto &ag : agents_) {
        if (!ag->cust)
            continue;

        // ── Behavior tick (avoidance is blended inside behavior when collision manager is set) ─
        ag->update(dt, store_, &queueManager_, cmPtr);

        // ── Handle: PickProduct ─────────────────────────────────────────────
        if (ag->cust->getLastDecisionType() ==
            static_cast<int>(Decision::DecisionType::PickProduct)) {

            const int sku = ag->cust->getLastDecisionTargetId();
            if (sku >= 0 && store_.catalog.productExists(sku)) {
                ag->basket.addProduct(store_.catalog.getProduct(sku));

                // Decrement shelf inventory at the closest valid cell
                auto [edgeIdx, cellIdx] = resolvePickCell(
                    ag->cust->getPosX(),
                    ag->cust->getPosZ(),
                    ag->cust->getCurrentEdgeIndex(),
                    ag->cust->getDistOnEdge(),
                    sku);

                if (edgeIdx >= 0 && cellIdx >= 0 &&
                    edgeIdx < store_.numEdges()) {
                    Edge &edge = store_.mutableEdgeAt(edgeIdx);
                    if (cellIdx < static_cast<int>(edge.cells.size()))
                        edge.cells[static_cast<size_t>(cellIdx)].takeOneBySku(
                            static_cast<std::uint32_t>(sku));
                }
            }
        }

        // ── Handle: Checkout ────────────────────────────────────────────────
        // Fire exactly once: first tick the behavior emits Checkout and the
        // basket is non-empty. Capture mission vs basket for UI before clearing.
        if (ag->cust->getLastDecisionType() ==
            static_cast<int>(Decision::DecisionType::Checkout)) {
            if (!ag->hasPaid && ag->basket.getSize() > 0) {
                const auto *mskus =
                    ag->cust->getBehavior() ? ag->cust->getBehavior()->getMissionSkus() : nullptr;
                if (mskus && !mskus->empty()) {
                    MissionCheckoutSnapshot snap;
                    snap.simTime       = static_cast<float>(elapsedTime_);
                    snap.customerId    = ag->cust->getId();
                    snap.basketTotal   = ag->basket.getTotal();
                    for (int sku : *mskus) {
                        snap.missionItems.push_back(
                            store_.catalog.productExists(sku)
                                ? store_.catalog.getProduct(sku).name
                                : "SKU#" + std::to_string(sku));
                    }
                    for (const auto &p : ag->basket.getProducts())
                        snap.basketItems.push_back(p.name);

                    missionCheckoutLog_.push_back(std::move(snap));
                    while (missionCheckoutLog_.size() > MISSION_CHECKOUT_LOG_MAX)
                        missionCheckoutLog_.pop_front();

                    // #region agent log
                    {
                        const int missionCount = static_cast<int>(mskus->size());
                        const int basketCount  = ag->basket.getSize();
                        std::ofstream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log",
                                         std::ios::app);
                        if (lf) {
                            lf << "{\"hypothesisId\":\"MC\",\"location\":\"simulator.cpp:checkout_snapshot\","
                                  "\"message\":\"Mission checkout snapshot captured\","
                                  "\"data\":{\"customerId\":" << ag->cust->getId()
                               << ",\"missionCount\":" << missionCount
                               << ",\"basketCount\":"  << basketCount
                               << ",\"basketTotal\":"  << ag->basket.getTotal()
                               << "},\"timestamp\":"
                               << std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count()
                               << "}\n";
                        }
                    }
                    // #endregion
                }
                finalizeCheckout(*ag);
            }
        }

        // ── Traffic heatmap sampling: increment cell visit counts ───────────
        if (ag->cust && ag->cust->getCurrentEdgeIndex() >= 0 &&
            ag->cust->getCurrentEdgeIndex() < store_.numEdges()) {

            const Edge &edge = store_.edgeAt(ag->cust->getCurrentEdgeIndex());
            const double cellLen = edge.getCellLength();

            if (cellLen > 0.0 && !edge.cells.empty()) {
                int cellIdx = static_cast<int>(ag->cust->getDistOnEdge() / cellLen);
                if (cellIdx < 0)
                    cellIdx = 0;
                if (cellIdx >= static_cast<int>(edge.cells.size()))
                    cellIdx = static_cast<int>(edge.cells.size()) - 1;

                if (ag->cust->getCurrentEdgeIndex() <
                    static_cast<int>(cellVisitCounts_.size())) {
                    auto &edgeCounts =
                        cellVisitCounts_[static_cast<size_t>(ag->cust->getCurrentEdgeIndex())];
                    if (cellIdx >= 0 &&
                        cellIdx < static_cast<int>(edgeCounts.size())) {
                        edgeCounts[static_cast<size_t>(cellIdx)]++;
                    }
                }
            }
        }
    }

    // ── Collision resolution (post-movement) ────────────────────────────────
    if (store_.hasPhysicsWorld()) {
        for (auto &ag : agents_) {
            if (ag->cust)
                collisionManager_.resolveCollisions(ag->cust.get(), 0.35);
        }
    }
}

void Simulator::updateWorkers(float dt) {
    // Workers currently execute tasks in place; for the first debugging pass
    // we do not include collision separation between workers/shelves.
    // (CollisionManager is customer-typed in this codebase.)
    for (auto &w : workers_) {
        if (!w)
            continue;
        w->update(dt, store_, &queueManager_, nullptr);
    }
}

void Simulator::generateTasks(float dt) {
    taskScanTimer_ += dt;
    if (taskScanTimer_ < taskScanInterval_)
        return;
    taskScanTimer_ = 0.0f;

    const double now = static_cast<double>(elapsedTime_);

    // Simple queue-based register tasks.
    if (autoRegisterTasks_) {
        const size_t laneCount = queueManager_.getLaneCount();
        for (size_t lane = 0; lane < laneCount; ++lane) {
            const int len =
                static_cast<int>(queueManager_.getQueueLength(static_cast<int>(lane)));
            const int threshold = 3;
            if (len >= threshold) {
                Task t;
                t.type     = TaskType::ProcessRegister;
                t.priority = len;
                t.targetId = static_cast<int>(lane);
                taskManager_.createTask(t, now);
            }
        }
    }

    // Very simple stock-shelves tasks: periodically choose a random edge that has cells.
    if (autoStockTasks_ && store_.numEdges() > 0) {
        std::uniform_int_distribution<int> edgeDist(0, std::max(0, store_.numEdges() - 1));
        for (int i = 0; i < 2; ++i) { // at most a couple of tasks per scan
            int eIdx = edgeDist(rng_);
            if (eIdx < 0 || eIdx >= store_.numEdges())
                continue;
            const Edge &edge = store_.edgeAt(eIdx);
            if (edge.getCellCount() == 0)
                continue;
            Task t;
            t.type     = TaskType::StockShelves;
            t.priority = 1;
            t.targetId = eIdx;
            taskManager_.createTask(t, now);
        }
    }

    // Assign tasks to idle workers.
    for (auto &w : workers_) {
        if (!w)
            continue;
        if (w->hasTasks())
            continue;
        if (auto taskOpt = taskManager_.requestTaskForWorker(*w)) {
            w->addTask(*taskOpt);
        }
    }
}

std::vector<WorkerSnapshot> Simulator::getWorkerSnapshots() const {
    std::vector<WorkerSnapshot> out;
    out.reserve(workers_.size());
    for (const auto &wPtr : workers_) {
        if (!wPtr)
            continue;
        const Worker &w = *wPtr;
        WorkerSnapshot s;
        s.id             = w.getId();
        s.posX           = w.getPosX();
        s.posZ           = w.getPosZ();
        s.canStock       = w.canStock();
        s.canServe       = w.canServe();
        s.happiness      = w.getHappiness();
        s.taskEfficiency = w.getTaskEfficiency();
        if (const Task *t = w.currentTask()) {
            s.hasTask      = true;
            s.taskType     = t->type;
            s.taskTargetId = t->targetId;
        } else {
            s.hasTask = false;
        }
        out.push_back(s);
    }
    return out;
}

void Simulator::finalizeCheckout(CustomerVisit &agent) {
    // Build timestamp string from current sim time
    const int    totalSec  = static_cast<int>(elapsedTime_);
    const int    hours     = totalSec / 3600;
    const int    minutes   = (totalSec % 3600) / 60;
    const int    seconds   = totalSec % 60;
    std::ostringstream ts;
    ts << std::setw(2) << std::setfill('0') << hours   << ':'
       << std::setw(2) << std::setfill('0') << minutes << ':'
       << std::setw(2) << std::setfill('0') << seconds;

    // Satisfaction: simple heuristic – full basket pays full price → max score.
    // Could be extended to factor in queue wait time, out-of-stock events, etc.
    const int satisfaction = (agent.basket.getSize() >= 3) ? 8 : 6;

    Transaction tx = agent.basket.toTransaction(nextTransactionId_++,
                                                satisfaction,
                                                ts.str());
    updateCustomerHistory(agent.cust.get(), tx);

    {
        std::lock_guard<std::mutex> lk(transactionMutex_);
        completedTransactions_.push_back(std::move(tx));
    }

    agent.basket.clear();
    agent.hasPaid = true;
}

void Simulator::removeDeadAgents() {
    // Two-pass: first finalize despawning agents, then erase them.
    // This avoids const_cast inside a remove_if lambda.
    for (auto &ag : agents_) {
        if (!ag->cust || ag->cust->getCurrentEdgeIndex() == -1) {
            if (ag->cust) {
                // If the agent despawns with unpaid items, generate a transaction.
                if (!ag->hasPaid && ag->basket.getSize() > 0)
                    finalizeCheckout(*ag);
                collisionManager_.unregisterAgent(ag->cust.get());
            }
        }
    }

    agents_.erase(
        std::remove_if(agents_.begin(), agents_.end(),
                       [](const std::unique_ptr<CustomerVisit> &a) {
                           return !a->cust || a->cust->getCurrentEdgeIndex() == -1;
                       }),
        agents_.end());
}

void Simulator::sampleQueues() {
    const size_t laneCount = queueManager_.getLaneCount();
    if (laneCount == 0)
        return;

    if (queueLengthsHistory_.empty() ||
        queueLengthsHistory_.size() != laneCount) {
        queueLengthsHistory_.assign(laneCount, {});
    }

    queueSampleTimes_.push_back(elapsedTime_);
    for (size_t lane = 0; lane < laneCount; ++lane) {
        const int len =
            static_cast<int>(queueManager_.getQueueLength(static_cast<int>(lane)));
        queueLengthsHistory_[lane].push_back(len);
    }
}

std::pair<int, int> Simulator::resolvePickCell(double px, double pz,
                                               int    edgeIdx,
                                               double distOnEdge,
                                               int    sku) const {
    // ── Strategy 1: Layout geometry (most accurate, matches what the renderer
    //                shows to the player) ─────────────────────────────────────
    if ((px != 0.0 || pz != 0.0) && !layout_.edgeGeoms.empty()) {
        int    bestEdge = -1, bestCell = -1;
        double bestDist = 1e99;

        for (const auto &[edgeId, geo] : layout_.edgeGeoms) {
            if (store_.getEdgeIdToIndex().count(edgeId) == 0)
                continue;
            const int   eIdx  = store_.edgeIndexById(edgeId);
            const Edge &edge  = store_.edgeAt(eIdx);
            const int   nCells = edge.getCellCount();

            for (int c = 0; c < nCells; ++c) {
                // Only consider cells that actually stock this SKU
                if (c >= static_cast<int>(edge.cells.size()))
                    break;
                if (!edge.cells[static_cast<size_t>(c)].containsSku(
                        static_cast<std::uint32_t>(sku)))
                    continue;

                const double frac = (c + 0.5) / static_cast<double>(nCells);
                const double cx   = geo.startX + (geo.endX - geo.startX) * frac;
                const double cz   = geo.startZ + (geo.endZ - geo.startZ) * frac;
                const double dx   = px - cx, dz = pz - cz;
                const double d2   = dx * dx + dz * dz;

                if (d2 < bestDist) {
                    bestDist = d2;
                    bestEdge = eIdx;
                    bestCell = c;
                }
            }
        }
        if (bestEdge >= 0 && bestCell >= 0)
            return {bestEdge, bestCell};

        // ── Strategy 2: StoreGraph::findClosestCell ──────────────────────────
        auto [e2, c2] = store_.findClosestCell(px, pz);
        if (e2 >= 0 && c2 >= 0 && e2 < store_.numEdges()) {
            const Edge &edge = store_.edgeAt(e2);
            if (c2 < static_cast<int>(edge.cells.size()) &&
                edge.cells[static_cast<size_t>(c2)].containsSku(
                    static_cast<std::uint32_t>(sku)))
                return {e2, c2};
        }
    }

    // ── Strategy 3: distOnEdge fallback ─────────────────────────────────────
    if (edgeIdx >= 0 && edgeIdx < store_.numEdges()) {
        const Edge &edge    = store_.edgeAt(edgeIdx);
        const double cellLen = edge.getCellLength();
        const int cellIdx   = (cellLen > 0.0)
                              ? static_cast<int>(distOnEdge / cellLen)
                              : 0;
        if (cellIdx >= 0 && cellIdx < static_cast<int>(edge.cells.size()))
            return {edgeIdx, cellIdx};
    }

    return {-1, -1};
}

} // namespace priceriot