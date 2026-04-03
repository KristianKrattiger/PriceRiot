"""
temporal.py
===========
Temporal simulation engine for PriceRiot.

Bridges the store's operating schedule (from YAML) and the C++ headless
Simulator to produce multi-day, temporally-accurate simulations.  Spawn
intensity varies across four named intraday periods — morning ramp, midday
plateau, evening peak, and close decay — using the same two-peak Gaussian
mixture that drives the synthetic POS generator.

Public API
----------
    from python.analytics.temporal import (
        SimulationRangeConfig,
        SimDay,
        TemporalScheduler,
        run_temporal_simulation,
    )

    cfg = SimulationRangeConfig(
        store_yaml="examples/cowboy_market.yaml",
        start_date=date(2024, 1, 1),
        end_date=date(2024, 1, 14),
        base_spawn_interval=5.0,
        mission_probability=0.5,
        seed=42,
    )
    scheduler = TemporalScheduler(cfg)
    scheduler.print_summary()
    result = run_temporal_simulation(cfg)
"""

from __future__ import annotations

import math
import os
import sys
from dataclasses import dataclass, field
from datetime import date, timedelta
from typing import List, Optional

import pandas as pd
import yaml


# ---------------------------------------------------------------------------
# Intraday distribution (mirrors generate_pos_data.py)
# ---------------------------------------------------------------------------

_PEAKS = [
    {"mu": 11.5, "sigma": 1.2, "weight": 0.40},  # morning/lunch rush
    {"mu": 16.5, "sigma": 1.4, "weight": 0.60},  # after-work rush
]

_PERIOD_BOUNDARIES = [
    ("morning", None,  11.0),   # open_hour → 11:00
    ("midday",  11.0,  14.0),   # 11:00 → 14:00
    ("evening", 14.0,  17.0),   # 14:00 → 17:00
    ("close",   17.0,  None),   # 17:00 → close_hour
]


def _intraday_pdf(hour: float) -> float:
    """Unnormalised two-peak Gaussian PDF at decimal hour *hour*."""
    total = 0.0
    for p in _PEAKS:
        total += p["weight"] * math.exp(-0.5 * ((hour - p["mu"]) / p["sigma"]) ** 2)
    return total


def _period_avg_pdf(start_h: float, end_h: float, steps: int = 60) -> float:
    """Mean PDF value over [start_h, end_h] using *steps* sample points."""
    if end_h <= start_h:
        return 1e-9
    points = [start_h + (end_h - start_h) * i / steps for i in range(steps + 1)]
    return sum(_intraday_pdf(h) for h in points) / len(points)


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

@dataclass
class SpawnPeriod:
    """Named intraday period with a derived spawn interval."""
    name: str             # "morning" | "midday" | "evening" | "close"
    start_offset: float   # seconds from store open for this period
    end_offset: float     # seconds from store open until this period ends
    spawn_interval: float # sim-seconds between customer spawns in this period


@dataclass
class SimDay:
    """All scheduling information for one simulated business day."""
    date: date
    day_of_week: int          # 0=Mon … 6=Sun
    open_hour: int
    close_hour: int
    total_duration: float     # (close_hour - open_hour) * 3600.0 seconds
    spawn_periods: List[SpawnPeriod]


@dataclass
class SimulationRangeConfig:
    """Configuration for a multi-day temporal simulation run."""
    store_yaml: str
    start_date: date
    end_date: date
    # Optional per-run hour override; if set, replaces per-day YAML hours.
    daily_window_override: Optional[tuple[int, int]] = None
    base_spawn_interval: float = 5.0    # baseline interval at average density
    mission_probability: float = 0.5
    seed: int = 42
    dt: float = 1.0 / 60.0             # simulation timestep
    num_stockers: int = 2
    num_cashiers: int = 1
    auto_stock_tasks: bool = True
    auto_register_tasks: bool = True
    # Optional explicit date whitelist (weekday_repeat / custom_days presets).
    # When set, only dates in this set are simulated regardless of range.
    active_dates: Optional[List[date]] = None


# ---------------------------------------------------------------------------
# TemporalScheduler
# ---------------------------------------------------------------------------

