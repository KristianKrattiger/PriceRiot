from __future__ import annotations

import os
import sys
import tempfile
from typing import List

from fastapi import FastAPI, File, HTTPException, UploadFile, Form
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, StreamingResponse
import yaml

_script_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _script_dir not in sys.path:
    sys.path.insert(0, _script_dir)

from analytics import get_project_root

from .comparison import comparison_engine
from .models import ComparisonRequest, RunConfig, RunResult, RunStatus
from .simulation_runner import simulation_runner
from .storage import session_store


app = FastAPI(title="PriceRiot Webapp API")

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

    run_id = session_store.create_run(config)
    run = session_store.get_run(run_id)
    assert run is not None
    return run


@app.get("/api/runs", response_model=List[RunResult])
def list_runs() -> List[RunResult]:
    return session_store.list_runs()


@app.get("/api/runs/{run_id}", response_model=RunResult)
def get_run(run_id: str) -> RunResult:
    run = session_store.get_run(run_id)
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
def compare_runs(body: ComparisonRequest):
    try:
        return comparison_engine.compare_runs(body.run_ids)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


def _materialize_csv(run_id: str, kind: str) -> str:
    run = session_store.get_run(run_id)
    if run is None:
        raise HTTPException(status_code=404, detail="Run not found")
    if run.status != RunStatus.COMPLETED:
        raise HTTPException(status_code=400, detail="Run is not completed")

    if kind == "transactions":
        contents = run.transactions_csv
        filename = "transactions.csv"
    elif kind == "customers":
        contents = run.customers_csv
        filename = "customers.csv"
    else:
        raise HTTPException(status_code=400, detail="Unknown CSV kind")

    if not contents:
        raise HTTPException(status_code=404, detail="No CSV data available")

    tmp_dir = tempfile.mkdtemp(prefix="priceriot_csv_")
    path = os.path.join(tmp_dir, filename)
    with open(path, "w", encoding="utf-8") as f:
        f.write(contents)
    return path


@app.get("/api/runs/{run_id}/transactions.csv")
def download_transactions_csv(run_id: str):
    path = _materialize_csv(run_id, "transactions")
    return FileResponse(path, media_type="text/csv", filename="transactions.csv")


@app.get("/api/runs/{run_id}/customers.csv")
def download_customers_csv(run_id: str):
    path = _materialize_csv(run_id, "customers")
    return FileResponse(path, media_type="text/csv", filename="customers.csv")


@app.get("/api/workers")
def get_workers(run_id: str):
    """Return worker snapshots for a completed run."""
    run = session_store.get_run(run_id)
    if run is None:
        raise HTTPException(status_code=404, detail="Run not found")
    if run.status != RunStatus.COMPLETED:
        raise HTTPException(status_code=400, detail="Run is not completed")
    return run.workers or []


if __name__ == "__main__":
    import uvicorn

    uvicorn.run("python.webapp.app:app", host="127.0.0.1", port=8000, reload=True)

