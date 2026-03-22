"""
Persistent run storage backed by aiosqlite.

CSV blobs (transactions_csv, customers_csv) are written to disk under
  data/runs/{run_id}/
to keep the DB row lightweight.  All other analytics data is stored as
JSON in the `data` column.

A module-level `session_store` singleton is created at import time.
Call `await session_store.init()` once during app startup (FastAPI lifespan).
A synchronous shim `session_store.get_run` / `session_store.update_run` is
retained for code paths that cannot be made async easily; they open a
temporary synchronous connection.
"""
from __future__ import annotations

import asyncio
import json
import os
import threading
from typing import Any, Dict, List, Optional

import aiosqlite

from .models import RunConfig, RunResult, RunStatus


def _runs_dir() -> str:
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = os.path.join(root, "data", "runs")
    os.makedirs(path, exist_ok=True)
    return path


def _db_path() -> str:
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    data_dir = os.path.join(root, "data")
    os.makedirs(data_dir, exist_ok=True)
    return os.path.join(data_dir, "priceriot.db")


_CREATE_SQL = """
CREATE TABLE IF NOT EXISTS runs (
    run_id     TEXT PRIMARY KEY,
    data       TEXT NOT NULL,
    created_at TEXT NOT NULL
);
"""

_NEXT_ID_SQL = """
CREATE TABLE IF NOT EXISTS counters (
    name  TEXT PRIMARY KEY,
    value INTEGER NOT NULL DEFAULT 0
);
"""


class Database:
    """Async-first SQLite store for simulation runs."""

    def __init__(self) -> None:
        self._db: Optional[aiosqlite.Connection] = None
        self._lock = asyncio.Lock()
        # Synchronous fallback lock for sync shim methods
        self._sync_lock = threading.RLock()

    async def init(self, path: Optional[str] = None) -> None:
        path = path or _db_path()
        self._db = await aiosqlite.connect(path)
        self._db.row_factory = aiosqlite.Row
        await self._db.execute(_CREATE_SQL)
        await self._db.execute(_NEXT_ID_SQL)
        await self._db.execute(
            "INSERT OR IGNORE INTO counters (name, value) VALUES ('run_id', 0)"
        )
        await self._db.commit()

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _csv_dir(self, run_id: str) -> str:
        d = os.path.join(_runs_dir(), run_id)
        os.makedirs(d, exist_ok=True)
        return d

    def _write_csvs(self, run_id: str, result: RunResult) -> RunResult:
        """Persist CSV blobs to disk; replace blob with file path in model."""
        updates: Dict[str, Any] = {}
        d = self._csv_dir(run_id)
        for attr, filename in [
            ("transactions_csv", "transactions.csv"),
            ("customers_csv", "customers.csv"),
        ]:
            blob = getattr(result, attr)
            if blob and not os.path.isfile(blob):
                path = os.path.join(d, filename)
                with open(path, "w", encoding="utf-8") as f:
                    f.write(blob)
                updates[attr] = path
        if updates:
            result = result.copy(update=updates)
        return result

    def _read_csv(self, path_or_blob: Optional[str]) -> Optional[str]:
        """Return CSV content: read from file path if it's a file, else return as-is."""
        if not path_or_blob:
            return None
        if os.path.isfile(path_or_blob):
            with open(path_or_blob, encoding="utf-8") as f:
                return f.read()
        return path_or_blob

    def _serialise(self, result: RunResult) -> str:
        d = result.model_dump()
        return json.dumps(d)

    def _deserialise(self, raw: str) -> RunResult:
        return RunResult.model_validate_json(raw)

    # ------------------------------------------------------------------
    # Async CRUD
    # ------------------------------------------------------------------

    async def create_run(self, config: RunConfig) -> str:
        async with self._lock:
            await self._db.execute(
                "UPDATE counters SET value = value + 1 WHERE name = 'run_id'"
            )
            async with self._db.execute(
                "SELECT value FROM counters WHERE name = 'run_id'"
            ) as cur:
                row = await cur.fetchone()
            run_id = str(row["value"])

        from datetime import datetime, timezone
        created_at = datetime.now(timezone.utc).isoformat()
        result = RunResult(run_id=run_id, config=config, created_at=created_at)

        async with self._lock:
            await self._db.execute(
                "INSERT INTO runs (run_id, data, created_at) VALUES (?, ?, ?)",
                (run_id, self._serialise(result), created_at),
            )
            await self._db.commit()
        return run_id

    async def async_get_run(self, run_id: str) -> Optional[RunResult]:
        async with self._db.execute(
            "SELECT data FROM runs WHERE run_id = ?", (run_id,)
        ) as cur:
            row = await cur.fetchone()
        if row is None:
            return None
        return self._deserialise(row["data"])

    async def async_update_run(self, run_id: str, **kwargs) -> Optional[RunResult]:
        async with self._lock:
            existing = await self.async_get_run(run_id)
            if existing is None:
                return None
            updated = existing.copy(update=kwargs)
            updated = self._write_csvs(run_id, updated)
            await self._db.execute(
                "UPDATE runs SET data = ? WHERE run_id = ?",
                (self._serialise(updated), run_id),
            )
            await self._db.commit()
        return updated

    async def async_list_runs(self) -> List[RunResult]:
        async with self._db.execute(
            "SELECT data FROM runs ORDER BY CAST(run_id AS INTEGER)"
        ) as cur:
            rows = await cur.fetchall()
        return [self._deserialise(r["data"]) for r in rows]

    async def async_delete_run(self, run_id: str) -> bool:
        async with self._lock:
            result = await self.async_get_run(run_id)
            if result is None:
                return False
            await self._db.execute("DELETE FROM runs WHERE run_id = ?", (run_id,))
            await self._db.commit()
        # Best-effort: remove CSV files
        csv_dir = os.path.join(_runs_dir(), run_id)
        if os.path.isdir(csv_dir):
            import shutil
            shutil.rmtree(csv_dir, ignore_errors=True)
        return True

    # ------------------------------------------------------------------
    # Synchronous shim (used by non-async callers in existing code paths)
    # ------------------------------------------------------------------

    def _run_sync(self, coro):
        """Run a coroutine synchronously, reusing the existing event loop if possible."""
        try:
            loop = asyncio.get_event_loop()
            if loop.is_running():
                # We're inside an async context; schedule the coro as a task.
                import concurrent.futures
                future = asyncio.run_coroutine_threadsafe(coro, loop)
                return future.result(timeout=30)
            return loop.run_until_complete(coro)
        except RuntimeError:
            return asyncio.run(coro)

    def get_run(self, run_id: str) -> Optional[RunResult]:
        return self._run_sync(self.async_get_run(run_id))

    def update_run(self, run_id: str, **kwargs) -> Optional[RunResult]:
        return self._run_sync(self.async_update_run(run_id, **kwargs))

    def list_runs(self) -> List[RunResult]:
        return self._run_sync(self.async_list_runs())

    def create_run_sync(self, config: RunConfig) -> str:
        return self._run_sync(self.create_run(config))

    def get_csv(self, run_id: str, kind: str) -> Optional[str]:
        """Return CSV content for a completed run (reads from disk if needed)."""
        result = self.get_run(run_id)
        if result is None:
            return None
        attr = "transactions_csv" if kind == "transactions" else "customers_csv"
        return self._read_csv(getattr(result, attr))


session_store = Database()
