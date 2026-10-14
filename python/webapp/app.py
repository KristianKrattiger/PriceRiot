from __future__ import annotations

import asyncio
import logging
import os
import sys
import tempfile
from contextlib import asynccontextmanager
from typing import List, Optional

_pr_log = logging.getLogger("priceriot")
if not _pr_log.handlers:
    _h = logging.StreamHandler()
    _h.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s", "%H:%M:%S"))
    _pr_log.addHandler(_h)
_pr_log.setLevel(logging.INFO)
_pr_log.propagate = False

from fastapi import BackgroundTasks, FastAPI, File, HTTPException, UploadFile, Form
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, Response, StreamingResponse
from pydantic import BaseModel
import yaml

_script_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _script_dir not in sys.path:
    sys.path.insert(0, _script_dir)

from analytics import get_project_root

from .comparison import comparison_engine
from .layout_serializer import (
    delete_layout_data_files as _delete_layout_data_files,
    layouts_dir as _layouts_dir_fn,
    parse_yaml as _parse_yaml,
    save_layout as _save_layout,
    validate as _validate_layout,
)
from .models import ComparisonRequest, RunConfig, RunResult, RunStatus
from .sim_models import SimJobConfig, SimJobResult, SimJobStatus
from .sim_runner import run_simulation_job
from .sim_storage import sim_job_store
from .simulation_runner import simulation_runner
from .storage import session_store


@asynccontextmanager
async def lifespan(app: FastAPI):
    await session_store.init()
    await sim_job_store.init()
    yield


app = FastAPI(title="PriceRiot Webapp API", lifespan=lifespan)

# Configure CORS once at app creation time
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5173", "http://127.0.0.1:5173"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


def _uploads_dir() -> str:
    root = get_project_root()
    path = os.path.join(root, "data", "tmp", "webapp_uploads")
    os.makedirs(path, exist_ok=True)
    return path


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}


@app.post("/api/upload")
async def upload_file(file: UploadFile = File(...)) -> dict:
    """Save an uploaded file to the webapp uploads directory and return its filename."""
    upload_dir = _uploads_dir()
    safe_name = file.filename or "upload"
    path = os.path.join(upload_dir, safe_name)
    with open(path, "wb") as f:
        f.write(await file.read())
    return {"filename": safe_name}


@app.post("/api/runs", response_model=RunResult)
async def create_run(
    duration_seconds: float = Form(...),
    spawn_interval: float = Form(5.0),
    mission_probability: float = Form(0.5),
    random_seed: int = Form(0),
    num_stockers: int = Form(2),
    num_cashiers: int = Form(1),
    auto_stock_tasks: bool = Form(True),
    auto_register_tasks: bool = Form(True),
    run_name: str | None = Form(default=None),
    store_file: UploadFile = File(...),
    product_file: UploadFile | None = File(default=None),
    pos_file: UploadFile | None = File(default=None),
) -> RunResult:
    upload_dir = _uploads_dir()
    # Save required store.yaml and validate YAML
    store_path = os.path.join(upload_dir, store_file.filename or "store.yaml")
    contents = await store_file.read()
    try:
        yaml.safe_load(contents)
    except Exception as exc:
        raise HTTPException(status_code=400, detail=f"Invalid store YAML: {exc}") from exc
    with open(store_path, "wb") as f:
        f.write(contents)

    product_path: str | None = None
    if product_file is not None:
        product_path = os.path.join(upload_dir, product_file.filename or "products.csv")
        with open(product_path, "wb") as f:
            f.write(await product_file.read())

    pos_path: str | None = None
    if pos_file is not None:
        pos_path = os.path.join(upload_dir, pos_file.filename or "pos.csv")
        with open(pos_path, "wb") as f:
            f.write(await pos_file.read())

    config = RunConfig(
        run_name=run_name or None,
        store_yaml=store_path,
        duration_seconds=duration_seconds,
        spawn_interval=spawn_interval,
        mission_probability=mission_probability,
        random_seed=random_seed,
        num_stockers=num_stockers,
        num_cashiers=num_cashiers,
        auto_stock_tasks=auto_stock_tasks,
        auto_register_tasks=auto_register_tasks,
        product_csv=product_path,
        pos_data=pos_path,
    )

    run_id = await session_store.create_run(config)
    run = await session_store.async_get_run(run_id)
    assert run is not None
    return run


