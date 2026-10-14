# NavMesh / Pathfinding Performance Sprint
**Date:** 2026-04-20 · **Branch:** `navmesh-movement`

---

## Background

A single-day simulation run on the **Cowboy_Outpost** layout (23,500 sqft, full `cowboy_products` catalog) was completing in **52 minutes** of wall time. This made multi-run and multi-day scenarios impractical from the webapp. The run produced 52 transactions, confirming the engine was functionally correct — just far too slow.

---

## Root Causes Identified

### 1. O(N) polygon lookups on every path request
`NavMesh::findPolygonContaining()` and `findNearestPolygon()` performed a **full linear scan** over every polygon in the mesh on each call. With a large layout, those two calls were hit twice at the start of every `findPath()` invocation — before A* even started.

### 2. No shared path cache
The A* search was re-run in full every time an agent needed a path, subject only to a **per-agent 2.5-second TTL cache**. The navmesh is static: the polygon corridor between any two polygon indices never changes. Multiple agents heading to the same shelf, checkout, or exit were each paying full A* cost independently.

### 3. Tick rate hardcoded to 60 Hz with time-baked constants
The simulation stepped at `dt = 1/60 s` (2,160,000 ticks for a 10-hour day). Agent dwell times were stored as **tick counts** with the 60 Hz rate baked in (`dwellTicks += dec.duration * 60`). The stuck-waypoint detector used a raw tick counter (`kMaxStuckTicks = 180`). These hardcoded assumptions made it unsafe to raise the timestep without breaking behaviour.

---

## Fixes Implemented

### Fix A — Shared A* path cache (`navmesh.h/.cpp`, `navmesh_pathfinder.cpp`)

`NavMesh` now carries a `mutable unordered_map` keyed by `(startPolyIdx << 22 | goalPolyIdx)`. On any `findPath()` call:

- **Cache hit:** skip A* entirely, re-run funnel smoothing only with the actual world-space start/end coordinates (fast — O(path length)).
- **Cache miss:** run A* as before, store the resulting polygon corridor.

The cache is part of `NavMesh` and clears automatically on `NavMesh::clear()`, which is called each time a new store YAML is loaded. No manual invalidation is required. The cache grows most valuable in multi-day and multi-run scenarios where the same agent-to-destination corridors recur across many sim-days.

### Fix B — Spatial grid for polygon lookup (`navmesh.h/.cpp`)

A 2D spatial grid (4 m cell size) is built lazily on the first query and reused for the lifetime of the navmesh. Each polygon's AABB is inserted into all overlapping cells at build time.

- **`findPolygonContaining(x, z)`:** looks up a single grid cell — O(1) average instead of O(N).
- **`findNearestPolygon(x, z)`:** expands outward in cell rings (radius 0 → 4) until a walkable polygon candidate is found, with a full linear scan as a final fallback only if the store has an unusual gap in coverage.

### Fix C — Time-based dwell and raised timestep (`customer.h/.cpp`, `customer_behavior.h/.cpp`, Python)

`BehaviorState::dwellTicks` (int, tick-counted) was replaced with `dwellTimeSeconds` (float, decremented by `dt` each update). `getDwellTicks()` is kept as a backward-compat shim returning `(int)(dwellTimeSeconds * 60)` so the SFML visualiser and behavior logs continue to compile unmodified.

The stuck-waypoint detector in both `DefaultBehavior` and `MissionBehavior` was similarly converted: `waypointStuckSeconds` (float) accumulates `ctx.dt` per tick; the limit is `3.0f` seconds of real sim time regardless of step size.

With those baked-in 60 Hz assumptions removed, the default timestep was raised from **`dt = 1/60`** to **`dt = 1/10`** across `core.py`, `temporal.py`, and `temporal_workload.py`. This reduces tick count from 2,160,000 to 360,000 per sim-day — a 6× reduction with no change to observable timing behaviour.

---

## Results

| Scenario | Before | After | Speedup |
|---|---|---|---|
| Cowboy_Outpost, single day, 52 txns | 52 min 18 s | 8 min 27 s | **6.1×** |
| Cowboy_Market_v2, 1 month, 3 parallel runs | — | 4 min 2 s / run | baseline |

The Cowboy_Outpost result lands at ~6.6 simulated seconds per wall-second. The path cache provides additional compounding benefit on multi-day runs as the warm cache eliminates A* for repeat origin-destination pairs from day 2 onward.

---

## Current Platform State

### NavMesh & Pathfinding
- **Fully operational** with A* + SSFA (Simple Stupid Funnel Algorithm) smoothing.
- Spatial grid brings polygon lookup to O(1) average — no longer a bottleneck on large layouts.
- Shared path cache eliminates redundant A* searches across agents and sim-days.
- All timing constants (dwell, stuck detection) are now rate-independent; `dt` is a safe tuning parameter.
- The 10 Hz default is suitable for all current layouts. It can be lowered (e.g. to 1/20) for higher precision at the cost of speed, or the Python caller can override `dt` explicitly.
- **Remaining ceiling:** A* itself is still O(P log P) per cache miss. On very large or fragmented meshes, hierarchical pathfinding (cluster-based) would be the next lever if needed.

### Simulation Engine
- Single-day runs on large layouts (~23,500 sqft) complete in under 10 minutes.
- Multi-day temporal runs (1 month) complete in ~4 minutes on mid-sized layouts.
- Parallel multi-run jobs work correctly — each run owns its own `Simulator` and `NavMesh` instance; caches are isolated per instance.
- Transaction, heatmap, queue, and worker metrics are all captured and surfaced to analytics.

### Webapp
- Layout editor, run configuration, and results UI are functional.
- The dynamic timeout budget in `sim_runner.py` scales with `approx_step_ticks`, which is now computed at the new `dt = 1/10` rate — severity thresholds updated accordingly.
- Progress polling, per-run summaries, and aggregate stats all work end-to-end.
- The webapp can now realistically queue multi-day or multi-run jobs without risking timeout on large layouts under normal conditions.

### Known Remaining Work
- Wall-snap broken in layout editor (Phase 3 backlog).
- Register auto-connect and line-of-sight junction connect not yet implemented.
- Shelf side picker, delete shelf from edge, free shelf resize/rotate are deferred.
- Multi-threaded simulation (running multiple agent ticks in parallel) is on the planned roadmap and would provide further speedup for very large or very long runs.
