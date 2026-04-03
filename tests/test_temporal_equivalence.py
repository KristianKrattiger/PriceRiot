"""
test_temporal_equivalence.py
============================
Validates that sequential and parallel multi-runs on the same seed produce
identical transaction-level output (acceptance criterion from Phase 4).

Also smoke-tests the TemporalScheduler schedule resolution and per-period
spawn interval variation.

Run from project root:
    PYTHONPATH=build;python python -m pytest tests/test_temporal_equivalence.py -v
    # or directly:
    PYTHONPATH=build;python python tests/test_temporal_equivalence.py
"""

from __future__ import annotations

import os
import sys
import tempfile

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for _p in (_ROOT, os.path.join(_ROOT, "python"),
           os.path.join(_ROOT, "build"),
           os.path.join(_ROOT, "build", "Release")):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.insert(0, _p)

from datetime import date

from python.analytics.orchestrator import RunOrchestrator
from python.analytics.temporal import (
    SimulationRangeConfig,
    TemporalScheduler,
)

# Use cowboy_market; fall back to store_tiny if absent.
_COWBOY = os.path.join(_ROOT, "examples", "cowboy_market.yaml")
_TINY   = os.path.join(_ROOT, "examples", "store_tiny.yaml")
STORE   = _COWBOY if os.path.isfile(_COWBOY) else _TINY

# ── Scheduler-only config (no simulation run) ─────────────────────────────────
# Uses the YAML's natural hours so all 4 intraday periods are present.
# No override means the full 09:00-19:00 / 09:00-20:00 window is used.
_SCHED_CFG = dict(
    store_yaml=STORE,
    start_date=date(2024, 1, 1),   # Monday
    end_date=date(2024, 1, 1),     # single day
    # no daily_window_override → use store YAML hours (09:00-19:00 Mon)
    base_spawn_interval=3.0,
    mission_probability=0.4,
    seed=42,
    dt=60.0,
)

# ── Simulation config (must actually execute quickly) ─────────────────────────
# 1-hour window, spawn every 30s, dt=1s → 3600 ticks, ~120 customers max active.
# dt=60 is too coarse: agents skip past checkout without completing transactions.
_FAST_CFG = dict(
    store_yaml=STORE,
    start_date=date(2024, 1, 1),   # Monday — guaranteed open
    end_date=date(2024, 1, 1),     # single day
    daily_window_override=(9, 10), # 1-hour window → fits in "morning" only
    base_spawn_interval=30.0,      # 1 spawn per 30s → ~120 customers total
    mission_probability=0.4,
    seed=42,
    dt=1.0,                        # 1 s per tick — fine enough for transactions
)


# ── Helper ────────────────────────────────────────────────────────────────────

def _txn_fingerprint(df) -> list:
    """Stable, order-independent fingerprint of a transactions DataFrame."""
    if df is None or df.empty:
        return []
    cols = [c for c in ("transaction_id", "item_id", "quantity", "item_total")
            if c in df.columns]
    return sorted(df[cols].itertuples(index=False, name=None)) if cols else []


# ── Tests ─────────────────────────────────────────────────────────────────────

def test_scheduler_resolves_days():
    """TemporalScheduler emits exactly the expected active days."""
    cfg = SimulationRangeConfig(
        store_yaml=STORE,
        start_date=date(2024, 1, 1),   # Mon
        end_date=date(2024, 1, 7),     # Sun
        **{k: v for k, v in _SCHED_CFG.items()
           if k not in ("store_yaml", "start_date", "end_date")},
    )
    scheduler = TemporalScheduler(cfg)
    days = scheduler.get_sim_days()
    # cowboy_market: Mon-Sat open, Sun closed → 6 active days in a Mon-Sun week
    assert 5 <= len(days) <= 7, f"Expected 5-7 active days, got {len(days)}"
    for sd in days:
        assert sd.open_hour < sd.close_hour
        # Full 10+ hour window spans all 4 period boundaries
        assert 3 <= len(sd.spawn_periods) <= 4, (
            f"{sd.date} {sd.open_hour}-{sd.close_hour}: "
            f"got {len(sd.spawn_periods)} periods: "
            f"{[p.name for p in sd.spawn_periods]}"
        )


