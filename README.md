# PriceRiot

**PriceRiot** is a high-fidelity retail store simulation engine built in C++ with real-time visualization. It models realistic customer behavior, shelf inventory management, traffic flow dynamics, and transaction generation for retail analytics and optimization.

## Overview

PriceRiot simulates a physical retail store environment where autonomous customer agents navigate through a graph-based store layout, interact with detailed shelf inventory systems, build shopping baskets, and complete transactions. The simulation runs in real-time with SFML-based visualization, while generating rich transaction data for downstream Python analytics.

## Core Architecture

### C++ Simulation Engine
- **Agent-based simulation** with per-tick customer decision making
- **Graph-based store layout** (nodes: entrances, exits, junctions, registers, stockrooms; edges: aisles with cells)
- **Shelf inventory system** with bays, faces, slots, and stall-based customer positioning
- **Behavior system** using strategy pattern for extensible customer AI
- **Real-time visualization** with SFML graphics and ImGui control panels

### Python Analytics Layer
- **Data processing pipeline** for CSV transaction analysis
- **FastAPI framework** for REST API endpoints (in development)
- **Customer analytics** including churn analysis, category statistics, and transaction aggregation

## Implemented Features

### ✅ Store Environment & Layout
- **YAML-configurable store graphs** with nodes and edges
- **Spatial discretization** with cell-based traffic flow modeling
- **Geometry system** for visualization (node hubs, edge centerlines)
- **Density-aware navigation** with congestion and personal space calculations
- **Shelf protrusion modeling** affecting aisle width and lane capacity

### ✅ Shelf Inventory System
- **Three-tier hierarchy**: Bays → Faces → Slots
- **Stall-based customer positioning** aligned with shelf bays
- **Product picking** with SKU tracking and quantity decrementing
- **Blocking system** for staff restocking operations
- **Side bands** for managing customer flow in/out of stalls
- **Inventory pool** for backstock management

### ✅ Customer Agents
- **Behavior profiles**: basket size multiplier, price sensitivity, impulsivity, trip purpose (StockUp/TopUp/Mission)
- **State machine**: Entering → Browsing → HeadingToCheckout → InQueue → HeadingToExit → Done
- **BFS pathfinding** for navigation between nodes
- **Product interaction**: shelf browsing, product picking, basket building
- **Customer history tracking**: total spent, loyalty rating, purchase frequency, churn prediction

### ✅ Basket & Transactions
- **Dynamic basket building** based on customer behavior and shelf availability
- **Transaction generation** with line items (SKU, quantity, price, total)
- **CSV export capability** for transaction logging
- **Customer satisfaction** and transaction metadata

### ✅ Visualization & Controls
- **Real-time SFML rendering** of store layout and agents
- **ImGui control panel** with:
  - Pause/resume simulation
  - Spawn rate adjustment
  - Time scale control
  - Active agent count
  - Store topology stats
- **Agent color coding**: browsing (white), shopping (magenta), paid (green), idle (cyan)

### ✅ Staff & Restocking
- **StockBoy class** for automated shelf restocking
- **Inventory pool** management (backstock → shelf)
- **Blocking mechanics** during restocking operations

### ✅ Data Processing (Python)
- **CSV data loading** and cleaning
- **Customer statistics** aggregation (spending, loyalty, churn indicators)
- **Category-level analytics** (price averages, quantity totals, return rates)
- **Transaction information** processing and export

## Project Structure

```
PriceRiot-main/
├── cxx/                          # C++ simulation engine
│   ├── agents/                   # Customer and staff agents
│   │   ├── customer.cpp/h        # Customer class with behavior profiles
│   │   ├── customer_behavior.cpp/h  # Behavior strategy pattern
│   │   ├── basket.cpp/h          # Shopping basket implementation
│   │   └── staff.cpp/h           # StockBoy restocking logic
│   ├── environment/              # Store environment
│   │   ├── environment.cpp/h     # StoreGraph (nodes, edges, cells)
│   │   ├── products.cpp/h        # Product catalog
│   │   ├── shelf.cpp/h           # Shelf inventory system
│   │   ├── store_inventory.cpp/h # InventoryPool (backstock)
│   │   ├── store_init.cpp/h      # YAML loading and initialization
│   │   └── storeLayout.cpp/h     # Geometry for visualization
│   ├── engine/                   # Simulation loop
│   │   ├── sim.cpp/h             # Main simulation with SFML
│   │   └── transaction.cpp/h     # Transaction data structures
│   └── tools/
│       └── io_csv.cpp/h          # CSV import/export
├── python/                       # Python analytics layer
│   ├── data_processing.py        # CSV analysis and aggregation
│   ├── main.py                   # FastAPI application (skeleton)
│   ├── dashboard.py              # (planned) Visualization dashboard
│   ├── models.py                 # (planned) ML models
│   └── optimization.py            # (planned) Optimization algorithms
├── data/
│   ├── raw/                      # Input CSV datasets
│   └── processed/                # Generated analytics outputs
└── CMakeLists.txt                # Build configuration
```

## Installation

### Prerequisites

