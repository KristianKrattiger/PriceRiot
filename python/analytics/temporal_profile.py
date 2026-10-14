"""
temporal_profile.py
===================
Empirical hour-of-day (and day-of-week) transaction intensities from POS CSV,
blended with the synthetic two-peak curve used elsewhere in temporal.py.

Collective timing follows the spirit of Guidotti et al., EPJ Data Science 2018
(discovering temporal regularities in retail shopping behaviour).
"""
from __future__ import annotations

import math
import os
from collections import defaultdict
from typing import Dict, List, Tuple

import pandas as pd

# Same mixture as temporal._PEAKS (keep in sync for blending).
_PEAKS = [
    {"mu": 11.5, "sigma": 1.2, "weight": 0.40},
    {"mu": 16.5, "sigma": 1.4, "weight": 0.60},
]

# Weight of synthetic prior when blending (sparse hours pull toward typical retail curve).
_SYNTHETIC_BLEND = 0.25


def _intraday_pdf(hour: float) -> float:
    total = 0.0
    for p in _PEAKS:
        total += p["weight"] * math.exp(-0.5 * ((hour - p["mu"]) / p["sigma"]) ** 2)
    return total


def _synthetic_hour_vector() -> List[float]:
    """24 hourly weights, mean 1.0."""
    raw = [_intraday_pdf(float(h) + 0.5) for h in range(24)]
    m = sum(raw) / 24.0
    if m <= 1e-12:
        return [1.0] * 24
    return [x / m for x in raw]


def _get_project_root() -> str:
    try:
        from analytics.core import get_project_root

        return get_project_root()
    except ImportError:
        cur = os.path.dirname(os.path.abspath(__file__))
        for _ in range(6):
            cur = os.path.dirname(cur)
            if not cur:
                break
            if os.path.exists(os.path.join(cur, ".git")) or os.path.exists(
                os.path.join(cur, "CMakeLists.txt")
            ):
                return cur
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _resolve_path(path: str) -> str:
    if os.path.isabs(path):
        return os.path.normpath(path)
    root = _get_project_root()
    return os.path.normpath(os.path.join(root, path))


def _blend_to_hourly(counts_by_hour: Dict[int, float], synth: List[float], a: float) -> List[float]:
    """Blend empirical hourly counts with synthetic 24-vector; normalize mean to 1.0."""
    nonzero = [counts_by_hour.get(i, 0.0) for i in range(24) if counts_by_hour.get(i, 0.0) > 0]
    if nonzero:
        m_emp = sum(nonzero) / len(nonzero)
    else:
        m_emp = 1.0
    out: List[float] = []
    for h in range(24):
        c = float(counts_by_hour.get(h, 0.0))
        emp_norm = (c / max(m_emp, 1e-9)) if c > 0 else 0.0
        blended = (1.0 - a) * emp_norm + a * synth[h]
        out.append(max(blended, 1e-6))
    m = sum(out) / 24.0
    if m <= 1e-12:
        return [1.0] * 24
    return [x / m for x in out]


def load_blended_hourly_weights(
    pos_csv_path: str,
    *,
    synthetic_blend: float = _SYNTHETIC_BLEND,
) -> Tuple[Dict[int, List[float]], List[float]]:
    """
    Read POS CSV; return (weights_by_dow, pooled_weights).

    Each value is a length-24 list: relative intensity per clock hour, mean ≈ 1.0.

    Blends empirical counts with the synthetic Gaussian mixture so sparse hours
    are stable. DOW keys 0=Mon … 6=Sun only present when that day has data.
    """
    resolved = _resolve_path(pos_csv_path)
    if not os.path.isfile(resolved):
        raise FileNotFoundError(f"POS file not found: {resolved}")

    df = pd.read_csv(resolved)
    date_col = next((c for c in df.columns if c.lower() in ("date", "datetime", "timestamp")), None)
    tx_col = next((c for c in df.columns if "transaction" in c.lower() and "id" in c.lower()), None)

    synth = _synthetic_hour_vector()
    a = float(synthetic_blend)

    if date_col is None or tx_col is None:
        return {}, list(synth)

    df = df.copy()
    df[date_col] = pd.to_datetime(df[date_col], errors="coerce")
    df = df.dropna(subset=[date_col])

    tx_times = (
        df.groupby(tx_col)[date_col]
        .min()
        .reset_index()
        .dropna(subset=[date_col])
    )
    if tx_times.empty:
        return {}, list(synth)

    ts = tx_times[date_col]
    pooled_counts: Dict[int, float] = defaultdict(float)
    dow_counts: Dict[int, Dict[int, float]] = defaultdict(lambda: defaultdict(float))

    for t in ts:
        h = int(t.hour)
        d = int(t.dayofweek)
        pooled_counts[h] += 1.0
        dow_counts[d][h] += 1.0

    by_dow: Dict[int, List[float]] = {}
    for d in range(7):
        if d not in dow_counts or sum(dow_counts[d].values()) < 1:
            continue
        by_dow[d] = _blend_to_hourly(dict(dow_counts[d]), synth, a)

    pooled = _blend_to_hourly(dict(pooled_counts), synth, a)

    return by_dow, pooled


def period_intensity_from_hourly(
    hourly: List[float],
    open_h: int,
    close_h: int,
    p_start_h: float,
    p_end_h: float,
) -> float:
    """Mean relative weight for integer hours in [p_start_h, p_end_h) ∩ [open_h, close_h)."""
    hrs: List[float] = []
    for h in range(open_h, close_h):
        if p_start_h <= h < p_end_h and 0 <= h < len(hourly):
            hrs.append(hourly[h])
    if not hrs:
        return 1.0
    return sum(hrs) / len(hrs)


def overall_hourly_mean(hourly: List[float], open_h: int, close_h: int) -> float:
    slice_h = [hourly[h] for h in range(open_h, close_h) if 0 <= h < len(hourly)]
    if not slice_h:
        return 1.0
    return sum(slice_h) / len(slice_h)
