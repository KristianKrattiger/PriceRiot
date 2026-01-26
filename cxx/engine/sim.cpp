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
#include "transaction.h"

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
    void draw(sf::RenderWindow& window, const StoreLayout& layout) {
        for(const auto& [id, geo] : layout.edgeGeoms) {
            auto corners = geo.getCorners();
            sf::ConvexShape shape;
            shape.setPointCount(4);
            for(int i=0; i<4; ++i) {
                shape.setPoint(i, sf::Vector2f(corners[i].x * PIXELS_PER_METER + OFFSET_X,
                                               corners[i].y * PIXELS_PER_METER + OFFSET_Y));
            }
            shape.setFillColor(sf::Color(60, 60, 70));
            shape.setOutlineColor(sf::Color(100, 100, 100));
            shape.setOutlineThickness(1.0f);
            window.draw(shape);
        }

        for(const auto& [id, geo] : layout.nodeGeoms) {
            sf::RectangleShape shape;
            shape.setSize(sf::Vector2f(geo.width * PIXELS_PER_METER, geo.length * PIXELS_PER_METER));
            shape.setOrigin(shape.getSize() / 2.0f);
            shape.setPosition(geo.x * PIXELS_PER_METER + OFFSET_X, geo.z * PIXELS_PER_METER + OFFSET_Y);

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

    } catch (const std::exception& ex) {
        std::cerr << "Critical Error: " << ex.what() << "\n";
        return 1;
    }

    std::vector<std::unique_ptr<Agent>> agents;
    // Fix: Type is fully resolved via 'using namespace priceriot'
    std::vector<std::shared_ptr<Customer>> raw_customers_ref;
    std::default_random_engine rng(std::random_device{}());

    LayoutVisualizer visualizer;

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
                    agents.push_back(std::move(ag));
                }
            }

            agents.erase(std::remove_if(agents.begin(), agents.end(),
                [&](const std::unique_ptr<Agent>& a) {
                    bool alive = a->update(dt, store);
                    return !alive || a->cust->currentEdgeIndex == -1;
                }),
                agents.end());
        }

        ImGui::Begin("Simulation Control");
        ImGui::Text("Nodes: %d  Edges: %d", store.numNodes(), store.numEdges());
        ImGui::Text("Active Agents: %zu", agents.size());
        ImGui::Checkbox("Pause Sim", &isPaused);
        ImGui::SliderFloat("Spawn Rate", &spawnInterval, 0.1f, 5.0f);
        ImGui::SliderFloat("Time Scale", &timeScale, 0.1f, 10.0f);
        ImGui::End();

        window.clear(sf::Color(30, 30, 40));

        visualizer.draw(window, layout);

        sf::CircleShape agentShape(6.0f);
        agentShape.setOrigin(3.0f, 3.0f);

        for(const auto& agent : agents) {
            if (agent->cust->currentEdgeIndex != -1) {
                if (agent->cust->getDwellTicks() > 0) agentShape.setFillColor(sf::Color::White);
                else if (agent->hasPaid) agentShape.setFillColor(sf::Color::Green);
                // Fix: basket is now visible
                else if (agent->basket.getSize() > 0) agentShape.setFillColor(sf::Color::Magenta);
                else agentShape.setFillColor(sf::Color::Cyan);

                int eIdx = agent->cust->currentEdgeIndex;
                if (layout.edgeGeoms.count(eIdx)) {
                    const auto& geo = layout.edgeGeoms.at(eIdx);

                    float edgeLen = std::sqrt(std::pow(geo.endX - geo.startX, 2) + std::pow(geo.endZ - geo.startZ, 2));
                    float t = static_cast<float>(agent->cust->distOnEdge) / edgeLen;
                    t = std::max(0.0f, std::min(1.0f, t));

                    float currX = geo.startX + (geo.endX - geo.startX) * t;
                    float currZ = geo.startZ + (geo.endZ - geo.startZ) * t;

                    agentShape.setPosition(currX * PIXELS_PER_METER + OFFSET_X,
                                           currZ * PIXELS_PER_METER + OFFSET_Y);
                    window.draw(agentShape);
                }
            }
        }

        ImGui::SFML::Render(window);
        window.display();
    }

    ImGui::SFML::Shutdown();
    return 0;
}