"""
sweep_runner.py
===============
SQLite-backed store and async executor for POST /api/sweeps parameter sweeps.
"""
from __future__ import annotations

import asyncio
import os
import time
from datetime import datetime, timezone
from typing import List, Optional

import aiosqlite

from .sweep_models import SweepConfig, SweepPointResult, SweepResult, SweepStatus


_CREATE_SWEEPS_SQL = """
CREATE TABLE IF NOT EXISTS sweeps (
    sweep_id   TEXT PRIMARY KEY,
    data       TEXT NOT NULL,
    created_at TEXT NOT NULL
);
"""

_CREATE_SWEEP_COUNTER_SQL = """
CREATE TABLE IF NOT EXISTS sweep_counters (
    name  TEXT PRIMARY KEY,
    value INTEGER NOT NULL DEFAULT 0
);
"""


def _db_path() -> str:
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    data_dir = os.path.join(root, "data")
    os.makedirs(data_dir, exist_ok=True)
    return os.path.join(data_dir, "priceriot.db")


class SweepStore:
    """Async SQLite persistence for parameter sweeps."""

    def __init__(self) -> None:
        self._db: Optional[aiosqlite.Connection] = None
        self._lock = asyncio.Lock()

    async def init(self, path: Optional[str] = None) -> None:
        path = path or _db_path()
        self._db = await aiosqlite.connect(path)
        self._db.row_factory = aiosqlite.Row
        await self._db.execute(_CREATE_SWEEPS_SQL)
        await self._db.execute(_CREATE_SWEEP_COUNTER_SQL)
        await self._db.execute(
            "INSERT OR IGNORE INTO sweep_counters (name, value) VALUES ('sweep_id', 0)"
        )
        await self._db.commit()

    async def close(self) -> None:
        if self._db is not None:
            await self._db.close()
            self._db = None

    async def create_sweep(self, config: SweepConfig) -> str:
        async with self._lock:
            await self._db.execute(
                "UPDATE sweep_counters SET value = value + 1 WHERE name = 'sweep_id'"
            )
            async with self._db.execute(
                "SELECT value FROM sweep_counters WHERE name = 'sweep_id'"
            ) as cur:
                row = await cur.fetchone()
            sweep_id = str(row["value"])

        created_at = datetime.now(timezone.utc).isoformat()
        sweep = SweepResult(
            sweep_id=sweep_id,
            config=config,
            status=SweepStatus.QUEUED,
            created_at=created_at,
            total_points=config.grid_size(),
        )
        async with self._lock:
            await self._db.execute(
                "INSERT INTO sweeps (sweep_id, data, created_at) VALUES (?, ?, ?)",
                (sweep_id, sweep.model_dump_json(), created_at),
            )
            await self._db.commit()
        return sweep_id

    async def get_sweep(self, sweep_id: str) -> Optional[SweepResult]:
        async with self._db.execute(
            "SELECT data FROM sweeps WHERE sweep_id = ?", (sweep_id,)
        ) as cur:
            row = await cur.fetchone()
        if row is None:
            return None
        return SweepResult.model_validate_json(row["data"])

    async def update_sweep(self, sweep_id: str, **kwargs) -> Optional[SweepResult]:
        async with self._lock:
            existing = await self.get_sweep(sweep_id)
            if existing is None:
                return None
            updated = existing.model_copy(update=kwargs)
            await self._db.execute(
                "UPDATE sweeps SET data = ? WHERE sweep_id = ?",
                (updated.model_dump_json(), sweep_id),
            )
            await self._db.commit()
        return updated

    async def list_sweeps(self) -> List[SweepResult]:
        async with self._db.execute(
            "SELECT data FROM sweeps ORDER BY CAST(sweep_id AS INTEGER) DESC"
        ) as cur:
            rows = await cur.fetchall()
        return [SweepResult.model_validate_json(r["data"]) for r in rows]

    async def delete_sweep(self, sweep_id: str) -> bool:
        async with self._lock:
            async with self._db.execute(
                "DELETE FROM sweeps WHERE sweep_id = ?", (sweep_id,)
            ) as cur:
                deleted = cur.rowcount > 0
            await self._db.commit()
        return deleted


sweep_store = SweepStore()


def _resolve_store_yaml(store_yaml: str) -> Optional[str]:
    from analytics import get_project_root

    if os.path.isabs(store_yaml) and os.path.isfile(store_yaml):
        return store_yaml
    root = get_project_root()
    candidates = [
        os.path.join(root, store_yaml),
        os.path.join(root, "examples", store_yaml),
        os.path.join(root, "data", "tmp", "webapp_uploads", store_yaml),
        os.path.join(root, "data", "layouts", store_yaml),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


async def execute_sweep(sweep_id: str) -> Optional[SweepResult]:
    """Run all grid points for *sweep_id* and persist comparison outputs."""
    from analytics.sweeps import run_parameter_sweep

    sweep = await sweep_store.get_sweep(sweep_id)
    if sweep is None:
        return None

    cfg = sweep.config
    store_abs = _resolve_store_yaml(cfg.store_yaml)
    if store_abs is None:
        return await sweep_store.update_sweep(
            sweep_id,
            status=SweepStatus.FAILED,
            error_message=f"Store YAML not found: {cfg.store_yaml}",
        )

    await sweep_store.update_sweep(sweep_id, status=SweepStatus.RUNNING)
    t0 = time.monotonic()

    try:
        rows, df = await asyncio.to_thread(
            run_parameter_sweep,
            store_path=store_abs,
            duration_seconds=cfg.duration_seconds,
            spawn_intervals=cfg.spawn_intervals,
            mission_probabilities=cfg.mission_probabilities,
            num_stockers_list=cfg.num_stockers,
            num_cashiers_list=cfg.num_cashiers,
            price_multipliers=cfg.price_multipliers,
            seed=cfg.seed,
            dt=cfg.dt,
            max_threads=cfg.max_threads,
            auto_stock_tasks=cfg.auto_stock_tasks,
            auto_register_tasks=cfg.auto_register_tasks,
        )
        elapsed = round(time.monotonic() - t0, 3)
        points = [SweepPointResult(**r) for r in rows]
        csv_str = df.to_csv(index=False)
        return await sweep_store.update_sweep(
            sweep_id,
            status=SweepStatus.COMPLETE,
            completed_at=datetime.now(timezone.utc).isoformat(),
            elapsed_seconds=elapsed,
            results=points,
            results_csv=csv_str,
        )
    except Exception as exc:
        return await sweep_store.update_sweep(
            sweep_id,
            status=SweepStatus.FAILED,
            error_message=str(exc),
        )
