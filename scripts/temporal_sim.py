"""
temporal_sim.py
===============
CLI entry point for temporally-scheduled PriceRiot simulations.

Accepts a date range and optional hour override, validates both against the
store's operating schedule, prints a summary of active sim days, then runs
the simulation and writes per-day and aggregate outputs.

Usage
-----
    # Dry-run — prints schedule only, no simulation
    python scripts/temporal_sim.py \\
        --store examples/cowboy_market.yaml \\
        --start 2024-01-01 --end 2024-01-14 \\
        --dry-run

    # Full temporal run
    python scripts/temporal_sim.py \\
        --store examples/cowboy_market.yaml \\
        --start 2024-01-01 --end 2024-01-14 \\
        --spawn-interval 4.0 \\
        --seed 99 \\
        --output data/processed/temporal_run.csv

    # Override hours (ignore YAML schedule, use 10:00-20:00 every open day)
    python scripts/temporal_sim.py \\
        --store examples/cowboy_market.yaml \\
        --start 2024-03-01 --end 2024-03-07 \\
        --open-hour 10 --close-hour 20
"""

from __future__ import annotations

import argparse
import os
import sys
from datetime import date, datetime


def _setup_pythonpath() -> None:
    """Add project root and build dirs to sys.path so imports work."""
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

from python.analytics.temporal import (  # noqa: E402
    SimulationRangeConfig,
    TemporalScheduler,
    run_temporal_simulation,
)


def _parse_date(s: str) -> date:
    try:
        return datetime.strptime(s, "%Y-%m-%d").date()
    except ValueError:
        raise argparse.ArgumentTypeError(f"Invalid date '{s}' — expected YYYY-MM-DD")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run a temporally-scheduled PriceRiot simulation.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--store", required=True,
                        help="Path to store YAML (relative to project root or absolute)")
    parser.add_argument("--start", required=True, type=_parse_date, metavar="YYYY-MM-DD",
                        help="First day of simulation range (inclusive)")
    parser.add_argument("--end",   required=True, type=_parse_date, metavar="YYYY-MM-DD",
                        help="Last day of simulation range (inclusive)")

    # Optional hour override
    parser.add_argument("--open-hour",  type=int, default=None, metavar="H",
                        help="Override store open hour (0-23) for all active days")
    parser.add_argument("--close-hour", type=int, default=None, metavar="H",
                        help="Override store close hour (1-24) for all active days")

    # Simulation params
    parser.add_argument("--spawn-interval", type=float, default=5.0,
                        help="Baseline spawn interval in sim-seconds (default: 5.0)")
    parser.add_argument("--mission-prob", type=float, default=0.5,
                        help="Mission customer probability 0-1 (default: 0.5)")
    parser.add_argument("--seed", type=int, default=42,
                        help="RNG seed for reproducibility (default: 42)")

    # Output
    parser.add_argument("--dt", type=float, default=None,
                        help="Simulation timestep in seconds (default: 1/60 ≈ 0.0167). "
                             "Larger values run faster but are less precise; "
                             "use 1.0 for quick sanity checks.")
    parser.add_argument("--output", default=None,
                        help="Path for the line-item CSV output (default: auto-generated)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print schedule summary only; do not simulate")

    args = parser.parse_args()

    # ── Validation ────────────────────────────────────────────────────────────
    if args.start > args.end:
        parser.error(f"--start ({args.start}) must be on or before --end ({args.end})")

    window_override: tuple[int, int] | None = None
    if args.open_hour is not None or args.close_hour is not None:
        if args.open_hour is None or args.close_hour is None:
            parser.error("Both --open-hour and --close-hour must be specified together")
        if not (0 <= args.open_hour <= 23):
            parser.error(f"--open-hour must be 0-23, got {args.open_hour}")
        if not (1 <= args.close_hour <= 24):
            parser.error(f"--close-hour must be 1-24, got {args.close_hour}")
        if args.close_hour <= args.open_hour:
            parser.error(
                f"--close-hour ({args.close_hour}) must be > --open-hour ({args.open_hour})"
            )
        window_override = (args.open_hour, args.close_hour)

    # ── Build config and print summary ────────────────────────────────────────
    config_kwargs = dict(
        store_yaml=args.store,
        start_date=args.start,
        end_date=args.end,
        daily_window_override=window_override,
        base_spawn_interval=args.spawn_interval,
        mission_probability=args.mission_prob,
        seed=args.seed,
    )
    if args.dt is not None:
        config_kwargs["dt"] = args.dt
    config = SimulationRangeConfig(**config_kwargs)

    scheduler = TemporalScheduler(config)
    sim_days = scheduler.get_sim_days()

    if not sim_days:
        print("[temporal_sim] No active simulation days in the requested range.")
        print("               Check days_of_operation in the store YAML.")
        sys.exit(1)

    scheduler.print_summary()

    if args.dry_run:
        print("[dry-run] No simulation executed.")
        return

    # ── Run simulation ────────────────────────────────────────────────────────
    result = run_temporal_simulation(config, verbose=True)

    if not result.transactions.empty:
        # Resolve output path
        if args.output:
            out_path = args.output
        else:
            cur = os.path.dirname(os.path.abspath(__file__))
            for _ in range(5):
                if os.path.exists(os.path.join(cur, ".git")) or \
                   os.path.exists(os.path.join(cur, "CMakeLists.txt")):
                    break
                cur = os.path.dirname(cur)
            stem = os.path.splitext(os.path.basename(args.store))[0]
            out_path = os.path.join(cur, "data", "processed",
                                    f"{stem}_{args.start}_{args.end}_temporal.csv")

        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        result.transactions.to_csv(out_path, index=False)

        # Print aggregate summary
        total_tx  = sum(s["transactions"] for s in result.day_summaries)
        total_rev = sum(s["revenue"]       for s in result.day_summaries)
        days_run  = len(result.day_summaries)
        print(f"\n{'─'*54}")
        print(f"  Aggregate summary ({days_run} active days)")
        print(f"{'─'*54}")
        print(f"  Total transactions: {total_tx:,}")
        print(f"  Total revenue:      £{total_rev:,.2f}")
        print(f"  Avg txns/day:       {total_tx/days_run:.1f}")
        print(f"  Avg revenue/day:    £{total_rev/days_run:,.2f}")
        print(f"  Output written:     {os.path.relpath(out_path)}")
        print(f"{'─'*54}\n")
    else:
        print("[temporal_sim] Simulation completed but produced no transactions.")


if __name__ == "__main__":
    main()
