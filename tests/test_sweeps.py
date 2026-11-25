"""
Unit tests for python/analytics/sweeps.py multi-run parameter sweeps.
"""
from __future__ import annotations

import os
import sys

import pytest

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for _p in (
    _ROOT,
    os.path.join(_ROOT, "python"),
    os.path.join(_ROOT, "build"),
    os.path.join(_ROOT, "build", "Release"),
):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.insert(0, _p)

from python.analytics.sweeps import run_parameter_sweep  # noqa: E402

COWBOY_YAML = os.path.join(_ROOT, "examples", "cowboy_market.yaml")


def test_parameter_sweep_cartesian_grid():
    rows, df = run_parameter_sweep(
        store_path=COWBOY_YAML,
        duration_seconds=60.0,
        spawn_intervals=[10.0, 20.0],
        mission_probabilities=[0.2, 0.8],
        num_stockers_list=[1],
        num_cashiers_list=[1],
        price_multipliers=[1.0],
        seed=42,
        dt=1.0 / 30.0,
        max_threads=2,
    )
    assert len(rows) == 4
    assert len(df) == 4
    assert [r["point_index"] for r in rows] == [0, 1, 2, 3]
    for col in (
        "spawn_interval",
        "mission_probability",
        "num_stockers",
        "num_cashiers",
        "price_multiplier",
        "total_customers",
        "total_transactions",
        "total_revenue",
        "mean_queue_length",
    ):
        assert col in df.columns


def test_parameter_sweep_empty_grid_raises():
    with pytest.raises(ValueError):
        run_parameter_sweep(
            store_path=COWBOY_YAML,
            duration_seconds=30.0,
            spawn_intervals=[],
        )
