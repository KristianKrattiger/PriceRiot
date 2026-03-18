from __future__ import annotations

import threading
from typing import Dict, List, Optional

from .models import RunConfig, RunResult, RunStatus


class SessionStore:
    """Thread-safe in-memory store for simulation runs."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._runs: Dict[str, RunResult] = {}
        self._next_id = 1

    def _generate_id(self) -> str:
        with self._lock:
            run_id = str(self._next_id)
            self._next_id += 1
            return run_id

    def create_run(self, config: RunConfig) -> str:
        run_id = self._generate_id()
        result = RunResult(run_id=run_id, config=config)
        with self._lock:
            self._runs[run_id] = result
        return run_id

    def get_run(self, run_id: str) -> Optional[RunResult]:
        with self._lock:
            return self._runs.get(run_id)

    def update_run(self, run_id: str, **kwargs) -> Optional[RunResult]:
        with self._lock:
            existing = self._runs.get(run_id)
            if existing is None:
                return None
            updated = existing.copy(update=kwargs)
            self._runs[run_id] = updated
            return updated

    def list_runs(self) -> List[RunResult]:
        with self._lock:
            # Return in creation order (run_id is monotonically increasing)
            return sorted(self._runs.values(), key=lambda r: int(r.run_id))


session_store = SessionStore()

