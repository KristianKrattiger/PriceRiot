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
import json
import logging
import os
import time
import uuid
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
_DEBUG_LOG_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "debug-570d5e.log")


def _write_debug_log(run_id: str, hypothesis_id: str, location: str, message: str, data: dict) -> None:
    # #region agent log
    payload = {
        "sessionId": "570d5e",
        "runId": run_id,
        "hypothesisId": hypothesis_id,
        "location": location,
        "message": message,
        "data": data,
        "timestamp": int(time.time() * 1000),
        "id": f"log_{uuid.uuid4().hex}",
    }
    with open(_DEBUG_LOG_PATH, "a", encoding="utf-8") as fh:
        fh.write(json.dumps(payload, separators=(",", ":")) + "\n")
    # #endregion


def _resolve_pos_dataset(root: str, filename: Optional[str]) -> Optional[str]:
    """Same search order as app.get_pos_params."""
    if not filename:
        return None
    for d in (
        os.path.join(root, "data", "tmp", "webapp_uploads"),
        os.path.join(root, "data", "raw"),
        os.path.join(root, "data", "layouts"),
    ):
        p = os.path.join(d, filename)
        if os.path.isfile(p):
            return p
    return None


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


def compute_temporal_workload_estimate(
    store_yaml_abs: str,
    start_date: date,
    end_date: date,
    active_dates: Optional[List[date]],
    time_window: Optional[TimeWindow],
) -> dict:
    """How many business days and sim-seconds the temporal engine will step through.

    Used to tell apart an actual C++ hang from a legitimately huge date range.
    """
    from analytics.temporal_workload import estimate_temporal_workload

    daily_override = None
    if time_window is not None:
        def _parse_hm(s: str) -> int:
            h, _ = s.split(":")
            return int(h)

        daily_override = (_parse_hm(time_window.open), _parse_hm(time_window.close))

    return estimate_temporal_workload(
        store_yaml_abs,
        start_date,
        end_date,
        active_dates,
        daily_override,
    )


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
    pos_csv_path: Optional[str] = None,
) -> PerRunSummary:
    """Execute a single temporal simulation run (called inside a thread)."""
    import sys
    logger.info("sim_runner: _execute_one_run start run_index=%d store=%r", run_index, store_yaml_abs)

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
        pos_csv_path=pos_csv_path,
    )

    t0 = time.monotonic()
    logger.info("sim_runner: run_index=%d SimRun.execute() starting", run_index)
    run = SimRun(cfg, run_index=run_index, output_dir=output_dir)
    output = run.execute(verbose=False)
    logger.info("sim_runner: run_index=%d SimRun.execute() done in %.1fs", run_index, time.monotonic() - t0)
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
    print(
        f"[SIM] background task run_simulation_job entered sim_id={sim_id} "
        f"(if job stays queued, BackgroundTasks did not run)",
        flush=True,
    )
    print(f"[SIM] background task fired for sim_id={sim_id}", flush=True)
    try:
        await _run_simulation_job_inner(sim_id)
    except Exception as exc:  # noqa: BLE001
        logger.exception("sim_runner: unhandled exception in job %s", sim_id)
        try:
            await sim_job_store.update_job(
                sim_id,
                status=SimJobStatus.FAILED,
                error_message=f"Internal error: {exc}",
            )
        except Exception:
            pass


