# PriceRiot

A retail store simulation engine for generating synthetic transaction data, studying customer behavior, and stress-testing store layouts. The core runs in C++17 and is exposed to Python via a pybind11 extension module. A FastAPI + Vue 3 web app, a Streamlit dashboard, and Jupyter notebooks sit on top for analysis, scenario comparison, and data ingestion.

For deeper architectural detail see **[cxx/docs/ARCHITECTURE.md](cxx/docs/ARCHITECTURE.md)** and the store config schema in **[cxx/docs/CONFIGURATION.md](cxx/docs/CONFIGURATION.md)**.

For a **phase-by-phase checklist** (worker movement, task effects, ingestion, SQLite, deferred API work), see **[IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md)**.

---

## What it does

- Spawns autonomous customer agents with demographics, shopping lists, and behavioral profiles navigating a YAML-defined store graph
- Agents pathfind via A\* on a navmesh with SSFA funnel smoothing, agent-radius erosion, physics collision avoidance, and personal-space separation
- Generates transaction records per customer: SKUs, quantities, prices, checkout queueing, satisfaction scoring
- Worker agents (stockers, cashiers) accept prioritized tasks from a `TaskManager` — task compatibility, starvation mitigation, and happiness/efficiency coupling are all modeled
- Exposes traffic heatmaps (per-shelf-cell visit counts) and queue depth time-series to Python
- Runs headlessly from Python for data generation, or as a real-time SFML/ImGui visualiser
- FastAPI + Vue 3 web app: single-run uploads (YAML + optional POS), SSE progress, results dashboards, scenario comparison, optional **layout editor** tab, and **temporal / multi-run** jobs via the simulations API
- POS ingestion pipeline cleans historical CSV data, maps SKUs, and extracts simulation parameters (applied at run time when POS data is attached)
- **Temporal analytics**: date-range scheduling and multi-run orchestration in `python/analytics` plus CLI/scripts (`scripts/temporal_sim.py`, `scripts/multi_run.py`, `scripts/sanity_check.py`)

---

## Repository layout

```
PriceRiot/
├── cxx/                        # C++17 source
│   ├── agents/                 # Customer, Worker, Basket, behavior strategies
│   ├── environment/            # StoreGraph, NavMesh, PhysicsWorld, Shelves, CheckoutQueues
│   ├── engine/                 # Simulator (headless), SFML visualiser, TaskManager
│   ├── bindings/               # pybind11 module: simulation
│   ├── tools/                  # CSV/utility helpers
│   └── docs/
│       ├── ARCHITECTURE.md     # Layer diagram and data-flow descriptions
│       └── CONFIGURATION.md    # store.yaml schema reference
├── python/
│   ├── analytics/              # run_simulation(), temporal + orchestrator, DataFrame helpers
│   ├── dashboard/              # Streamlit app (simulation + POS analysis modes)
│   ├── ingestion/              # POS CSV cleaning, SKU mapping, parameter extraction
│   ├── webapp/                 # FastAPI (+ sim jobs), Vue 3 frontend, SQLite run store
│   ├── run_analysis.py         # CLI: run sim → export CSVs
│   ├── main.py                 # FastAPI entrypoint
│   └── simulate.py             # Thin notebook/script wrapper
├── notebooks/
│   ├── basic_analysis.ipynb    # Transactions, customers, basket composition
│   └── queues_and_heatmaps.ipynb
├── examples/
│   ├── store_tiny.yaml
│   ├── store_bottleneck.yaml
│   ├── cowboy_market.yaml
│   └── cowboy_market_two_registers.yaml
├── tests/
│   └── test_simulation.py      # Smoke/integration tests for the simulation module
└── scripts/
    ├── build_and_test.sh
    ├── build_and_test.ps1
    ├── temporal_sim.py         # CLI: date-range temporal simulation
    ├── multi_run.py            # Batch / multi-run helper
    └── sanity_check.py         # Regression-style sanity runs
```

---

## Architecture

Four layers. Keep them cleanly separated:

| Layer | Location | Responsibility |
|---|---|---|
| **Drivers** | `cxx/engine/sim.cpp`, `python/`, `python/webapp/` | Consume the Simulator API; no business logic |
| **Simulation Engine** | `cxx/engine/simulator.cpp` | Owns the loop, RNG, agents, graph, queues, metrics |
| **Agent Layer** | `cxx/agents/` | Customer and Worker state machines, basket building, task execution |
| **Environment Layer** | `cxx/environment/` | StoreGraph, NavMesh, PhysicsWorld, Shelves, CheckoutQueues |

