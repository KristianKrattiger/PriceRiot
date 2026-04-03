"""Quick 13-day multi-run sanity check."""
from __future__ import annotations
import os, sys

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for _p in (_ROOT, os.path.join(_ROOT, "python"),
           os.path.join(_ROOT, "build"),
           os.path.join(_ROOT, "build", "Release")):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.insert(0, _p)

from datetime import date
from python.analytics.orchestrator import RunOrchestrator
from python.analytics.temporal import SimulationRangeConfig, TemporalScheduler

cfg = SimulationRangeConfig(
    store_yaml=os.path.join(_ROOT, "examples", "cowboy_market.yaml"),
    start_date=date(2024, 1, 1),
    end_date=date(2024, 1, 13),
    daily_window_override=(9, 10),   # 1h window keeps run time under 2 minutes
    base_spawn_interval=30.0,
    seed=42,
    dt=1.0,
)

scheduler = TemporalScheduler(cfg)
sim_days = scheduler.get_sim_days()
print(f"\n  13-day range: {len(sim_days)} active days (Mon-Sat, excl. Sun)")
scheduler.print_summary()

orch = RunOrchestrator(cfg, n_runs=3, output_dir=os.path.join(_ROOT, "data", "runs", "sanity_13day"), max_threads=3)
summary = orch.run_all(verbose=True)

# Sanity assertions
assert summary.n_runs == 3, f"Expected 3 runs, got {summary.n_runs}"
assert summary.min_transactions >= 0
assert summary.max_transactions >= summary.min_transactions
assert summary.mean_revenue >= 0
assert len(sim_days) >= 10, f"Expected 10-11 active days (Mon-Sat), got {len(sim_days)}"

print(f"\n  SANITY CHECK PASSED")
print(f"  Active days in range: {len(sim_days)}")
print(f"  Transactions min={summary.min_transactions} max={summary.max_transactions} mean={summary.mean_transactions:.1f}")
print(f"  Revenue min=GBP{summary.min_revenue:.2f} max=GBP{summary.max_revenue:.2f} mean=GBP{summary.mean_revenue:.2f}")
print(f"  Peak period votes: {summary.peak_period_votes}")
