import sys
import os

# Project root (parent of python/) and build dir for simulation.pyd
_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_build = os.path.join(_root, "build")
if os.path.isdir(_build):
    sys.path.insert(0, _build)
_release = os.path.join(_root, "build", "Release")
if os.path.isdir(_release):
    sys.path.insert(0, _release)

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field

try:
    import simulation  # PriceRiot C++ extension from build/
except ImportError as e:
    if "PyInit_simulation" in str(e):
        raise RuntimeError(
            "The simulation module was built for a different Python version. "
            "Run uvicorn with the same Python used for the build (e.g. Python 3.11). "
            "Example: py -3.11 -m uvicorn python.main:app --host 127.0.0.1 --port 8000 --app-dir ."
        ) from e
    raise

app = FastAPI()

# Max sim duration (seconds) per request to avoid long-blocking runs
MAX_DURATION_SECONDS = 86400  # 24 hours


class SimRunRequest(BaseModel):
    store_path: str = Field(default="store.yaml", description="Path to store.yaml (relative to project root or absolute)")
    duration_seconds: float = Field(..., gt=0, le=MAX_DURATION_SECONDS, description="Sim-time to run")
    spawn_interval: float = Field(default=5.0, gt=0, description="Seconds between customer spawns")
    mission_probability: float = Field(default=0.5, ge=0, le=1, description="Fraction of mission-behavior customers")
    seed: int = Field(default=0, ge=0, description="RNG seed; 0 = non-deterministic")
    export_path: str | None = Field(default=None, description="If set, export transactions CSV to this path (relative to project root or absolute)")


class SimRunResponse(BaseModel):
    transaction_count: int
    customer_count: int
    elapsed_time: float
    export_path: str | None = None


@app.get("/")
def read_root():
    return {"message": "Welcome to PriceRiot API"}


@app.post("/sim/run", response_model=SimRunResponse)
def sim_run(body: SimRunRequest):
    """Run the simulation with the given parameters and return summary (and optionally export CSV)."""
    store_abs = os.path.normpath(os.path.join(_root, body.store_path) if not os.path.isabs(body.store_path) else body.store_path)
    if not os.path.isfile(store_abs):
        raise HTTPException(status_code=400, detail=f"Store file not found: {store_abs}")
    sim = simulation.Simulator(store_abs, body.spawn_interval, body.mission_probability, body.seed)
    sim.run(body.duration_seconds)
    tx_count = sim.transaction_count()
    cust_count = len(sim.get_customers())
    elapsed = sim.elapsed_time
    export_out = None
    if body.export_path:
        export_abs = os.path.normpath(os.path.join(_root, body.export_path) if not os.path.isabs(body.export_path) else body.export_path)
        sim.export_transactions(export_abs)
        export_out = export_abs
    return SimRunResponse(
        transaction_count=tx_count,
        customer_count=cust_count,
        elapsed_time=elapsed,
        export_path=export_out,
    )

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host="127.0.0.1", port=8000, reload=True)