@app.get("/api/runs", response_model=List[RunResult])
async def list_runs() -> List[RunResult]:
    return await session_store.async_list_runs()


@app.get("/api/runs/{run_id}", response_model=RunResult)
async def get_run(run_id: str) -> RunResult:
    run = await session_store.async_get_run(run_id)
    if run is None:
        raise HTTPException(status_code=404, detail="Run not found")
    return run


@app.get("/api/runs/{run_id}/stream")
async def stream_run(run_id: str):
    async def event_generator():
        async for chunk in simulation_runner.run_and_stream(run_id):
            yield chunk

    return StreamingResponse(
        event_generator(),
        media_type="text/event-stream",
    )


@app.post("/api/compare")
async def compare_runs(body: ComparisonRequest):
    try:
        return await comparison_engine.compare_runs(body.run_ids)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


async def _materialize_csv(run_id: str, kind: str) -> str:
    run = await session_store.async_get_run(run_id)
    if run is None:
        raise HTTPException(status_code=404, detail="Run not found")
    if run.status != RunStatus.COMPLETED:
        raise HTTPException(status_code=400, detail="Run is not completed")

    contents = session_store.get_csv(run_id, kind)
    filename = "transactions.csv" if kind == "transactions" else "customers.csv"

    if not contents:
        raise HTTPException(status_code=404, detail="No CSV data available")

    tmp_dir = tempfile.mkdtemp(prefix="priceriot_csv_")
    path = os.path.join(tmp_dir, filename)
    with open(path, "w", encoding="utf-8") as f:
        f.write(contents)
    return path


@app.get("/api/runs/{run_id}/transactions.csv")
async def download_transactions_csv(run_id: str):
    path = await _materialize_csv(run_id, "transactions")
    return FileResponse(path, media_type="text/csv", filename="transactions.csv")


@app.get("/api/runs/{run_id}/customers.csv")
async def download_customers_csv(run_id: str):
    path = await _materialize_csv(run_id, "customers")
    return FileResponse(path, media_type="text/csv", filename="customers.csv")


@app.get("/api/runs/{run_id}/profile")
async def get_ingestion_profile(run_id: str):
    """Return the POS-derived ingestion profile for a completed run."""
    run = await session_store.async_get_run(run_id)
    if run is None:
        raise HTTPException(status_code=404, detail="Run not found")
    if run.ingestion_profile is None:
        raise HTTPException(status_code=404, detail="No ingestion profile available for this run")
    return run.ingestion_profile


@app.delete("/api/runs")
async def delete_runs(incomplete_only: bool = False):
    """Bulk delete runs. incomplete_only=true removes only queued/running runs."""
    count = await session_store.async_delete_all_runs(incomplete_only=incomplete_only)
    return {"deleted": count}


@app.delete("/api/runs/{run_id}")
async def delete_run(run_id: str):
    deleted = await session_store.async_delete_run(run_id)
    if not deleted:
        raise HTTPException(status_code=404, detail="Run not found")
    return {"deleted": run_id}



# ---------------------------------------------------------------------------
# Simulation Control Plane — POS Datasets & Temporal Multi-Run Jobs
# ---------------------------------------------------------------------------

def _uploads_dir_path() -> str:
    return _uploads_dir()


def _raw_dir() -> str:
    return os.path.join(get_project_root(), "data", "raw")


def _examples_dir() -> str:
    return os.path.join(get_project_root(), "examples")


def _find_matching_yaml(csv_basename: str, search_dirs: list[str]) -> str | None:
    """Look for a YAML with the same stem as *csv_basename* in *search_dirs*."""
    stem = os.path.splitext(csv_basename)[0]
    # Layout bundle: ``mystore.pos.csv`` → pair with ``mystore.yaml``
    if stem.endswith(".pos"):
        stem = stem[: -len(".pos")]
    for d in search_dirs:
        for ext in (".yaml", ".yml"):
            p = os.path.join(d, stem + ext)
            if os.path.isfile(p):
                return os.path.basename(p)
    return None


