#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui-SFML.h"
#include "imgui.h"
#include <SFML/Graphics.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>

#include "../agents/basket.h"
#include "../agents/customer.h"
#include "../agents/customer_behavior.h"
#include "../environment/checkout_queue.h"
#include "../environment/collision_manager.h"
#include "../environment/environment.h"
#include "../environment/products.h"
#include "../environment/shelf.h"
#include "../environment/store_layout.h"
#include "behavior_log.h"
#include "navmesh_visualizer.h"
#include "transaction.h"
#include <yaml-cpp/yaml.h>

using namespace priceriot;

// --- Section: Debug label drawing helpers ---
namespace {
constexpr bool kAgentLogEnabled = false;
struct NullLogStream {
    NullLogStream(const char *, std::ios_base::openmode) {}
    explicit operator bool() const { return false; }
    template <typename T> NullLogStream &operator<<(const T &) { return *this; }
};
using AgentLogStream = std::conditional_t<kAgentLogEnabled, std::ofstream, NullLogStream>;

static const char *nodeTypeStr(Node::NodeType t) {
    switch (t) {
        case Node::NodeType::Entrance:
            return "Entrance";
        case Node::NodeType::Exit:
            return "Exit";
        case Node::NodeType::Junction:
            return "Junction";
        case Node::NodeType::Register:
            return "Register";
        case Node::NodeType::Stockroom:
            return "Stockroom";
        default:
            return "?";
    }
}

void drawStoreLabels(sf::RenderWindow &window, const StoreLayout &layout, const StoreGraph &store,
                     const sf::Font &font, bool enable, float pxPerM, float offX, float offY) {
    if (!enable)
        return;
    const unsigned csize = 10u;
    for (const auto &[nid, geo] : layout.nodeGeoms) {
        if (store.getNodeIdToIndex().count(nid) == 0)
            continue;
        int idx = store.nodeIndexById(nid);
        const Node &n = store.nodeAt(idx);
        sf::Text text;
        text.setFont(font);
        text.setCharacterSize(csize);
        text.setFillColor(sf::Color::Cyan);
        text.setString(std::to_string(nid) + ": " + nodeTypeStr(n.getNodeType()));
        float sx = geo.x * pxPerM + offX, sy = geo.z * pxPerM + offY;
        text.setPosition(sx, sy);
        window.draw(text);
    }
    for (const auto &[eid, geo] : layout.edgeGeoms) {
        if (store.getEdgeIdToIndex().count(eid) == 0)
            continue;
        int idx = store.edgeIndexById(eid);
        const Edge &e = store.edgeAt(idx);
        int fromId = store.nodeAt(e.getFromNode()).getNodeId();
        int toId = store.nodeAt(e.getToNode()).getNodeId();
        sf::Text text;
        text.setFont(font);
        text.setCharacterSize(csize);
        text.setFillColor(sf::Color::Yellow);
        text.setString(std::to_string(eid) + " (" + std::to_string(fromId) + "->" +
                       std::to_string(toId) + ")");
        float mx = (geo.startX + geo.endX) * 0.5f * pxPerM + offX;
        float my = (geo.startZ + geo.endZ) * 0.5f * pxPerM + offY;
        text.setPosition(mx, my);
        window.draw(text);
    }
}
/** Resolve (posX, posZ) to (edgeIdx, cellIdx) using layout edge segments so cell centers match rendering. */
static std::pair<int, int> resolveClosestCellFromLayout(const StoreGraph &store,
                                                         const StoreLayout &layout, double px,
                                                         double pz) {
    int bestEdgeIdx = -1;
    int bestCellIdx = -1;
    double bestDistSq = 1e99;
    for (const auto &[edgeId, geo] : layout.edgeGeoms) {
        if (store.getEdgeIdToIndex().count(edgeId) == 0)
            continue;
        int edgeIdx = store.edgeIndexById(edgeId);
        const Edge &edge = store.edgeAt(edgeIdx);
        const int nCells = edge.getCellCount();
        if (nCells <= 0)
            continue;
        for (int c = 0; c < nCells; ++c) {
            double frac = (c + 0.5) / static_cast<double>(nCells);
            double cx = geo.startX + (geo.endX - geo.startX) * frac;
            double cz = geo.startZ + (geo.endZ - geo.startZ) * frac;
            double dx = px - cx, dz = pz - cz;
            double d2 = dx * dx + dz * dz;
            if (d2 < bestDistSq) {
                bestDistSq = d2;
                bestEdgeIdx = edgeIdx;
                bestCellIdx = c;
            }
        }
    }
    return {bestEdgeIdx, bestCellIdx};
}

} // namespace

// --- Section: Display constants ---
const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 800;
const float PIXELS_PER_METER = 20.0f;
const float OFFSET_X = WINDOW_WIDTH / 2.0f;
const float OFFSET_Y = WINDOW_HEIGHT / 2.0f;

