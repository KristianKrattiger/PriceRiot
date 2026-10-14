#!/usr/bin/env python3
"""
CLI helper for the layout-runner hang diagnosis plan.

1) Prints temporal workload (sim_days, total_sim_seconds, approx_step_ticks) for a
   store YAML + date preset — same math as analytics.temporal_workload.estimate_temporal_workload.

2) Optional: run run_temporal_simulation(verbose=True) for a small date range
   to verify the engine progresses.

3) Optional: print native stack sampling hints when --native-stack-hint.

Usage (from repo root, PowerShell):
  $env:PYTHONPATH = "build;python"
  python scripts/diagnose_temporal_workload.py examples/store_tiny.yaml --preset single_day_n_runs --date 2024-01-02

  python scripts/diagnose_temporal_workload.py examples/store_tiny.yaml --run-verbose --preset single_day_n_runs --date 2024-01-02
"""
from __future__ import annotations

import argparse
import importlib.util
import os
import sys
import types
from datetime import date, timedelta
from typing import Any, Callable, List, Optional, Tuple


def _repo_root() -> str:
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _ensure_paths() -> None:
    root = _repo_root()
    # Repo root first: analytics.temporal uses `from python.analytics.core` (namespace package).
    if root not in sys.path:
        sys.path.insert(0, root)
    for p in (
        os.path.join(root, "python"),
        os.path.join(root, "build"),
        os.path.join(root, "build", "Release"),
    ):
        if os.path.isdir(p) and p not in sys.path:
            sys.path.insert(0, p)


def _load_temporal_submodules(root: str) -> Tuple[Callable[..., dict[str, Any]], Any]:
    """Load analytics.temporal + temporal_workload without analytics/__init__.py (avoids simulation pyd)."""
    pkg_dir = os.path.join(root, "python", "analytics")
    if "analytics" not in sys.modules:
        ap = types.ModuleType("analytics")
        ap.__path__ = [pkg_dir]  # type: ignore[attr-defined]
        sys.modules["analytics"] = ap

    def _load(name: str, fname: str) -> Any:
        path = os.path.join(pkg_dir, fname)
        spec = importlib.util.spec_from_file_location(f"analytics.{name}", path)
        if spec is None or spec.loader is None:
            raise ImportError(f"Cannot load {path}")
        mod = importlib.util.module_from_spec(spec)
        mod.__package__ = "analytics"
        sys.modules[f"analytics.{name}"] = mod
        spec.loader.exec_module(mod)
        return mod

    tmod = _load("temporal", "temporal.py")
    tw = _load("temporal_workload", "temporal_workload.py")
    return tw.estimate_temporal_workload, tmod


def _resolve_active_dates_cli(
    preset: str,
    *,
    start: Optional[str],
    end: Optional[str],
    single_date: Optional[str],
    anchor: Optional[str],
    weeks: int,
    days_of_week: List[int],
    custom_dates: List[str],
) -> Tuple[date, date, Optional[List[date]]]:
    """Mirror webapp.sim_runner.resolve_active_dates without Pydantic."""
    if preset == "contiguous_range":
        if not start or not end:
            raise ValueError("contiguous_range requires --start and --end")
        return date.fromisoformat(start), date.fromisoformat(end), None
    if preset == "single_day_n_runs":
        if not single_date:
            raise ValueError("single_day_n_runs requires --date")
        d = date.fromisoformat(single_date)
        return d, d, None
    if preset == "weekday_repeat":
        if not anchor or not weeks or not days_of_week:
            raise ValueError("weekday_repeat requires --anchor, --weeks, and --dow")
        anchor_d = date.fromisoformat(anchor)
        target_dow = set(days_of_week)
        dates: List[date] = []
        for delta in range(weeks * 7):
            d = anchor_d + timedelta(days=delta)
            if d.weekday() in target_dow:
                dates.append(d)
        if not dates:
            raise ValueError("weekday_repeat produced no active dates")
        dates.sort()
        return dates[0], dates[-1], dates
    if preset == "custom_days":
        if not custom_dates:
            raise ValueError("custom_days requires --custom-dates")
        dates = sorted(date.fromisoformat(s) for s in custom_dates)
        return dates[0], dates[-1], dates
    raise ValueError(f"Unknown preset: {preset!r}")


