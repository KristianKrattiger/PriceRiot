#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <random>
#include <map>
#include <cmath>
#include <algorithm>
#include <iomanip>

#include <SFML/Graphics.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include "imgui.h"
#include "imgui-SFML.h"

#include "../agents/customer.h"
#include "../agents/customer_behavior.h"
#include "../agents/basket.h"
#include "../environment/environment.h"
#include "../environment/storeLayout.h"
#include "../environment/products.h"
#include "../environment/shelf.h"
#include "../environment/collision_manager.h"
#include "transaction.h"
#include "navmesh_visualizer.h"

using namespace priceriot;

const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 800;
const float PIXELS_PER_METER = 20.0f;
const float OFFSET_X = WINDOW_WIDTH / 2.0f;
const float OFFSET_Y = WINDOW_HEIGHT / 2.0f;

struct Agent {
    std::shared_ptr<Customer> cust;
    Basket basket;
    bool hasPaid = false;

    // Pass Basket by value or move
    Agent(std::shared_ptr<Customer> c, Basket b) : cust(c), basket(std::move(b)) {
        cust->setBehavior(new DefaultBehavior());
    }

    bool update(float dt, const StoreGraph& store) {
        bool alive = cust->update(dt, store, basket);
        if (basket.getSize() == 0 && cust->getTotalSpent() > 0) hasPaid = true;
        return alive;
    }
};

struct LayoutVisualizer {
    float pixelsPerMeter = PIXELS_PER_METER;
    float offsetX = OFFSET_X;
    float offsetY = OFFSET_Y;
    