class TemporalScheduler:
    """
    Resolves a SimulationRangeConfig into an ordered list of SimDay objects.

    Days not in the store's days_of_operation are automatically skipped.
    If daily_window_override is provided it replaces the per-day YAML hours
    for every open day.
    """

    def __init__(self, config: SimulationRangeConfig) -> None:
        self._cfg = config
        self._day_schedule = self._load_schedule()
        self._sim_days: List[SimDay] = self._build_sim_days()

    # ── Public ────────────────────────────────────────────────────────────

    def get_sim_days(self) -> List[SimDay]:
        """Return the resolved list of SimDay objects in date order."""
        return list(self._sim_days)

    def print_summary(self) -> None:
        """Print a human-readable summary of the scheduled simulation days."""
        cfg = self._cfg
        total_days = (cfg.end_date - cfg.start_date).days + 1
        active = len(self._sim_days)
        skipped = total_days - active

        print(f"\n{'─'*54}")
        print(f"  Temporal Simulation Summary")
        print(f"{'─'*54}")
        print(f"  Store:          {cfg.store_yaml}")
        print(f"  Date range:     {cfg.start_date} → {cfg.end_date}")
        print(f"  Calendar days:  {total_days}  (active: {active}, skipped: {skipped})")
        print(f"  Spawn interval: {cfg.base_spawn_interval:.1f}s (baseline)")
        print(f"  Mission prob:   {cfg.mission_probability:.0%}")
        print(f"  Seed:           {cfg.seed}")
        print(f"{'─'*54}")
        print(f"  {'Date':<12} {'Day':<5} {'Window':<14} {'Periods (interval s)'}")
        print(f"  {'─'*50}")
        _DOW = ["Mon","Tue","Wed","Thu","Fri","Sat","Sun"]
        for sd in self._sim_days:
            periods_str = "  ".join(
                f"{p.name}={p.spawn_interval:.1f}s" for p in sd.spawn_periods
            )
            window = f"{sd.open_hour:02d}:00–{sd.close_hour:02d}:00"
            print(f"  {sd.date!s:<12} {_DOW[sd.day_of_week]:<5} {window:<14} {periods_str}")
        print(f"{'─'*54}\n")

    # ── Internal ──────────────────────────────────────────────────────────

    def _load_schedule(self) -> List[Optional[tuple[int, int]]]:
        """Read days_of_operation / hours_of_operation from the store YAML.
        Returns list[7] where None = closed, (open_h, close_h) = open."""
        yaml_path = self._cfg.store_yaml
        if not os.path.isabs(yaml_path):
            # Resolve relative to project root (walk up from this file).
            root = _find_project_root()
            yaml_path = os.path.join(root, yaml_path)

        if not os.path.isfile(yaml_path):
            raise FileNotFoundError(f"Store YAML not found: {yaml_path}")

        with open(yaml_path, "r", encoding="utf-8") as fh:
            root_node = yaml.safe_load(fh)

        _DAY_ABBREVS = ["Mon","Tue","Wed","Thu","Fri","Sat","Sun"]
        hop = root_node.get("hours_of_operation") or {}
        dop = root_node.get("days_of_operation") or []

        open_days: set[str] = set(dop) if dop else {"Mon","Tue","Wed","Thu","Fri","Sat"}

        schedule: List[Optional[tuple[int, int]]] = [None] * 7
        for i, abbr in enumerate(_DAY_ABBREVS):
            if abbr not in open_days:
                continue
            entry = hop.get(abbr, {}) if isinstance(hop, dict) else {}
            o = int(entry.get("open",  9))
            c = int(entry.get("close", 19))
            if c <= o:
                raise ValueError(
                    f"Invalid hours for {abbr}: close ({c}) must be > open ({o})"
                )
            schedule[i] = (o, c)

        return schedule

    def _build_spawn_periods(self, open_h: int, close_h: int) -> List[SpawnPeriod]:
        """Build 3-4 SpawnPeriod objects whose spawn intervals track the PDF."""
        # Normalise: compute overall average PDF for the open window.
        overall_avg = _period_avg_pdf(open_h, close_h)

        periods: List[SpawnPeriod] = []
        for name, lo, hi in _PERIOD_BOUNDARIES:
            p_start_h = float(open_h) if lo is None else max(float(lo), float(open_h))
            p_end_h   = float(close_h) if hi is None else min(float(hi), float(close_h))
            if p_end_h <= p_start_h:
                continue  # period falls outside this day's window

            avg = _period_avg_pdf(p_start_h, p_end_h)
            relative_intensity = avg / max(overall_avg, 1e-9)
            # More intensity → shorter interval (more customers per sim-second)
            interval = max(0.5, self._cfg.base_spawn_interval / relative_intensity)

            start_offset = (p_start_h - open_h) * 3600.0
            end_offset   = (p_end_h   - open_h) * 3600.0

            periods.append(SpawnPeriod(
                name=name,
                start_offset=start_offset,
                end_offset=end_offset,
                spawn_interval=round(interval, 2),
            ))

        return periods

    def _build_sim_days(self) -> List[SimDay]:
        cfg = self._cfg
        days: List[SimDay] = []
        active_set = set(cfg.active_dates) if cfg.active_dates else None
        current = cfg.start_date
        while current <= cfg.end_date:
            if active_set is not None and current not in active_set:
                current += timedelta(days=1)
                continue
            dow = current.weekday()  # 0=Mon … 6=Sun
            window = self._day_schedule[dow]
            if window is not None:
                # Apply override if requested.
                if cfg.daily_window_override is not None:
                    o, c = cfg.daily_window_override
                    if c <= o:
                        raise ValueError(
                            f"daily_window_override: close ({c}) must be > open ({o})"
                        )
                    window = (o, c)
                open_h, close_h = window
                duration = (close_h - open_h) * 3600.0
                periods = self._build_spawn_periods(open_h, close_h)
                days.append(SimDay(
                    date=current,
                    day_of_week=dow,
                    open_hour=open_h,
                    close_hour=close_h,
                    total_duration=duration,
                    spawn_periods=periods,
                ))
            current += timedelta(days=1)
        return days