def _parse_yaml_meta(yaml_path: str) -> dict:
    """Extract store name and days_of_operation from a YAML file."""
    try:
        with open(yaml_path, "r", encoding="utf-8") as fh:
            data = yaml.safe_load(fh)
        name = data.get("name") or data.get("store_name") or os.path.splitext(
            os.path.basename(yaml_path)
        )[0].replace("_", " ").title()
        dop  = data.get("days_of_operation") or ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
        hop  = data.get("hours_of_operation") or {}
        return {"name": name, "days_of_operation": dop, "hours_of_operation": hop}
    except Exception:
        return {"name": os.path.splitext(os.path.basename(yaml_path))[0], "days_of_operation": [], "hours_of_operation": {}}


def _parse_csv_meta(csv_path: str) -> dict:
    """Return record_count and date_from/date_to by scanning the CSV header."""
    import csv as _csv

    date_cols = {"date", "transaction_date", "datetime", "trans_date", "sale_date"}
    date_from = date_to = None
    record_count = 0

    try:
        with open(csv_path, "r", encoding="utf-8", errors="replace") as fh:
            reader = _csv.DictReader(fh)
            if reader.fieldnames is None:
                return {"record_count": 0, "date_from": None, "date_to": None}
            col = next(
                (c for c in reader.fieldnames if c.strip().lower() in date_cols), None
            )
            for row in reader:
                record_count += 1
                if col and row.get(col):
                    val = str(row[col])[:10]
                    if date_from is None or val < date_from:
                        date_from = val
                    if date_to is None or val > date_to:
                        date_to = val
    except Exception:
        pass

    return {
        "record_count": record_count,
        "date_from": date_from,
        "date_to": date_to,
    }


@app.get("/api/pos-datasets")
async def list_pos_datasets() -> list[dict]:
    """Enumerate available POS CSV files with metadata for the dataset picker."""
    uploads = _uploads_dir_path()
    raw     = _raw_dir()
    layouts_d = _layouts_dir_fn(get_project_root())
    all_dirs = [uploads, raw]

    # Locate YAMLs in uploads, raw, examples, and layouts for name lookup.
    yaml_search_dirs = [uploads, raw, _examples_dir(), get_project_root(), layouts_d]

    datasets = []
    seen = set()

    for d in (*all_dirs, layouts_d):
        if not os.path.isdir(d):
            continue
        for fname in sorted(os.listdir(d)):
            if not fname.lower().endswith(".csv"):
                continue
            if d == layouts_d and not fname.lower().endswith(".pos.csv"):
                continue
            if fname in seen:
                continue
            seen.add(fname)

            csv_path   = os.path.join(d, fname)
            csv_meta   = _parse_csv_meta(csv_path)
            # Only surface files that look like POS data (have a date column).
            if csv_meta["date_from"] is None and csv_meta["record_count"] == 0:
                continue

            yaml_name  = _find_matching_yaml(fname, yaml_search_dirs)
            store_meta: dict = {}
            if yaml_name:
                for yd in yaml_search_dirs:
                    yp = os.path.join(yd, yaml_name)
                    if os.path.isfile(yp):
                        store_meta = _parse_yaml_meta(yp)
                        break

            datasets.append({
                "filename":          fname,
                "store_yaml":        yaml_name,
                "store_name":        store_meta.get("name") or fname,
                "date_from":         csv_meta["date_from"],
                "date_to":           csv_meta["date_to"],
                "record_count":      csv_meta["record_count"],
                "days_of_operation": store_meta.get("days_of_operation", []),
                "hours_of_operation": store_meta.get("hours_of_operation", {}),
            })

    return datasets


