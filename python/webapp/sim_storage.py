"""
sim_storage.py
==============
Persistent storage for simulation control-plane jobs.
Adds a `simulation_jobs` table to the existing data/priceriot.db SQLite file.
"""
from __future__ import annotations

import asyncio
import os
from typing import List, Optional

import aiosqlite

from .sim_models import SimJobConfig, SimJobResult, SimJobStatus


_CREATE_JOBS_SQL = """
CREATE TABLE IF NOT EXISTS simulation_jobs (
    sim_id     TEXT PRIMARY KEY,
    data       TEXT NOT NULL,
    created_at TEXT NOT NULL
);
"""

_CREATE_COUNTER_SQL = """
CREATE TABLE IF NOT EXISTS sim_counters (
    name  TEXT PRIMARY KEY,
    value INTEGER NOT NULL DEFAULT 0
);
"""


def _db_path() -> str:
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    data_dir = os.path.join(root, "data")
    os.makedirs(data_dir, exist_ok=True)
    return os.path.join(data_dir, "priceriot.db")


class SimJobStore:
    """Async-first store for temporal simulation jobs."""

    def __init__(self) -> None:
        self._db: Optional[aiosqlite.Connection] = None
        self._lock = asyncio.Lock()

    async def init(self, path: Optional[str] = None) -> None:
        path = path or _db_path()
        self._db = await aiosqlite.connect(path)
        self._db.row_factory = aiosqlite.Row
        await self._db.execute(_CREATE_JOBS_SQL)
        await self._db.execute(_CREATE_COUNTER_SQL)
        await self._db.execute(
            "INSERT OR IGNORE INTO sim_counters (name, value) VALUES ('sim_id', 0)"
        )
        await self._db.commit()

    # ── Internal helpers ───────────────────────────────────────────────────

    def _serialise(self, job: SimJobResult) -> str:
        return job.model_dump_json()

    def _deserialise(self, raw: str) -> SimJobResult:
        return SimJobResult.model_validate_json(raw)

    # ── Async CRUD ─────────────────────────────────────────────────────────

    async def create_job(self, config: SimJobConfig) -> str:
        from datetime import datetime, timezone
        async with self._lock:
            await self._db.execute(
                "UPDATE sim_counters SET value = value + 1 WHERE name = 'sim_id'"
            )
            async with self._db.execute(
                "SELECT value FROM sim_counters WHERE name = 'sim_id'"
            ) as cur:
                row = await cur.fetchone()
            sim_id = str(row["value"])

        created_at = datetime.now(timezone.utc).isoformat()
        job = SimJobResult(
            sim_id=sim_id,
            config=config,
            created_at=created_at,
            progress={"completed_runs": 0, "total_runs": config.runs},
        )
        async with self._lock:
            await self._db.execute(
                "INSERT INTO simulation_jobs (sim_id, data, created_at) VALUES (?, ?, ?)",
                (sim_id, self._serialise(job), created_at),
            )
            await self._db.commit()
        return sim_id

    async def get_job(self, sim_id: str) -> Optional[SimJobResult]:
        async with self._db.execute(
            "SELECT data FROM simulation_jobs WHERE sim_id = ?", (sim_id,)
        ) as cur:
            row = await cur.fetchone()
        if row is None:
            return None
        return self._deserialise(row["data"])

    async def update_job(self, sim_id: str, **kwargs) -> Optional[SimJobResult]:
        async with self._lock:
            existing = await self.get_job(sim_id)
            if existing is None:
                return None
            updated = existing.model_copy(update=kwargs)
            await self._db.execute(
                "UPDATE simulation_jobs SET data = ? WHERE sim_id = ?",
                (self._serialise(updated), sim_id),
            )
            await self._db.commit()
        return updated

    async def list_jobs(self) -> List[SimJobResult]:
        async with self._db.execute(
            "SELECT data FROM simulation_jobs ORDER BY CAST(sim_id AS INTEGER) DESC"
        ) as cur:
            rows = await cur.fetchall()
        return [self._deserialise(r["data"]) for r in rows]

    async def delete_job(self, sim_id: str) -> bool:
        async with self._lock:
            async with self._db.execute(
                "DELETE FROM simulation_jobs WHERE sim_id = ?", (sim_id,)
            ) as cur:
                deleted = cur.rowcount > 0
            await self._db.commit()
        return deleted

    # ── Synchronous shim ───────────────────────────────────────────────────

    def _run_sync(self, coro):
        try:
            loop = asyncio.get_event_loop()
            if loop.is_running():
                import concurrent.futures
                future = asyncio.run_coroutine_threadsafe(coro, loop)
                return future.result(timeout=30)
            return loop.run_until_complete(coro)
        except RuntimeError:
            return asyncio.run(coro)

    def update_job_sync(self, sim_id: str, **kwargs) -> Optional[SimJobResult]:
        return self._run_sync(self.update_job(sim_id, **kwargs))


sim_job_store = SimJobStore()