# ---------------------------------------------------------------------------
# Simulation runner
# ---------------------------------------------------------------------------

@dataclass
class TemporalSimResult:
    """Results from a full temporal (multi-day) simulation run."""
    transactions: pd.DataFrame
    sim_days: List[SimDay]
    config: SimulationRangeConfig
    day_summaries: List[dict]
    # Accumulated across all simulated days (for KPI computation)
    cell_heatmap: List[List[int]] = field(default_factory=list)
    queue_data: List[dict]        = field(default_factory=list)   # {lane_index, time_s, queue_length}
    worker_timeseries: List[dict] = field(default_factory=list)   # {time, worker_id, task_efficiency}
    # Raw queue stats for KPI computation (not sent to frontend)
    _queue_lengths_all: List[int] = field(default_factory=list)


def run_temporal_simulation(
    config: SimulationRangeConfig,
    *,
    verbose: bool = True,
) -> TemporalSimResult:
    """
    Run a temporally-scheduled headless simulation.

    For each active SimDay the Simulator is reset, then stepped through the
    day's intraday periods.  Spawn intensity (via setSpawnInterval) varies
    across four named periods derived from the two-peak intraday distribution.

    Parameters
    ----------
    config:
        Full simulation configuration including date range and store YAML path.
    verbose:
        If True, print progress per day.

    Returns
    -------
    TemporalSimResult
    """
    # Lazy import so this module doesn't drag in simulation at import time.
    from python.analytics.core import _resolve_store_path  # type: ignore[import]
    try:
        import simulation as _sim_mod  # type: ignore[import]
    except ImportError:
        # Try adding build dirs to path.
        root = _find_project_root()
        for d in (os.path.join(root, "build"), os.path.join(root, "build", "Release")):
            if os.path.isdir(d) and d not in sys.path:
                sys.path.insert(0, d)
        import simulation as _sim_mod  # type: ignore[import]

    scheduler = TemporalScheduler(config)
    sim_days = scheduler.get_sim_days()

    if not sim_days:
        raise ValueError("No active simulation days in the requested range.")

    if verbose:
        scheduler.print_summary()

    store_abs = config.store_yaml
    if not os.path.isabs(store_abs):
        root = _find_project_root()
        store_abs = os.path.join(root, store_abs)

    sim = _sim_mod.Simulator(
        store_abs,
        spawn_interval=config.base_spawn_interval,
        mission_probability=config.mission_probability,
        seed=config.seed,
    )
    if hasattr(sim, "set_worker_config"):
        sim.set_worker_config(
            num_stockers=config.num_stockers,
            num_cashiers=config.num_cashiers,
            auto_stock_tasks=config.auto_stock_tasks,
            auto_register_tasks=config.auto_register_tasks,
        )

    all_transactions: list[dict] = []
    day_summaries: list[dict] = []

    acc_heatmap: List[List[int]] = []
    acc_queue_data: List[dict]   = []
    acc_worker_ts: List[dict]    = []
    acc_queue_raw: List[int]     = []   # flat list of all queue lengths for KPI stats
    accumulated_time: float      = 0.0

    for sd in sim_days:
        sim.reset()
        sim.set_seed(config.seed ^ hash(sd.date) & 0xFFFFFFFF)

        if verbose:
            _DOW = ["Mon","Tue","Wed","Thu","Fri","Sat","Sun"]
            print(f"  [{_DOW[sd.day_of_week]} {sd.date}] simulating "
                  f"{sd.open_hour:02d}:00–{sd.close_hour:02d}:00 "
                  f"({len(sd.spawn_periods)} periods) ...", end=" ", flush=True)

        # Step through each intraday period, adjusting spawn interval.
        for period in sd.spawn_periods:
            sim.set_spawn_interval(period.spawn_interval)
            period_duration = period.end_offset - period.start_offset
            sim.run(period_duration, config.dt)

        # ── Capture per-day sim metrics before reset ───────────────────────
        # Cell heatmap (accumulate by adding counts)
        raw_hm = sim.get_cell_heatmap()
        for ei, cells in enumerate(raw_hm):
            while len(acc_heatmap) <= ei:
                acc_heatmap.append([])
            if not acc_heatmap[ei]:
                acc_heatmap[ei] = list(cells)
            else:
                for ci, c in enumerate(cells):
                    if ci < len(acc_heatmap[ei]):
                        acc_heatmap[ei][ci] += c
                    else:
                        acc_heatmap[ei].append(c)

        # Queue data — downsample to ~200 points per day for charting
        q_times   = sim.get_queue_sample_times()
        q_lengths = sim.get_queue_lengths_history()  # list[list[int]], outer=lane
        step = max(1, len(q_times) // 200)
        for lane_idx, lane_samples in enumerate(q_lengths):
            acc_queue_raw.extend(lane_samples)
            for i in range(0, len(lane_samples), step):
                if i < len(q_times):
                    acc_queue_data.append({
                        "lane_index":   lane_idx,
                        "time_s":       accumulated_time + q_times[i],
                        "queue_length": lane_samples[i],
                    })

        # Worker mood samples
        for sample in sim.get_worker_mood_samples():
            acc_worker_ts.append({
                "time":            accumulated_time + sample["time"],
                "worker_id":       sample["worker_id"],
                "task_efficiency": sample["task_efficiency"],
            })

        accumulated_time += sd.total_duration

        # Harvest completed transactions, tag with date.
        tx_before = len(all_transactions)
        for tx in sim.get_transactions():
            d = tx.to_dict()
            d["sim_date"]      = sd.date.isoformat()
            d["day_of_week"]   = _DOW[sd.day_of_week] if verbose else sd.day_of_week
            for item in tx.items():
                row = {**d, **item.to_dict()}
                all_transactions.append(row)

        tx_count = sim.transaction_count()
        revenue  = sum(
            r.get("item_total", 0.0)
            for r in all_transactions[tx_before:]
        )

        day_summaries.append({
            "date":          sd.date.isoformat(),
            "day_of_week":   sd.day_of_week,
            "transactions":  tx_count,
            "revenue":       round(revenue, 2),
            "open_hour":     sd.open_hour,
            "close_hour":    sd.close_hour,
        })

        if verbose:
            print(f"✓  {tx_count} txns, £{revenue:,.2f}")

    df = pd.DataFrame(all_transactions)
    return TemporalSimResult(
        transactions=df,
        sim_days=sim_days,
        config=config,
        day_summaries=day_summaries,
        cell_heatmap=acc_heatmap,
        queue_data=acc_queue_data,
        worker_timeseries=acc_worker_ts,
        _queue_lengths_all=acc_queue_raw,
    )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _find_project_root() -> str:
    cur = os.path.dirname(os.path.abspath(__file__))
    for _ in range(6):
        if os.path.exists(os.path.join(cur, ".git")) or \
           os.path.exists(os.path.join(cur, "CMakeLists.txt")):
            return cur
        cur = os.path.dirname(cur)
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