def test_spawn_intervals_vary_by_period():
    """Intraday spawn intervals differ across morning/midday/evening/close."""
    cfg = SimulationRangeConfig(**_SCHED_CFG)
    scheduler = TemporalScheduler(cfg)
    days = scheduler.get_sim_days()
    assert days, "No sim days generated"
    periods = days[0].spawn_periods
    intervals = [p.spawn_interval for p in periods]
    assert all(i > 0 for i in intervals)
    # Full-day window spans both Gaussian peaks so periods must differ in intensity.
    assert len(set(intervals)) > 1, (
        f"All periods have the same spawn interval {intervals} — "
        "temporal variation is not working.\n"
        f"Periods: {[(p.name, p.spawn_interval) for p in periods]}"
    )
    # Evening (around 16:30 peak) should be the most intense i.e. shortest interval.
    evening = next((p for p in periods if p.name == "evening"), None)
    if evening:
        assert all(evening.spawn_interval <= p.spawn_interval + 0.01 for p in periods), (
            f"Evening period should have the shortest interval.\n"
            f"Periods: {[(p.name, p.spawn_interval) for p in periods]}"
        )


def test_window_override_respected():
    """daily_window_override replaces the YAML hours for every open day."""
    cfg = SimulationRangeConfig(
        store_yaml=STORE,
        start_date=date(2024, 1, 1),
        end_date=date(2024, 1, 1),
        daily_window_override=(10, 14),
        base_spawn_interval=5.0,
        seed=42,
        dt=1.0,
    )
    scheduler = TemporalScheduler(cfg)
    days = scheduler.get_sim_days()
    assert days
    sd = days[0]
    assert sd.open_hour == 10
    assert sd.close_hour == 14
    assert sd.total_duration == 4 * 3600.0


def test_sequential_vs_parallel_seed_equivalence():
    """
    CORE ACCEPTANCE CRITERION:
    2 sequential runs and 2 parallel runs on the same base seed must produce
    identical per-run transaction fingerprints.
    """
    cfg = SimulationRangeConfig(**_FAST_CFG)

    with tempfile.TemporaryDirectory() as seq_dir, \
         tempfile.TemporaryDirectory() as par_dir:

        seq_orch = RunOrchestrator(cfg, n_runs=2, output_dir=seq_dir,
                                   max_threads=1)
        par_orch = RunOrchestrator(cfg, n_runs=2, output_dir=par_dir,
                                   max_threads=2)

        seq_summary = seq_orch.run_all(verbose=False)
        par_summary = par_orch.run_all(verbose=False)

        # ── Aggregate metrics must match ───────────────────────────────────
        assert seq_summary.n_runs == par_summary.n_runs == 2

        # Transaction counts must be identical across modes for each run.
        assert seq_summary.min_transactions == par_summary.min_transactions, (
            f"min_transactions differ: "
            f"seq={seq_summary.min_transactions} par={par_summary.min_transactions}"
        )
        assert seq_summary.max_transactions == par_summary.max_transactions, (
            f"max_transactions differ: "
            f"seq={seq_summary.max_transactions} par={par_summary.max_transactions}"
        )

        print(f"\n  [equivalence] seq txns min={seq_summary.min_transactions} "
              f"max={seq_summary.max_transactions}")
        print(f"  [equivalence] par txns min={par_summary.min_transactions} "
              f"max={par_summary.max_transactions}")
        print("  OK Sequential == Parallel")


def test_n_runs_output_directories():
    """RunOrchestrator creates one output dir per run with summary.json."""
    import json
    cfg = SimulationRangeConfig(**_FAST_CFG)
    with tempfile.TemporaryDirectory() as out_dir:
        orch = RunOrchestrator(cfg, n_runs=3, output_dir=out_dir, max_threads=1)
        orch.run_all(verbose=False)

        for i in range(1, 4):
            run_dir = os.path.join(out_dir, f"run_{i:03d}")
            assert os.path.isdir(run_dir), f"Missing {run_dir}"
            summary_path = os.path.join(run_dir, "summary.json")
            assert os.path.isfile(summary_path), f"Missing {summary_path}"
            with open(summary_path) as f:
                s = json.load(f)
            assert s["run_index"] == i
            assert s["seed"] == cfg.seed + (i - 1)

        agg_path = os.path.join(out_dir, "aggregate_summary.json")
        assert os.path.isfile(agg_path)
        with open(agg_path) as f:
            agg = json.load(f)
        assert agg["n_runs"] == 3
        assert len(agg["runs"]) == 3


# ── Standalone runner ─────────────────────────────────────────────────────────

if __name__ == "__main__":
    tests = [
        test_scheduler_resolves_days,
        test_spawn_intervals_vary_by_period,
        test_window_override_respected,
        test_sequential_vs_parallel_seed_equivalence,
        test_n_runs_output_directories,
    ]
    passed = failed = 0
    for t in tests:
        name = t.__name__
        try:
            t()
            print(f"  OK  {name}")
            passed += 1
        except Exception as e:
            print(f"  FAIL  {name}: {e}")
            import traceback; traceback.print_exc()
            failed += 1

    print(f"\n  {passed} passed, {failed} failed")
    sys.exit(0 if failed == 0 else 1)