async def _run_simulation_job_inner(sim_id: str) -> None:
    from datetime import datetime, timezone
    import statistics

    logger.info("sim_runner: starting job %s", sim_id)
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
    logger.info("sim_runner: job %s → RUNNING, store_yaml=%r preset=%r",
                sim_id, cfg.store_yaml, cfg.preset)

    # ── Resolve store YAML absolute path ──────────────────────────────────
    from analytics import get_project_root
    root = get_project_root()
    logger.info("sim_runner: project root = %s", root)

    uploads_dir = os.path.join(root, "data", "tmp", "webapp_uploads")
    raw_dir     = os.path.join(root, "data", "raw")

    layouts_dir = os.path.join(root, "data", "layouts")

    def _find_yaml(filename: str) -> Optional[str]:
        for d in (layouts_dir, uploads_dir, raw_dir, os.path.join(root, "examples"), root):
            p = os.path.join(d, filename)
            if os.path.isfile(p):
                return p
        return None

    store_yaml_abs = _find_yaml(cfg.store_yaml)
    logger.info("sim_runner: job %s store_yaml resolved to %r", sim_id, store_yaml_abs)
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

    workload = compute_temporal_workload_estimate(
        store_yaml_abs,
        start_date,
        end_date,
        active_dates,
        cfg.time_window,
    )
    await sim_job_store.update_job(sim_id, workload=workload)
    if "error" in workload:
        logger.warning("sim_runner: workload estimate failed: %s", workload["error"])
    else:
        logger.info(
            "sim_runner: job %s workload sim_days=%s total_sim_seconds=%s approx_step_ticks=%s severity=%s",
            sim_id,
            workload.get("sim_days"),
            workload.get("total_sim_seconds"),
            workload.get("approx_step_ticks"),
            workload.get("severity"),
        )
        print(
            f"[SIM] job {sim_id} workload: sim_days={workload.get('sim_days')} "
            f"total_sim_seconds={workload.get('total_sim_seconds')} "
            f"approx_step_ticks={workload.get('approx_step_ticks')} "
            f"severity={workload.get('severity')}",
            flush=True,
        )

    # ── Output directory ──────────────────────────────────────────────────
    output_root = os.path.join(root, "data", "runs", f"sim_{sim_id}")
    os.makedirs(output_root, exist_ok=True)

    n_runs      = cfg.runs
    max_threads = min(cfg.max_threads, n_runs)
    base_seed   = cfg.seed

    logger.info("sim_runner: job %s launching %d run(s) with %d thread(s), dates %s..%s",
                sim_id, n_runs, max_threads, start_date, end_date)

    pos_csv_abs = _resolve_pos_dataset(root, cfg.pos_dataset)
    if cfg.pos_dataset and pos_csv_abs is None:
        logger.warning(
            "sim_runner: pos_dataset=%r not found under uploads/raw/layouts — intraday curve falls back to synthetic",
            cfg.pos_dataset,
        )

    # ── Execute runs concurrently ─────────────────────────────────────────
    loop      = asyncio.get_running_loop()
    semaphore = asyncio.Semaphore(max_threads)
    executor  = ThreadPoolExecutor(max_workers=max_threads)

    completed_runs: List[PerRunSummary] = []
    failed = False

    # Derive timeout from estimated step workload to avoid false "stuck" failures
    # on large-but-valid layouts. Keep hard bounds for safety.
    approx_ticks = 0
    if isinstance(workload, dict):
        try:
            approx_ticks = int(workload.get("approx_step_ticks", 0) or 0)
        except Exception:
            approx_ticks = 0
    # Wall time ≫ linear tick estimate on large layouts: sim 71 logs showed ~13.4
    # sim-seconds per wall-second; 984s budget died mid-day while chunks still advanced.
    # Agent density scales inversely with spawn_interval — more agents = more path queries.
    _spawn = cfg.spawn_interval if cfg.spawn_interval and cfg.spawn_interval > 0 else 5.0
    _agent_load_factor = max(1.0, 5.0 / _spawn)
    # 300 ticks/s is a conservative throughput floor for large/complex layouts.
    # Cowboy_Outpost with default spawn ran at ~442 ticks/s and hit the 814s cap.
    dynamic_timeout_s = max(600, min(21600, int(approx_ticks / 300 * _agent_load_factor) + 600))
    _write_debug_log(
        run_id="post-fix",
        hypothesis_id="H14",
        location="sim_runner.py:_run_simulation_job_inner:timeout_budget",
        message="Calculated dynamic timeout budget",
        data={
            "simId": sim_id,
            "approxStepTicks": approx_ticks,
            "timeoutSeconds": dynamic_timeout_s,
        },
    )

    async def run_one(run_index: int) -> PerRunSummary:
        async with semaphore:
            try:
                return await asyncio.wait_for(
                    loop.run_in_executor(
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
                        pos_csv_abs,
                    ),
                    timeout=float(dynamic_timeout_s),
                )
            except asyncio.TimeoutError:
                _write_debug_log(
                    run_id="post-fix",
                    hypothesis_id="H13",
                    location="sim_runner.py:run_one:timeout",
                    message="run_one timeout reached",
                    data={
                        "runIndex": run_index,
                        "timeoutSeconds": float(dynamic_timeout_s),
                        "storeYamlAbs": store_yaml_abs,
                        "startDate": start_date.isoformat(),
                        "endDate": end_date.isoformat(),
                    },
                )
                raise RuntimeError(
                    f"Run {run_index} timed out after {dynamic_timeout_s} s "
                    f"(approx {approx_ticks} step ticks) — the simulation may be stuck "
                    f"or the workload may need a longer server-side budget. "
                    f"Check store YAML for degenerate navmesh geometry (e.g. very narrow aisles)."
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
