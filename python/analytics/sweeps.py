"""
sweeps.py
=========
Multi-run parameter-sweep engine for PriceRiot.

Executes a Cartesian grid over (spawn_interval, mission_probability,
num_stockers, num_cashiers, price_multiplier) in a ThreadPoolExecutor, taking
advantage of the GIL-released C++ Simulator::run() binding.
"""
from __future__ import annotations

import itertools
import time
from concurrent.futures import ThreadPoolExecutor
from typing import Any, Dict, List, Sequence, Tuple

import numpy as np
import pandas as pd

from .core import queue_metrics_to_frame, run_simulation


def _run_single_point(
    point_index: int,
    store_path: str,
    duration_seconds: float,
    spawn_interval: float,
    mission_probability: float,
    num_stockers: int,
    num_cashiers: int,
    price_multiplier: float,
    seed: int,
    dt: float,
    auto_stock_tasks: bool,
    auto_register_tasks: bool,
) -> Dict[str, Any]:
    t0 = time.monotonic()
    res = run_simulation(
        store_path=store_path,
        duration_seconds=duration_seconds,
        spawn_interval=spawn_interval,
        mission_probability=mission_probability,
        seed=seed,
        dt=dt,
        num_stockers=num_stockers,
        num_cashiers=num_cashiers,
        auto_stock_tasks=auto_stock_tasks,
        auto_register_tasks=auto_register_tasks,
        price_multiplier=price_multiplier if abs(price_multiplier - 1.0) > 1e-6 else None,
    )
    elapsed_wall = time.monotonic() - t0

    tx = res.transactions
    cust = res.customers

    total_customers = int(len(cust))
    total_transactions = (
        int(tx["transaction_id"].nunique())
        if (not tx.empty and "transaction_id" in tx.columns)
        else int(len(tx))
    )
    total_revenue = (
        round(float(pd.to_numeric(tx["item_total"], errors="coerce").fillna(0.0).sum()), 2)
        if (not tx.empty and "item_total" in tx.columns)
        else 0.0
    )
    total_units = (
        int(pd.to_numeric(tx["quantity"], errors="coerce").fillna(0.0).sum())
        if (not tx.empty and "quantity" in tx.columns)
        else 0
    )

    avg_basket_value = round(total_revenue / total_transactions, 2) if total_transactions > 0 else 0.0
    avg_items_per_basket = round(total_units / total_transactions, 2) if total_transactions > 0 else 0.0

    mean_q = 0.0
    max_q = 0
    p95_q = 0.0
    if hasattr(res.simulator, "get_queue_sample_times") and hasattr(
        res.simulator, "get_queue_lengths_history"
    ):
        qdf = queue_metrics_to_frame(
            res.simulator.get_queue_sample_times(),
            res.simulator.get_queue_lengths_history(),
        )
        if not qdf.empty:
            lengths = qdf["queue_length"].to_numpy(dtype=float)
            mean_q = round(float(np.mean(lengths)), 3)
            max_q = int(np.max(lengths))
            p95_q = round(float(np.percentile(lengths, 95)), 3)

    return {
        "point_index": point_index,
        "spawn_interval": float(spawn_interval),
        "mission_probability": float(mission_probability),
        "num_stockers": int(num_stockers),
        "num_cashiers": int(num_cashiers),
        "price_multiplier": float(price_multiplier),
        "seed": int(seed),
        "total_customers": total_customers,
        "total_transactions": total_transactions,
        "total_units": total_units,
        "total_revenue": total_revenue,
        "avg_basket_value": avg_basket_value,
        "avg_items_per_basket": avg_items_per_basket,
        "mean_queue_length": mean_q,
        "max_queue_length": max_q,
        "p95_queue_length": p95_q,
        "elapsed_wall_seconds": round(elapsed_wall, 3),
    }


def run_parameter_sweep(
    store_path: str = "store.yaml",
    duration_seconds: float = 300.0,
    spawn_intervals: Sequence[float] = (5.0,),
    mission_probabilities: Sequence[float] = (0.5,),
    num_stockers_list: Sequence[int] = (2,),
    num_cashiers_list: Sequence[int] = (1,),
    price_multipliers: Sequence[float] = (1.0,),
    seed: int = 42,
    dt: float = 1.0 / 60.0,
    max_threads: int = 4,
    auto_stock_tasks: bool = True,
    auto_register_tasks: bool = True,
) -> Tuple[List[Dict[str, Any]], pd.DataFrame]:
    """
    Evaluate every point in the Cartesian product of the provided parameter grids.

    Returns
    -------
    (rows, df)
        rows : list[dict] sorted by point_index
        df   : comparison pd.DataFrame with one row per grid point
    """
    combos = list(
        itertools.product(
            spawn_intervals,
            mission_probabilities,
            num_stockers_list,
            num_cashiers_list,
            price_multipliers,
        )
    )
    if not combos:
        raise ValueError("Parameter sweep grid is empty.")

    workers = max(1, min(int(max_threads), len(combos)))
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [
            pool.submit(
                _run_single_point,
                idx,
                store_path,
                duration_seconds,
                float(si),
                float(mp),
                int(ns),
                int(nc),
                float(pm),
                int(seed),
                float(dt),
                bool(auto_stock_tasks),
                bool(auto_register_tasks),
            )
            for idx, (si, mp, ns, nc, pm) in enumerate(combos)
        ]
        rows = [f.result() for f in futures]

    rows.sort(key=lambda r: r["point_index"])
    return rows, pd.DataFrame(rows)