// --- Section: Agent wrapper (Customer + Basket) ---
struct Agent {
    std::shared_ptr<Customer> cust;
    Basket basket;
    bool hasPaid = false;

    Agent(std::shared_ptr<Customer> c, Basket b, double missionProbability,
          std::default_random_engine &engine)
        : cust(c), basket(std::move(b)) {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        if (c->getTripPurpose() == Customer::TripPurpose::Mission || u(engine) < missionProbability)
            cust->setBehavior(new MissionBehavior());
        else
            cust->setBehavior(new DefaultBehavior());
    }

    bool update(float dt, const StoreGraph &store, CheckoutQueueManager *queueManager = nullptr) {
        bool alive = cust->update(dt, store, basket, queueManager);
        if (basket.getSize() == 0 && cust->getTotalSpent() > 0)
            hasPaid = true;
        return alive;
    }
};

// --- Section: Layout and navmesh visualizers ---
struct LayoutVisualizer {
    float pixelsPerMeter = PIXELS_PER_METER;
    float offsetX = OFFSET_X;
    float offsetY = OFFSET_Y;

    void draw(sf::RenderWindow &window, const StoreLayout &layout) {
        for (const auto &[id, geo] : layout.edgeGeoms) {
            auto corners = geo.getCorners();
            sf::ConvexShape shape;
            shape.setPointCount(4);
            for (int i = 0; i < 4; ++i) {
                shape.setPoint(i, sf::Vector2f(corners[i].x * pixelsPerMeter + offsetX,
                                               corners[i].y * pixelsPerMeter + offsetY));
            }
            shape.setFillColor(sf::Color(60, 60, 70));
            shape.setOutlineColor(sf::Color(100, 100, 100));
            shape.setOutlineThickness(1.0f);
            window.draw(shape);
        }

        for (const auto &[id, geo] : layout.nodeGeoms) {
            sf::RectangleShape shape;
            shape.setSize(sf::Vector2f(geo.width * pixelsPerMeter, geo.length * pixelsPerMeter));
            shape.setOrigin(shape.getSize() / 2.0f);
            shape.setPosition(geo.x * pixelsPerMeter + offsetX, geo.z * pixelsPerMeter + offsetY);

            shape.setFillColor(sf::Color(80, 80, 100));
            shape.setOutlineColor(sf::Color::White);
            shape.setOutlineThickness(1.0f);
            window.draw(shape);
        }
    }
};

// --- Section: Main entry and setup ---
int main() {
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "PriceRiot Simulator");
    window.setFramerateLimit(60);
    if (!ImGui::SFML::Init(window))
        return -1;

    StoreGraph store;
    StoreLayout layout;
    CheckoutQueueManager queueManager;

    try {
        store.loadFromYaml("store.yaml");
        layout.buildGeometry(store);
        store.buildNavMesh(layout);
        store.buildPhysicsWorld(layout);

        // Load checkout queue configuration
        YAML::Node storeYaml = YAML::LoadFile("store.yaml");
        if (storeYaml["checkout_queues"]) {
            queueManager.loadFromYaml(storeYaml["checkout_queues"]);
        }
    } catch (const std::exception &ex) {
        std::cerr << "Critical Error: " << ex.what() << "\n";
        return 1;
    }

    // --- Section: Font loading for debug labels ---
    sf::Font debugFont;
    bool debugFontLoaded = false;
#if defined(_WIN32) || defined(_WIN64)
    if (debugFont.loadFromFile("C:/Windows/Fonts/arial.ttf"))
        debugFontLoaded = true;