@app.get("/api/pos-datasets/{filename}/params")
async def get_pos_params(filename: str) -> dict:
    """Extract simulation parameters (spawn interval, avg basket value) from a POS CSV."""
    import sys as _sys
    root = get_project_root()
    layouts_d = _layouts_dir_fn(root)
    for d in (os.path.join(root, "data", "tmp", "webapp_uploads"),
              os.path.join(root, "data", "raw"),
              layouts_d):
        p = os.path.join(d, filename)
        if os.path.isfile(p):
            _sys.path.insert(0, os.path.join(root, "python"))
            try:
                from ingestion.param_extractor import extract_params
                return extract_params(p)
            except Exception as exc:
                raise HTTPException(status_code=500, detail=str(exc)) from exc
    raise HTTPException(status_code=404, detail=f"Dataset not found: {filename}")


def _sim_status_diagnostic_hint(job: SimJobResult) -> Optional[str]:
    """Short hint for telling apart queue issues vs long runs vs stuck sim."""
    if job.status == SimJobStatus.QUEUED:
        return (
            "Still queued: if this persists, BackgroundTasks may not be running "
            "(check uvicorn / single process)."
        )
    if job.status == SimJobStatus.RUNNING and job.workload and "error" not in job.workload:
        sev = job.workload.get("severity")
        if sev in ("large", "very_large"):
            return (
                "Large temporal workload — long runtime is expected; narrow the date range if unintended."
            )
        if job.progress.get("completed_runs", 0) == 0:
            return (
                "First statistical run in progress. Per-run timeout is 300s; "
                "if CPU is idle, the sim may be blocked."
            )
    return None


@app.post("/api/simulations", status_code=202)
async def create_simulation(
    body: SimJobConfig,
    background_tasks: BackgroundTasks,
) -> SimJobResult:
    """Enqueue a temporal multi-run simulation job and return its ID immediately."""
    print(f"[SIM] POST /api/simulations received, store_yaml={body.store_yaml!r}", flush=True)
    if not body.store_yaml:
        raise HTTPException(status_code=400, detail="store_yaml is required")

    sim_id = await sim_job_store.create_job(body)
    background_tasks.add_task(run_simulation_job, sim_id)

    job = await sim_job_store.get_job(sim_id)
    print(f"[SIM] POST /api/simulations returning 202 sim_id={job.sim_id} status={job.status}", flush=True)
    return job


@app.get("/api/simulations")
async def list_simulations() -> list[SimJobResult]:
    """Return all simulation jobs in reverse-chronological order."""
    return await sim_job_store.list_jobs()


@app.delete("/api/simulations", status_code=200)
async def delete_all_simulations():
    """Delete all simulation jobs."""
    count = await sim_job_store.delete_all_jobs()
    return {"deleted": count}


@app.delete("/api/simulations/{sim_id}", status_code=204)
async def delete_simulation(sim_id: str):
    """Delete a simulation job by ID."""
    deleted = await sim_job_store.delete_job(sim_id)
    if not deleted:
        raise HTTPException(status_code=404, detail="Simulation not found")


@app.get("/api/simulations/{sim_id}/status")
async def get_simulation_status(sim_id: str) -> dict:
    """Lightweight status poll — returns status, progress, and elapsed time."""
    job = await sim_job_store.get_job(sim_id)
    if job is None:
        raise HTTPException(status_code=404, detail="Simulation not found")
    return {
        "sim_id":        job.sim_id,
        "status":        job.status,
        "progress":      job.progress,
        "elapsed_seconds": job.elapsed_seconds,
        "error_message": job.error_message,
        "workload":      job.workload,
        "diagnostic_hint": _sim_status_diagnostic_hint(job),
    }


@app.get("/api/simulations/{sim_id}/results")
async def get_simulation_results(sim_id: str) -> SimJobResult:
    """Full result payload including aggregate summary and per-run list."""
    job = await sim_job_store.get_job(sim_id)
    if job is None:
        raise HTTPException(status_code=404, detail="Simulation not found")
    return job


