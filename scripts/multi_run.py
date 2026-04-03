"""
multi_run.py
============
CLI for running N independent PriceRiot simulations and aggregating results.

Usage
-----
    # 5 temporal runs over two weeks
    python scripts/multi_run.py \\
        --store examples/cowboy_market.yaml \\
        --start 2024-01-01 --end 2024-01-14 \\
        --runs 5 \\
        --output outputs/batch_01

    # Dry-run (prints schedule and run plan, no simulation)
    python scripts/multi_run.py \\
        --store examples/cowboy_market.yaml \\
        --start 2024-01-01 --end 2024-01-07 \\
        --runs 3 --dry-run
"""

from __future__ import annotations

import argparse
import os
import sys
from datetime import date, datetime


def _setup_pythonpath() -> None:
    cur = os.path.dirname(os.path.abspath(__file__))
    for _ in range(5):
        if os.path.exists(os.path.join(cur, ".git")) or \
           os.path.exists(os.path.join(cur, "CMakeLists.txt")):
            break
        cur = os.path.dirname(cur)
    root = cur
    for p in (root, os.path.join(root, "python"),
              os.path.join(root, "build"),
              os.path.join(root, "build", "Release")):
        if os.path.isdir(p) and p not in sys.path:
            sys.path.insert(0, p)


_setup_pythonpath()

from python.analytics.temporal import SimulationRangeConfig, TemporalScheduler  # noqa: E402
from python.analytics.orchestrator import RunOrchestrator                         # noqa: E402


def _parse_date(s: str) -> date:
    try:
        return datetime.strptime(s, "%Y-%m-%d").date()
    except ValueError:
        raise argparse.ArgumentTypeError(f"Invalid date '{s}' — expected YYYY-MM-DD")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run N independent PriceRiot simulations and aggregate results.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--store", required=True,
                        help="Path to store YAML")
    parser.add_argument("--start", required=True, type=_parse_date, metavar="YYYY-MM-DD",
                        help="Start date (inclusive)")
    parser.add_argument("--end",   required=True, type=_parse_date, metavar="YYYY-MM-DD",
                        help="End date (inclusive)")
    parser.add_argument("--runs", type=int, default=1,
                        help="Number of independent runs (default: 1)")
    parser.add_argument("--spawn-interval", type=float, default=5.0,
                        help="Baseline spawn interval in sim-seconds (default: 5.0)")
    parser.add_argument("--mission-prob", type=float, default=0.5,
                        help="Mission customer probability 0-1 (default: 0.5)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Base RNG seed; run i uses seed + (i-1) (default: 42)")
    parser.add_argument("--open-hour",  type=int, default=None, metavar="H",
                        help="Override open hour for all active days")
    parser.add_argument("--close-hour", type=int, default=None, metavar="H",
                        help="Override close hour for all active days")
    parser.add_argument("--dt", type=float, default=None,
                        help="Simulation timestep seconds (default: 1/60). Use 1.0 for quick checks.")
    parser.add_argument("--output", default=None,
                        help="Root output directory (default: outputs/)")
    parser.add_argument("--threads", type=int, default=None,
                        help="Max parallel threads (default: cpu_count). Use 1 for sequential.")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print plan only; do not simulate")
    args = parser.parse_args()

    # Validate
    if args.start > args.end:
        parser.error(f"--start ({args.start}) must be <= --end ({args.end})")
    if args.runs < 1:
        parser.error("--runs must be >= 1")

    window_override = None
    if args.open_hour is not None or args.close_hour is not None:
        if args.open_hour is None or args.close_hour is None:
            parser.error("Both --open-hour and --close-hour required together")
        if args.close_hour <= args.open_hour:
            parser.error("--close-hour must be > --open-hour")
        window_override = (args.open_hour, args.close_hour)

    # Resolve output dir
    if args.output:
        out_dir = args.output
    else:
        cur = os.path.dirname(os.path.abspath(__file__))
        for _ in range(5):
            if os.path.exists(os.path.join(cur, ".git")) or \
               os.path.exists(os.path.join(cur, "CMakeLists.txt")):
                break
            cur = os.path.dirname(cur)
        out_dir = os.path.join(cur, "outputs")

    cfg_kwargs = dict(
        store_yaml=args.store,
        start_date=args.start,
        end_date=args.end,
        daily_window_override=window_override,
        base_spawn_interval=args.spawn_interval,
        mission_probability=args.mission_prob,
        seed=args.seed,
    )
    if args.dt is not None:
        cfg_kwargs["dt"] = args.dt
    cfg = SimulationRangeConfig(**cfg_kwargs)

    # Validate schedule and print
    scheduler = TemporalScheduler(cfg)
    sim_days = scheduler.get_sim_days()
    if not sim_days:
        print("[multi_run] No active days in range. Check days_of_operation in YAML.")
        sys.exit(1)

    print(f"\n  Planned: {args.runs} run(s) x {len(sim_days)} active days "
          f"-> {out_dir}")
    scheduler.print_summary()

    if args.dry_run:
        print("[dry-run] No simulation executed.")
        return

    orchestrator = RunOrchestrator(
        cfg,
        n_runs=args.runs,
        output_dir=out_dir,
        max_threads=args.threads,
    )
    orchestrator.run_all(verbose=True)


if __name__ == "__main__":
    main()
