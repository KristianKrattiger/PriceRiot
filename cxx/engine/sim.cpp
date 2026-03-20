/**
 * @file sim.cpp
 * @brief SFML visualiser entry point.
 *
 * All simulation logic (spawning, updates, collision, transactions) has moved
 * to engine/simulator.h/cpp.  This file owns only the window, ImGui panels,
 * and rendering.  It drives the simulation by calling Simulator::step() each
 * frame and reading back agent/store state for drawing.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui-SFML.h"
#include "imgui.h"
#include <SFML/Graphics.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>

#include "behavior_log.h"
#include "navmesh_visualizer.h"
#include "simulator.h"      // ← single include now replaces the old agent/env soup

using namespace priceriot;

// ─────────────────────────────────────────────────────────────────────────────
// Display constants
// ─────────────────────────────────────────────────────────────────────────────
constexpr int   WINDOW_WIDTH     = 1280;
constexpr int   WINDOW_HEIGHT    = 800;
constexpr float PIXELS_PER_METER = 20.0f;
constexpr float OFFSET_X         = WINDOW_WIDTH  / 2.0f;
constexpr float OFFSET_Y         = WINDOW_HEIGHT / 2.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Debug label helpers  (local to this translation unit)
// ─────────────────────────────────────────────────────────────────────────────
namespace {

static const char *nodeTypeStr(Node::NodeType t) {
    switch (t) {
        case Node::NodeType::Entrance:  return "Entrance";
        case Node::NodeType::Exit:      return "Exit";
        case Node::NodeType::Junction:  return "Junction";
        case Node::NodeType::Register:  return "Register";
        case Node::NodeType::Stockroom: return "Stockroom";
        default:                        return "?";
    }
}

void drawStoreLabels(sf::RenderWindow &window, const StoreLayout &layout,
                     const StoreGraph &store, const sf::Font &font,
                     bool enable, float pxPerM, float offX, float offY) {
    if (!enable) return;
    constexpr unsigned kSize = 10u;

    for (const auto &[nid, geo] : layout.nodeGeoms) {
        if (store.getNodeIdToIndex().count(nid) == 0) continue;
        int idx = store.nodeIndexById(nid);
        const Node &n = store.nodeAt(idx);
        sf::Text text;
        text.setFont(font);
        text.setCharacterSize(kSize);
        text.setFillColor(sf::Color::Cyan);
        text.setString(std::to_string(nid) + ": " + nodeTypeStr(n.getNodeType()));
        text.setPosition(geo.x * pxPerM + offX, geo.z * pxPerM + offY);
        window.draw(text);
    }

    for (const auto &[eid, geo] : layout.edgeGeoms) {
        if (store.getEdgeIdToIndex().count(eid) == 0) continue;
        int idx = store.edgeIndexById(eid);
        const Edge &e = store.edgeAt(idx);
        int fromId = store.nodeAt(e.getFromNode()).getNodeId();
        int toId   = store.nodeAt(e.getToNode()).getNodeId();
        sf::Text text;
        text.setFont(font);
        text.setCharacterSize(kSize);
        text.setFillColor(sf::Color::Yellow);
        text.setString(std::to_string(eid) + " (" + std::to_string(fromId) +
                       "->" + std::to_string(toId) + ")");
        float mx = (geo.startX + geo.endX) * 0.5f * pxPerM + offX;
        float my = (geo.startZ + geo.endZ) * 0.5f * pxPerM + offY;
        text.setPosition(mx, my);
        window.draw(text);
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// LayoutVisualizer
// ─────────────────────────────────────────────────────────────────────────────
struct LayoutVisualizer {
    float pixelsPerMeter = PIXELS_PER_METER;
    float offsetX        = OFFSET_X;
    float offsetY        = OFFSET_Y;

    void draw(sf::RenderWindow &window, const StoreLayout &layout) const {
        for (const auto &[id, geo] : layout.edgeGeoms) {
            auto corners = geo.getCorners();
            sf::ConvexShape shape;
            shape.setPointCount(4);
            for (int i = 0; i < 4; ++i)
                shape.setPoint(i, sf::Vector2f(corners[i].x * pixelsPerMeter + offsetX,
                                               corners[i].y * pixelsPerMeter + offsetY));
            shape.setFillColor(sf::Color(60, 60, 70));
            shape.setOutlineColor(sf::Color(100, 100, 100));
            shape.setOutlineThickness(1.0f);
            window.draw(shape);
        }

        for (const auto &[id, geo] : layout.nodeGeoms) {
            sf::RectangleShape shape;
            shape.setSize(sf::Vector2f(geo.width  * pixelsPerMeter,
                                       geo.length * pixelsPerMeter));
            shape.setOrigin(shape.getSize() / 2.0f);
            shape.setPosition(geo.x * pixelsPerMeter + offsetX,
                              geo.z * pixelsPerMeter + offsetY);
            shape.setFillColor(sf::Color(80, 80, 100));
            shape.setOutlineColor(sf::Color::White);
            shape.setOutlineThickness(1.0f);
            window.draw(shape);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char **argv) {
    // ── Window setup ────────────────────────────────────────────────────────
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT),
                            "PriceRiot Simulator");
    window.setFramerateLimit(60);
    if (!ImGui::SFML::Init(window))
        return -1;

    // ── Simulator construction ───────────────────────────────────────────────
    std::string storePath = "store.yaml";
    if (argc > 1 && argv[1] && std::strlen(argv[1]) > 0) {
        storePath = argv[1];
    }
    // Default settings; the user can adjust via the Config modal before starting.
    float spawnInterval      = 5.0f;
    float missionWeight      = 1.0f;
    float defaultWeight      = 1.0f;

    // Staff configuration (used by the Simulator when starting a run).
    int   numStockers        = 2;
    int   numCashiers        = 1;
    bool  autoStockTasks     = true;
    bool  autoRegisterTasks  = true;

    std::unique_ptr<Simulator> simPtr;
    try {
        simPtr = std::make_unique<Simulator>(storePath, spawnInterval, 0.5f);
    } catch (const std::exception &ex) {
        std::cerr << "Critical Error while loading store '" << storePath
                  << "': " << ex.what() << "\n";
        return 1;
    }
    Simulator &sim = *simPtr;

    const StoreGraph  &store  = sim.getStore();
    const StoreLayout &layout = sim.getLayout();

    // ── Dynamic centering ───────────────────────────────────────────────────
    float storeCenterX = 0.0f, storeCenterZ = 0.0f;
    layout.getCenter(storeCenterX, storeCenterZ);
    float dynamicOffsetX = OFFSET_X - storeCenterX * PIXELS_PER_METER;
    float dynamicOffsetY = OFFSET_Y - storeCenterZ * PIXELS_PER_METER;

    // ── Visualiser objects ───────────────────────────────────────────────────
    LayoutVisualizer  visualizer;
    visualizer.pixelsPerMeter = PIXELS_PER_METER;
    visualizer.offsetX        = dynamicOffsetX;
    visualizer.offsetY        = dynamicOffsetY;

    NavMeshVisualizer navmeshVisualizer;
    navmeshVisualizer.pixelsPerMeter = PIXELS_PER_METER;
    navmeshVisualizer.offsetX        = dynamicOffsetX;
    navmeshVisualizer.offsetY        = dynamicOffsetY;

    // ── Font (optional, for debug labels) ───────────────────────────────────
    sf::Font debugFont;
    bool     debugFontLoaded = false;
#if defined(_WIN32) || defined(_WIN64)
    if (debugFont.loadFromFile("C:/Windows/Fonts/arial.ttf"))
        debugFontLoaded = true;
#endif
    if (!debugFontLoaded && debugFont.loadFromFile("fonts/arial.ttf"))
        debugFontLoaded = true;

    // ── Simulation control state ─────────────────────────────────────────────
    float timeScale      = 1.0f;
    bool  isPaused       = true;
    bool  hasStarted     = false;
    bool  stepOnce       = false;
    bool  showConfigModal= true;
    float runDuration    = 300.0f;   // seconds of sim-time; 0 = unlimited
    float elapsedDisplay = 0.0f;     // mirrors sim.getElapsedTime() for ImGui

    // ── Debug overlay flags ──────────────────────────────────────────────────
    bool showStoreLabels    = false;
    bool showNodesAndEdges  = true;

    // ── Customer debug (ImGui behaviour log) ────────────────────────────────
    BehaviorEventLog behaviorLog;
    bool  enableBehaviorLog  = false;
    bool  logFocusedOnly     = true;
    char  behaviorLogPathBuf[256] = "behavior_log.csv";
    int   selectedCustomerId  = -1;
    bool  showPathTrail       = false;
    bool  showGuiBehaviorLogAll = false;

    static constexpr size_t PATH_TRAIL_MAX      = 500;
    static constexpr size_t GUI_BEHAVIOR_LOG_MAX = 100;

    std::deque<std::pair<double, double>> pathTrail;

    struct GuiBehaviorEntry {
        float  simTime;
        int    customerId;
        std::string stateName;
        int    decisionType;
        int    targetId;
        int    basketSize;
        int    edgeIndex;
    };
    std::deque<GuiBehaviorEntry> guiBehaviorLog;

    unsigned long long simSeed = 0; // captured for the behavior log header

    // Worker inspection selection.
    int selectedWorkerId = -1;

    // ── Main loop ────────────────────────────────────────────────────────────
    sf::Clock deltaClock;

    while (window.isOpen()) {
        // ── Events ──────────────────────────────────────────────────────────
        sf::Event event{};
        while (window.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(window, event);
            if (event.type == sf::Event::Closed)
                window.close();
        }

        sf::Time dtObj = deltaClock.restart();
        ImGui::SFML::Update(window, dtObj);

        const float rawDt = dtObj.asSeconds();
        const float dt    = rawDt * timeScale;

        // Mission probability derived from UI weights
        const float weightSum = missionWeight + defaultWeight;
        const float missionProb = (weightSum > 0.0f)
                                  ? missionWeight / weightSum
                                  : 0.5f;

        const bool runComplete = (runDuration > 0.0f &&
                                  sim.getElapsedTime() >= runDuration);
        if (runComplete) { isPaused = true; stepOnce = false; }

        const bool shouldSimulate =
            hasStarted && (!isPaused || stepOnce) && !runComplete;

        // ── Simulation tick ──────────────────────────────────────────────────
        if (shouldSimulate) {
            sim.setSpawnInterval(spawnInterval);
            sim.setMissionProbability(missionProb);
            sim.step(dt);

            elapsedDisplay = sim.getElapsedTime();

            // ── ImGui event capture (post-step) ──────────────────────────────
            const auto &agents = sim.getAgents();

            for (const auto &ag : agents) {
                if (!ag->cust) continue;

                const char *stateName =
                    ag->cust->getBehavior()
                    ? ag->cust->getBehavior()->getStateName() : "?";

                // GUI behaviour log (ring buffer)
                if (showGuiBehaviorLogAll ||
                    ag->cust->getId() == selectedCustomerId) {
                    guiBehaviorLog.push_back({
                        elapsedDisplay,
                        ag->cust->getId(),
                        stateName,
                        ag->cust->getLastDecisionType(),
                        ag->cust->getLastDecisionTargetId(),
                        ag->basket.getSize(),
                        ag->cust->getCurrentEdgeIndex()
                    });
                    while (guiBehaviorLog.size() > GUI_BEHAVIOR_LOG_MAX)
                        guiBehaviorLog.pop_front();
                }

                // CSV behaviour log
                if (enableBehaviorLog && behaviorLog.isOpen()) {
                    double px = ag->cust->getPosX();
                    double pz = ag->cust->getPosZ();
                    // Fall back to edge-based position if world pos is zero
                    if (px == 0.0 && pz == 0.0 &&
                        ag->cust->getCurrentEdgeIndex() >= 0 &&
                        ag->cust->getCurrentEdgeIndex() < store.numEdges()) {
                        int edgeId = store.edgeAt(ag->cust->getCurrentEdgeIndex()).getEdgeId();
                        if (layout.edgeGeoms.count(edgeId)) {
                            const auto &geo = layout.edgeGeoms.at(edgeId);
                            float edgeLen = std::sqrt(
                                std::pow(geo.endX - geo.startX, 2) +
                                std::pow(geo.endZ - geo.startZ, 2));
                            float t = (edgeLen > 0)
                                ? static_cast<float>(ag->cust->getDistOnEdge()) / edgeLen
                                : 0.0f;
                            t  = std::max(0.0f, std::min(1.0f, t));
                            px = geo.startX + (geo.endX - geo.startX) * t;
                            pz = geo.startZ + (geo.endZ - geo.startZ) * t;
                        }
                    }
                    const char *behaviorType =
                        ag->cust->getBehavior()
                        ? ag->cust->getBehavior()->getBehaviorType() : "Unknown";
                    behaviorLog.logTick(elapsedDisplay, ag->cust->getId(),
                                        px, pz, behaviorType, stateName,
                                        ag->cust->getLastDecisionType(),
                                        ag->cust->getLastDecisionTargetId(),
                                        ag->basket.getSize(),
                                        ag->cust->getCurrentEdgeIndex(),
                                        ag->cust->getDwellTicks());
                }

            }

            // Path trail for selected customer
            if (selectedCustomerId >= 0 && showPathTrail) {
                for (const auto &ag : agents) {
                    if (!ag->cust || ag->cust->getId() != selectedCustomerId)
                        continue;
                    double px = ag->cust->getPosX();
                    double pz = ag->cust->getPosZ();
                    if (px == 0.0 && pz == 0.0 &&
                        ag->cust->getCurrentEdgeIndex() >= 0 &&
                        ag->cust->getCurrentEdgeIndex() < store.numEdges()) {
                        int edgeId = store.edgeAt(ag->cust->getCurrentEdgeIndex()).getEdgeId();
                        if (layout.edgeGeoms.count(edgeId)) {
                            const auto &geo = layout.edgeGeoms.at(edgeId);
                            float edgeLen = std::sqrt(
                                std::pow(geo.endX - geo.startX, 2) +
                                std::pow(geo.endZ - geo.startZ, 2));
                            float t = (edgeLen > 0)
                                ? static_cast<float>(ag->cust->getDistOnEdge()) / edgeLen
                                : 0.0f;
                            t  = std::max(0.0f, std::min(1.0f, t));
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

            if (stepOnce) { stepOnce = false; isPaused = true; }
        }

        // ── ImGui: Config modal ──────────────────────────────────────────────
        if (!hasStarted && showConfigModal)
            ImGui::OpenPopup("Run Configuration");

        if (ImGui::BeginPopupModal("Run Configuration", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Configure simulation run");
            ImGui::Separator();
            ImGui::SliderFloat("Spawn Interval (s)", &spawnInterval, 0.1f, 30.0f);
            ImGui::InputFloat("Run Duration (s)", &runDuration, 1.0f, 10.0f, "%.1f");
            if (runDuration < 0.0f) runDuration = 0.0f;
            ImGui::SliderFloat("Mission Weight",  &missionWeight,  0.0f, 1.0f);
            ImGui::SliderFloat("Default Weight",  &defaultWeight,  0.0f, 1.0f);
            float modalTotal = missionWeight + defaultWeight;
            ImGui::Text("Mission probability: %.2f",
                        (modalTotal > 0.0f) ? missionWeight / modalTotal : 0.5f);
            ImGui::Separator();

            ImGui::Text("Staffing");
            ImGui::InputInt("Stockers", &numStockers);
            ImGui::InputInt("Cashiers", &numCashiers);
            ImGui::Checkbox("Auto-stock shelves", &autoStockTasks);
            ImGui::Checkbox("Auto-open registers", &autoRegisterTasks);

            if (ImGui::Button("Start")) {
                sim.setWorkerConfig(numStockers, numCashiers,
                                    autoStockTasks, autoRegisterTasks);
                sim.reset(); // apply staffing config by respawning staff pool
                hasStarted     = true;
                isPaused       = false;
                stepOnce       = false;
                showConfigModal= false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                showConfigModal = false;
                isPaused        = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ── ImGui: Simulation Control ────────────────────────────────────────
        ImGui::Begin("Simulation Control");
        ImGui::Text("Nodes: %d  Edges: %d", store.numNodes(), store.numEdges());
        ImGui::Text("Elapsed: %.1f s  Transactions: %zu",
                    sim.getElapsedTime(), sim.getTransactionCount());

        if (store.hasNavMesh()) {
            const NavMesh &nm = store.getNavMesh();
            int total  = nm.getPolygonCount();
            int nodeP  = 0, edgeP = 0;
            for (int i = 0; i < total; ++i) {
                const NavPolygon &p = nm.getPolygon(i);
                if (p.getAssociatedNodeId() >= 0) ++nodeP;
                if (p.getAssociatedEdgeId() >= 0) ++edgeP;
            }
            ImGui::Text("Navmesh: %d polygons (%d node, %d edge)", total, nodeP, edgeP);
            ImGui::Separator();
            ImGui::Text("Navmesh Visualization:");
            ImGui::Checkbox("Show Polygons",    &navmeshVisualizer.showPolygons);
            ImGui::Checkbox("Show Connections", &navmeshVisualizer.showConnections);
            ImGui::Checkbox("Show Agent Paths", &navmeshVisualizer.showPaths);
            ImGui::Checkbox("Show Centers",     &navmeshVisualizer.showCenters);
        }

        ImGui::Separator();
        ImGui::Text("Active Agents: %zu", sim.getAgents().size());
        ImGui::Text("Active Workers: %zu", sim.getWorkers().size());

        if (!hasStarted) {
            ImGui::Text("Run not started");
            if (ImGui::Button("Configure Run"))   showConfigModal = true;
            ImGui::SameLine();
            if (ImGui::Button("Start Run")) {
                sim.setWorkerConfig(numStockers, numCashiers,
                                    autoStockTasks, autoRegisterTasks);
                sim.reset(); // apply staffing config
                hasStarted = true;
                isPaused   = false;
                stepOnce   = false;
                showConfigModal = false;
            }
        }

        ImGui::Checkbox("Pause Sim", &isPaused);
        ImGui::SameLine();
        if (ImGui::Button("Step") && hasStarted && isPaused) stepOnce = true;

        ImGui::SliderFloat("Spawn Interval (s)", &spawnInterval, 0.1f, 30.0f);
        ImGui::SliderFloat("Time Scale",          &timeScale,    0.1f, 10.0f);
        ImGui::InputFloat("Run Duration (s)", &runDuration, 1.0f, 10.0f, "%.1f");
        if (runDuration < 0.0f) runDuration = 0.0f;
        if (runDuration > 0.0f && sim.getElapsedTime() >= runDuration)
            ImGui::Text("Run complete");

        ImGui::SliderFloat("Mission Weight", &missionWeight, 0.0f, 1.0f);
        ImGui::SliderFloat("Default Weight", &defaultWeight, 0.0f, 1.0f);
        {
            float wsum = missionWeight + defaultWeight;
            ImGui::Text("Mission probability: %.2f",
                        (wsum > 0.0f) ? missionWeight / wsum : 0.5f);
        }

        ImGui::Separator();
        ImGui::Text("Store Debug:");
        ImGui::Checkbox("Show Nodes & Edges",    &showNodesAndEdges);
        ImGui::Checkbox("Show Node/Edge Labels", &showStoreLabels);

        if (ImGui::Button("Export Transactions")) {
            try {
                sim.exportTransactions("transactions_export.csv");
                ImGui::OpenPopup("Export OK");
            } catch (const std::exception &ex) {
                std::cerr << "Export failed: " << ex.what() << "\n";
            }
        }
        if (ImGui::BeginPopupModal("Export OK", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Saved to transactions_export.csv");
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::End();

        // ── ImGui: Customer Debug ────────────────────────────────────────────
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
        if (ImGui::Button("Clear")) guiBehaviorLog.clear();

        if (ImGui::BeginChild("BehaviorLogScroll", ImVec2(0, 120), true)) {
            static const char *kDecisionNames[] = {
                "Move","SwitchEdge","PickProduct","Wait","Checkout","Despawn"
            };
            for (const auto &entry : guiBehaviorLog) {
                const char *dname = (entry.decisionType >= 0 && entry.decisionType < 6)
                                    ? kDecisionNames[entry.decisionType] : "?";
                ImGui::Text("%.1fs  ID %d  %s  %s  tgt=%d  bsk=%d  edge=%d",
                            entry.simTime, entry.customerId,
                            entry.stateName.c_str(), dname,
                            entry.targetId, entry.basketSize, entry.edgeIndex);
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Text("Select customer to inspect:");
        for (const auto &ag : sim.getAgents()) {
            if (!ag->cust) continue;
            int   cid   = ag->cust->getId();
            const char *btype = ag->cust->getBehavior()
                                ? ag->cust->getBehavior()->getBehaviorType() : "?";
            char label[64];
            std::snprintf(label, sizeof(label), "ID %d  basket=%d  %s",
                          cid, ag->basket.getSize(), btype);
            if (ImGui::Selectable(label, selectedCustomerId == cid)) {
                selectedCustomerId = cid;
                pathTrail.clear();
            }
        }

        // Clear selection if the selected customer has despawned
        if (selectedCustomerId >= 0) {
            bool found = false;
            for (const auto &ag : sim.getAgents())
                if (ag->cust && ag->cust->getId() == selectedCustomerId)
                    { found = true; break; }
            if (!found) { selectedCustomerId = -1; pathTrail.clear(); }
        }

        ImGui::Separator();
        if (selectedCustomerId >= 0) {
            const Customer *selCust   = nullptr;
            const Basket   *selBasket = nullptr;
            for (const auto &ag : sim.getAgents()) {
                if (ag->cust && ag->cust->getId() == selectedCustomerId) {
                    selCust   = ag->cust.get();
                    selBasket = &ag->basket;
                    break;
                }
            }
            if (selCust && selBasket) {
                ImGui::Text("State:      %s",
                    selCust->getBehavior()
                    ? selCust->getBehavior()->getStateName() : "?");
                ImGui::Text("Position:   %.2f, %.2f",
                            selCust->getPosX(), selCust->getPosZ());
                ImGui::Text("Edge index: %d", selCust->getCurrentEdgeIndex());
                static const char *kDecisionNames[] = {
                    "Move","SwitchEdge","PickProduct","Wait","Checkout","Despawn"
                };
                int di = selCust->getLastDecisionType();
                ImGui::Text("Last decision: %s (target %d)",
                    (di >= 0 && di < 6) ? kDecisionNames[di] : "?",
                    selCust->getLastDecisionTargetId());
                ImGui::Text("Basket size:  %d", selBasket->getSize());
                ImGui::Text("Dwell ticks:  %d", selCust->getDwellTicks());
            }
            ImGui::Checkbox("Show path trail", &showPathTrail);
        } else {
            ImGui::TextDisabled("Select a customer from the list above.");
        }
        ImGui::End();

        // ── ImGui: Worker Debug ──────────────────────────────────────────────
        ImGui::Begin("Worker Debug");
        ImGui::Text("Select a worker to inspect:");
        for (const auto &wPtr : sim.getWorkers()) {
            if (!wPtr) continue;
            const Worker &w = *wPtr;
            const bool isSelected = (w.getId() == selectedWorkerId);

            const char *role =
                (w.canStock() && w.canServe()) ? "Hybrid" :
                (w.canStock()) ? "Stocker" :
                (w.canServe()) ? "Cashier" : "Worker";

            const Task *t = w.currentTask();
            const char *taskState = t ? "Executing" : "Idle";

            char label[128];
            std::snprintf(label, sizeof(label), "ID %d  %s  %s",
                          w.getId(), role, taskState);
            if (ImGui::Selectable(label, isSelected)) {
                selectedWorkerId = w.getId();
            }
        }

        if (selectedWorkerId >= 0) {
            const Worker *selWorker = nullptr;
            for (const auto &wPtr : sim.getWorkers()) {
                if (wPtr && wPtr->getId() == selectedWorkerId) {
                    selWorker = wPtr.get();
                    break;
                }
            }
            if (selWorker) {
                const Worker &w = *selWorker;
                const Task *t = w.currentTask();

                const char *role =
                    (w.canStock() && w.canServe()) ? "Hybrid" :
                    (w.canStock()) ? "Stocker" :
                    (w.canServe()) ? "Cashier" : "Worker";

                ImGui::Separator();
                ImGui::Text("Role: %s", role);
                ImGui::Text("Position: %.2f, %.2f", w.getPosX(), w.getPosZ());
                ImGui::Text("Happiness: %.2f", w.getHappiness());
                ImGui::Text("Efficiency: %.2f", w.getTaskEfficiency());

                if (t) {
                    const char *typeStr =
                        (t->type == TaskType::StockShelves) ? "StockShelves" :
                        (t->type == TaskType::ProcessRegister) ? "ProcessRegister" :
                        "AssistCustomer";
                    ImGui::Text("Current task: %s", typeStr);
                    ImGui::Text("Task target_id: %d", t->targetId);
                } else {
                    ImGui::Text("Current task: —");
                }
            } else {
                selectedWorkerId = -1;
            }
        }

        ImGui::End();

        // ── ImGui: Mission Checkout Log ──────────────────────────────────────
        ImGui::Begin("Mission Checkout Log");
        ImGui::Text("Mission vs actual purchases at checkout");
        ImGui::Separator();
        {
            const auto &log = sim.getMissionCheckoutLog();
            if (log.empty()) {
                ImGui::TextDisabled("No checkouts yet...");
            } else {
                ImGui::BeginChild("MissionLogScroll", ImVec2(0, 200), true);
                for (auto it = log.rbegin(); it != log.rend(); ++it) {
                    const auto &entry = *it;
                    ImGui::Text("Customer #%d @ %.1fs", entry.customerId, entry.simTime);
                    std::string ms, bs;
                    for (size_t i = 0; i < entry.missionItems.size(); ++i)
                        ms += (i ? ", " : "") + entry.missionItems[i];
                    for (size_t i = 0; i < entry.basketItems.size(); ++i)
                        bs += (i ? ", " : "") + entry.basketItems[i];
                    ImGui::Text("  Mission: %s", ms.c_str());
                    ImGui::Text("  Basket:  %s", bs.c_str());
                    ImGui::Text("  Total:   $%.2f", entry.basketTotal);
                    ImGui::Separator();
                }
                ImGui::EndChild();
            }
            ImGui::Text("Entries: %zu / 50", log.size());
        }
        ImGui::End();

        // ── Render ───────────────────────────────────────────────────────────
        window.clear(sf::Color(30, 30, 40));

        if (showNodesAndEdges)
            visualizer.draw(window, layout);

        if (debugFontLoaded && showStoreLabels)
            drawStoreLabels(window, layout, store, debugFont, true,
                            PIXELS_PER_METER, dynamicOffsetX, dynamicOffsetY);

        // Physics overlays
        if (store.hasPhysicsWorld()) {
            const PhysicsWorld &physics = store.getPhysicsWorld();

            sf::RectangleShape obstacleShape;
            obstacleShape.setFillColor(sf::Color(150, 100, 50, 100));
            obstacleShape.setOutlineColor(sf::Color(200, 150, 100));
            obstacleShape.setOutlineThickness(1.0f);
            for (const auto &obs : physics.getObstacles()) {
                float w = static_cast<float>((obs.maxX - obs.minX) * PIXELS_PER_METER);
                float h = static_cast<float>((obs.maxZ - obs.minZ) * PIXELS_PER_METER);
                obstacleShape.setSize(sf::Vector2f(w, h));
                obstacleShape.setPosition(
                    static_cast<float>(obs.minX * PIXELS_PER_METER + dynamicOffsetX),
                    static_cast<float>(obs.minZ * PIXELS_PER_METER + dynamicOffsetY));
                window.draw(obstacleShape);
            }

            sf::RectangleShape boundaryShape;
            boundaryShape.setFillColor(sf::Color(100, 100, 150, 80));
            boundaryShape.setOutlineColor(sf::Color(150, 150, 200));
            boundaryShape.setOutlineThickness(1.0f);
            for (const auto &bnd : physics.getBoundaries()) {
                float w = static_cast<float>((bnd.maxX - bnd.minX) * PIXELS_PER_METER);
                float h = static_cast<float>((bnd.maxZ - bnd.minZ) * PIXELS_PER_METER);
                boundaryShape.setSize(sf::Vector2f(w, h));
                boundaryShape.setPosition(
                    static_cast<float>(bnd.minX * PIXELS_PER_METER + dynamicOffsetX),
                    static_cast<float>(bnd.minZ * PIXELS_PER_METER + dynamicOffsetY));
                window.draw(boundaryShape);
            }
        }

        // Navmesh overlays
        if (store.hasNavMesh()) {
            const NavMesh &navmesh = store.getNavMesh();
            navmeshVisualizer.drawPolygons(window, navmesh);
            navmeshVisualizer.drawConnections(window, navmesh);
            navmeshVisualizer.drawCenters(window, navmesh);
            if (navmeshVisualizer.showPaths) {
                for (const auto &ag : sim.getAgents()) {
                    if (ag->cust->isUsingNavmesh() &&
                        !ag->cust->getNavmeshPath().empty())
                        navmeshVisualizer.drawPath(window, ag->cust->getNavmeshPath());
                }
            }
        }

        // Path trail
        if (showPathTrail && pathTrail.size() >= 2) {
            sf::VertexArray strip(sf::LineStrip, pathTrail.size());
            for (size_t i = 0; i < pathTrail.size(); ++i) {
                strip[i].position = sf::Vector2f(
                    static_cast<float>(pathTrail[i].first  * PIXELS_PER_METER + dynamicOffsetX),
                    static_cast<float>(pathTrail[i].second * PIXELS_PER_METER + dynamicOffsetY));
                strip[i].color = sf::Color(255, 200, 0, 120);
            }
            window.draw(strip);
        }

        // Agents
        sf::CircleShape agentShape(6.0f);
        agentShape.setOrigin(3.0f, 3.0f);

        for (const auto &ag : sim.getAgents()) {
            if (ag->cust->getCurrentEdgeIndex() == -1) continue;

            const bool selected = (ag->cust->getId() == selectedCustomerId);
            if (selected) {
                agentShape.setRadius(8.0f);
                agentShape.setOrigin(4.0f, 4.0f);
                agentShape.setFillColor(sf::Color(255, 165, 0));
            } else {
                agentShape.setRadius(6.0f);
                agentShape.setOrigin(3.0f, 3.0f);
                if      (ag->cust->getDwellTicks() > 0) agentShape.setFillColor(sf::Color::White);
                else if (ag->hasPaid)                   agentShape.setFillColor(sf::Color::Green);
                else if (ag->basket.getSize() > 0)      agentShape.setFillColor(sf::Color::Magenta);
                else                                    agentShape.setFillColor(sf::Color::Cyan);
            }

            // World position: prefer navmesh pos, fall back to edge interpolation
            float currX = 0.0f, currZ = 0.0f;
            if (ag->cust->getPosX() != 0.0 || ag->cust->getPosZ() != 0.0) {
                currX = static_cast<float>(ag->cust->getPosX());
                currZ = static_cast<float>(ag->cust->getPosZ());
            } else {
                int eIdx = ag->cust->getCurrentEdgeIndex();
                if (eIdx < 0 || eIdx >= store.numEdges()) continue;
                int edgeId = store.edgeAt(eIdx).getEdgeId();
                if (!layout.edgeGeoms.count(edgeId)) continue;
                const auto &geo = layout.edgeGeoms.at(edgeId);
                float edgeLen = std::sqrt(std::pow(geo.endX - geo.startX, 2) +
                                          std::pow(geo.endZ - geo.startZ, 2));
                float t = (edgeLen > 0)
                    ? static_cast<float>(ag->cust->getDistOnEdge()) / edgeLen
                    : 0.0f;
                t     = std::max(0.0f, std::min(1.0f, t));
                currX = geo.startX + (geo.endX - geo.startX) * t;
                currZ = geo.startZ + (geo.endZ - geo.startZ) * t;
            }

            agentShape.setPosition(currX * PIXELS_PER_METER + dynamicOffsetX,
                                   currZ * PIXELS_PER_METER + dynamicOffsetY);
            window.draw(agentShape);
        }

        // ── Staff workers overlay (task debugging) ──────────────────────────
        for (const auto &wPtr : sim.getWorkers()) {
            if (!wPtr) continue;
            const Worker &w = *wPtr;
            const Task *t = w.currentTask();

            // Role base color.
            sf::Color baseColor = sf::Color(200, 200, 200);
            if (w.canStock() && w.canServe())
                baseColor = sf::Color(170, 100, 255); // hybrid
            else if (w.canStock())
                baseColor = sf::Color(80, 200, 255);  // stocker
            else if (w.canServe())
                baseColor = sf::Color(120, 255, 120); // cashier

            const float radius = t ? 7.0f : 5.0f;
            sf::CircleShape workerShape(radius);
            workerShape.setOrigin(radius * 0.5f, radius * 0.5f);
            workerShape.setFillColor(baseColor);

            if (t) {
                workerShape.setOutlineColor(sf::Color::Yellow);
                workerShape.setOutlineThickness(1.5f);
            } else {
                workerShape.setOutlineThickness(0.0f);
            }

            workerShape.setPosition(
                static_cast<float>(w.getPosX() * PIXELS_PER_METER + dynamicOffsetX),
                static_cast<float>(w.getPosZ() * PIXELS_PER_METER + dynamicOffsetY));
            window.draw(workerShape);
        }

        ImGui::SFML::Render(window);
        window.display();
    }

    ImGui::SFML::Shutdown();
    return 0;
}