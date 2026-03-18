from __future__ import annotations

from typing import Any, Dict, List

from .models import RunResult, RunStatus
from .storage import session_store


class ComparisonEngine:
    def compare_runs(self, run_ids: List[str]) -> Dict[str, Any]:
        runs: List[RunResult] = []
        for rid in run_ids:
            run = session_store.get_run(rid)
            if run is None:
                raise ValueError(f"Run {rid} not found")
            if run.status != RunStatus.COMPLETED:
                raise ValueError(f"Run {rid} is not completed (status={run.status})")
            runs.append(run)

        if len(runs) < 2:
            raise ValueError("At least two completed runs are required for comparison")

        kpi_comparison: Dict[str, Dict[str, Any]] = {}
        heatmap_overlay: Dict[str, List[Dict[str, Any]]] = {}
        queue_comparison: Dict[str, List[Dict[str, Any]]] = {}

        for run in runs:
            kpi_comparison[run.run_id] = run.kpis
            heatmap_overlay[run.run_id] = run.heatmap_data or []
            queue_comparison[run.run_id] = run.queue_data or []

        return {
            "runs": [
                {
                    "run_id": run.run_id,
                    "config": run.config.dict(),
                    "status": run.status.value,
                }
                for run in runs
            ],
            "kpi_comparison": kpi_comparison,
            "heatmap_overlay": heatmap_overlay,
            "queue_comparison": queue_comparison,
        }


comparison_engine = ComparisonEngine()

