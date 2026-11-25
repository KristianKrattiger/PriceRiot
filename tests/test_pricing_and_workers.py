"""
Engine unit & integration tests for dynamic pricing hooks, price elasticity,
worker pool / task lifecycle, and deterministic seeding.
"""
from __future__ import annotations

import os
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for _p in (
    _ROOT,
    os.path.join(_ROOT, "python"),
    os.path.join(_ROOT, "build"),
    os.path.join(_ROOT, "build", "Release"),
):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.insert(0, _p)

import simulation  # noqa: E402

COWBOY_YAML = os.path.join(_ROOT, "examples", "cowboy_market.yaml")


def test_pricing_api_get_set_and_validation():
    sim = simulation.Simulator(COWBOY_YAML, spawn_interval=10.0, mission_probability=0.5, seed=42)
    prices = sim.get_sku_prices()
    assert isinstance(prices, dict)
    assert len(prices) == 30
    assert 1 in prices and prices[1] > 0.0

    orig = sim.get_sku_price(1)
    assert abs(orig - prices[1]) < 1e-6

    # Valid price update
    assert sim.set_sku_price(1, orig * 2.0) is True
    assert abs(sim.get_sku_price(1) - orig * 2.0) < 1e-6

    # Reject non-positive prices and unknown SKUs
    assert sim.set_sku_price(1, 0.0) is False
    assert sim.set_sku_price(1, -5.0) is False
    assert sim.set_sku_price(99999, 10.0) is False
    assert sim.get_sku_price(99999) == 0.0

    # reset() restores catalog base prices
    sim.reset()
    assert abs(sim.get_sku_price(1) - orig) < 1e-6


def test_sku_sell_through_tracks_sales():
    sim = simulation.Simulator(COWBOY_YAML, spawn_interval=8.0, mission_probability=0.5, seed=42)
    sim.run(600.0, dt=1.0 / 30.0)

    sell_through = sim.get_sku_sell_through()
    assert isinstance(sell_through, list)
    assert len(sell_through) == 30

    total_units = sum(row["units_sold"] for row in sell_through)
    total_rev = sum(row["revenue"] for row in sell_through)
    assert total_units > 0
    assert total_rev > 0.0

    for row in sell_through:
        assert row["units_sold"] >= 0
        assert row["revenue"] >= 0.0
        assert row["base_price"] > 0.0
        assert row["current_price"] > 0.0
        assert row["sell_through_per_hour"] >= 0.0

    # Reset clears sell-through counters
    sim.reset()
    assert sum(row["units_sold"] for row in sim.get_sku_sell_through()) == 0


def test_price_hike_reduces_demand_monotonically():
    """Raising prices on target SKUs 1..5 reduces their unit sales compared to baseline."""
    target_skus = [1, 2, 3, 4, 5]

    def _units_for_multiplier(mult: float) -> int:
        total = 0
        for seed in (101, 102, 103):
            sim = simulation.Simulator(
                COWBOY_YAML, spawn_interval=10.0, mission_probability=0.5, seed=seed
            )
            base_prices = sim.get_sku_prices()
            for sku in target_skus:
                sim.set_sku_price(sku, base_prices[sku] * mult)
            sim.run(900.0, dt=1.0 / 30.0)
            st = sim.get_sku_sell_through()
            total += sum(r["units_sold"] for r in st if r["sku"] in target_skus)
        return total

    units_base = _units_for_multiplier(1.0)
    units_2x = _units_for_multiplier(2.0)
    units_4x = _units_for_multiplier(4.0)

    assert units_base > 0
    assert units_base > units_2x > units_4x, (
        f"Expected monotonic demand drop on price hikes, got 1x={units_base}, 2x={units_2x}, 4x={units_4x}"
    )


def test_worker_config_movement_and_mood_samples():
    sim = simulation.Simulator(COWBOY_YAML, spawn_interval=5.0, mission_probability=0.4, seed=7)
    sim.set_worker_config(num_stockers=3, num_cashiers=2, auto_stock_tasks=True, auto_register_tasks=True)

    initial_workers = sim.get_workers()
    assert len(initial_workers) == 5
    stockers = [w for w in initial_workers if w["can_stock"] and not w["can_serve"]]
    cashiers = [w for w in initial_workers if w["can_serve"] and not w["can_stock"]]
    assert len(stockers) == 3
    assert len(cashiers) == 2

    initial_positions = {w["id"]: (w["pos_x"], w["pos_z"]) for w in initial_workers}

    sim.run(120.0, dt=1.0 / 30.0)
    final_workers = sim.get_workers()
    moved = any(
        abs(w["pos_x"] - initial_positions[w["id"]][0]) > 0.1
        or abs(w["pos_z"] - initial_positions[w["id"]][1]) > 0.1
        for w in final_workers
    )
    assert moved, "Expected at least one worker to move spatially during 120s run"

    moods = sim.get_worker_mood_samples()
    assert len(moods) > 0
    assert all(0.5 <= m["task_efficiency"] <= 1.2 for m in moods)


def test_deterministic_seed_reproducibility():
    def _run():
        sim = simulation.Simulator(COWBOY_YAML, spawn_interval=10.0, mission_probability=0.5, seed=99)
        sim.set_worker_config(num_stockers=2, num_cashiers=1, auto_stock_tasks=True, auto_register_tasks=True)
        sim.run(300.0, dt=1.0 / 30.0)
        return (
            sim.transaction_count(),
            [(t.trans_id(), t.cust_id(), round(t.total_spent(), 2)) for t in sim.get_transactions()],
        )

    assert _run() == _run()
