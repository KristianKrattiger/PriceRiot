# PriceRiot C++ Architecture

This document describes the layered abstraction and data flows of the PriceRiot retail simulation engine.

## Layered Abstraction Diagram

```mermaid
flowchart TB
    subgraph Drivers [Drivers]
        sim_cpp[sim.cpp SFML Visualiser]
        py_mod[simulation Python module]
    end

    subgraph SimLayer [Simulation Engine Layer]
        Simulator[Simulator step run reset]
        transaction[Transaction]
        navmeshViz[NavMeshVisualizer]
    end

    subgraph AgentLayer [Agent Layer]
        Customer[Customer]
        Behavior[ICustomerBehavior]
        Basket[Basket]
        Staff[Staff]
    end

    subgraph EnvLayer [Environment Layer]
        StoreGraph[StoreGraph]
        StoreLayout[StoreLayout]
        NavMesh[NavMesh]
        PhysicsWorld[PhysicsWorld]
        CollisionMgr[CollisionManager]
    end

    subgraph CoreData [Core Data]
        Node[Node]
        Edge[Edge]
        EdgeCell[EdgeCell]
        NavPolygon[NavPolygon]
        ShelfSide[ShelfSide]
    end

    sim_cpp -->|step getAgents getStore getLayout| Simulator
    py_mod -->|step run get_transactions| Simulator
    Simulator --> Customer
    Simulator --> StoreGraph
    Simulator --> StoreLayout
    sim_cpp --> navmeshViz
    Customer --> Behavior
    Customer --> Basket
    Behavior --> StoreGraph
    Behavior --> NavMesh
    StoreGraph --> Node
    StoreGraph --> Edge
    StoreGraph --> NavMesh
    StoreGraph --> PhysicsWorld
    Edge --> EdgeCell
    EdgeCell --> ShelfSide
    NavMesh --> NavPolygon
```

## Module Responsibilities

### engine/

- **simulator.cpp/h**: Headless simulation core. Owns store graph, layout, agents, collision manager, checkout queues, and RNG. Exposes `step(dt)`, `run(duration, dt)`, `reset()`, and accessors for transactions, customers, and state. No SFML/ImGui; used by both the visualiser and the Python module.
- **sim.cpp**: SFML visualiser. Creates a `Simulator`, drives it with `step(dt)` each frame, and renders using `getAgents()`, `getStore()`, `getLayout()`. All simulation logic lives in `Simulator`.
- **transaction.cpp/h**: Transaction and line-item generation, CSV export.
- **navmesh_visualizer.cpp/h**: SFML drawing of navmesh polygons, agent paths, and debug overlays.

### bindings/

- **priceriot_bindings.cpp**: pybind11 Python extension module `simulation`. Exposes `Simulator`, `Transaction`, `Customer`, `LineItem`, `TripPurpose` for headless runs and analytics. Built with the same C++ core as the simulator executable (no SFML).

### agents/

- **customer.cpp/h**: Customer entity with demographics, behavior profile, navigation state (edge-based or navmesh), basket, and loyalty metrics.
- **customer_behavior.cpp/h**: Strategy pattern for customer AI. `ICustomerBehavior::decide()` returns a `Decision` (Move, PickProduct, Checkout, etc.). `DefaultBehavior` (browsing) and `MissionBehavior` (targeted SKU list).
- **basket.cpp/h**: Shopping basket with SKU/quantity tracking.
- **staff.cpp/h**: StockBoy restocking logic.

### environment/

- **environment.cpp/h**: Core graph model. `Node` (Entrance, Exit, Junction, Register, Stockroom), `Edge` (aisles with cells), `StoreGraph` (nodes, edges, adjacency, navmesh, physics world). YAML loading via `loadFromYaml()`.
- **cell.cpp/h**: `EdgeCell` – longitudinal slice of an aisle with left/right `ShelfSide`, `SideBand` stall management, peel/pick/merge operations.
- **shelf.cpp/h**: Bay → Face → Slot hierarchy. `ShelfSide`, `SideBand`, `BayFace`, `PickResult`. Planogram mapping.
- **store_layout.cpp/h**: `StoreLayout` – geometry for visualization (node hubs, edge centerlines, corners).
- **navmesh.cpp/h**: `NavPolygon`, `NavMesh` – walkable polygons and spatial queries (find polygon containing point).
- **navmesh_generator.cpp/h**: Builds `NavMesh` from `StoreGraph` and `StoreLayout`.
- **navmesh_pathfinder.cpp/h**: A* pathfinding on navmesh with funnel-algorithm path smoothing.
- **physics.cpp/h**: `PhysicsWorld` – obstacle AABBs and boundary walls for collision.
- **physics_generator.cpp/h**: Generates physics from layout (shelf obstacles, walls).
- **collision_manager.cpp/h**: Agent-agent separation, obstacle avoidance, collision resolution.
- **products.cpp/h**: Product catalog, SKU definitions.
- **store_init.cpp/h**, **store_inventory.cpp/h**: YAML parsing, planogram application, inventory pools.

## Key Data Flows

```mermaid
flowchart LR
    YAML[store.yaml] --> StoreGraph
    StoreGraph --> StoreLayout
    StoreGraph --> NavMeshGenerator
    StoreLayout --> NavMeshGenerator
    NavMeshGenerator --> NavMesh
    StoreGraph --> PhysicsGenerator
    StoreLayout --> PhysicsGenerator
    PhysicsGenerator --> PhysicsWorld
```

1. **YAML → StoreGraph**: `StoreGraph::loadFromYaml()` parses nodes, edges, cells, and planograms.
2. **StoreGraph → NavMesh/Physics**: `StoreGraph::buildNavMesh(layout)` and `buildPhysicsWorld(layout)` produce walkable polygons and collision geometry.
3. **Customer → Behavior → Pathfinding**: Each tick, `Customer::update()` calls `behavior->decide()`. The decision drives edge-based movement (BFS on graph) or navmesh movement (`NavMeshPathfinder::findPath()`).

## Behavior and Navigation Flow

```mermaid
sequenceDiagram
    participant Sim as Simulator
    participant Cust as Customer
    participant Beh as ICustomerBehavior
    participant Nav as NavMeshPathfinder
    participant Store as StoreGraph

    Sim->>Cust: update(dt, store, basket)
    Cust->>Beh: decide(customer, ctx)
    Beh-->>Cust: Decision (Move, PickProduct, etc.)
    alt Navmesh available
        Cust->>Nav: findPath(start, end)
        Nav-->>Cust: waypoints
        Cust->>Cust: Advance along waypoints
    else Edge-based fallback
        Cust->>Store: BFS / getNextEdge
        Cust->>Cust: Move along edge cells
    end
```

- **DefaultBehavior**: Entering → Browsing (with preferred aisles) → HeadingToCheckout → InQueue → HeadingToExit → Done.
- **MissionBehavior**: Entering → MissionBrowse (visit cells containing target SKUs) → HeadingToCheckout → InQueue → HeadingToExit → Done.
- **Hybrid navigation**: When navmesh is built, agents use `NavMeshPathfinder`. Otherwise they use graph-based edge traversal.
