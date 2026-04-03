# Implementation status vs plan

This checklist tracks **[`.claude/implementation_plan.md`](.claude/implementation_plan.md)** against the codebase (**last reviewed: 2026-04-03**). Use it for roadmap planning, not as a CI gate.

Legend: **Done** · **Partial** · **Open** · **N/A** (superseded or intentionally different)

---

## Phase 1 — Worker spatial movement

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 1.1 | Position + waypoints on worker | **Done** | `Worker` uses `Agent::posX_` / `posZ_`; `waypoints_`, `waypointIdx_` in [`cxx/agents/worker.h`](cxx/agents/worker.h) |
| 1.2 | `WorkerContext` struct | **N/A** | Plan suggested a single context type; implementation passes `StoreGraph`, `CheckoutQueueManager*`, `CollisionManager*` into `Worker::update()` instead ([`cxx/agents/worker.cpp`](cxx/agents/worker.cpp)) |
| 1.3 | `MovingToTask` navmesh traversal | **Done** | Path via `NavMeshPathfinder::findPath`, step movement + optional avoidance ([`cxx/agents/worker.cpp`](cxx/agents/worker.cpp)) |
| 1.4 | Task target resolution | **Done** | Stocking: edge cell center; register: lane waypoint ([`cxx/agents/worker.cpp`](cxx/agents/worker.cpp)) |
| 1.5 | Simulator wires worker update | **Done** | [`cxx/engine/simulator.cpp`](cxx/engine/simulator.cpp) `updateWorkers` calls `w->update(dt, store_, &queueManager_, cmPtr)` |
| 1.6 | pybind11 `get_workers()` | **Done** | Worker snapshots consumed in [`python/webapp/simulation_runner.py`](python/webapp/simulation_runner.py) |

---

## Phase 2 — Task lifecycle closure

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 2.1–2.2 | `WorkerContext` with `Environment*`, effects in worker | **Partial** | Completion handling lives in **`Simulator::updateWorkers`**, not inside `Worker::tickExecuting` ([`cxx/engine/simulator.cpp`](cxx/engine/simulator.cpp)) |
| 2.3 | `Environment::restockEdge` / `serviceQueueLane` | **Partial** | **StockShelves:** `Edge` cells `restock(3)` on completion in simulator (same intent as plan, different API). **ProcessRegister:** comment states queue drains with checkout; no explicit `serviceQueueLane`-style pop |
| 2.4 | Thread safety | **Done** | Single-threaded tick; no new locks required |

---

## Phase 3 — POS ingestion → webapp wiring

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 3.1 | `extract_params` in run path | **Done** | [`python/webapp/simulation_runner.py`](python/webapp/simulation_runner.py) imports and calls `extract_params` when `pos_data` set |
| 3.2 | `ingestion_profile` on `RunResult` | **Done** | [`python/webapp/models.py`](python/webapp/models.py) |
| 3.3 | `GET /api/runs/{run_id}/profile` | **Done** | [`python/webapp/app.py`](python/webapp/app.py) |

---

## Phase 4 — Ingestion summary UI

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 4.1 | `IngestionProfile.vue` | **Done** | [`python/webapp/frontend/src/components/IngestionProfile.vue`](python/webapp/frontend/src/components/IngestionProfile.vue) |
| 4.2 | Wired into run flow | **Done** | Run tab + profile fetch path (see `RunTab.vue`, API client) |

---

## Phase 5 — SQLite persistence

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 5.1 | `aiosqlite` dependency | **Done** | [`requirements.txt`](requirements.txt) |
| 5.2 | Async DB for runs + CSV on disk | **Done** | [`python/webapp/storage.py`](python/webapp/storage.py) — `Database`, `data/runs/`, `data/priceriot.db` |
| 5.3 | Delete endpoint | **Done** | `DELETE /api/runs/{run_id}` and bulk delete [`python/webapp/app.py`](python/webapp/app.py) |
| 5.4 | Lifespan `init` | **Done** | `session_store.init()` in app lifespan |

---

## Phase 6 — `POST /api/runs/{id}/workers`

| Step | Plan item | Status | Evidence / notes |
|------|-----------|--------|------------------|
| 6.1 | Endpoint to tweak staff counts on queued run | **Open** | No `POST .../workers` in [`python/webapp/app.py`](python/webapp/app.py); staff counts set at run creation (`SimConfigPanel` / form) |
| 6.2 | `WorkersPanel.vue` | **N/A** | Component removed; configuration consolidated in run UI |

---

## Deferred (P3 / P4) — unchanged from plan

Items in the plan’s “Deferred” table (pricing hooks, sweep API, layout optimizer, ML) remain **Open** at the product level unless separately specified.

---

## Not in original plan — implemented elsewhere

| Area | Notes |
|------|--------|
| **Temporal / multi-day sim** | [`python/analytics/temporal.py`](python/analytics/temporal.py), [`python/analytics/orchestrator.py`](python/analytics/orchestrator.py), CLI [`scripts/temporal_sim.py`](scripts/temporal_sim.py) |
| **Webapp simulation jobs** | `POST /api/simulations` and job store [`python/webapp/sim_runner.py`](python/webapp/sim_runner.py), [`python/webapp/sim_storage.py`](python/webapp/sim_storage.py) |
| **Layout editor tab** | Vue: [`python/webapp/frontend/src/components/LayoutTab.vue`](python/webapp/frontend/src/components/LayoutTab.vue), backend serializer [`python/webapp/layout_serializer.py`](python/webapp/layout_serializer.py) |
| **Utility scripts** | e.g. [`scripts/multi_run.py`](scripts/multi_run.py), [`scripts/sanity_check.py`](scripts/sanity_check.py) |

---

## How to refresh this document

After meaningful changes, re-check:

1. Worker/task flow: `cxx/agents/worker.*`, `cxx/engine/simulator.cpp` (`updateWorkers`, `generateTasks`).
2. Webapp routes: `grep @app\\.` in `python/webapp/app.py`.
3. Ingestion: `simulation_runner.py`, `ingestion/param_extractor.py`.

Update the tables and this file’s intro sentence with the date or commit range.
