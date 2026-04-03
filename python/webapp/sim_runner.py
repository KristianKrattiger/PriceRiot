"""
sim_runner.py
=============
Async background runner for temporal multi-run simulation jobs.

Resolves dates from the preset/date_config, builds SimulationRangeConfig
instances (one per statistical run), executes them via asyncio + thread pool
for true CPU parallelism, updates progress after each run completes, and
writes per-run summaries + aggregate back to SimJobStore.
"""
from __future__ import annotations

import asyncio
import dataclasses
import logging
import os
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import date, timedelta
from typing import List, Optional, Tuple

from .sim_models import (
    DateConfig,
    PerRunSummary,
    SimJobResult,
    SimJobStatus,
    TimeWindow,
)
from .sim_storage import sim_job_store

logger = logging.getLogger("priceriot.sim_runner")


# ---------------------------------------------------------------------------
# Date resolution helpers
# ---------------------------------------------------------------------------

def resolve_active_dates(
    preset: str,
    date_config: DateConfig,
) -> Tuple[date, date, Optional[List[date]]]:
    """Return (start_date, end_date, active_dates_or_None).

    active_dates is None for contiguous_range / single_day_n_runs (the
    TemporalScheduler uses the full range + YAML schedule).  For
    weekday_repeat and custom_days, an explicit list is returned so the
    scheduler only simulates exactly those dates.
    """
    if preset == "contiguous_range":
        if not date_config.start or not date_config.end:
            raise ValueError("contiguous_range requires start and end dates")
        return (
            date.fromisoformat(date_config.start),
            date.fromisoformat(date_config.end),
            None,
        )

    if preset == "single_day_n_runs":
        if not date_config.date:
            raise ValueError("single_day_n_runs requires a date")
        d = date.fromisoformat(date_config.date)
        return d, d, None

    if preset == "weekday_repeat":
        if not date_config.anchor or not date_config.weeks or not date_config.days_of_week:
            raise ValueError("weekday_repeat requires anchor, weeks, and days_of_week")
        anchor     = date.fromisoformat(date_config.anchor)
        target_dow = set(date_config.days_of_week)
        weeks      = date_config.weeks
        dates: List[date] = []
        for delta in range(weeks * 7):
            d = anchor + timedelta(days=delta)
            if d.weekday() in target_dow:
                dates.append(d)
        if not dates:
            raise ValueError("weekday_repeat produced no active dates")
        dates.sort()
        return dates[0], dates[-1], dates

    if preset == "custom_days":
        if not date_config.dates:
            raise ValueError("custom_days requires a dates list")
        dates = sorted(date.fromisoformat(s) for s in date_config.dates)
        return dates[0], dates[-1], dates

    raise ValueError(f"Unknown preset: {preset!r}")


# ---------------------------------------------------------------------------
# Per-run executor
# ---------------------------------------------------------------------------

def _execute_one_run(
    store_yaml_abs: str,
    start_date: date,
    end_date: date,
    active_dates: Optional[List[date]],
    time_window: Optional[TimeWindow],
    base_seed: int,
    run_index: int,
    output_dir: str,
    mission_probability: float = 0.5,
    spawn_interval: float = 5.0,
    num_stockers: int = 2,
    num_cashiers: int = 1,
    auto_stock_tasks: bool = True,
    auto_register_tasks: bool = True,
) -> PerRunSummary:
    """Execute a single temporal simulation run (called inside a thread)."""
    import sys

    # Ensure the build directory is on sys.path so `simulation` is importable.
    # __file__ = python/webapp/sim_runner.py → up 3 levels = project root
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)
    )))
    for build_dir in (os.path.join(root, "build"), os.path.join(root, "build", "Release")):
        if os.path.isdir(build_dir) and build_dir not in sys.path:
            sys.path.insert(0, build_dir)

    from python.analytics.temporal import SimulationRangeConfig, run_temporal_simulation
    from python.analytics.orchestrator import SimRun

    daily_override = None
    if time_window is not None:
        def _parse_hm(s: str) -> int:
            h, m = s.split(":")
            return int(h)  # we use integer hours to match YAML schema
        daily_override = (
            _parse_hm(time_window.open),
            _parse_hm(time_window.close),
        )

    cfg = SimulationRangeConfig(
        store_yaml=store_yaml_abs,
        start_date=start_date,
        end_date=end_date,
        daily_window_override=daily_override,
        seed=base_seed + (run_index - 1),
        active_dates=active_dates,
        mission_probability=mission_probability,
        base_spawn_interval=spawn_interval,
        num_stockers=num_stockers,
        num_cashiers=num_cashiers,
        auto_stock_tasks=auto_stock_tasks,
        auto_register_tasks=auto_register_tasks,
    )

    t0 = time.monotonic()
    run = SimRun(cfg, run_index=run_index, output_dir=output_dir)
    output = run.execute(verbose=False)
    elapsed = time.monotonic() - t0

    return PerRunSummary(
        run_index=run_index,
        transaction_count=output.transaction_count,
        customer_count=output.customer_count,
        total_revenue=round(output.total_revenue, 2),
        peak_period=output.peak_period,
        output_path=output.output_dir,
        duration_seconds=round(elapsed, 2),
        status="complete",
        day_summaries=output.result.day_summaries,
        kpis=output.kpis or {},
        sku_breakdown=output.sku_breakdown or [],
        traffic_edges=output.traffic_edges or [],
        queue_data=output.queue_data or [],
        worker_timeseries=output.worker_timeseries or [],
    )