    void draw(sf::RenderWindow& window, const StoreLayout& layout) {
        for(const auto& [id, geo] : layout.edgeGeoms) {
            auto corners = geo.getCorners();
            sf::ConvexShape shape;
            shape.setPointCount(4);
            for(int i=0; i<4; ++i) {
                shape.setPoint(i, sf::Vector2f(corners[i].x * pixelsPerMeter + offsetX,
                                               corners[i].y * pixelsPerMeter + offsetY));
            }
            shape.setFillColor(sf::Color(60, 60, 70));
            shape.setOutlineColor(sf::Color(100, 100, 100));
            shape.setOutlineThickness(1.0f);
            window.draw(shape);
        }

        for(const auto& [id, geo] : layout.nodeGeoms) {
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

int main() {
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "PriceRiot Simulator");
    window.setFramerateLimit(60);
    if (!ImGui::SFML::Init(window)) return -1;

    StoreGraph store;
    StoreLayout layout;

    try {
        std::cout << "Loading store..." << std::endl;
        store.loadFromYaml("store.yaml");
        std::cout << "Store loaded: " << store.numNodes() << " nodes, " << store.numEdges() << " edges.\n";

        layout.buildGeometry(store);
        std::cout << "Layout geometry baked.\n";
        
        // Build navmesh
        store.buildNavMesh(layout);
        std::cout << "Navmesh built: " << store.getNavMesh().getPolygonCount() << " polygons.\n";
        
        // Build physics world
        store.buildPhysicsWorld(layout);
        std::cout << "Physics world built.\n";

    } catch (const std::exception& ex) {
        std::cerr << "Critical Error: " << ex.what() << "\n";
        return 1;
    }

    std::vector<std::unique_ptr<Agent>> agents;
    // Fix: Type is fully resolved via 'using namespace priceriot'
    std::vector<std::shared_ptr<Customer>> raw_customers_ref;
    std::default_random_engine rng(std::random_device{}());

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

    sf::Clock deltaClock;
    float spawnTimer = 0.0f;
    float spawnInterval = 0.5f;
    float timeScale = 1.0f;
    bool isPaused = false;

    while (window.isOpen()) {
        sf::Event event{};
        while (window.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(window, event);
            if (event.type == sf::Event::Closed) window.close();
        }

        sf::Time dtObj = deltaClock.restart();
        ImGui::SFML::Update(window, dtObj);
        float dt = dtObj.asSeconds() * timeScale;

        if (!isPaused) {
            spawnTimer += dt;
            if (spawnTimer >= spawnInterval && store.numEdges() > 0) {
                spawnTimer = 0.0f;
                // Fix: structured binding from priceriot::newCustomer
                auto [fst, snd] = newCustomer(raw_customers_ref, rng);

                auto ag = std::make_unique<Agent>(fst, std::move(snd));

                int startEdgeIdx = -1;
                for(int i=0; i<store.numEdges(); ++i) {
                    if (store.nodeAt(store.edgeAt(i).getFromNode()).getNodeType() == Node::NodeType::Entrance) {
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
                            const auto& entranceNode = store.nodeAt(i);
                            ag->cust->setPosition(entranceNode.getX(), entranceNode.getZ());
                            break;
                        }
                    }
                    
                    // Register agent for collision detection
                    collisionManager.registerAgent(ag->cust.get(), 0.35);
                    
                    agents.push_back(std::move(ag));
                }
            }

            // Ensure all agents are registered
            for (auto& agent : agents) {
                collisionManager.registerAgent(agent->cust.get(), 0.35);
            }
            
            // Update agents with collision avoidance
            for (auto& agent : agents) {
                // Apply avoidance steering before movement
                if (store.hasPhysicsWorld() && agent->cust) {
                    double avoidX = 0.0, avoidZ = 0.0;
                    collisionManager.getAvoidanceVector(
                        agent->cust->getPosX(), agent->cust->getPosZ(), 
                        0.35, avoidX, avoidZ, 2.0
                    );
                    
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
                agent->update(dt, store);
            }
            
            // Resolve any remaining collisions (push agents apart)
            for (auto& agent : agents) {
                if (store.hasPhysicsWorld() && agent->cust) {
                    collisionManager.resolveCollisions(agent->cust.get(), 0.35);
                }
            }
            
            // Remove dead agents
            agents.erase(std::remove_if(agents.begin(), agents.end(),
                [&](const std::unique_ptr<Agent>& a) {
                    bool shouldRemove = !a->cust || a->cust->currentEdgeIndex == -1;
                    if (shouldRemove && a->cust) {
                        collisionManager.unregisterAgent(a->cust.get());
                    }
                    return shouldRemove;
                }),
                agents.end());
        }

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
        ImGui::Checkbox("Pause Sim", &isPaused);
        ImGui::SliderFloat("Spawn Rate", &spawnInterval, 0.1f, 5.0f);
        ImGui::SliderFloat("Time Scale", &timeScale, 0.1f, 10.0f);
        ImGui::End();

        window.clear(sf::Color(30, 30, 40));

        // Draw store layout (base layer)
        visualizer.draw(window, layout);
        
        // Draw navmesh visualization (overlay layer)
        if (store.hasNavMesh()) {
            const NavMesh& navmesh = store.getNavMesh();
            
            // Draw polygons
            navmeshVisualizer.drawPolygons(window, navmesh);
            
            // Draw connections
            navmeshVisualizer.drawConnections(window, navmesh);
            
            // Draw polygon centers
            navmeshVisualizer.drawCenters(window, navmesh);
            
            // Draw agent paths
            if (navmeshVisualizer.showPaths) {
                for (const auto& agent : agents) {
                    if (agent->cust->isUsingNavmesh() && !agent->cust->getNavmeshPath().empty()) {
                        navmeshVisualizer.drawPath(window, agent->cust->getNavmeshPath());
                    }
                }
            }
        }
        
        // Draw physics world obstacles (for debugging)
        if (store.hasPhysicsWorld()) {
            const PhysicsWorld& physics = store.getPhysicsWorld();
            
            // Draw obstacles (shelves)
            sf::RectangleShape obstacleShape;
            obstacleShape.setFillColor(sf::Color(150, 100, 50, 100)); // Semi-transparent brown
            obstacleShape.setOutlineColor(sf::Color(200, 150, 100));
            obstacleShape.setOutlineThickness(1.0f);
            
            for (const auto& obstacle : physics.getObstacles()) {
                float width = static_cast<float>((obstacle.maxX - obstacle.minX) * PIXELS_PER_METER);
                float height = static_cast<float>((obstacle.maxZ - obstacle.minZ) * PIXELS_PER_METER);
                obstacleShape.setSize(sf::Vector2f(width, height));
                obstacleShape.setPosition(
                    static_cast<float>(obstacle.minX * PIXELS_PER_METER + dynamicOffsetX),
                    static_cast<float>(obstacle.minZ * PIXELS_PER_METER + dynamicOffsetY)
                );
                window.draw(obstacleShape);
            }
            
            // Draw boundaries (walls) - optional, can be commented out if too cluttered
            sf::RectangleShape boundaryShape;
            boundaryShape.setFillColor(sf::Color(100, 100, 150, 80)); // Semi-transparent blue
            boundaryShape.setOutlineColor(sf::Color(150, 150, 200));
            boundaryShape.setOutlineThickness(1.0f);
            
            for (const auto& boundary : physics.getBoundaries()) {
                float width = static_cast<float>((boundary.maxX - boundary.minX) * PIXELS_PER_METER);
                float height = static_cast<float>((boundary.maxZ - boundary.minZ) * PIXELS_PER_METER);
                boundaryShape.setSize(sf::Vector2f(width, height));
                boundaryShape.setPosition(
                    static_cast<float>(boundary.minX * PIXELS_PER_METER + dynamicOffsetX),
                    static_cast<float>(boundary.minZ * PIXELS_PER_METER + dynamicOffsetY)
                );
                window.draw(boundaryShape);
            }
        }

        sf::CircleShape agentShape(6.0f);
        agentShape.setOrigin(3.0f, 3.0f);

        for(const auto& agent : agents) {
            if (agent->cust->currentEdgeIndex != -1) {
                if (agent->cust->getDwellTicks() > 0) agentShape.setFillColor(sf::Color::White);
                else if (agent->hasPaid) agentShape.setFillColor(sf::Color::Green);
                // Fix: basket is now visible
                else if (agent->basket.getSize() > 0) agentShape.setFillColor(sf::Color::Magenta);
                else agentShape.setFillColor(sf::Color::Cyan);

                // Use world position if available (navmesh), otherwise fall back to edge-based calculation
                float currX, currZ;
                if (agent->cust->getPosX() != 0.0 || agent->cust->getPosZ() != 0.0) {
                    // Use world position from navmesh
                    currX = static_cast<float>(agent->cust->getPosX());
                    currZ = static_cast<float>(agent->cust->getPosZ());
                } else {
                    // Fall back to edge-based calculation for backward compatibility
                    int eIdx = agent->cust->currentEdgeIndex;
                    if (layout.edgeGeoms.count(eIdx)) {
                        const auto& geo = layout.edgeGeoms.at(eIdx);
                        float edgeLen = std::sqrt(std::pow(geo.endX - geo.startX, 2) + std::pow(geo.endZ - geo.startZ, 2));
                        float t = static_cast<float>(agent->cust->distOnEdge) / edgeLen;
                        t = std::max(0.0f, std::min(1.0f, t));
                        currX = geo.startX + (geo.endX - geo.startX) * t;
                        currZ = geo.startZ + (geo.endZ - geo.startZ) * t;
                    } else {
                        continue; // Skip if no valid position
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