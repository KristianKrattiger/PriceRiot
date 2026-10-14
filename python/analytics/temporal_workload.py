"""
temporal_workload.py
====================
Pure estimate of how much simulated time the temporal engine will run for a given
YAML + date range. Used by the webapp job runner and CLI diagnostics (no FastAPI/Pydantic).
"""
from __future__ import annotations

from datetime import date
from typing import List, Optional

from .temporal import SimulationRangeConfig, TemporalScheduler


def estimate_temporal_workload(
    store_yaml_abs: str,
    start_date: date,
    end_date: date,
    active_dates: Optional[List[date]],
    daily_window_override: Optional[tuple[int, int]] = None,
) -> dict:
    """Return sim_days, total_sim_seconds, approx_step_ticks, severity, etc."""
    cfg = SimulationRangeConfig(
        store_yaml=store_yaml_abs,
        start_date=start_date,
        end_date=end_date,
        daily_window_override=daily_window_override,
        seed=0,
        active_dates=active_dates,
    )
    try:
        scheduler = TemporalScheduler(cfg)
        days = scheduler.get_sim_days()
    except Exception as exc:
        return {"error": str(exc)}

    total_sec = sum(sd.total_duration for sd in days)
    calendar_span = (end_date - start_date).days + 1
    dt = 1.0 / 10.0
    approx_ticks = int(total_sec / dt) if total_sec else 0
    out = {
        "sim_days": len(days),
        "calendar_span_days": calendar_span,
        "total_sim_seconds": round(total_sec, 1),
        "approx_step_ticks": approx_ticks,
        "dt_seconds": dt,
    }
    if approx_ticks > 8_000_000 or len(days) > 60:
        out["severity"] = "very_large"
    elif approx_ticks > 800_000 or len(days) > 14:
        out["severity"] = "large"
    else:
        out["severity"] = "typical"
    return out
