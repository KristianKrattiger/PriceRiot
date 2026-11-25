# Implementation status vs plan

This checklist tracks **[`.claude/implementation_plan.md`](.claude/implementation_plan.md)** and the project roadmap against the codebase (**last reviewed: 2026-11-25**).

**Project status: COMPLETED.** All core simulation, pricing, parameter-sweep, persistence, calibration evaluation, and full-stack test items are implemented and verified. Deferred ML / layout-optimizer ideas have been formally closed as **Out of Scope**.

Legend: **Done** · **Out of Scope** · **N/A** (superseded or intentionally different)

---

## Phase 1 — Worker spatial movement

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 1.1 | Position + waypoints on worker | **Done** | `Worker` uses `Agent::posX_` / `posZ_`; `waypoints_`, `waypointIdx_` in [`cxx/agents/worker.h`](cxx/agents/worker.h) |
| 1.2 | `WorkerContext` struct | **N/A** | Implementation passes `StoreGraph`, `CheckoutQueueManager*`, `CollisionManager*` directly into `Worker::update()` ([`cxx/agents/worker.cpp`](cxx/agents/worker.cpp)) |
| 1.3 | `MovingToTask` navmesh traversal | **Done** | Path via `NavMeshPathfinder::findPath`, step movement + optional avoidance ([`cxx/agents/worker.cpp`](cxx/agents/worker.cpp)) |
| 1.4 | Task target resolution | **Done** | Stocking: edge cell center; register: lane waypoint ([`cxx/agents/worker.cpp`](cxx/agents/worker.cpp)) |
| 1.5 | Simulator wires worker update | **Done** | [`cxx/engine/simulator.cpp`](cxx/engine/simulator.cpp) `updateWorkers` calls `w->update(dt, store_, &queueManager_, cmPtr)` |
| 1.6 | pybind11 `get_workers()` | **Done** | Worker snapshots consumed in [`python/webapp/simulation_runner.py`](python/webapp/simulation_runner.py) and tested in [`tests/test_pricing_and_workers.py`](tests/test_pricing_and_workers.py) |

---

## Phase 2 — Task lifecycle closure

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 2.1–2.3 | Task completion effects | **Done** | `Worker` emits `CompletedTask`; `Simulator::updateWorkers` applies `StockShelves` restock (`cell.restock(3)`) across edge cells ([`cxx/engine/simulator.cpp`](cxx/engine/simulator.cpp)). Register tasks route cashiers to lane waypoints while checkout flow drains queues |
| 2.4 | Thread safety | **Done** | Single-threaded tick loop; transaction reads mutex-protected |

---

## Phase 3 — POS ingestion → webapp wiring

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 3.1 | `extract_params` in run path | **Done** | [`python/webapp/simulation_runner.py`](python/webapp/simulation_runner.py) calls `extract_params` when `pos_data` is set |
| 3.2 | `ingestion_profile` on `RunResult` | **Done** | [`python/webapp/models.py`](python/webapp/models.py) |
| 3.3 | `GET /api/runs/{run_id}/profile` | **Done** | [`python/webapp/app.py`](python/webapp/app.py) |

---

## Phase 4 — Ingestion summary UI

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 4.1 | `IngestionProfile.vue` | **Done** | [`python/webapp/frontend/src/components/IngestionProfile.vue`](python/webapp/frontend/src/components/IngestionProfile.vue) (unit-tested via Vitest) |
| 4.2 | Wired into run flow | **Done** | Run tab + POS param extraction in [`python/webapp/frontend/src/components/RunTab.vue`](python/webapp/frontend/src/components/RunTab.vue) |

---

## Phase 5 — SQLite persistence

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 5.1 | `aiosqlite` dependency | **Done** | [`requirements.txt`](requirements.txt) |
| 5.2 | Async DB for runs + CSV on disk | **Done** | [`python/webapp/storage.py`](python/webapp/storage.py) (`Database`, `async_get_csv`, `data/runs/`, `data/priceriot.db`) |
| 5.3 | Delete endpoints | **Done** | `DELETE /api/runs/{run_id}` and bulk `DELETE /api/runs` in [`python/webapp/app.py`](python/webapp/app.py) |
| 5.4 | Lifespan `init` | **Done** | `session_store.init()`, `sim_job_store.init()`, and `sweep_store.init()` in FastAPI lifespan |

---

