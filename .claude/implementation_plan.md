# PriceRiot — Implementation Plan

Priority order: each phase unblocks or motivates the next. P3/P4 are long-term and skipped for now.

---

## Phase 1 — Worker Spatial Movement (P0.1)

Workers currently skip `MovingToTask` and execute in place. The fix mirrors how `Customer` follows
navmesh waypoints. Workers need a position, a waypoint buffer, and a movement tick.

### Step 1.1 — Add position + waypoints to Worker

**File: `cxx/agents/worker.h`**

Add to private section:
```cpp
float posX_ = 0.f, posZ_ = 0.f;
std::vector<PathPoint> waypoints_;   // PathPoint from navmesh_pathfinder.h
int waypointIdx_ = 0;
float taskTargetX_ = 0.f, taskTargetZ_ = 0.f;
```
Add public getter: `float posX() const`, `float posZ() const`

### Step 1.2 — Define WorkerContext

**File: `cxx/agents/worker.h`** (or a new `worker_context.h`)

```cpp
struct WorkerContext {
    const NavMesh*           navmesh;
    NavMeshPathfinder*       pathfinder;
    const StoreGraph*        graph;
    // for lifecycle effects (Phase 2):
    // Shelves*              shelves;
    // CheckoutQueues*       queues;
};
```

Change `Worker::update(float dt)` signature to `Worker::update(float dt, const WorkerContext& ctx)`.

### Step 1.3 — Implement MovingToTask in worker.cpp

Replace the placeholder with:
```cpp
case State::MovingToTask: {
    if (waypoints_.empty()) {
        // compute path on first tick
        waypoints_ = ctx.pathfinder->findPath(
            *ctx.navmesh, posX_, posZ_, taskTargetX_, taskTargetZ_);
        waypointIdx_ = 0;
    }
    if (waypointIdx_ < (int)waypoints_.size()) {
        auto& wp = waypoints_[waypointIdx_];
        float dx = wp.x - posX_, dz = wp.z - posZ_;
        float dist = std::sqrt(dx*dx + dz*dz);
        float step = WORKER_SPEED * dt;
        if (dist <= step) {
            posX_ = wp.x; posZ_ = wp.z;
            ++waypointIdx_;
        } else {
            posX_ += dx / dist * step;
            posZ_ += dz / dist * step;
        }
    } else {
        // arrived
        waypoints_.clear();
        state_ = State::ExecutingTask;
    }
    break;
}
```

Define `WORKER_SPEED` (e.g. 1.2 m/s — slightly faster than customers).

### Step 1.4 — Set taskTarget when assigning a task

**File: `cxx/agents/worker.cpp`** `assignTask()`:

The `Task::targetId` is a node or edge string. Resolve the target position from
`ctx.graph->getNode(targetId).position` (or edge midpoint). Store into
`taskTargetX_`, `taskTargetZ_`, and set `waypoints_.clear()` so MovingToTask recomputes.

> **Note:** Worker needs access to `ctx` at assignTask time, so either pass it there too, or
> store the target coords at the start of the MovingToTask branch (compute once, waypoints empty = first tick).
> The latter is simpler — just resolve from `ctx.graph` on the first tick of MovingToTask.

### Step 1.5 — Wire WorkerContext in simulator.cpp

**File: `cxx/engine/simulator.cpp`** — find the worker update loop, construct a `WorkerContext`
from the simulator's owned navmesh, pathfinder, and graph, and pass it into `worker.update()`.

### Step 1.6 — Update pybind11 worker snapshot

**File: `cxx/bindings/priceriot_bindings.cpp`** — `get_workers()` already includes `posX`/`posZ`
placeholders. Confirm they read from the new getters. Add `waypointCount` or `isMoving` bool if
useful for the workers panel.

---

## Phase 2 — Task Lifecycle Closure (P0.2)

When a `StockShelves` task completes, the target shelf cell should gain inventory.
When a `ProcessRegister` task completes, the queue lane gets a service credit.

Workers don't currently hold environment refs. Extend `WorkerContext` to include them.

### Step 2.1 — Extend WorkerContext with write access