@app.get("/api/simulations/{sim_id}/runs/{run_index}")
async def get_simulation_run_detail(sim_id: str, run_index: int) -> dict:
    """Drill-down for a single run within a simulation job."""
    job = await sim_job_store.get_job(sim_id)
    if job is None:
        raise HTTPException(status_code=404, detail="Simulation not found")

    run = next((r for r in job.per_runs if r.run_index == run_index), None)
    if run is None:
        raise HTTPException(status_code=404, detail=f"Run {run_index} not found")

    result = run.model_dump()

    # Attach transactions CSV if it exists on disk.
    tx_path = os.path.join(run.output_path, "transactions.csv")
    if os.path.isfile(tx_path):
        result["transactions_csv_url"] = (
            f"/api/simulations/{sim_id}/runs/{run_index}/transactions.csv"
        )

    return result


@app.get("/api/simulations/{sim_id}/runs/{run_index}/transactions.csv")
async def download_sim_run_transactions(sim_id: str, run_index: int):
    job = await sim_job_store.get_job(sim_id)
    if job is None:
        raise HTTPException(status_code=404, detail="Simulation not found")
    run = next((r for r in job.per_runs if r.run_index == run_index), None)
    if run is None:
        raise HTTPException(status_code=404, detail=f"Run {run_index} not found")
    tx_path = os.path.join(run.output_path, "transactions.csv")
    if not os.path.isfile(tx_path):
        raise HTTPException(status_code=404, detail="Transactions CSV not available")
    return FileResponse(tx_path, media_type="text/csv",
                        filename=f"sim_{sim_id}_run_{run_index}_transactions.csv")


@app.get("/api/system/info")
async def system_info() -> dict:
    """Expose server-side CPU count for the thread-limit default."""
    return {"cpu_count": os.cpu_count() or 4}


# ---------------------------------------------------------------------------
# Layout Test — quick single-run simulation launched from the layout editor
# ---------------------------------------------------------------------------

class LayoutTestRequest(BaseModel):
    store_yaml:          str
    duration_seconds:    float = 600.0
    spawn_interval:      float = 5.0
    mission_probability: float = 0.5
    seed:                int   = 42


_layout_test_log = __import__("logging").getLogger("priceriot.layout_test")


async def _run_layout_test(sim_id: str, store_yaml_abs: str, req: LayoutTestRequest) -> None:
    """Background task: runs a single analytics.run_simulation() call."""
    import time
    import traceback
    from datetime import datetime, timezone

    from .sim_models import PerRunSummary, SimJobStatus

    try:
        await sim_job_store.update_job(
            sim_id,
            status=SimJobStatus.RUNNING,
            started_at=datetime.now(timezone.utc).isoformat(),
        )

        root = get_project_root()

        def _run_sync():
            old_cwd = os.getcwd()
            try:
                os.chdir(root)  # C++ simulator resolves products_file relative to CWD
                from python.analytics.core import run_simulation
                return run_simulation(
                    store_path=store_yaml_abs,
                    duration_seconds=req.duration_seconds,
                    spawn_interval=req.spawn_interval,
                    mission_probability=req.mission_probability,
                    seed=req.seed,
                )
            finally:
                os.chdir(old_cwd)

        t0 = time.monotonic()
        try:
            result = await asyncio.wait_for(
                asyncio.get_running_loop().run_in_executor(None, _run_sync),
                timeout=180.0,
            )
        except asyncio.TimeoutError:
            raise RuntimeError(
                f"Layout test timed out after 180 s — the simulation may be stuck. "
                f"Check store YAML for degenerate navmesh or missing exit nodes."
            )
        elapsed = time.monotonic() - t0

        tx_count   = len(result.transactions)
        cust_count = len(result.customers)
        # item_total is the per-line-item column; sum across all rows = total revenue
        if tx_count and "item_total" in result.transactions.columns:
            revenue = float(result.transactions["item_total"].sum())
        else:
            revenue = 0.0

        summary = PerRunSummary(
            run_index=1,
            transaction_count=tx_count,
            customer_count=cust_count,
            total_revenue=round(revenue, 2),
            peak_period="n/a",
            output_path="",
            duration_seconds=round(elapsed, 2),
            status="complete",
        )

        await sim_job_store.update_job(
            sim_id,
            status=SimJobStatus.COMPLETE,
            completed_at=datetime.now(timezone.utc).isoformat(),
            elapsed_seconds=round(elapsed, 2),
            progress={"completed_runs": 1, "total_runs": 1},
            per_runs=[summary],
            aggregate={
                "mean_transactions": tx_count,
                "mean_revenue":      round(revenue, 2),
                "mean_customers":    cust_count,
                "std_transactions":  0.0,
                "std_revenue":       0.0,
                "completed_runs":    1,
            },
        )

    except Exception as exc:
        msg = traceback.format_exc()
        _layout_test_log.error("layout_test %s failed:\n%s", sim_id, msg)
        await sim_job_store.update_job(
            sim_id,
            status=SimJobStatus.FAILED,
            error_message=str(exc) or repr(exc),
        )