## Phase 6 — `POST /api/runs/{id}/workers`

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 6.1 | Endpoint to update staff counts on queued run | **Done** | `POST /api/runs/{run_id}/workers` in [`python/webapp/app.py`](python/webapp/app.py) (200 for queued runs, 400 for non-queued, 404 for unknown) + `updateRunWorkers` in [`python/webapp/frontend/src/api/client.js`](python/webapp/frontend/src/api/client.js) |
| 6.2 | `WorkersPanel.vue` | **N/A** | Staffing controls consolidated in `RunTab.vue`; API client wired for queued updates |

---

## Roadmap Closure (P3 / P4)

| Item | Status | Evidence / notes |
|------|--------|------------------|
| **P3.7 — Dynamic pricing & elasticity hooks** | **Done** | `Product::basePrice`, customer price-elasticity acceptance in [`cxx/agents/customer_behavior.cpp`](cxx/agents/customer_behavior.cpp), `set_sku_price` / `get_sku_price` / `get_sku_prices` / `get_sku_sell_through` in [`cxx/engine/simulator.cpp`](cxx/engine/simulator.cpp) and [`cxx/bindings/priceriot_bindings.cpp`](cxx/bindings/priceriot_bindings.cpp) |
| **P3.8 — Multi-run parameter sweeps** | **Done** | `run_parameter_sweep()` in [`python/analytics/sweeps.py`](python/analytics/sweeps.py); `POST/GET/DELETE /api/sweeps` and CSV export in [`python/webapp/app.py`](python/webapp/app.py), [`python/webapp/sweep_models.py`](python/webapp/sweep_models.py), [`python/webapp/sweep_runner.py`](python/webapp/sweep_runner.py) |
| **P3.9 — Layout optimization hook** | **Out of Scope** | Closed as out of scope for PriceRiot v1; external callers can already mutate `store.yaml` and evaluate via `run_parameter_sweep()` or `POST /api/sweeps` |
| **P4.10–4.12 — ML (churn, demand forecast, staffing optimizer)** | **Out of Scope** | Closed as out of scope for the simulation engine repository; synthetic transaction/customer CSVs are exported cleanly for downstream ML pipelines |

---

## Verification, Evaluation & Additional Capabilities

| Area | Status | Evidence / notes |
|------|--------|------------------|
| **Headless CMake build** | **Done** | `-DBUILD_VISUALIZER=OFF` (`PRICERIOT_HEADLESS`) builds `simulation` without SFML/ImGui ([`cxx/CMakeLists.txt`](cxx/CMakeLists.txt), [`scripts/build_and_test.sh`](scripts/build_and_test.sh)) |
| **Held-out POS calibration & elasticity eval** | **Done** | Reproducible script [`scripts/evaluate_calibration.py`](scripts/evaluate_calibration.py) with committed Tier-1 outputs [`data/eval/calibration_cowboy_holdout.json`](data/eval/calibration_cowboy_holdout.json) and [`data/eval/CALIBRATION_REPORT.md`](data/eval/CALIBRATION_REPORT.md) |
| **Full-stack test suite** | **Done** | Python/C++ pytest suite ([`tests/test_simulation.py`](tests/test_simulation.py), [`tests/test_temporal_equivalence.py`](tests/test_temporal_equivalence.py), [`tests/test_pricing_and_workers.py`](tests/test_pricing_and_workers.py), [`tests/test_ingestion.py`](tests/test_ingestion.py), [`tests/test_sweeps.py`](tests/test_sweeps.py), [`tests/test_webapp_api.py`](tests/test_webapp_api.py)) + Vue 3 Vitest suite ([`python/webapp/frontend/src/__tests__/`](python/webapp/frontend/src/__tests__/)) |
| **Temporal / multi-day sim & jobs** | **Done** | [`python/analytics/temporal.py`](python/analytics/temporal.py), [`python/analytics/orchestrator.py`](python/analytics/orchestrator.py), `POST /api/simulations` ([`python/webapp/sim_runner.py`](python/webapp/sim_runner.py), [`python/webapp/sim_storage.py`](python/webapp/sim_storage.py)) |
| **Layout editor tab** | **Done** | [`python/webapp/frontend/src/components/LayoutTab.vue`](python/webapp/frontend/src/components/LayoutTab.vue), [`python/webapp/layout_serializer.py`](python/webapp/layout_serializer.py) |