```cpp
struct WorkerContext {
    const NavMesh*       navmesh;
    NavMeshPathfinder*   pathfinder;
    const StoreGraph*    graph;
    Environment*         env;     // exposes shelves and queues
};
```

Or pass `Simulator*` if that's cleaner — whatever avoids circular headers.

### Step 2.2 — Apply effect in Worker::tickExecuting()

**File: `cxx/agents/worker.cpp`** — after `remainingWorkSeconds_` reaches 0:

```cpp
if (currentTask_.type == TaskType::StockShelves) {
    ctx.env->restockEdge(currentTask_.targetId, RESTOCK_AMOUNT);
} else if (currentTask_.type == TaskType::ProcessRegister) {
    ctx.env->serviceQueueLane(currentTask_.targetId);
}
// notify task manager
ctx.taskManager->releaseTask(currentTask_.id);
// happiness bump
happiness_ = std::min(1.f, happiness_ + 0.05f);
state_ = State::Idle;
```

### Step 2.3 — Implement restockEdge / serviceQueueLane

Check `cxx/environment/environment.h` (or wherever shelves are) for an existing restock API.
If it doesn't exist, add:
- `Environment::restockEdge(const std::string& edgeId, int qty)` — find the edge's shelf, call `shelf.addStock(qty)`.
- `Environment::serviceQueueLane(const std::string& nodeId)` — find the checkout queue for `nodeId`, pop or reduce a waiting customer.

### Step 2.4 — Thread safety

`simulator.cpp` runs a single-threaded tick loop, so no new locks needed here.
`releaseTask` on `TaskManager` is already safe (called from within the tick).

---

## Phase 3 — POS Ingestion → Webapp Wiring (P1.3)

The extractor already works; the gap is that `simulation_runner.py` ignores the POS path.

### Step 3.1 — Call extract_params in simulation_runner.py

**File: `python/webapp/simulation_runner.py`** — near the top of `run_and_stream()`,
before calling `run_simulation()`:

```python
from python.ingestion.param_extractor import extract_params

ingestion_profile = None
if config.pos_data:  # pos_data holds the uploaded CSV path
    try:
        ingestion_profile = extract_params(config.pos_data)
        # override defaults only if not explicitly set by user
        if config.spawn_interval == DEFAULT_SPAWN_INTERVAL:
            config.spawn_interval = ingestion_profile["spawn_interval_mean"]
        if config.mission_probability == DEFAULT_MISSION_PROB:
            config.mission_probability = ingestion_profile["mission_probability"]
        # basket_size_multiplier: store in config if model supports it
    except Exception as e:
        # non-fatal — proceed with user-supplied values
        pass
```

### Step 3.2 — Add ingestion_profile to RunResult

**File: `python/webapp/models.py`**

```python
class RunResult(BaseModel):
    ...
    ingestion_profile: dict | None = None
```

Populate it in `simulation_runner.py` before the `yield "complete"` event.

### Step 3.3 — Add GET /api/runs/{run_id}/profile endpoint

**File: `python/webapp/app.py`**

```python
@app.get("/api/runs/{run_id}/profile")
async def get_ingestion_profile(run_id: str):
    run = session_store.get_run(run_id)
    if not run or not run.ingestion_profile:
        raise HTTPException(404)
    return run.ingestion_profile
```

---

## Phase 4 — Ingestion Summary Panel (P1.4)

### Step 4.1 — Create IngestionProfile.vue

**File: `python/webapp/frontend/src/components/IngestionProfile.vue`**

Minimal read-only panel:
```
Top SKUs        | SKU, Volume, % of sales
Inferred spawn rate | X customers/min
Mean basket size | X items
Mission probability | X%
```

Props: `profile: Object | null`
Shows a skeleton/spinner while loading, hides when `profile` is null.

### Step 4.2 — Wire into RunTab.vue

**File: `python/webapp/frontend/src/components/RunTab.vue`**

- After POS file upload succeeds (new `POST /api/upload-pos` or parse after run starts), fetch
  `/api/runs/{run_id}/profile` and store in local `ingestionProfile` ref.
