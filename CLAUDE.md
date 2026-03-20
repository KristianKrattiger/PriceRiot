# CLAUDE.md — PriceRiot

This file guides Claude Code when working in the PriceRiot repository.

---

## Project Overview

**PriceRiot** is a retail store simulation engine. The core is written in C++17 and exposed to Python via a pybind11 extension module (`simulation`). A Python analytics layer sits on top, along with a Streamlit dashboard, FastAPI service, and Jupyter notebooks.

Primary goals:
- Simulate autonomous customer agents in a configurable store graph.
- Generate realistic transaction/basket/traffic data.
- Expose that data to Python analytics, dashboards, and ML pipelines.

---

## Repository Layout

```
PriceRiot/
├── cxx/                        # All C++ source
│   ├── agents/                 # Customer, Staff, Basket, behaviours
│   ├── environment/            # StoreGraph, NavMesh, Physics, Shelves, Queues
│   ├── engine/                 # Simulator (headless), SFML visualiser, NavMesh debug
│   ├── bindings/               # pybind11 module: simulation
│   ├── tools/                  # CSV/utility helpers
│   └── docs/
│       ├── ARCHITECTURE.md     # Layer diagram and data-flow descriptions
│       └── CONFIGURATION.md    # store.yaml schema reference
├── python/
│   ├── analytics/              # run_simulation(), DataFrame converters, heatmaps
│   ├── dashboard/              # Streamlit app (app.py)
│   ├── ingestion/              # CSV cleaning, SKU mapping, parameter extraction
│   ├── webapp/                 # FastAPI + frontend for scenario comparison
│   ├── run_analysis.py         # CLI: run sim → export CSVs
│   ├── main.py                 # FastAPI: POST /sim/run
│   └── simulate.py             # Thin notebook/script wrapper
├── notebooks/
│   ├── basic_analysis.ipynb
│   └── queues_and_heatmaps.ipynb
├── examples/
│   ├── store_tiny.yaml
│   └── store_bottleneck.yaml
├── tests/
│   └── test_simulation.py
└── scripts/
    ├── build_and_test.sh
    └── build_and_test.ps1
```

---

## Architecture (Key Mental Model)

The engine has four layers. Always keep them in mind when editing:

| Layer | Location | Responsibility |
|-------|----------|----------------|
| **Drivers** | `cxx/engine/sim.cpp`, `python/`, `python/webapp/`, `python/dashboard/` | Consume the `Simulator` API; no business logic here |
| **Simulation Engine** | `cxx/engine/simulator.cpp` | Owns the simulation loop, RNG, agents, graph, queues, metrics |
| **Agent Layer** | `cxx/agents/` | Customer state machines, basket building, staff restocking |
| **Environment Layer** | `cxx/environment/` | StoreGraph, NavMesh, PhysicsWorld, Shelves, CheckoutQueues |

**Key rule:** `simulator.cpp` has no SFML/ImGui. The visualiser (`sim.cpp`) and Python module (`pybind11`) both drive it from the outside.

### Navigation

Agents use either:
- **NavMesh pathfinding** (`NavMeshPathfinder` → A* + funnel smoothing) when a navmesh is built.
- **Graph-based edge traversal** (BFS on `StoreGraph`) as a fallback.

### Data Flow

```
store.yaml → StoreGraph → NavMesh + PhysicsWorld
                       → Customers (update each tick)
                       → Transactions (CSV / DataFrame)
                       → Heatmaps + Queue metrics (via analytics)
```

---

## Build System

### Prerequisites

- C++17 compiler (MSVC, Clang, or GCC)
- CMake 3.14+
- Python 3.10+ (must match the version used at CMake configure time)
- SFML, ImGui-SFML, yaml-cpp, pybind11 — all fetched automatically by CMake

### Build Commands

```bash
# Recommended (builds + runs smoke tests)
./scripts/build_and_test.sh        # Linux/macOS
.\scripts\build_and_test.ps1       # Windows PowerShell

# Manual CMake
mkdir build && cd build
cmake ../cxx
cmake --build . --config Release
```

Output lands in `build/` (single-config) or `build/Release/` (MSVC multi-config).

### PYTHONPATH

Always set before running Python tooling:

```bash
# Linux/macOS
export PYTHONPATH=build:python

# Windows PowerShell
$env:PYTHONPATH = "build;python"
```

The `simulation` module is tied to the Python version used at build time. Use the same interpreter everywhere.

---

## Running Things

### C++ Visualiser

```bash
cd build
./simulator                                      # uses store.yaml in CWD
./simulator ../examples/store_tiny.yaml
./simulator ../examples/store_bottleneck.yaml
```

### Python Tests

```bash
PYTHONPATH=build:python python tests/test_simulation.py
```

### Analytics CLI

```bash
PYTHONPATH=build:python python -m python.run_analysis --duration 600 --store store.yaml
# Outputs: data/processed/transactions.csv, data/processed/customers.csv
```

### Streamlit Dashboard

```bash
PYTHONPATH=build:python streamlit run python/dashboard/app.py
```

### FastAPI Service

```bash
PYTHONPATH=build:python uvicorn python.main:app --reload
# POST /sim/run
```

### Jupyter Notebooks

```bash
PYTHONPATH=build:python jupyter lab
```

---

## Store Configuration (`store.yaml`)

Full schema in `cxx/docs/CONFIGURATION.md`. Key sections:

| Section | Purpose |
|---------|---------|
| `nodes` | Entrances, Exits, Junctions, Registers, Stockrooms — with position, traffic, and service params |
| `edges` | Aisles connecting nodes — width, length, flow direction, shelf protrusions |
| `planogram.edges` | Per-edge SKU assignments (bay / face / slot / qty) |
| `left_side` / `right_side` | Top-level fallback shelf definitions for edges not in `planogram.edges` |
| `checkout_queues` | Per-register FIFO queue with waypoints and processing time |

When modifying YAML, validate quickly with the smoke tests:

```bash
PYTHONPATH=build:python python tests/test_simulation.py
```

---

## Python Analytics API

```python
import analytics

result = analytics.run_simulation(
    store_path="store.yaml",
    duration_seconds=3600.0,
    spawn_interval=5.0,
    mission_probability=0.5,
    seed=42,
)

result.transactions   # pd.DataFrame — one row per line item
result.customers      # pd.DataFrame — one row per customer
result.simulator      # simulation.Simulator instance

# Heatmaps and queue metrics
cell_counts = result.simulator.get_cell_heatmap()
heatmap_df  = analytics.cell_heatmap_to_frame(cell_counts)

times    = result.simulator.get_queue_sample_times()
lengths  = result.simulator.get_queue_lengths_history()
queue_df = analytics.queue_metrics_to_frame(times, lengths)
```

---

## Code Conventions

### C++

- Standard: C++17. Avoid C++20 features unless the project already uses them.
- Headers in the same directory as `.cpp` files (no separate `include/` tree).
- Module boundaries matter: `environment/` knows nothing about `agents/`; `engine/` orchestrates both.
- No SFML/ImGui headers outside `engine/sim.cpp` and `engine/navmesh_visualizer.cpp`.
- New environment features belong in `environment/`; new agent behaviours in `agents/`.

### Python

- Python 3.10+. Type hints encouraged on new code.
- `python/analytics/` wraps the C++ API and returns DataFrames — keep this layer thin.
- Driver scripts (`run_analysis.py`, `main.py`, etc.) should delegate to `analytics/`, not re-implement logic.
- Avoid importing `simulation` directly outside `analytics/` and `bindings/` — go through the helpers.

### YAML

- Store layouts live in `examples/` (checked in) or project root (`store.yaml`, gitignored).
- Use descriptive node/edge IDs and comments for non-trivial layouts.

---

## Testing

`tests/test_simulation.py` covers:
- Simulator construction and `step()` / `run()`.
- Invariant checks (non-negative counts, etc.).
- Metric exposure (`get_cell_heatmap`, `get_queue_*`).

Run after every C++ change. Add tests for new public API surface on the `simulation` module.

---

## Current Status

### Done
- C++ engine: agents, shelves, restocking, transactions, navmesh, physics, queues
- SFML/ImGui visualiser
- `simulation` pybind11 module
- Python analytics helpers (DataFrames, heatmaps, queue metrics)
- Streamlit dashboard
- FastAPI service + webapp
- Ingestion pipeline (CSV cleaning, SKU mapping)
- Smoke/systemic tests

### Planned / In Progress
- Multi-threaded simulation for larger scenarios
- ML models for churn prediction and demand forecasting
- Layout and staffing optimisation loops
- Optional streaming pipeline for real-time feeds

---

## Common Tasks

### Add a new customer behaviour
1. Subclass `ICustomerBehavior` in `cxx/agents/customer_behavior.h/.cpp`.
2. Implement `decide(customer, ctx)` returning a `Decision`.
3. Wire it into `Simulator` construction or expose a factory if parameterised.
4. Add a test case in `tests/test_simulation.py` that exercises the new behaviour.

### Add a new metric
1. Aggregate in `cxx/engine/simulator.cpp` (or the relevant `environment/` class).
2. Expose via a getter on `Simulator`.
3. Bind in `cxx/bindings/priceriot_bindings.cpp`.
4. Add a DataFrame converter in `python/analytics/core.py`.
5. Smoke-test the new binding in `tests/test_simulation.py`.

### Add a new store layout
1. Create `examples/my_layout.yaml` following the schema in `cxx/docs/CONFIGURATION.md`.
2. Test it: `PYTHONPATH=build:python python tests/test_simulation.py` (point at your file if needed).
3. Run the visualiser: `./build/simulator examples/my_layout.yaml`.

### Modify store.yaml schema
1. Update parsing in `cxx/environment/environment.cpp`, `store_init.cpp`, or `checkout_queue.cpp`.
2. Update `cxx/docs/CONFIGURATION.md`.
3. Update or add an example layout.

---

## Pitfalls to Avoid

- **Python version mismatch**: The `simulation` module is compiled against a specific CPython ABI. Always use the same interpreter for build and runtime.
- **SFML in headless code**: Never include SFML/ImGui headers in `simulator.cpp`, `agents/`, or `environment/`. The Python module is built without SFML.
- **Direct simulation import in analytics**: Route through `python/analytics/` so the DataFrame interface stays stable.
- **Multi-config build output**: On MSVC, binaries land in `build/Release/`, not `build/`. Update `PYTHONPATH` accordingly.
- **Hardcoded `store.yaml` paths**: Prefer passing paths explicitly rather than assuming CWD; scripts in `data/processed/` are generated output, not source.
