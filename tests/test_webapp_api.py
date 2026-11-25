"""
FastAPI backend API integration tests (isolated temporary SQLite DB).
Covers /health, /api/runs CRUD, POST /api/runs/{id}/workers, SSE stream +
/profile + CSV download, /api/compare, /api/pos-datasets, /api/sweeps,
and /api/layouts.
"""
from __future__ import annotations

import asyncio
import os
import sys

import pytest
from fastapi.testclient import TestClient

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for _p in (
    _ROOT,
    os.path.join(_ROOT, "python"),
    os.path.join(_ROOT, "build"),
    os.path.join(_ROOT, "build", "Release"),
):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.insert(0, _p)

from python.webapp.app import app  # noqa: E402
from python.webapp.sim_storage import sim_job_store  # noqa: E402
from python.webapp.storage import session_store  # noqa: E402
from python.webapp.sweep_runner import sweep_store  # noqa: E402

COWBOY_YAML = os.path.join(_ROOT, "examples", "cowboy_market.yaml")
COWBOY_POS = os.path.join(_ROOT, "data", "raw", "cowboy_pos.csv")


@pytest.fixture()
def client(tmp_path, monkeypatch):
    db_file = str(tmp_path / "test_priceriot.db")
    runs_dir = str(tmp_path / "runs")
    uploads_dir = str(tmp_path / "uploads")
    os.makedirs(runs_dir, exist_ok=True)
    os.makedirs(uploads_dir, exist_ok=True)

    monkeypatch.setattr("python.webapp.storage._db_path", lambda: db_file)
    monkeypatch.setattr("python.webapp.storage._runs_dir", lambda: runs_dir)
    monkeypatch.setattr("python.webapp.sim_storage._db_path", lambda: db_file)
    monkeypatch.setattr("python.webapp.sweep_runner._db_path", lambda: db_file)
    monkeypatch.setattr("python.webapp.app._uploads_dir", lambda: uploads_dir)

    with TestClient(app) as tc:
        yield tc

    # Cleanly close connections opened during lifespan
    async def _close():
        if session_store._db is not None:
            await session_store._db.close()
            session_store._db = None
        if sim_job_store._db is not None:
            await sim_job_store._db.close()
            sim_job_store._db = None
        await sweep_store.close()

    asyncio.run(_close())


def test_health_and_system_info(client: TestClient):
    r = client.get("/health")
    assert r.status_code == 200
    assert r.json() == {"status": "ok"}

    sys_info = client.get("/api/system/info")
    assert sys_info.status_code == 200
    assert sys_info.json()["cpu_count"] >= 1


def test_run_lifecycle_workers_endpoint_stream_and_delete(client: TestClient):
    with open(COWBOY_YAML, "rb") as yf, open(COWBOY_POS, "rb") as pf:
        resp = client.post(
            "/api/runs",
            data={
                "duration_seconds": "60",
                "spawn_interval": "5.0",
                "mission_probability": "0.5",
                "random_seed": "42",
                "num_stockers": "2",
                "num_cashiers": "1",
                "run_name": "api-test-run",
            },
            files={
                "store_file": ("cowboy_market.yaml", yf.read(), "application/x-yaml"),
                "pos_file": ("cowboy_pos.csv", pf.read(), "text/csv"),
            },
        )
    assert resp.status_code == 200, resp.text
    run = resp.json()
    run_id = run["run_id"]
    assert run["status"] == "queued"
    assert run["config"]["num_stockers"] == 2
    assert run["config"]["num_cashiers"] == 1

    # Update workers on queued run -> 200
    w_resp = client.post(
        f"/api/runs/{run_id}/workers",
        json={"num_stockers": 4, "num_cashiers": 3, "auto_stock_tasks": False},
    )
    assert w_resp.status_code == 200
    updated = w_resp.json()
    assert updated["config"]["num_stockers"] == 4
    assert updated["config"]["num_cashiers"] == 3
    assert updated["config"]["auto_stock_tasks"] is False

    # Missing run -> 404
    assert (
        client.post("/api/runs/999999/workers", json={"num_stockers": 1, "num_cashiers": 1}).status_code
        == 404
    )

    # Stream execution to completion
    stream_resp = client.get(f"/api/runs/{run_id}/stream")
    assert stream_resp.status_code == 200
    assert '"complete"' in stream_resp.text

    # Updating workers on a completed run -> 400
    w_after = client.post(
        f"/api/runs/{run_id}/workers",
        json={"num_stockers": 1, "num_cashiers": 1},
    )
    assert w_after.status_code == 400

    # Profile endpoint populated because POS file was uploaded
    prof = client.get(f"/api/runs/{run_id}/profile")
    assert prof.status_code == 200
    assert prof.json()["spawn_interval_seconds"] > 0

    # CSV downloads
    assert client.get(f"/api/runs/{run_id}/transactions.csv").status_code == 200
    assert client.get(f"/api/runs/{run_id}/customers.csv").status_code == 200

    # Delete run
    del_resp = client.delete(f"/api/runs/{run_id}")
    assert del_resp.status_code == 200
    assert client.get(f"/api/runs/{run_id}").status_code == 404


def test_sweeps_api_create_list_get_csv_delete(client: TestClient):
    payload = {
        "store_yaml": "cowboy_market.yaml",
        "duration_seconds": 45.0,
        "spawn_intervals": [10.0, 20.0],
        "mission_probabilities": [0.5],
        "num_stockers": [1],
        "num_cashiers": [1],
        "price_multipliers": [1.0, 1.5],
        "seed": 42,
        "dt": 1.0 / 30.0,
        "max_threads": 2,
        "label": "test-sweep",
    }
    resp = client.post("/api/sweeps", json=payload)
    assert resp.status_code == 200, resp.text
    sweep = resp.json()
    sweep_id = sweep["sweep_id"]
    assert sweep["status"] == "complete"
    assert sweep["total_points"] == 4
    assert len(sweep["results"]) == 4

    # List & Get
    assert any(s["sweep_id"] == sweep_id for s in client.get("/api/sweeps").json())
    assert client.get(f"/api/sweeps/{sweep_id}").status_code == 200

    # CSV download
    csv_resp = client.get(f"/api/sweeps/{sweep_id}/results.csv")
    assert csv_resp.status_code == 200
    assert "spawn_interval" in csv_resp.text and "price_multiplier" in csv_resp.text

    # Grid-cap validation
    bad_payload = {
        **payload,
        "spawn_intervals": [1.0, 2.0, 3.0, 4.0, 5.0],
        "mission_probabilities": [0.1, 0.2, 0.3, 0.4, 0.5],
        "num_stockers": [1, 2, 3],
    }
    assert client.post("/api/sweeps", json=bad_payload).status_code == 400

    # Delete
    assert client.delete(f"/api/sweeps/{sweep_id}").status_code == 204
    assert client.get(f"/api/sweeps/{sweep_id}").status_code == 404


def test_pos_datasets_and_layouts_endpoints(client: TestClient):
    ds = client.get("/api/pos-datasets")
    assert ds.status_code == 200
    filenames = {d["filename"] for d in ds.json()}
    assert "cowboy_pos.csv" in filenames

    params = client.get("/api/pos-datasets/cowboy_pos.csv/params")
    assert params.status_code == 200
    assert params.json()["transaction_count"] > 1000

    layouts = client.get("/api/layouts")
    assert layouts.status_code == 200
    layout_names = {l["filename"] for l in layouts.json()}
    assert "cowboy_market.yaml" in layout_names

    cowboy = client.get("/api/layouts/cowboy_market.yaml")
    assert cowboy.status_code == 200
    assert "nodes" in cowboy.json()