- Render `<IngestionProfile :profile="ingestionProfile" />` below the file input.

> Simpler alternative: add a `POST /api/preview-pos` endpoint that runs extraction without
> creating a run, so the user sees the profile before submitting. This is the better UX but
> requires a temporary file store. Implement as a stretch goal.

---

## Phase 5 — SQLite Persistence (P2.5)

### Step 5.1 — Add aiosqlite dependency

```bash
pip install aiosqlite
```
Add to `requirements.txt`.

### Step 5.2 — Rewrite storage.py

Replace `SessionStore` with an async `Database` class:

```python
class Database:
    async def init(self, path="priceriot.db"):
        self.db = await aiosqlite.connect(path)
        await self.db.execute("""
            CREATE TABLE IF NOT EXISTS runs (
                run_id TEXT PRIMARY KEY,
                data   TEXT NOT NULL,
                created_at TEXT NOT NULL
            )
        """)
        await self.db.commit()

    async def create_run(self, result: RunResult) -> RunResult: ...
    async def get_run(self, run_id: str) -> RunResult | None: ...
    async def update_run(self, result: RunResult): ...
    async def list_runs(self) -> list[RunResult]: ...
    async def delete_run(self, run_id: str): ...
```

Store `RunResult` as `result.model_dump_json()`, load with `RunResult.model_validate_json(data)`.
Heavy CSV blobs (transactions_csv, customers_csv) should be stored as files in `data/runs/{run_id}/`
and replaced with a path string in the DB row.

### Step 5.3 — Add DELETE endpoint

**File: `python/webapp/app.py`**

```python
@app.delete("/api/runs/{run_id}", status_code=204)
async def delete_run(run_id: str):
    await db.delete_run(run_id)
```

### Step 5.4 — Update app lifespan

Use FastAPI's `lifespan` context manager to call `db.init()` on startup.

---

## Phase 6 — POST /api/runs/{id}/workers (P2.6)

Depends on Phase 1 being done so worker config actually affects navmesh travel.

### Step 6.1 — Add endpoint

**File: `python/webapp/app.py`**

```python
class WorkerConfigUpdate(BaseModel):
    num_stockers: int
    num_cashiers: int

@app.post("/api/runs/{run_id}/workers")
async def update_workers(run_id: str, body: WorkerConfigUpdate):
    run = await db.get_run(run_id)
    if not run or run.status != RunStatus.queued:
        raise HTTPException(400, "Run must be in queued state")
    run.config.num_stockers = body.num_stockers
    run.config.num_cashiers = body.num_cashiers
    await db.update_run(run)
    return {"ok": True}
```

### Step 6.2 — Wire WorkersPanel.vue

**File: `python/webapp/frontend/src/components/WorkersPanel.vue`**

The input fields for `numStockers`/`numCashiers` already exist but don't POST.
Add a "Apply" button that calls `POST /api/runs/{run_id}/workers` when run is queued.

---

## Deferred (P3 / P4)

These are lower priority and depend on Phase 1–2 being solid.

| Item | Pre-req | Notes |
|---|---|---|
| Demand-responsive pricing (P3.7) | Phase 2 | Needs sell-through tracking in C++ first |
| Multi-run sweeps (P3.8) | Phase 5 | Thread pool + DB for sweep results |
| Layout optimization hook (P3.9) | Phase 3.8 | Objective function + mutation loop |
| Churn prediction (P4.10) | Lots of data | scikit-learn; offline first |
| Demand forecasting (P4.11) | Phase 2 | Feed into restock threshold |
| Staffing optimizer (P4.12) | Phase 3.8 | Binary search wrapper over sweeps |

---

## Order of execution

```
Phase 1  →  Phase 2  →  Phase 3  →  Phase 4  →  Phase 5  →  Phase 6
(worker     (lifecycle   (POS        (POS         (SQLite     (workers
 movement)   closure)    wiring)     UI panel)    persist)    endpoint)
```

Phases 1+2 are C++ and tightly coupled; do them together.
Phases 3+4 are Python/frontend and independent of 1+2; can be done in parallel.
Phase 5 is a prerequisite for Phase 6.