@app.post("/api/layout-test", status_code=202)
async def layout_test(body: LayoutTestRequest, background_tasks: BackgroundTasks) -> dict:
    """Run a quick single-run layout test without POS data."""
    # Resolve YAML path
    yaml_abs: str | None = None
    for d in _yaml_search_dirs():
        p = os.path.join(d, body.store_yaml)
        if os.path.isfile(p):
            yaml_abs = p
            break
    if yaml_abs is None:
        raise HTTPException(status_code=404, detail=f"Layout not found: {body.store_yaml}")

    from .sim_models import DateConfig, SimJobConfig, SimJobStatus
    from datetime import date

    dummy_config = SimJobConfig(
        pos_dataset="",
        store_yaml=body.store_yaml,
        preset="layout_test",
        date_config=DateConfig(date=date.today().isoformat()),
        runs=1,
        max_threads=1,
        label=f"Layout test: {body.store_yaml}",
        seed=body.seed,
        mission_probability=body.mission_probability,
        spawn_interval=body.spawn_interval,
    )
    sim_id = await sim_job_store.create_job(dummy_config)
    background_tasks.add_task(_run_layout_test, sim_id, yaml_abs, body)
    return {"sim_id": sim_id}


# ---------------------------------------------------------------------------
# Layout Editor — CRUD for store YAML files
# ---------------------------------------------------------------------------

def _get_layouts_dir() -> str:
    return _layouts_dir_fn(get_project_root())


def _yaml_search_dirs() -> list[str]:
    root = get_project_root()
    return [
        _get_layouts_dir(),
        os.path.join(root, "examples"),
        os.path.join(root, "data", "tmp", "webapp_uploads"),
        root,
    ]


@app.get("/api/layouts")
async def list_layouts() -> list[dict]:
    """List all available store layout YAML files (user-saved + examples)."""
    results = []
    seen: set[str] = set()
    for d in _yaml_search_dirs():
        if not os.path.isdir(d):
            continue
        for fname in sorted(os.listdir(d)):
            if not fname.lower().endswith((".yaml", ".yml")):
                continue
            if fname in seen:
                continue
            seen.add(fname)
            full = os.path.join(d, fname)
            try:
                with open(full, "r", encoding="utf-8") as f:
                    doc = yaml.safe_load(f)
                # Only surface files that look like store layouts (have nodes key)
                if not isinstance(doc, dict) or "nodes" not in doc:
                    continue
                stat = os.stat(full)
                results.append({
                    "filename":   fname,
                    "directory":  d,
                    "updated_at": stat.st_mtime,
                    "units":      doc.get("units", "legacy"),
                    "node_count": len(doc.get("nodes", [])),
                    "edge_count": len(doc.get("edges", [])),
                })
            except Exception:
                continue
    return results


@app.get("/api/layouts/{filename}")
async def get_layout(filename: str) -> dict:
    """Parse a store YAML and return frontend-ready layout JSON."""
    for d in _yaml_search_dirs():
        p = os.path.join(d, filename)
        if os.path.isfile(p):
            try:
                layout = _parse_yaml(p)
                layout["_sourceFile"] = filename
                return layout
            except Exception as exc:
                raise HTTPException(status_code=400, detail=f"Failed to parse layout: {exc}") from exc
    raise HTTPException(status_code=404, detail=f"Layout not found: {filename}")


class SaveLayoutBody(BaseModel):
    name: str
    overwrite: bool = False
    layout: dict