Key rules:
- `simulator.cpp` has no SFML/ImGui — the visualiser and Python module drive it from outside
- `environment/` knows nothing about `agents/`
- New environment features go in `environment/`; new behaviors in `agents/`

---

## Build

### Prerequisites

- C++17 compiler (MSVC, Clang, or GCC)
- CMake 3.14+
- Python 3.10+ (must match the version used at CMake configure time)
- SFML, ImGui-SFML, yaml-cpp, pybind11 — all fetched automatically by CMake

### Build commands

```bash
# Recommended: builds + runs test suite
.\scripts\build_and_test.ps1          # Windows PowerShell
./scripts/build_and_test.sh           # Linux/macOS (full: visualizer + Python module)
./scripts/build_and_test.sh --headless # Linux/macOS (Python module only, no SFML/ImGui)

# Manual headless CMake (from repo root)
cmake -S cxx -B build -DBUILD_VISUALIZER=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j4
```

Output: `build/` (single-config) or `build/Release/` (MSVC multi-config).

### PYTHONPATH

```bash
# Linux/macOS
export PYTHONPATH=build:python

# Windows PowerShell
$env:PYTHONPATH = "build;python"
```

---

## Running things

### C++ visualiser

```bash
./build/simulator ../examples/store_tiny.yaml
./build/simulator ../examples/cowboy_market.yaml
```

ImGui controls: pause/resume, time scale, spawn interval, stats overlay, navmesh/physics debug views.

### Tests & Calibration Evaluation

```bash
# Python + C++/pybind11 engine, ETL, sweeps, and FastAPI backend tests
PYTHONPATH=build:python:. pytest -v tests/

# Vue 3 frontend tests (Vitest)
cd python/webapp/frontend && npm test

# Held-out POS calibration & controlled price-elasticity evaluation
# Outputs: data/eval/calibration_cowboy_holdout.json and data/eval/CALIBRATION_REPORT.md
PYTHONPATH=build:python:. python scripts/evaluate_calibration.py
```

### Analytics CLI

```bash
PYTHONPATH=build:python python -m python.run_analysis --duration 600 --store store.yaml
# Writes: data/processed/transactions.csv, data/processed/customers.csv
```

### Streamlit dashboard

```bash
PYTHONPATH=build:python streamlit run python/dashboard/app.py
```

### FastAPI + Vue 3 web app

```bash
# Backend
PYTHONPATH=build:python uvicorn python.main:app --reload

# Frontend (separate terminal)
cd python/webapp/frontend && npm run dev
# Open http://localhost:5173
```

### Jupyter notebooks

```bash
PYTHONPATH=build:python jupyter lab
```

---

## Python analytics API

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

## Current status

### Working

**Simulation core**

| Component | Notes |
|---|---|
| C++ simulation engine | Headless, deterministic, mutex-safe transaction access |
| Customer agents | Demographics, trip purpose (StockUp/TopUp/Mission), impulse buying, basket building |
| NavMesh pathfinding | A\* + SSFA funnel, agent-radius erosion, corner smoothing, waypoint-skip LOS |
| Physics collision | AABB shelf obstacles trimmed at junctions, per-agent circle resolution |
| Checkout queues | Multi-lane FIFO, service times, waypoint-based queue positioning |
| Transaction recording | Full line-item detail (SKU, qty, price), satisfaction scoring |
| Traffic heatmaps | Per-shelf-cell visit counts, exposed to Python |
| Queue metrics | Per-lane depth sampled over time, exposed to Python |

**Worker subsystem**

| Component | Notes |
|---|---|
| Worker agents | Idle → move along navmesh (with light collision avoidance) → execute task |
| Worker task lifecycle | `StockShelves` completion applies restock via `Simulator::updateWorkers` (per-edge cells, 3 units/slot). Register tasks move workers to lane waypoints; queue service is driven by checkout flow (no separate `serviceQueueLane`-style hook) |
| TaskManager | Priority queue with starvation mitigation, worker-task compatibility matching |
| Auto task generation | `generateTasks()` monitors queue depth and shelf levels; assigns tasks to idle workers |
| `set_worker_config` binding | Exposed in pybind11; Python/analytics can adjust staff counts when constructing the sim |

