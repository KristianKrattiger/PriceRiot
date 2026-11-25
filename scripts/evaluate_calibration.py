"""
evaluate_calibration.py
=======================
Reproducible held-out POS calibration & dynamic pricing elasticity evaluation
for PriceRiot (produces Tier-1 committed artifacts under data/eval/).

Experiment A — Held-out POS calibration (synthetic: true):
  - Dataset: data/raw/cowboy_pos.csv (28 calendar days, 2024-01-01..2024-01-28)
  - Train split:    2024-01-01..2024-01-21 (18 open Mon-Sat days)
  - Held-out split: 2024-01-22..2024-01-28 (6 open Mon-Sat days; Sun closed)
  - Fit: python/ingestion/param_extractor.extract_params() on train CSV only
  - Eval: N=5 seeds (42..46) of C++ Simulator via TemporalScheduler on the
    held-out 6 open days using only the train-extracted spawn interval.
  - Metrics: transaction count, daily MAPE, total revenue, mean basket value,
    mean units/basket, intraday period share TVD, SKU mix cosine similarity,
    Jensen-Shannon divergence, and top-10 SKU overlap.

Experiment B — Controlled price-elasticity verification (synthetic: true):
  - Store: examples/cowboy_market.yaml, duration=1800s, spawn_interval=15.0s,
    N=5 seeds (42..46).
  - Target SKUs 1..5 multiplied by [0.75, 1.0, 1.5, 2.0, 3.0] via set_sku_price().
  - Measures unit volume, revenue, control-SKU stability, and arc price elasticity.

Usage:
    PYTHONPATH=build:python:. .venv/bin/python scripts/evaluate_calibration.py
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from datetime import date, datetime, timezone
from typing import Any, Dict, List

import numpy as np
import pandas as pd

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for _p in (
    _ROOT,
    os.path.join(_ROOT, "python"),
    os.path.join(_ROOT, "build"),
    os.path.join(_ROOT, "build", "Release"),
):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.insert(0, _p)

from python.analytics.core import run_simulation  # noqa: E402
from python.analytics.temporal import (  # noqa: E402
    SimulationRangeConfig,
    run_temporal_simulation,
)
from python.ingestion.param_extractor import extract_params  # noqa: E402


PERIOD_NAMES = ("morning", "midday", "evening", "close")


def _file_sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _git_sha() -> str:
    try:
        return (
            subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=_ROOT)
            .decode("utf-8")
            .strip()
        )
    except Exception:
        return "unknown"


def _hour_to_period(hour: float) -> str:
    if hour < 11.0:
        return "morning"
    if hour < 14.0:
        return "midday"
    if hour < 17.0:
        return "evening"
    return "close"


def _ts_to_seconds(ts: str) -> float:
    parts = str(ts).split(":")
    if len(parts) != 3:
        return 0.0
    try:
        return int(parts[0]) * 3600.0 + int(parts[1]) * 60.0 + float(parts[2])
    except ValueError:
        return 0.0


def _cosine_similarity(a: np.ndarray, b: np.ndarray) -> float:
    na = float(np.linalg.norm(a))
    nb = float(np.linalg.norm(b))
    if na == 0.0 or nb == 0.0:
        return 0.0
    return float(np.dot(a, b) / (na * nb))


def _js_divergence_base2(p: np.ndarray, q: np.ndarray) -> float:
    p_sum = float(p.sum())
    q_sum = float(q.sum())
    if p_sum == 0.0 or q_sum == 0.0:
        return 1.0
    p_n = p / p_sum
    q_n = q / q_sum
    m = 0.5 * (p_n + q_n)

    def _kl(x: np.ndarray, y: np.ndarray) -> float:
        mask = x > 0
        return float(np.sum(x[mask] * np.log2(x[mask] / y[mask])))

    return 0.5 * _kl(p_n, m) + 0.5 * _kl(q_n, m)


def _summarize_pos_slice(df: pd.DataFrame, all_skus: List[int]) -> Dict[str, Any]:
    df = df.copy()
    df["_dt"] = pd.to_datetime(df["Date"])
    df["_date"] = df["_dt"].dt.date.astype(str)
    df["_hour"] = df["_dt"].dt.hour + df["_dt"].dt.minute / 60.0

    per_tx = df.groupby("Transaction_ID").agg(
        sim_date=("_date", "first"),
        hour=("_hour", "first"),
        tx_total=("tx_total", "first"),
        units=("quantity", "sum"),
    )
    total_txns = int(len(per_tx))
    total_rev = float(per_tx["tx_total"].sum())
    total_units = int(per_tx["units"].sum())
    avg_basket_value = total_rev / max(1, total_txns)
    avg_items_per_basket = total_units / max(1, total_txns)

    daily_txns = per_tx.groupby("sim_date").size().to_dict()
    daily_rev = per_tx.groupby("sim_date")["tx_total"].sum().round(2).to_dict()

    periods = per_tx["hour"].map(_hour_to_period)
    period_counts = {p: int((periods == p).sum()) for p in PERIOD_NAMES}
    period_shares = {
        p: round(period_counts[p] / max(1, total_txns), 4) for p in PERIOD_NAMES
    }

    sku_units_series = df.groupby("sku")["quantity"].sum()
    sku_units = {int(s): int(sku_units_series.get(s, 0)) for s in all_skus}
    top10_skus = [
        int(s)
        for s, _ in sorted(sku_units.items(), key=lambda kv: kv[1], reverse=True)[:10]
    ]

    return {
        "open_days": int(len(daily_txns)),
        "total_transactions": total_txns,
        "mean_daily_transactions": round(total_txns / max(1, len(daily_txns)), 2),
        "total_revenue": round(total_rev, 2),
        "mean_daily_revenue": round(total_rev / max(1, len(daily_txns)), 2),
        "avg_basket_value": round(avg_basket_value, 2),
        "avg_items_per_basket": round(avg_items_per_basket, 2),
        "daily_transactions": {k: int(v) for k, v in sorted(daily_txns.items())},
        "daily_revenue": {k: float(v) for k, v in sorted(daily_rev.items())},
        "period_counts": period_counts,
        "period_shares": period_shares,
        "sku_units": sku_units,
        "top10_skus": top10_skus,
    }


def _summarize_sim_run(
    sim_res,
    all_skus: List[int],
) -> Dict[str, Any]:
    tx = sim_res.transactions
    open_hour_by_date = {sd.date.isoformat(): sd.open_hour for sd in sim_res.sim_days}

    if tx.empty:
        return {
            "total_transactions": 0,
            "total_revenue": 0.0,
            "avg_basket_value": 0.0,
            "avg_items_per_basket": 0.0,
            "daily_transactions": {},
            "daily_revenue": {},
            "period_shares": {p: 0.0 for p in PERIOD_NAMES},
            "sku_units": {s: 0 for s in all_skus},
            "top10_skus": [],
        }

    tx = tx.copy()
    tx["_tx_key"] = tx["sim_date"].astype(str) + "::" + tx["transaction_id"].astype(str)
    per_tx = tx.groupby("_tx_key").agg(
        sim_date=("sim_date", "first"),
        timestamp=("timestamp", "first"),
        total_spent=("total_spent", "first"),
        units=("quantity", "sum"),
    )
    per_tx["hour"] = [
        open_hour_by_date.get(d, 9) + _ts_to_seconds(ts) / 3600.0
        for d, ts in zip(per_tx["sim_date"], per_tx["timestamp"])
    ]
    total_txns = int(len(per_tx))
    total_rev = float(per_tx["total_spent"].sum())
    total_units = int(per_tx["units"].sum())

    daily_txns = {k: int(v) for k, v in per_tx.groupby("sim_date").size().items()}
    daily_rev = {
        k: round(float(v), 2)
        for k, v in per_tx.groupby("sim_date")["total_spent"].sum().items()
    }

    periods = per_tx["hour"].map(_hour_to_period)
    period_shares = {
        p: float((periods == p).sum()) / max(1, total_txns) for p in PERIOD_NAMES
    }

    sku_series = tx.groupby("item_id")["quantity"].sum()
    sku_units = {int(s): int(sku_series.get(s, 0)) for s in all_skus}
    top10_skus = [
        int(s)
        for s, _ in sorted(sku_units.items(), key=lambda kv: kv[1], reverse=True)[:10]
    ]

    return {
        "total_transactions": total_txns,
        "total_revenue": round(total_rev, 2),
        "avg_basket_value": round(total_rev / max(1, total_txns), 2),
        "avg_items_per_basket": round(total_units / max(1, total_txns), 2),
        "daily_transactions": daily_txns,
        "daily_revenue": daily_rev,
        "period_shares": period_shares,
        "sku_units": sku_units,
        "top10_skus": top10_skus,
    }


def run_calibration_eval(seeds: List[int] = [42, 43, 44, 45, 46]) -> Dict[str, Any]:
    pos_path = os.path.join(_ROOT, "data", "raw", "cowboy_pos.csv")
    products_path = os.path.join(_ROOT, "data", "raw", "cowboy_products.csv")
    store_yaml = os.path.join(_ROOT, "examples", "cowboy_market.yaml")

    pos_df = pd.read_csv(pos_path)
    prod_df = pd.read_csv(products_path)
    all_skus = sorted(prod_df["sku"].astype(int).tolist())

    pos_dates = pd.to_datetime(pos_df["Date"]).dt.date
    train_mask = pos_dates <= date(2024, 1, 21)
    test_mask = pos_dates >= date(2024, 1, 22)

    train_df = pos_df[train_mask].copy()
    test_df = pos_df[test_mask].copy()

    # Fit strictly on train slice via ingestion.param_extractor
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False, encoding="utf-8") as tmp:
        train_df.to_csv(tmp.name, index=False)
        tmp_train_path = tmp.name
    try:
        train_params = extract_params(tmp_train_path)
    finally:
        os.unlink(tmp_train_path)

    train_summary = _summarize_pos_slice(train_df, all_skus)
    test_actual = _summarize_pos_slice(test_df, all_skus)

    # Also compute held-out actuals restricted to Mon-Sat (the 6 open days in cowboy_market.yaml;
    # cowboy_pos.csv contains transactions on Sunday 2024-01-28 whereas cowboy_market.yaml is closed Sun).
    test_dates_dt = pd.to_datetime(test_df["Date"])
    test_open_df = test_df[test_dates_dt.dt.day_of_week < 6].copy()
    test_actual_open = _summarize_pos_slice(test_open_df, all_skus)

    # Simulate held-out 6 open days (2024-01-22..2024-01-27) across N seeds
    def _run_seed(seed: int) -> Dict[str, Any]:
        cfg = SimulationRangeConfig(
            store_yaml=store_yaml,
            start_date=date(2024, 1, 22),
            end_date=date(2024, 1, 28),
            base_spawn_interval=float(train_params["spawn_interval_seconds"]),
            mission_probability=0.5,
            seed=seed,
            dt=1.0,
            num_stockers=2,
            num_cashiers=1,
        )
        res = run_temporal_simulation(cfg, verbose=False)
        s = _summarize_sim_run(res, all_skus)
        s["seed"] = seed
        return s

    with ThreadPoolExecutor(max_workers=min(len(seeds), os.cpu_count() or 4)) as pool:
        per_seed = list(pool.map(_run_seed, seeds))

    # Aggregate simulation results and score against held-out POS actuals
    sim_txns = np.array([r["total_transactions"] for r in per_seed], dtype=float)
    sim_revs = np.array([r["total_revenue"] for r in per_seed], dtype=float)
    sim_bval = np.array([r["avg_basket_value"] for r in per_seed], dtype=float)
    sim_bitm = np.array([r["avg_items_per_basket"] for r in per_seed], dtype=float)

    act_txns = float(test_actual["total_transactions"])
    act_rev = float(test_actual["total_revenue"])
    act_bval = float(test_actual["avg_basket_value"])
    act_bitm = float(test_actual["avg_items_per_basket"])

    # Daily transaction MAPE across all 7 held-out POS days AND across the 6 YAML-open Mon-Sat days
    day_keys_all = sorted(test_actual["daily_transactions"].keys())
    day_keys_open = sorted(test_actual_open["daily_transactions"].keys())
    daily_mapes = []
    daily_mapes_open = []
    for r in per_seed:
        errs = [
            abs(r["daily_transactions"].get(d, 0) - test_actual["daily_transactions"][d])
            / max(1.0, float(test_actual["daily_transactions"][d]))
            for d in day_keys_all
        ]
        daily_mapes.append(float(np.mean(errs)))
        errs_open = [
            abs(r["daily_transactions"].get(d, 0) - test_actual_open["daily_transactions"][d])
            / max(1.0, float(test_actual_open["daily_transactions"][d]))
            for d in day_keys_open
        ]
        daily_mapes_open.append(float(np.mean(errs_open)))

    # Intraday period Total Variation Distance (evaluated on matched open Mon-Sat days)
    act_pvec = np.array([test_actual_open["period_shares"][p] for p in PERIOD_NAMES], dtype=float)
    sim_pvecs = [
        np.array([r["period_shares"][p] for p in PERIOD_NAMES], dtype=float)
        for r in per_seed
    ]
    mean_sim_pvec = np.mean(sim_pvecs, axis=0)
    tvds = [0.5 * float(np.sum(np.abs(v - act_pvec))) for v in sim_pvecs]

    # SKU mix metrics (evaluated on matched open Mon-Sat days)
    act_svec = np.array([test_actual_open["sku_units"][s] for s in all_skus], dtype=float)
    sim_svecs = [
        np.array([r["sku_units"][s] for s in all_skus], dtype=float)
        for r in per_seed
    ]
    cosines = [_cosine_similarity(v, act_svec) for v in sim_svecs]
    js_divs = [_js_divergence_base2(v, act_svec) for v in sim_svecs]
    act_top10 = set(test_actual_open["top10_skus"])
    top10_overlaps = [len(set(r["top10_skus"]) & act_top10) / 10.0 for r in per_seed]

    def _rel_err(sim_val: float, act_val: float) -> float:
        return round((sim_val - act_val) / max(1e-9, act_val) * 100.0, 2)

    act_open_txns = float(test_actual_open["total_transactions"])
    act_open_rev = float(test_actual_open["total_revenue"])
    act_open_bval = float(test_actual_open["avg_basket_value"])
    act_open_bitm = float(test_actual_open["avg_items_per_basket"])

    return {
        "dataset": "data/raw/cowboy_pos.csv",
        "dataset_sha256": _file_sha256(pos_path),
        "store_yaml": "examples/cowboy_market.yaml",
        "synthetic": True,
        "synthetic_note": "cowboy_pos.csv was generated analytically by scripts/generate_pos_data.py",
        "split": {
            "train_range": "2024-01-01..2024-01-21",
            "train_pos_days": train_summary["open_days"],
            "train_transactions": train_summary["total_transactions"],
            "test_range": "2024-01-22..2024-01-28",
            "test_pos_days_all": test_actual["open_days"],
            "test_transactions_all": test_actual["total_transactions"],
            "test_yaml_open_days": test_actual_open["open_days"],
            "test_transactions_yaml_open": test_actual_open["total_transactions"],
            "schedule_mismatch_note": (
                "cowboy_pos.csv includes Sunday 2024-01-28 (7 days/wk), whereas "
                "examples/cowboy_market.yaml declares days_of_operation: [Mon..Sat] (6 days/wk). "
                "Both full-week (7d) and schedule-matched (6d Mon-Sat) denominators are reported."
            ),
        },
        "fitted_params_train_only": train_params,
        "seeds": seeds,
        "n_seeds": len(seeds),
        "heldout_actual_all_7d": {
            "days": test_actual["open_days"],
            "total_transactions": test_actual["total_transactions"],
            "mean_daily_transactions": test_actual["mean_daily_transactions"],
            "total_revenue": test_actual["total_revenue"],
            "avg_basket_value": test_actual["avg_basket_value"],
            "avg_items_per_basket": test_actual["avg_items_per_basket"],
        },
        "heldout_actual_matched_6d": {
            "days": test_actual_open["open_days"],
            "total_transactions": test_actual_open["total_transactions"],
            "mean_daily_transactions": test_actual_open["mean_daily_transactions"],
            "total_revenue": test_actual_open["total_revenue"],
            "avg_basket_value": test_actual_open["avg_basket_value"],
            "avg_items_per_basket": test_actual_open["avg_items_per_basket"],
            "period_shares": test_actual_open["period_shares"],
            "top10_skus": test_actual_open["top10_skus"],
        },
        "simulated_mean_across_seeds": {
            "simulated_open_days": 6,
            "total_transactions_mean": round(float(sim_txns.mean()), 2),
            "total_transactions_std": round(float(sim_txns.std(ddof=1)), 2),
            "total_revenue_mean": round(float(sim_revs.mean()), 2),
            "total_revenue_std": round(float(sim_revs.std(ddof=1)), 2),
            "avg_basket_value_mean": round(float(sim_bval.mean()), 2),
            "avg_basket_value_std": round(float(sim_bval.std(ddof=1)), 2),
            "avg_items_per_basket_mean": round(float(sim_bitm.mean()), 2),
            "avg_items_per_basket_std": round(float(sim_bitm.std(ddof=1)), 2),
            "period_shares_mean": {
                p: round(float(mean_sim_pvec[i]), 4) for i, p in enumerate(PERIOD_NAMES)
            },
        },
        "scores": {
            "matched_6d_total_transactions_rel_error_pct": _rel_err(float(sim_txns.mean()), act_open_txns),
            "matched_6d_daily_transactions_mape_pct": round(float(np.mean(daily_mapes_open)) * 100.0, 2),
            "matched_6d_total_revenue_rel_error_pct": _rel_err(float(sim_revs.mean()), act_open_rev),
            "matched_6d_avg_basket_value_rel_error_pct": _rel_err(float(sim_bval.mean()), act_open_bval),
            "matched_6d_avg_items_per_basket_rel_error_pct": _rel_err(float(sim_bitm.mean()), act_open_bitm),
            "all_7d_total_transactions_rel_error_pct": _rel_err(float(sim_txns.mean()), act_txns),
            "all_7d_daily_transactions_mape_pct": round(float(np.mean(daily_mapes)) * 100.0, 2),
            "all_7d_total_revenue_rel_error_pct": _rel_err(float(sim_revs.mean()), act_rev),
            "intraday_period_tvd_mean": round(float(np.mean(tvds)), 4),
            "sku_mix_cosine_similarity_mean": round(float(np.mean(cosines)), 4),
            "sku_mix_js_divergence_bits_mean": round(float(np.mean(js_divs)), 4),
            "sku_top10_overlap_mean": round(float(np.mean(top10_overlaps)), 4),
        },
        "per_seed": [
            {
                "seed": r["seed"],
                "total_transactions": r["total_transactions"],
                "total_revenue": r["total_revenue"],
                "avg_basket_value": r["avg_basket_value"],
                "avg_items_per_basket": r["avg_items_per_basket"],
            }
            for r in per_seed
        ],
    }


def run_pricing_elasticity_eval(
    seeds: List[int] = [42, 43, 44, 45, 46],
    multipliers: List[float] = [0.75, 1.0, 1.5, 2.0, 3.0],
) -> Dict[str, Any]:
    store_yaml = os.path.join(_ROOT, "examples", "cowboy_market.yaml")
    target_skus = [1, 2, 3, 4, 5]

    # Fetch baseline prices once
    base_res = run_simulation(
        store_path=store_yaml,
        duration_seconds=1.0,
        spawn_interval=60.0,
        seed=42,
        dt=1.0 / 30.0,
    )
    base_prices = base_res.simulator.get_sku_prices()

    def _run_point(mult: float, seed: int) -> Dict[str, Any]:
        overrides = {
            sku: round(base_prices[sku] * mult, 4)
            for sku in target_skus
            if sku in base_prices
        }
        res = run_simulation(
            store_path=store_yaml,
            duration_seconds=1800.0,
            spawn_interval=15.0,
            mission_probability=0.5,
            seed=seed,
            dt=1.0 / 30.0,
            num_stockers=2,
            num_cashiers=1,
            sku_prices=overrides,
        )
        st = res.simulator.get_sku_sell_through()
        target_units = sum(item["units_sold"] for item in st if item["sku"] in target_skus)
        target_rev = sum(item["revenue"] for item in st if item["sku"] in target_skus)
        ctrl_units = sum(item["units_sold"] for item in st if item["sku"] not in target_skus)
        return {
            "multiplier": mult,
            "seed": seed,
            "target_units": int(target_units),
            "target_revenue": round(float(target_rev), 2),
            "control_units": int(ctrl_units),
        }

    jobs = [(m, s) for m in multipliers for s in seeds]
    with ThreadPoolExecutor(max_workers=min(len(jobs), os.cpu_count() or 4)) as pool:
        raw = list(pool.map(lambda p: _run_point(p[0], p[1]), jobs))

    by_mult: List[Dict[str, Any]] = []
    baseline_units = None
    for m in multipliers:
        pts = [r for r in raw if r["multiplier"] == m]
        tu = np.array([p["target_units"] for p in pts], dtype=float)
        tr = np.array([p["target_revenue"] for p in pts], dtype=float)
        cu = np.array([p["control_units"] for p in pts], dtype=float)
        row = {
            "price_multiplier": m,
            "target_units_mean": round(float(tu.mean()), 2),
            "target_units_std": round(float(tu.std(ddof=1)), 2),
            "target_revenue_mean": round(float(tr.mean()), 2),
            "control_units_mean": round(float(cu.mean()), 2),
        }
        if abs(m - 1.0) < 1e-9:
            baseline_units = float(tu.mean())
        by_mult.append(row)

    for row in by_mult:
        m = row["price_multiplier"]
        if baseline_units and baseline_units > 0 and abs(m - 1.0) > 1e-9:
            pct_dq = (row["target_units_mean"] - baseline_units) / baseline_units
            pct_dp = m - 1.0
            row["arc_elasticity_vs_baseline"] = round(pct_dq / pct_dp, 3)
        else:
            row["arc_elasticity_vs_baseline"] = None

    units_seq = [r["target_units_mean"] for r in by_mult]
    monotonic_non_increasing = all(
        units_seq[i] >= units_seq[i + 1] for i in range(len(units_seq) - 1)
    )
    strict_drop_on_hikes = (
        units_seq[multipliers.index(1.0)]
        > units_seq[multipliers.index(1.5)]
        > units_seq[multipliers.index(2.0)]
        > units_seq[multipliers.index(3.0)]
    )

    return {
        "store_yaml": "examples/cowboy_market.yaml",
        "target_skus": target_skus,
        "duration_seconds": 1800.0,
        "spawn_interval": 15.0,
        "mission_probability": 0.5,
        "seeds": seeds,
        "n_seeds": len(seeds),
        "synthetic": True,
        "by_multiplier": by_mult,
        "monotonic_non_increasing": monotonic_non_increasing,
        "strict_drop_on_hikes": strict_drop_on_hikes,
    }


def write_markdown_report(payload: Dict[str, Any], out_md: str) -> None:
    cal = payload["calibration_holdout"]
    pri = payload["pricing_elasticity"]
    sc = cal["scores"]
    act6 = cal["heldout_actual_matched_6d"]
    act7 = cal["heldout_actual_all_7d"]
    sim = cal["simulated_mean_across_seeds"]

    lines = [
        "# PriceRiot Evaluation Report — Held-Out POS Calibration & Price Elasticity",
        "",
        f"- **Date:** {payload['as_of']}",
        f"- **Git SHA:** `{payload['git_sha']}`",
        f"- **System:** `{payload['system']}`",
        f"- **Command:** `{payload['command']}`",
        f"- **Synthetic:** `true` (`cowboy_pos.csv` is generated by `scripts/generate_pos_data.py`)",
        "",
        "---",
        "",
        "## Experiment A — Held-Out POS Calibration (`cowboy_pos.csv`)",
        "",
        f"- **Train split (fit only):** `{cal['split']['train_range']}` "
        f"({cal['split']['train_pos_days']} POS days, {cal['split']['train_transactions']} transactions) "
        f"→ extracted `spawn_interval_seconds = {cal['fitted_params_train_only']['spawn_interval_seconds']}s`",
        f"- **Held-out test split:** `{cal['split']['test_range']}` — "
        f"**{cal['split']['test_yaml_open_days']} schedule-matched Mon–Sat days** ({cal['split']['test_transactions_yaml_open']} actual txns); "
        f"full 7 calendar days incl. Sunday ({cal['split']['test_transactions_all']} actual txns, because `cowboy_pos.csv` has Sunday rows while `cowboy_market.yaml` closes Sundays).",
        f"- **Simulation trials:** `N = {cal['n_seeds']}` seeds (`{cal['seeds']}`), `dt = 1.0s`, "
        f"`examples/cowboy_market.yaml` ({sim['simulated_open_days']} open Mon–Sat days)",
        "",
        "| Metric | Held-out POS (6d Mon–Sat) | Simulated Mean (± SD, N=5) | Relative Error / Score |",
        "|---|---:|---:|---:|",
        f"| Total transactions (6 open days) | {act6['total_transactions']} | "
        f"{sim['total_transactions_mean']} ± {sim['total_transactions_std']} | "
        f"{sc['matched_6d_total_transactions_rel_error_pct']:+.2f}% (daily MAPE {sc['matched_6d_daily_transactions_mape_pct']:.2f}%) |",
        f"| Total transactions (all 7 calendar days incl. Sun) | {act7['total_transactions']} | "
        f"{sim['total_transactions_mean']} ± {sim['total_transactions_std']} | "
        f"{sc['all_7d_total_transactions_rel_error_pct']:+.2f}% (daily MAPE {sc['all_7d_daily_transactions_mape_pct']:.2f}%) |",
        f"| Total revenue (6 open days, $) | ${act6['total_revenue']:,.2f} | "
        f"${sim['total_revenue_mean']:,.2f} ± ${sim['total_revenue_std']:,.2f} | "
        f"{sc['matched_6d_total_revenue_rel_error_pct']:+.2f}% |",
        f"| Mean basket value ($) | ${act6['avg_basket_value']:.2f} | "
        f"${sim['avg_basket_value_mean']:.2f} ± ${sim['avg_basket_value_std']:.2f} | "
        f"{sc['matched_6d_avg_basket_value_rel_error_pct']:+.2f}% |",
        f"| Mean items per basket | {act6['avg_items_per_basket']:.2f} | "
        f"{sim['avg_items_per_basket_mean']:.2f} ± {sim['avg_items_per_basket_std']:.2f} | "
        f"{sc['matched_6d_avg_items_per_basket_rel_error_pct']:+.2f}% |",
        f"| Intraday period share TVD (4 periods) | — | — | `{sc['intraday_period_tvd_mean']:.4f}` (0 = exact) |",
        f"| SKU mix cosine similarity (30 SKUs) | — | — | `{sc['sku_mix_cosine_similarity_mean']:.4f}` (1 = exact) |",
        f"| SKU mix JS divergence (30 SKUs, bits) | — | — | `{sc['sku_mix_js_divergence_bits_mean']:.4f}` (0 = exact) |",
        f"| Top-10 SKU overlap | 10 / 10 | — | `{sc['sku_top10_overlap_mean']*10:.1f} / 10` ({sc['sku_top10_overlap_mean']*100:.0f}%) |",
        "",
        "### Intraday Period Shares (6 Mon–Sat Open Days)",
        "",
        "| Period | Held-out Actual Share | Simulated Mean Share |",
        "|---|---:|---:|",
    ]
    for p in PERIOD_NAMES:
        lines.append(
            f"| {p} | {act6['period_shares'][p]:.3f} | {sim['period_shares_mean'][p]:.3f} |"
        )

    lines += [
        "",
        "### Interpretation & Honest Limitations",
        "",
        "- **What is tested:** Strictly out-of-sample transfer of the POS parameter extractor (`extract_params()`) into the C++ discrete-event `Simulator` via `TemporalScheduler`.",
        "- **Why divergences occur:** `extract_params()` currently transfers only `spawn_interval_seconds` into the engine; it does **not** fit per-SKU popularity, day-of-week multipliers, or basket-size distributions into the C++ shopper state machines. Any gap in basket size, revenue, or SKU mix reflects that uncalibrated model boundary rather than test leakage.",
        "- **Does not show:** Fit to real (non-synthetic) store foot traffic or POS receipts. `cowboy_pos.csv` is synthetic.",
        "",
        "---",
        "",
        "## Experiment B — Controlled Dynamic Pricing Elasticity (`set_sku_price`)",
        "",
        f"- **Target SKUs:** `{pri['target_skus']}` (Cowboy Food aisle) vs control SKUs `6..30`",
        f"- **Config:** `examples/cowboy_market.yaml`, `duration = {pri['duration_seconds']:.0f}s`, "
        f"`spawn_interval = {pri['spawn_interval']}s`, `N = {pri['n_seeds']}` seeds (`{pri['seeds']}`)",
        f"- **Monotonic non-increasing demand:** `{pri['monotonic_non_increasing']}` | "
        f"**Strict drop across markups (1.0x → 1.5x → 2.0x → 3.0x):** `{pri['strict_drop_on_hikes']}`",
        "",
        "| Price Multiplier | Target Units Sold (Mean ± SD) | Target Revenue ($) | Control Units Sold | Implied Arc Elasticity vs 1.0x |",
        "|---:|---:|---:|---:|---:|",
    ]
    for row in pri["by_multiplier"]:
        el = (
            f"{row['arc_elasticity_vs_baseline']:+.3f}"
            if row["arc_elasticity_vs_baseline"] is not None
            else "—"
        )
        lines.append(
            f"| {row['price_multiplier']:.2f}x | "
            f"{row['target_units_mean']:.1f} ± {row['target_units_std']:.1f} | "
            f"${row['target_revenue_mean']:,.2f} | "
            f"{row['control_units_mean']:.1f} | {el} |"
        )

    with open(out_md, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main() -> None:
    out_dir = os.path.join(_ROOT, "data", "eval")
    os.makedirs(out_dir, exist_ok=True)

    print("Running Experiment A: Held-out POS calibration (cowboy_pos.csv, 21d -> 7d, N=5 seeds)...")
    cal = run_calibration_eval()
    print("  -> Calibration scores:", json.dumps(cal["scores"], indent=2))

    print("Running Experiment B: Controlled price elasticity (N=5 seeds x 5 multipliers)...")
    pri = run_pricing_elasticity_eval()
    print("  -> Pricing elasticity monotonic:", pri["monotonic_non_increasing"],
          "strict_drop_on_hikes:", pri["strict_drop_on_hikes"])

    payload = {
        "as_of": datetime.now(timezone.utc).strftime("%Y-%m-%d"),
        "git_sha": _git_sha(),
        "system": "PriceRiot C++17 Simulator + pybind11 + python/ingestion/param_extractor.py + python/analytics/temporal.py",
        "command": "PYTHONPATH=build:python:. .venv/bin/python scripts/evaluate_calibration.py",
        "calibration_holdout": cal,
        "pricing_elasticity": pri,
    }

    out_json = os.path.join(out_dir, "calibration_cowboy_holdout.json")
    out_md = os.path.join(out_dir, "CALIBRATION_REPORT.md")
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
    write_markdown_report(payload, out_md)
    print(f"Wrote {out_json} and {out_md}")


if __name__ == "__main__":
    main()