#endif
    if (!debugFontLoaded && debugFont.loadFromFile("fonts/arial.ttf"))
        debugFontLoaded = true;

    // --- Section: Agent list and visualizer initialization ---
    std::vector<std::unique_ptr<Agent>> agents;
    std::vector<std::shared_ptr<Customer>> raw_customers_ref;
    std::random_device rd;
    unsigned long long simSeed = rd();
    std::default_random_engine rng(static_cast<std::default_random_engine::result_type>(simSeed));

    // Calculate store center for proper centering
    float storeCenterX, storeCenterZ;
    layout.getCenter(storeCenterX, storeCenterZ);

    // Calculate dynamic offsets to center the store
    float dynamicOffsetX = OFFSET_X - storeCenterX * PIXELS_PER_METER;
    float dynamicOffsetY = OFFSET_Y - storeCenterZ * PIXELS_PER_METER;

    LayoutVisualizer visualizer;
    NavMeshVisualizer navmeshVisualizer;
    CollisionManager collisionManager;

    // Initialize visualizers with dynamic centering
    visualizer.pixelsPerMeter = PIXELS_PER_METER;
    visualizer.offsetX = dynamicOffsetX;
    visualizer.offsetY = dynamicOffsetY;

    navmeshVisualizer.pixelsPerMeter = PIXELS_PER_METER;
    navmeshVisualizer.offsetX = dynamicOffsetX;
    navmeshVisualizer.offsetY = dynamicOffsetY;

    // Initialize collision manager
    if (store.hasPhysicsWorld()) {
        collisionManager.setPhysicsWorld(&store.getPhysicsWorld());
    }

    // --- Section: Main simulation loop ---
    sf::Clock deltaClock;
    float spawnTimer = 0.0f;
    float spawnInterval = 10.0f;
    float timeScale = 1.0f;
    bool isPaused = true;
    bool showStoreLabels = false;
    bool hasStarted = false;
    bool stepOnce = false;
    bool showConfigModal = true;
    float runDuration = 0.0f;
    float elapsedRunTime = 0.0f;
    float missionWeight = 0.2f;
    float defaultWeight = 0.8f;

    // --- Section: Customer debug (behavior log + ImGui) ---
    BehaviorEventLog behaviorLog;
    bool enableBehaviorLog = false;
    bool logFocusedOnly = true;
    char behaviorLogPathBuf[256] = "behavior_log.csv";
    int selectedCustomerId = -1;
    bool showPathTrail = false;
    static constexpr size_t PATH_TRAIL_MAX = 500;
    std::deque<std::pair<double, double>> pathTrail;
    float simTimeAccum = 0.0f;

    // In-GUI live behavior log (ring buffer, no cout)
    struct GuiBehaviorEntry {
        double simTime;
        int customerId;
        std::string stateName;
        int decisionType;
        int targetId;
        int basketSize;
        int edgeIndex;
    };
    static constexpr size_t GUI_BEHAVIOR_LOG_MAX = 100;
    std::deque<GuiBehaviorEntry> guiBehaviorLog;
    bool showGuiBehaviorLogAll = false;

    while (window.isOpen()) {
        sf::Event event{};
        while (window.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(window, event);
            if (event.type == sf::Event::Closed)
                window.close();
        }

        sf::Time dtObj = deltaClock.restart();
        ImGui::SFML::Update(window, dtObj);
        float dt = dtObj.asSeconds() * timeScale;
        float missionProbability = 0.5f;
        float weightSum = missionWeight + defaultWeight;
        if (weightSum > 0.0f)
            missionProbability = missionWeight / weightSum;

        bool runComplete = (runDuration > 0.0f && elapsedRunTime >= runDuration);
        if (runComplete) {
            isPaused = true;
            stepOnce = false;
        }

        bool shouldSimulate = hasStarted && (!isPaused || stepOnce) && !runComplete;

        if (shouldSimulate) {
            // --- Spawn new agents ---
            spawnTimer += dt;
            if (spawnTimer >= spawnInterval && store.numEdges() > 0) {
                spawnTimer = 0.0f;
                auto [fst, snd] = newCustomer(raw_customers_ref, rng);

                auto ag = std::make_unique<Agent>(fst, std::move(snd), missionProbability, rng);

                int startEdgeIdx = -1;
                for (int i = 0; i < store.numEdges(); ++i) {
                    if (store.nodeAt(store.edgeAt(i).getFromNode()).getNodeType() ==
                        Node::NodeType::Entrance) {
                        startEdgeIdx = i;
                        break;
                    }
                }

                if (startEdgeIdx != -1) {
                    ag->cust->currentEdgeIndex = startEdgeIdx;
                    ag->cust->distOnEdge = 0.0;
                    ag->cust->speed = 0.75;

                    // Initialize world position from entrance node
                    for (int i = 0; i < store.numNodes(); ++i) {
                        if (store.nodeAt(i).getNodeType() == Node::NodeType::Entrance) {
                            const auto &entranceNode = store.nodeAt(i);
                            ag->cust->setPosition(entranceNode.getX(), entranceNode.getZ());
                            break;
                        }
                    }

                    // Register agent for collision detection
                    collisionManager.registerAgent(ag->cust.get(), 0.35);

                    agents.push_back(std::move(ag));
                }
            }

            // --- Collision avoidance and agent updates ---
            // Ensure all agents are registered
            for (auto &agent : agents) {
                collisionManager.registerAgent(agent->cust.get(), 0.35);
            }

            // Update agents with collision avoidance
            for (auto &agent : agents) {
                // Apply avoidance steering before movement
                if (store.hasPhysicsWorld() && agent->cust) {
                    double avoidX = 0.0, avoidZ = 0.0;
                    collisionManager.getAvoidanceVector(
                        agent->cust->getPosX(), agent->cust->getPosZ(), 0.35, avoidX, avoidZ, 2.0);

                    // Apply small avoidance offset to position
                    if (std::abs(avoidX) > 0.01 || std::abs(avoidZ) > 0.01) {
                        double avoidanceStrength = 0.3 * dt; // Scale by time
                        double newX = agent->cust->getPosX() + avoidX * avoidanceStrength;
                        double newZ = agent->cust->getPosZ() + avoidZ * avoidanceStrength;

                        // Only apply if valid position
                        if (store.getPhysicsWorld().isValidPosition(newX, newZ, 0.35)) {
                            agent->cust->setPosition(newX, newZ);
                        }
                    }
                }

                // Update agent behavior
                agent->update(dt, store, &queueManager);

                // When behavior returns PickProduct, add the product to the basket and decrement shelf inventory
                if (agent->cust->getLastDecisionType() == static_cast<int>(Decision::DecisionType::PickProduct)) {
                    int sku = agent->cust->getLastDecisionTargetId();
                    // #region agent log
                    { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"D\",\"location\":\"sim.cpp:basket_add\",\"message\":\"Adding product to basket\",\"data\":{\"customerId\":" << agent->cust->getId() << ",\"sku\":" << sku << ",\"basketSizeBefore\":" << agent->basket.getSize() << ",\"productExists\":" << (store.catalog.productExists(sku) ? "true" : "false") << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
                    // #endregion
                    if (sku >= 0 && store.catalog.productExists(sku)) {
                        agent->basket.addProduct(store.catalog.getProduct(sku));
                        double px = agent->cust->getPosX(), pz = agent->cust->getPosZ();
                        int edgeIdx = agent->cust->currentEdgeIndex;
                        int cellIdx = -1;
                        if (px != 0.0 || pz != 0.0) {
                            if (!layout.edgeGeoms.empty()) {
                                auto [e, c] = resolveClosestCellFromLayout(store, layout, px, pz);
                                if (e >= 0 && c >= 0 && e < store.numEdges()) {
                                    const Edge &closestEdge = store.edgeAt(e);
                                    if (c < static_cast<int>(closestEdge.cells.size()) &&
                                        closestEdge.cells[static_cast<size_t>(c)].containsSku(
                                            static_cast<std::uint32_t>(sku))) {
                                        edgeIdx = e;
                                        cellIdx = c;
                                    }
                                }
                            }
                            if (cellIdx < 0) {
                                auto [e, c] = store.findClosestCell(px, pz);
                                if (e >= 0 && c >= 0 && e < store.numEdges()) {
                                    const Edge &closestEdge = store.edgeAt(e);
                                    if (c < static_cast<int>(closestEdge.cells.size()) &&
                                        closestEdge.cells[static_cast<size_t>(c)].containsSku(
                                            static_cast<std::uint32_t>(sku))) {
                                        edgeIdx = e;
                                        cellIdx = c;
                                    }
                                }
                            }
                        }
                        if (cellIdx < 0 && edgeIdx >= 0 && edgeIdx < store.numEdges()) {
                            Edge &edge = store.mutableEdgeAt(edgeIdx);
                            double cellLen = edge.getCellLength();
                            cellIdx = (cellLen > 0) ? static_cast<int>(agent->cust->distOnEdge / cellLen) : 0;
                            if (cellIdx < 0 || cellIdx >= static_cast<int>(edge.cells.size()))
                                cellIdx = -1;
                        }
                        if (edgeIdx >= 0 && edgeIdx < store.numEdges() && cellIdx >= 0) {
                            Edge &edge = store.mutableEdgeAt(edgeIdx);
                            if (cellIdx < static_cast<int>(edge.cells.size())) {
                                edge.cells[static_cast<size_t>(cellIdx)].takeOneBySku(
                                    static_cast<std::uint32_t>(sku));
                            }
                        }
                    }
                }
            }

            simTimeAccum += dt;
            elapsedRunTime += dt;

            // Behavior event log (per-tick, after updates so last-decision is set)
            if (enableBehaviorLog && behaviorLog.isOpen()) {
                for (const auto &agent : agents) {
                    if (!agent->cust)
                        continue;
                    double px = agent->cust->getPosX();
                    double pz = agent->cust->getPosZ();
                    if (px == 0.0 && pz == 0.0 && agent->cust->currentEdgeIndex >= 0 &&
                        agent->cust->currentEdgeIndex < store.numEdges()) {
                        int edgeId = store.edgeAt(agent->cust->currentEdgeIndex).getEdgeId();
                        if (layout.edgeGeoms.count(edgeId)) {
                            const auto &geo = layout.edgeGeoms.at(edgeId);
                            float edgeLen = std::sqrt(std::pow(geo.endX - geo.startX, 2) +
                                                      std::pow(geo.endZ - geo.startZ, 2));
                            float t = edgeLen > 0
                                          ? static_cast<float>(agent->cust->distOnEdge) / edgeLen
                                          : 0;
                            t = std::max(0.0f, std::min(1.0f, t));
                            px = geo.startX + (geo.endX - geo.startX) * t;
                            pz = geo.startZ + (geo.endZ - geo.startZ) * t;
                        }
                    }
                    const char *behaviorType =
                        agent->cust->getBehavior() ? agent->cust->getBehavior()->getBehaviorType() : "Unknown";
                    const char *stateName =
                        agent->cust->getBehavior() ? agent->cust->getBehavior()->getStateName() : "Unknown";
                    behaviorLog.logTick(simTimeAccum, agent->cust->getId(), px, pz,
                                        behaviorType, stateName,
                                        agent->cust->getLastDecisionType(),
                                        agent->cust->getLastDecisionTargetId(),
                                        agent->basket.getSize(),
                                        agent->cust->currentEdgeIndex,
                                        agent->cust->getDwellTicks());
                }
            }

            // In-GUI behavior log: push recent events (selected customer or all)
            for (const auto &agent : agents) {
                if (!agent->cust)
                    continue;
                if (!showGuiBehaviorLogAll && agent->cust->getId() != selectedCustomerId)
                    continue;
                const char *stateName =
                    agent->cust->getBehavior() ? agent->cust->getBehavior()->getStateName() : "?";
                guiBehaviorLog.push_back({simTimeAccum, agent->cust->getId(), stateName,
                                          agent->cust->getLastDecisionType(),
                                          agent->cust->getLastDecisionTargetId(),
                                          agent->basket.getSize(),
                                          agent->cust->currentEdgeIndex});
                while (guiBehaviorLog.size() > GUI_BEHAVIOR_LOG_MAX)
                    guiBehaviorLog.pop_front();
            }

            // Path trail for selected customer
            if (selectedCustomerId >= 0 && showPathTrail) {
                for (const auto &agent : agents) {
                    if (agent->cust && agent->cust->getId() == selectedCustomerId) {
                        double px = agent->cust->getPosX();
                        double pz = agent->cust->getPosZ();
                        if (px == 0.0 && pz == 0.0 && agent->cust->currentEdgeIndex >= 0 &&
                            agent->cust->currentEdgeIndex < store.numEdges()) {
                            int edgeId = store.edgeAt(agent->cust->currentEdgeIndex).getEdgeId();
                            if (layout.edgeGeoms.count(edgeId)) {
                                const auto &geo = layout.edgeGeoms.at(edgeId);
                                float edgeLen = std::sqrt(std::pow(geo.endX - geo.startX, 2) +
                                                          std::pow(geo.endZ - geo.startZ, 2));
                                float t = edgeLen > 0
                                              ? static_cast<float>(agent->cust->distOnEdge) / edgeLen
                                              : 0;
                                t = std::max(0.0f, std::min(1.0f, t));
                                px = geo.startX + (geo.endX - geo.startX) * t;
                                pz = geo.startZ + (geo.endZ - geo.startZ) * t;
                            }
                        }
                        pathTrail.emplace_back(px, pz);
                        while (pathTrail.size() > PATH_TRAIL_MAX)
                            pathTrail.pop_front();
                        break;
                    }
                }
            }

            // Resolve any remaining collisions (push agents apart)
            for (auto &agent : agents) {
                if (store.hasPhysicsWorld() && agent->cust) {
                    collisionManager.resolveCollisions(agent->cust.get(), 0.35);
                }
            }

            // Remove dead agents
            agents.erase(std::remove_if(agents.begin(), agents.end(),
                                        [&](const std::unique_ptr<Agent> &a) {
                                            bool shouldRemove =
                                                !a->cust || a->cust->currentEdgeIndex == -1;
                                            // #region agent log
                                            if (shouldRemove && a->cust) {
                                                AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app);
                                                if(lf) lf << "{\"hypothesisId\":\"E\",\"location\":\"sim.cpp:agent_removal\",\"message\":\"Removing agent from pool\",\"data\":{\"customerId\":" << a->cust->getId() << ",\"edgeIndex\":" << a->cust->currentEdgeIndex << ",\"posX\":" << a->cust->getPosX() << ",\"posZ\":" << a->cust->getPosZ() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
                                                collisionManager.unregisterAgent(a->cust.get());
                                            }
                                            // #endregion
                                            return shouldRemove;
                                        }),
                         agents.end());
            if (stepOnce) {
                stepOnce = false;
                isPaused = true;
            }
            if (runDuration > 0.0f && elapsedRunTime >= runDuration) {
                isPaused = true;
            }
        }

        if (!hasStarted && showConfigModal)
            ImGui::OpenPopup("Run Configuration");
        if (ImGui::BeginPopupModal("Run Configuration", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Configure simulation run");
            ImGui::Separator();
            ImGui::SliderFloat("Spawn Interval (s)", &spawnInterval, 0.1f, 5.0f);
            ImGui::InputFloat("Run Duration (s)", &runDuration, 1.0f, 10.0f, "%.1f");
            if (runDuration < 0.0f)
                runDuration = 0.0f;
            ImGui::SliderFloat("Mission Weight", &missionWeight, 0.0f, 1.0f);
            ImGui::SliderFloat("Default Weight", &defaultWeight, 0.0f, 1.0f);
            float modalTotal = missionWeight + defaultWeight;
            float modalMissionProb = (modalTotal > 0.0f) ? (missionWeight / modalTotal) : 0.5f;
            ImGui::Text("Mission probability: %.2f", modalMissionProb);
            ImGui::Separator();
            if (ImGui::Button("Start")) {
                hasStarted = true;
                isPaused = false;
                stepOnce = false;
                elapsedRunTime = 0.0f;
                simTimeAccum = 0.0f;
                spawnTimer = 0.0f;
                showConfigModal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                showConfigModal = false;
                isPaused = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // --- ImGui control panel ---
        ImGui::Begin("Simulation Control");
        ImGui::Text("Nodes: %d  Edges: %d", store.numNodes(), store.numEdges());
        if (store.hasNavMesh()) {
            ImGui::Text("Navmesh: %d polygons", store.getNavMesh().getPolygonCount());

            ImGui::Separator();
            ImGui::Text("Navmesh Visualization:");
            ImGui::Checkbox("Show Polygons", &navmeshVisualizer.showPolygons);
            ImGui::Checkbox("Show Connections", &navmeshVisualizer.showConnections);
            ImGui::Checkbox("Show Agent Paths", &navmeshVisualizer.showPaths);
            ImGui::Checkbox("Show Polygon Centers", &navmeshVisualizer.showCenters);
        }
        ImGui::Separator();
        ImGui::Text("Active Agents: %zu", agents.size());
        if (!hasStarted) {
            ImGui::Text("Run not started");
            if (ImGui::Button("Configure Run"))
                showConfigModal = true;
            ImGui::SameLine();
            if (ImGui::Button("Start Run")) {
                hasStarted = true;
                isPaused = false;
                stepOnce = false;
                elapsedRunTime = 0.0f;
                simTimeAccum = 0.0f;
                spawnTimer = 0.0f;
            }
        }
        ImGui::Checkbox("Pause Sim", &isPaused);
        ImGui::SameLine();
        if (ImGui::Button("Step")) {
            if (hasStarted && isPaused)
                stepOnce = true;
        }
        ImGui::SliderFloat("Spawn Interval (s)", &spawnInterval, 0.1f, 5.0f);
        ImGui::SliderFloat("Time Scale", &timeScale, 0.1f, 10.0f);
        ImGui::InputFloat("Run Duration (s)", &runDuration, 1.0f, 10.0f, "%.1f");
        if (runDuration < 0.0f)
            runDuration = 0.0f;
        if (runDuration > 0.0f && elapsedRunTime >= runDuration) {
            ImGui::Text("Run complete");
        }
        ImGui::SliderFloat("Mission Weight", &missionWeight, 0.0f, 1.0f);
        ImGui::SliderFloat("Default Weight", &defaultWeight, 0.0f, 1.0f);
        float controlTotal = missionWeight + defaultWeight;
        float controlMissionProb = (controlTotal > 0.0f) ? (missionWeight / controlTotal) : 0.5f;
        ImGui::Text("Mission probability: %.2f", controlMissionProb);
        ImGui::Separator();
        ImGui::Text("Store Debug:");
        ImGui::Checkbox("Show Node/Edge Labels", &showStoreLabels);
        ImGui::End();

        // --- ImGui Customer Debug panel ---
        ImGui::Begin("Customer Debug");
        bool wasLogEnabled = enableBehaviorLog;
        ImGui::Checkbox("Enable behavior logging", &enableBehaviorLog);
        if (enableBehaviorLog && !wasLogEnabled) {
            if (behaviorLog.open(behaviorLogPathBuf)) {
                behaviorLog.writeSeedComment(simSeed);
                behaviorLog.setFocusedOnly(logFocusedOnly);
                behaviorLog.setFocusedCustomerId(selectedCustomerId);
            }
        } else if (!enableBehaviorLog && wasLogEnabled) {
            behaviorLog.close();
        }
        if (enableBehaviorLog) {
            ImGui::InputText("Log path", behaviorLogPathBuf, sizeof(behaviorLogPathBuf));
            ImGui::Checkbox("Focused customer only", &logFocusedOnly);
            behaviorLog.setFocusedOnly(logFocusedOnly);
            behaviorLog.setFocusedCustomerId(selectedCustomerId);
        }

        ImGui::Separator();
        ImGui::Text("Behavior log (live):");
        ImGui::Checkbox("Show all agents", &showGuiBehaviorLogAll);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            guiBehaviorLog.clear();
        if (ImGui::BeginChild("BehaviorLogScroll", ImVec2(0, 120), true)) {
            static const char *decisionNames[] = {"Move", "SwitchEdge", "PickProduct", "Wait",
                                                 "Checkout", "Despawn"};
            for (const auto &entry : guiBehaviorLog) {
                const char *dname = (entry.decisionType >= 0 && entry.decisionType < 6)
                                        ? decisionNames[entry.decisionType]
                                        : "?";
                ImGui::Text("%.1fs  ID %d  %s  %s  target=%d  basket=%d  edge=%d",
                            entry.simTime, entry.customerId, entry.stateName.c_str(), dname,
                            entry.targetId, entry.basketSize, entry.edgeIndex);
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Text("Select customer to inspect:");
        for (const auto &agent : agents) {
            if (!agent->cust)
                continue;
            int cid = agent->cust->getId();
            const char *btype = agent->cust->getBehavior() ?
                agent->cust->getBehavior()->getBehaviorType() : "?";
            char label[64];
            snprintf(label, sizeof(label), "ID %d  basket=%d  %s", cid,
                     agent->basket.getSize(), btype);
            if (ImGui::Selectable(label, selectedCustomerId == cid)) {
                selectedCustomerId = cid;
                pathTrail.clear();
            }
        }
        if (selectedCustomerId >= 0) {
            bool found = false;
            for (const auto &agent : agents)
                if (agent->cust && agent->cust->getId() == selectedCustomerId) {
                    found = true;
                    break;
                }
            if (!found) {
                selectedCustomerId = -1;
                pathTrail.clear();
            }
        }

        ImGui::Separator();
        if (selectedCustomerId >= 0) {
            const Customer *selCust = nullptr;
            const Basket *selBasket = nullptr;
            for (const auto &agent : agents) {
                if (agent->cust && agent->cust->getId() == selectedCustomerId) {
                    selCust = agent->cust.get();
                    selBasket = &agent->basket;
                    break;
                }
            }
            if (selCust && selBasket) {
                ImGui::Text("State: %s", selCust->getBehavior() ?
                    selCust->getBehavior()->getStateName() : "?");
                ImGui::Text("Position: %.2f, %.2f", selCust->getPosX(), selCust->getPosZ());
                ImGui::Text("Edge index: %d", selCust->currentEdgeIndex);
                static const char *decisionNames[] = {"Move", "SwitchEdge", "PickProduct", "Wait",
                                                     "Checkout", "Despawn"};
                int dt = selCust->getLastDecisionType();
                const char *dname = (dt >= 0 && dt < 6) ? decisionNames[dt] : "?";
                ImGui::Text("Last decision: %s (target %d)", dname, selCust->getLastDecisionTargetId());
                ImGui::Text("Basket size: %d", selBasket->getSize());
                ImGui::Text("Dwell ticks: %d", selCust->getDwellTicks());
            }
            ImGui::Checkbox("Show path trail", &showPathTrail);
        } else {
            ImGui::TextDisabled("Select a customer from the list above.");
        }
        ImGui::End();

        window.clear(sf::Color(30, 30, 40));

        // --- Render: store layout, navmesh, physics, agents ---
        // Draw store layout (base layer)
        visualizer.draw(window, layout);
        if (debugFontLoaded && showStoreLabels)
            drawStoreLabels(window, layout, store, debugFont, showStoreLabels, PIXELS_PER_METER,
                            dynamicOffsetX, dynamicOffsetY);

        // Draw navmesh visualization (overlay layer)
        if (store.hasNavMesh()) {
            const NavMesh &navmesh = store.getNavMesh();

            // Draw polygons
            navmeshVisualizer.drawPolygons(window, navmesh);

            // Draw connections
            navmeshVisualizer.drawConnections(window, navmesh);

            // Draw polygon centers
            navmeshVisualizer.drawCenters(window, navmesh);

            // Draw agent paths
            if (navmeshVisualizer.showPaths) {
                for (const auto &agent : agents) {
                    if (agent->cust->isUsingNavmesh() && !agent->cust->getNavmeshPath().empty()) {
                        navmeshVisualizer.drawPath(window, agent->cust->getNavmeshPath());
                    }
                }
            }
        }

        // Draw physics world obstacles (for debugging)
        if (store.hasPhysicsWorld()) {
            const PhysicsWorld &physics = store.getPhysicsWorld();

            // Draw obstacles (shelves)
            sf::RectangleShape obstacleShape;
            obstacleShape.setFillColor(sf::Color(150, 100, 50, 100)); // Semi-transparent brown
            obstacleShape.setOutlineColor(sf::Color(200, 150, 100));
            obstacleShape.setOutlineThickness(1.0f);

            for (const auto &obstacle : physics.getObstacles()) {
                float width =
                    static_cast<float>((obstacle.maxX - obstacle.minX) * PIXELS_PER_METER);
                float height =
                    static_cast<float>((obstacle.maxZ - obstacle.minZ) * PIXELS_PER_METER);
                obstacleShape.setSize(sf::Vector2f(width, height));
                obstacleShape.setPosition(
                    static_cast<float>(obstacle.minX * PIXELS_PER_METER + dynamicOffsetX),
                    static_cast<float>(obstacle.minZ * PIXELS_PER_METER + dynamicOffsetY));
                window.draw(obstacleShape);
            }

            // Draw boundaries (walls) - optional, can be commented out if too cluttered
            sf::RectangleShape boundaryShape;
            boundaryShape.setFillColor(sf::Color(100, 100, 150, 80)); // Semi-transparent blue
            boundaryShape.setOutlineColor(sf::Color(150, 150, 200));
            boundaryShape.setOutlineThickness(1.0f);

            for (const auto &boundary : physics.getBoundaries()) {
                float width =
                    static_cast<float>((boundary.maxX - boundary.minX) * PIXELS_PER_METER);
                float height =
                    static_cast<float>((boundary.maxZ - boundary.minZ) * PIXELS_PER_METER);
                boundaryShape.setSize(sf::Vector2f(width, height));
                boundaryShape.setPosition(
                    static_cast<float>(boundary.minX * PIXELS_PER_METER + dynamicOffsetX),
                    static_cast<float>(boundary.minZ * PIXELS_PER_METER + dynamicOffsetY));
                window.draw(boundaryShape);
            }
        }

        // Path trail for selected customer (draw before agents)
        if (showPathTrail && pathTrail.size() >= 2) {
            sf::VertexArray lineStrip(sf::LineStrip, pathTrail.size());
            for (size_t i = 0; i < pathTrail.size(); ++i) {
                float sx = static_cast<float>(pathTrail[i].first * PIXELS_PER_METER + dynamicOffsetX);
                float sy = static_cast<float>(pathTrail[i].second * PIXELS_PER_METER + dynamicOffsetY);
                lineStrip[i].position = sf::Vector2f(sx, sy);
                lineStrip[i].color = sf::Color(255, 200, 0, 120);
            }
            window.draw(lineStrip);
        }

        sf::CircleShape agentShape(6.0f);
        agentShape.setOrigin(3.0f, 3.0f);

        for (const auto &agent : agents) {
            if (agent->cust->currentEdgeIndex != -1) {
                bool selected = (agent->cust->getId() == selectedCustomerId);
                if (selected) {
                    agentShape.setRadius(8.0f);
                    agentShape.setOrigin(4.0f, 4.0f);
                    agentShape.setFillColor(sf::Color(255, 165, 0)); // Orange highlight
                } else {
                    agentShape.setRadius(6.0f);
                    agentShape.setOrigin(3.0f, 3.0f);
                    if (agent->cust->getDwellTicks() > 0)
                        agentShape.setFillColor(sf::Color::White);
                    else if (agent->hasPaid)
                        agentShape.setFillColor(sf::Color::Green);
                    else if (agent->basket.getSize() > 0)
                        agentShape.setFillColor(sf::Color::Magenta);
                    else
                        agentShape.setFillColor(sf::Color::Cyan);
                }

                // Use world position if available (navmesh), otherwise fall back to edge-based
                float currX, currZ;
                if (agent->cust->getPosX() != 0.0 || agent->cust->getPosZ() != 0.0) {
                    currX = static_cast<float>(agent->cust->getPosX());
                    currZ = static_cast<float>(agent->cust->getPosZ());
                } else {
                    int eIdx = agent->cust->currentEdgeIndex;
                    if (eIdx >= 0 && eIdx < store.numEdges()) {
                        int edgeId = store.edgeAt(eIdx).getEdgeId();
                        if (layout.edgeGeoms.count(edgeId)) {
                            const auto &geo = layout.edgeGeoms.at(edgeId);
                            float edgeLen = std::sqrt(std::pow(geo.endX - geo.startX, 2) +
                                                      std::pow(geo.endZ - geo.startZ, 2));
                            float t = edgeLen > 0
                                          ? static_cast<float>(agent->cust->distOnEdge) / edgeLen
                                          : 0;
                            t = std::max(0.0f, std::min(1.0f, t));
                            currX = geo.startX + (geo.endX - geo.startX) * t;
                            currZ = geo.startZ + (geo.endZ - geo.startZ) * t;
                        } else {
                            continue;
                        }
                    } else {
                        continue;
                    }
                }

                agentShape.setPosition(currX * PIXELS_PER_METER + dynamicOffsetX,
                                       currZ * PIXELS_PER_METER + dynamicOffsetY);
                window.draw(agentShape);
            }
        }

        ImGui::SFML::Render(window);
        window.display();
    }

    ImGui::SFML::Shutdown();
    return 0;
}