**Python / analytics layer**

| Component | Notes |
|---|---|
| pybind11 bindings | All core classes bound; `get_workers()` returns live snapshots as dicts |
| Python analytics | `run_simulation()`, DataFrame converters, heatmaps, queue metrics |
| POS ingestion pipeline | CSV cleaning, SKU mapping, parameter extraction from historical data |
| POS → run wiring | `extract_params()` auto-applied at run time; spawn interval + mission probability overridden from data when at defaults |

**Webapp (FastAPI + Vue 3)**

| Component | Notes |
|---|---|
| FastAPI backend | `POST /api/runs` (upload + stream), `POST /api/runs/{id}/workers`, run listing/detail, CSV + KPI export, comparison engine |
| Parameter sweeps | `POST /api/sweeps`, `GET /api/sweeps/{id}`, CSV export (`python/analytics/sweeps.py`, `sweep_runner.py`) |
| Dynamic pricing | C++ customer price elasticity + `set_sku_price()` / `get_sku_prices()` / `get_sku_sell_through()` pybind11 hooks |
| Temporal jobs | `POST /api/simulations` (202) and job/status/results endpoints for multi-day / batch workflows (`sim_runner`, `sim_storage`) |
| SQLite persistence | Runs, temporal jobs, and sweeps in `data/priceriot.db`; large CSVs under `data/runs/{id}/`; survives restarts |
| `DELETE /api/runs/{id}` | Removes run record and cleans up on-disk CSV artifacts |
| `GET /api/runs/{id}/profile` | POS-derived ingestion summary when a POS file backed the run |
| Vue 3 frontend | Tabs: **Run** (datasets, YAML, sim config), **Results**, **Compare**, **Layout** (canvas + YAML export) + Vitest test suite |
| Calibration eval | Leakage-free 21d→7d held-out POS evaluation + controlled pricing elasticity report in `data/eval/CALIBRATION_REPORT.md` |
| Ingestion profile | Spawn interval / basket hints from POS; `IngestionProfile.vue` and run-time `extract_params` |
| Streamlit dashboard | Simulation controls + charts, POS analysis mode |
| Jupyter notebooks | End-to-end examples: transactions, heatmaps, queue time-series |

### Out of Scope (Closed)

The following deferred ideas from early planning are formally **out of scope** for PriceRiot v1 (see **[IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md)**):

- Automated layout mutation / optimization loop (P3.9) — external callers can already mutate `store.yaml` and evaluate via `POST /api/sweeps` or `analytics.run_parameter_sweep()`.
- Built-in ML models for churn prediction, demand forecasting, and staffing optimization (P4.10–4.12) — PriceRiot exports clean synthetic transaction and customer CSVs for downstream ML pipelines rather than bundling model training inside the simulator repo.

---

## Common tasks

### Add a new customer behavior

1. Subclass `ICustomerBehavior` in `cxx/agents/customer_behavior.h/.cpp`
2. Implement `decide(customer, ctx)` returning a `Decision`
3. Wire it into `Simulator` construction or expose a factory
4. Add a test in `tests/test_simulation.py`

### Add a new metric

1. Aggregate in `cxx/engine/simulator.cpp` (or the relevant `environment/` class)
2. Expose via a getter on `Simulator`
3. Bind in `cxx/bindings/priceriot_bindings.cpp`
4. Add a DataFrame converter in `python/analytics/core.py`
5. Smoke-test in `tests/test_simulation.py`

### Add a new store layout

1. Create `examples/my_layout.yaml` following `cxx/docs/CONFIGURATION.md`
2. Test: `PYTHONPATH=build:python python tests/test_simulation.py`
3. Visualise: `./build/simulator examples/my_layout.yaml`

---

## Pitfalls

- **Python version mismatch** — the `simulation` module is compiled against a specific CPython ABI; use the same interpreter for build and runtime
- **SFML in headless code** — never include SFML/ImGui headers in `simulator.cpp`, `agents/`, or `environment/`; the pybind11 module builds without SFML
- **MSVC multi-config output** — binaries land in `build/Release/`, not `build/`; update `PYTHONPATH` accordingly
- **SQLite DB location** — `data/priceriot.db` is created relative to the project root on first webapp start; CSV blobs land in `data/runs/{run_id}/`

---

## License

MIT