@app.post("/api/layouts/save")
async def save_layout_endpoint(body: SaveLayoutBody) -> dict:
    """Persist a layout JSON as a versioned store YAML file."""
    errors = _validate_layout(body.layout)
    if errors:
        raise HTTPException(status_code=422, detail={"validation_errors": errors})

    ld = _get_layouts_dir()
    try:
        filename, stem = _save_layout(body.name, body.overwrite, body.layout, ld)
    except Exception as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc

    return {"filename": filename, "stem": stem, "directory": ld}


@app.delete("/api/layouts/{filename}")
async def delete_layout(filename: str) -> Response:
    """Delete a layout YAML and its .meta.json sidecar."""
    deleted = False
    for d in [_get_layouts_dir()]:   # only allow deleting from writable layouts dir
        p = os.path.join(d, filename)
        if os.path.isfile(p):
            os.remove(p)
            sidecar = p.replace(".yaml", ".meta.json").replace(".yml", ".meta.json")
            if os.path.isfile(sidecar):
                os.remove(sidecar)
            _delete_layout_data_files(d, filename)
            deleted = True
            break
    if not deleted:
        raise HTTPException(status_code=404, detail=f"Layout not found: {filename}")
    return Response(status_code=204)


@app.get("/api/products")
async def get_products(csv: str) -> list[dict]:
    """Return product list parsed from an uploaded product CSV."""
    import csv as _csv
    root = get_project_root()
    uploads = os.path.join(root, "data", "tmp", "webapp_uploads")
    raw_dir = os.path.join(root, "data", "raw")
    layouts_dir = _layouts_dir_fn(root)

    def _resolve_csv_path() -> str | None:
        if not csv:
            return None
        if os.path.isabs(csv) and os.path.isfile(csv):
            return csv
        # Project-relative paths (e.g. data/raw/products.csv from store YAML)
        rel = os.path.normpath(os.path.join(root, csv))
        if os.path.isfile(rel):
            return rel
        for d in (uploads, raw_dir, layouts_dir):
            for name in (csv, os.path.basename(csv)):
                p = os.path.join(d, name)
                if os.path.isfile(p):
                    return p
        return None

    csv_path = _resolve_csv_path()
    if not csv_path:
        raise HTTPException(status_code=404, detail=f"Product CSV not found: {csv}")

    products = []
    with open(csv_path, "r", encoding="utf-8", errors="replace") as f:
        reader = _csv.DictReader(f)
        # Build a lowercase→original key map for case-insensitive lookup
        def _get(row, *keys):
            lmap = {k.lower().strip(): v for k, v in row.items()}
            for k in keys:
                v = lmap.get(k.lower())
                if v and str(v).strip():
                    return str(v).strip()
            return None
        for row in reader:
            sku_id = _get(row, "sku_id", "sku", "id", "product_id",
                          "item_id", "barcode", "upc", "SKU", "ProductID")
            name   = _get(row, "name", "product_name", "description",
                          "item_name", "title") or ""
            price  = _get(row, "price", "unit_price", "retail_price",
                          "cost", "sell_price") or "0"
            cat    = _get(row, "category", "dept", "department",
                          "category_name", "section") or ""
            if not sku_id:
                continue
            # Prefer integer SKU ID; fall back to string
            try:
                sku_val = int(sku_id)
            except (ValueError, TypeError):
                sku_val = sku_id
            try:
                price_val = float(str(price).replace("$", "").replace(",", "").strip() or 0)
            except (ValueError, TypeError):
                price_val = 0.0
            products.append({
                "skuId":    sku_val,
                "name":     name.strip(),
                "price":    price_val,
                "category": cat.strip(),
            })
    return products


from fastapi.staticfiles import StaticFiles as _StaticFiles

_dist = os.path.join(os.path.dirname(__file__), "frontend", "dist")
if os.path.isdir(_dist):
    app.mount("/", _StaticFiles(directory=_dist, html=True), name="spa")


if __name__ == "__main__":
    import uvicorn

    uvicorn.run("python.webapp.app:app", host="127.0.0.1", port=8000, reload=True)