def main() -> int:
    _ensure_paths()

    parser = argparse.ArgumentParser(description="Temporal workload estimate + optional verbose sim")
    parser.add_argument("store_yaml", help="Path to store YAML (relative or absolute)")
    parser.add_argument(
        "--preset",
        default="single_day_n_runs",
        choices=(
            "contiguous_range",
            "single_day_n_runs",
            "weekday_repeat",
            "custom_days",
        ),
    )
    parser.add_argument("--start", help="ISO date (contiguous_range)")
    parser.add_argument("--end", help="ISO date (contiguous_range)")
    parser.add_argument("--date", help="ISO date (single_day_n_runs)")
    parser.add_argument("--anchor", help="ISO anchor (weekday_repeat)")
    parser.add_argument("--weeks", type=int, default=1, help="weekday_repeat weeks")
    parser.add_argument(
        "--dow",
        type=int,
        nargs="*",
        default=[0, 1, 2, 3, 4],
        help="weekday_repeat days of week 0=Mon .. 6=Sun",
    )
    parser.add_argument(
        "--custom-dates",
        nargs="*",
        default=[],
        help="custom_days ISO dates",
    )
    parser.add_argument("--open", dest="tw_open", default=None, help='Override hours "09:00"')
    parser.add_argument("--close", dest="tw_close", default=None, help='Override hours "21:00"')
    parser.add_argument(
        "--run-verbose",
        action="store_true",
        help="Run run_temporal_simulation(verbose=True) after workload print (max 3 sim_days)",
    )
    parser.add_argument(
        "--native-stack-hint",
        action="store_true",
        help="Print how to capture native stacks if the sim is stuck with CPU hot",
    )
    args = parser.parse_args()

    root = _repo_root()
    yaml_path = args.store_yaml
    if not os.path.isabs(yaml_path):
        yaml_path = os.path.normpath(os.path.join(root, yaml_path))
    if not os.path.isfile(yaml_path):
        print(f"ERROR: file not found: {yaml_path}", file=sys.stderr)
        return 1

    try:
        start_d, end_d, active = _resolve_active_dates_cli(
            args.preset,
            start=args.start,
            end=args.end,
            single_date=args.date,
            anchor=args.anchor,
            weeks=args.weeks,
            days_of_week=list(args.dow),
            custom_dates=list(args.custom_dates),
        )
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    daily_override = None
    if args.tw_open and args.tw_close:
        def _parse_hm(s: str) -> int:
            h, _ = s.split(":")
            return int(h)

        daily_override = (_parse_hm(args.tw_open), _parse_hm(args.tw_close))

    estimate_temporal_workload, tmod = _load_temporal_submodules(root)

    wl = estimate_temporal_workload(yaml_path, start_d, end_d, active, daily_override)

    print("store_yaml:", yaml_path)
    print("preset:", args.preset, "resolved_dates:", start_d, "..", end_d)
    print("workload:", wl)

    if args.native_stack_hint:
        print(
            "\n--- Native stack (hot CPU / suspected C++ hang) ---\n"
            "Windows: attach Visual Studio to python.exe -> Debug -> Break All -> copy stacks.\n"
            "Or: pip install py-spy  then  py-spy dump --pid <pid>\n"
            "If CPU is idle but the job never finishes, suspect queue/deadlock/I/O rather than tight loops.\n"
        )

    if args.run_verbose:
        SimulationRangeConfig = tmod.SimulationRangeConfig
        run_temporal_simulation = tmod.run_temporal_simulation

        sim_days_count = wl.get("sim_days") or 0
        if sim_days_count > 3:
            print(
                f"ERROR: refusing --run-verbose with sim_days={sim_days_count}; narrow the date range.",
                file=sys.stderr,
            )
            return 2

        cfg = SimulationRangeConfig(
            store_yaml=yaml_path,
            start_date=start_d,
            end_date=end_d,
            daily_window_override=daily_override,
            seed=42,
            active_dates=active,
        )
        print("\n--- run_temporal_simulation(verbose=True) ---\n")
        try:
            run_temporal_simulation(cfg, verbose=True)
        except ImportError as exc:
            print(
                "ERROR: could not run the simulator (build `simulation` and set PYTHONPATH).",
                file=sys.stderr,
            )
            print(exc, file=sys.stderr)
            return 3
        print("\n--- done ---")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