- **C++17** or later compiler (GCC, Clang, or MSVC)
- **CMake 3.14+**
- **Python 3.10+** (for analytics layer)
- **Dependencies** (automatically fetched via CMake):
  - [yaml-cpp](https://github.com/jbeder/yaml-cpp) (YAML parsing)
  - [SFML 2.6+](https://www.sfml-dev.org/) (Graphics and windowing)
  - [Dear ImGui](https://github.com/ocornut/imgui) (UI controls)
  - [ImGui-SFML](https://github.com/SFML/imgui-sfml) (ImGui-SFML binding)
  - [pybind11](https://github.com/pybind/pybind11) (Python bindings, in development)

### Build Instructions

```bash
# Clone the repository
git clone <repository-url>
cd PriceRiot-main

# Create build directory
mkdir build && cd build

# Configure and build
cmake ../cxx
cmake --build . --config Release

# Run the simulator (requires store.yaml in working directory)
./simulator
# or on Windows:
simulator.exe
```

The build produces:
- `simulator.exe` - Standalone C++ simulation with visualization
- `simulation.cp313-win_amd64.pyd` - Python extension module (when bindings are complete)

## Usage

### Running the C++ Simulator

1. **Create a store configuration** (`store.yaml`):
   ```yaml
   nodes:
     - id: 1
       type: Entrance
       x: 0.0
       z: 0.0
       length: 5.0
       width: 3.0
   edges:
     - id: 1
       from: 1
       to: 2
       length: 10.0
       width: 2.5
   # ... (see examples for full schema)
   ```

2. **Run the simulator**:
   ```bash
   ./simulator
   ```

3. **Use ImGui controls**:
   - Adjust spawn rate and time scale
   - Pause/resume simulation
   - Monitor active agents and store topology

### Python Analytics (Current)

```python
from python.data_processing import process_data

# Process retail sales and churn data
data = process_data(
    'retail_sales_dataset.csv',
    'online_retail_customer_churn.csv',
    'data/processed'
)

# Outputs:
# - customer_stats.csv
# - category_stats.csv
# - transaction_info.csv
# - merged_sales.csv
```

### Python Integration (Planned)

```python
import simulation  # Python bindings (to be implemented)

# Create and run simulation
sim = simulation.Simulator("store.yaml")
sim.run(duration_seconds=3600)

# Get transaction data
transactions = sim.get_transactions()
customers = sim.get_customers()

# Export to CSV
sim.export_transactions("transactions.csv")
```

## Current Status

### ✅ Completed (~75%)
- Core simulation engine with customer agents
- Shelf inventory system with picking and restocking
- Graph-based store layout with YAML configuration
- Real-time visualization with SFML/ImGui
- Basket and transaction generation
- Python data processing pipeline
- Staff restocking logic

### 🚧 In Progress
- Python bindings (pybind11 integration)
- FastAPI endpoints for simulation control
- Store configuration examples and documentation

### 📋 Planned
- **Multi-threaded simulation** for larger scale
- **Dashboard visualization** (Streamlit or similar)
- **ML models** for churn prediction and demand forecasting
- **Optimization algorithms** for layout and staffing
- **Kafka streaming** for real-time transaction pipeline
- **Advanced analytics** with real-time metrics
- **Store layout optimization** using simulation results

## Configuration

### Store Layout (YAML)

The store is defined as a graph with nodes and edges:

- **Nodes**: Entrances, Exits, Junctions, Registers, Stockrooms
- **Edges**: Aisles connecting nodes, with shelf configurations
- **Cells**: Discretized segments along edges for traffic flow
- **Shelves**: Bays, faces, and slots with SKU assignments

### Inventory Configuration

- **On-shelf inventory**: Defined per cell/side/bay/face/slot
- **Backstock**: Central inventory pool managed by `InventoryPool`
- **Restocking**: Automated via `StockBoy` class with configurable policies

## Technical Details

### Customer Behavior System

Customers use a **strategy pattern** with `ICustomerBehavior` interface:
- `DefaultBehavior`: State machine with BFS pathfinding
- Extensible for custom behaviors (e.g., price-sensitive, brand-loyal)

### Shelf System Architecture

- **Stalls**: 1:1 mapping with bays for customer positioning
- **Side bands**: Manage customer flow into/out of stalls
- **Blocking**: Prevents picking during restocking operations
- **Picking**: Preferred slot → fallback search → failure handling

### Traffic Flow Model

- **Cell-based discretization** for density calculations
- **Personal space** and **jam density** parameters
- **Lane capacity** based on clear width (accounting for shelf protrusions)
- **Congestion-aware** navigation decisions

## Future Vision

PriceRiot is designed as both a **portfolio project** demonstrating systems programming and simulation expertise, and a **prototype SaaS concept** for retail analytics.

**Potential Applications:**
- **Store layout optimization** through A/B testing in simulation
- **Staffing optimization** based on traffic patterns
- **Inventory management** with demand forecasting
- **Customer journey analysis** and conversion optimization
- **Churn prediction** using behavioral simulation data
- **Price elasticity testing** in controlled environments

## Contributing

This is a personal project, but suggestions and feedback are welcome!

## License

MIT License

---

**Note**: The Python bindings are currently incomplete. The C++ simulator runs standalone, and Python analytics work on pre-generated CSV data. Full integration is planned for future releases.