# ---------------------------------------------------------------------------
# Main async runner
# ---------------------------------------------------------------------------

async def run_simulation_job(sim_id: str) -> None:
    """Called as a FastAPI BackgroundTask.  Updates sim_job_store throughout."""
    from datetime import datetime, timezone
    import statistics

    job: SimJobResult = await sim_job_store.get_job(sim_id)
    if job is None:
        logger.error("sim_runner: job %s not found", sim_id)
        return

    cfg = job.config
    started_at = datetime.now(timezone.utc).isoformat()
    await sim_job_store.update_job(
        sim_id,
        status=SimJobStatus.RUNNING,
        started_at=started_at,
    )

    # ── Resolve store YAML absolute path ──────────────────────────────────
    from analytics import get_project_root
    root = get_project_root()

    uploads_dir = os.path.join(root, "data", "tmp", "webapp_uploads")
    raw_dir     = os.path.join(root, "data", "raw")

    def _find_yaml(filename: str) -> Optional[str]:
        for d in (uploads_dir, raw_dir, os.path.join(root, "examples"), root):
            p = os.path.join(d, filename)
            if os.path.isfile(p):
                return p
        return None

    store_yaml_abs = _find_yaml(cfg.store_yaml)
    if store_yaml_abs is None:
        await sim_job_store.update_job(
            sim_id,
            status=SimJobStatus.FAILED,
            error_message=f"Store YAML not found: {cfg.store_yaml}",
        )
        return

    # ── Resolve dates ─────────────────────────────────────────────────────
    try:
        start_date, end_date, active_dates = resolve_active_dates(
            cfg.preset, cfg.date_config
        )
    except ValueError as exc:
        await sim_job_store.update_job(
            sim_id,
            status=SimJobStatus.FAILED,
            error_message=str(exc),
        )
        return

    # ── Output directory ──────────────────────────────────────────────────
    output_root = os.path.join(root, "data", "runs", f"sim_{sim_id}")
    os.makedirs(output_root, exist_ok=True)

    n_runs      = cfg.runs
    max_threads = min(cfg.max_threads, n_runs)
    base_seed   = cfg.seed

    # ── Execute runs concurrently ─────────────────────────────────────────
    loop      = asyncio.get_event_loop()
    semaphore = asyncio.Semaphore(max_threads)
    executor  = ThreadPoolExecutor(max_workers=max_threads)

    completed_runs: List[PerRunSummary] = []
    failed = False

    async def run_one(run_index: int) -> PerRunSummary:
        async with semaphore:
            return await loop.run_in_executor(
                executor,
                _execute_one_run,
                store_yaml_abs,
                start_date,
                end_date,
                active_dates,
                cfg.time_window,
                base_seed,
                run_index,
                output_root,
                cfg.mission_probability,
                cfg.spawn_interval,
                cfg.num_stockers,
                cfg.num_cashiers,
                cfg.auto_stock_tasks,
                cfg.auto_register_tasks,
            )

    tasks = [asyncio.create_task(run_one(i)) for i in range(1, n_runs + 1)]

    per_runs: List[Optional[PerRunSummary]] = [None] * n_runs

    for coro in asyncio.as_completed(tasks):
        try:
            summary: PerRunSummary = await coro
            per_runs[summary.run_index - 1] = summary
            completed_count = sum(1 for x in per_runs if x is not None)
            await sim_job_store.update_job(
                sim_id,
                progress={"completed_runs": completed_count, "total_runs": n_runs},
                per_runs=[x for x in per_runs if x is not None],
            )
        except Exception as exc:
            logger.exception("sim_runner: run failed for sim %s", sim_id)
            failed = True
            await sim_job_store.update_job(
                sim_id,
                status=SimJobStatus.FAILED,
                error_message=str(exc),
            )
            break

    executor.shutdown(wait=False)

    if failed:
        return

    # ── Build aggregate ───────────────────────────────────────────────────
    valid_runs = [x for x in per_runs if x is not None]
    txns  = [r.transaction_count for r in valid_runs]
    revs  = [r.total_revenue     for r in valid_runs]
    custs = [r.customer_count    for r in valid_runs]
    votes: dict = {}
    for r in valid_runs:
        votes[r.peak_period] = votes.get(r.peak_period, 0) + 1

    agg = {
        "n_runs":            len(valid_runs),
        "mean_transactions": round(statistics.mean(txns), 1) if txns else 0.0,
        "min_transactions":  min(txns) if txns else 0,
        "max_transactions":  max(txns) if txns else 0,
        "mean_revenue":      round(statistics.mean(revs), 2) if revs else 0.0,
        "min_revenue":       round(min(revs), 2) if revs else 0.0,
        "max_revenue":       round(max(revs), 2) if revs else 0.0,
        "mean_customers":    round(statistics.mean(custs), 1) if custs else 0.0,
        "peak_period_votes": votes,
    }

    completed_at  = datetime.now(timezone.utc).isoformat()
    t_start       = datetime.fromisoformat(started_at)
    t_end         = datetime.fromisoformat(completed_at)
    elapsed       = (t_end - t_start).total_seconds()

    await sim_job_store.update_job(
        sim_id,
        status=SimJobStatus.COMPLETE,
        completed_at=completed_at,
        elapsed_seconds=round(elapsed, 1),
        aggregate=agg,
        per_runs=valid_runs,
        progress={"completed_runs": len(valid_runs), "total_runs": n_runs},
    )
