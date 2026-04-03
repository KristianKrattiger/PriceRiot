from __future__ import annotations

import asyncio
import json
import os
import sys
from datetime import datetime, timezone
from itertools import combinations
from typing import AsyncGenerator, Dict, Any, List

import pandas as pd

_script_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _script_dir not in sys.path:
    sys.path.insert(0, _script_dir)

from analytics import SimulationResult, cell_heatmap_to_frame, queue_metrics_to_frame, run_simulation
from ingestion.param_extractor import extract_params

from .models import RunStatus
from .storage import session_store


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def _load_sku_names(product_csv: str | None) -> Dict[str, str]:
    """Return a sku→name mapping from a product CSV, or an empty dict."""
    paths_to_try: list[str] = []
    if product_csv and os.path.isfile(product_csv):
        paths_to_try.append(product_csv)
    # Fallback: repo-default products CSV
    default_path = os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        "data", "raw", "products.csv",
    )
    if os.path.isfile(default_path):
        paths_to_try.append(default_path)
    for path in paths_to_try:
        try:
            df = pd.read_csv(path, dtype=str)
            if "sku" in df.columns and "name" in df.columns:
                return dict(zip(df["sku"].str.strip(), df["name"].str.strip()))
        except Exception:
            pass
    return {}


def _compute_sku_breakdown(
    result: SimulationResult, sku_names: Dict[str, str] | None = None
) -> List[Dict[str, Any]]:
    """Return a list of per-SKU {sku, name, quantity, revenue} dicts, sorted by revenue desc."""
    tx = result.transactions
    if tx.empty or "item_id" not in tx.columns:
        return []
    lookup = sku_names or {}
    rows: List[Dict[str, Any]] = []

    qty_series = pd.to_numeric(tx.get("quantity", pd.Series(dtype=float)), errors="coerce").fillna(0.0)
    rev_series = pd.to_numeric(tx.get("item_total", pd.Series(dtype=float)), errors="coerce").fillna(0.0)
    tx2 = tx.assign(_qty=qty_series, _rev=rev_series)

    grouped_qty = tx2.groupby("item_id")["_qty"].sum() if "quantity" in tx.columns else pd.Series(dtype=float)
    grouped_rev = tx2.groupby("item_id")["_rev"].sum() if "item_total" in tx.columns else pd.Series(dtype=float)

    all_skus = set(grouped_qty.index.tolist()) | set(grouped_rev.index.tolist())
    for sku in all_skus:
        sku_str = str(sku)
        rows.append({
            "sku": sku_str,
            "name": lookup.get(sku_str, sku_str),
            "quantity": float(grouped_qty.get(sku, 0.0)),
            "revenue": round(float(grouped_rev.get(sku, 0.0)), 2),
        })

    rows.sort(key=lambda r: r["revenue"], reverse=True)
    return rows


def _compute_kpis(result: SimulationResult, sku_names: Dict[str, str] | None = None) -> Dict[str, Any]:
    tx = result.transactions
    cust = result.customers

    # Total transactions and customers
    total_transactions = (
        len(tx["transaction_id"].unique()) if "transaction_id" in tx else len(tx)
    )
    total_customers = len(cust)

    # Average basket value:
    # 1) Prefer per-transaction total_spent if available.
    # 2) Fallback to summing item_total per transaction.
    avg_basket_value = 0.0
    if total_transactions > 0:
        totals_series = None
        if "total_spent" in tx.columns:
            totals = pd.to_numeric(tx["total_spent"], errors="coerce").fillna(0.0)
            # total_spent is per transaction; take first per transaction_id if present
            if "transaction_id" in tx.columns:
                totals_series = (
                    tx.assign(_total_spent=totals)
                    .drop_duplicates("transaction_id")
                    .set_index("transaction_id")["_total_spent"]
                )
            else:
                totals_series = totals
        elif "item_total" in tx.columns and "transaction_id" in tx.columns:
            item_totals = pd.to_numeric(tx["item_total"], errors="coerce").fillna(0.0)
            totals_series = (
                tx.assign(_item_total=item_totals)
                .groupby("transaction_id")["_item_total"]
                .sum()
            )

        if totals_series is not None and len(totals_series) > 0:
            avg_basket_value = float(totals_series.mean())

    # Average items per basket: sum(quantity) per transaction / transaction_count
    avg_items_per_basket = 0.0
    if total_transactions > 0 and "quantity" in tx.columns and "transaction_id" in tx.columns:
        quantities = pd.to_numeric(tx["quantity"], errors="coerce").fillna(0.0)
        per_tx_items = (
            tx.assign(_quantity=quantities)
            .groupby("transaction_id")["_quantity"]
            .sum()
        )
        if len(per_tx_items) > 0:
            avg_items_per_basket = float(per_tx_items.mean())

    total_revenue = 0.0
    if "item_total" in tx.columns and not tx.empty:
        total_revenue = round(float(pd.to_numeric(tx["item_total"], errors="coerce").fillna(0.0).sum()), 2)

    kpis: Dict[str, Any] = {
        "total_customers": int(total_customers),
        "total_transactions": int(total_transactions),
        "avg_basket_value": avg_basket_value,
        "avg_items_per_basket": avg_items_per_basket,
        "total_revenue": total_revenue,
    }

    if "dwell_time" in cust.columns:
        kpis["avg_dwell_time"] = float(cust["dwell_time"].mean())

    # Per-SKU breakdowns
    if "item_id" in tx.columns and not tx.empty:
        id_col = tx["item_id"].dropna()
        if not id_col.empty:
            lookup = sku_names or {}
            if "quantity" in tx.columns:
                qty = pd.to_numeric(tx["quantity"], errors="coerce").fillna(0.0)
                sku_qty = tx.assign(_qty=qty).groupby("item_id")["_qty"].sum()
                if not sku_qty.empty:
                    sku_id = str(sku_qty.idxmax())
                    kpis["most_bought_product"] = lookup.get(sku_id, sku_id)

            if "item_total" in tx.columns:
                rev = pd.to_numeric(tx["item_total"], errors="coerce").fillna(0.0)
                sku_rev = tx.assign(_rev=rev).groupby("item_id")["_rev"].sum()
                if not sku_rev.empty:
                    sku_id = str(sku_rev.idxmax())
                    kpis["highest_revenue_product"] = lookup.get(sku_id, sku_id)

    # Top co-purchased SKU pairs/triples (within the same transaction)
    if (
        "item_id" in tx.columns
        and "transaction_id" in tx.columns
        and not tx.empty
    ):
        pair_counts: Dict[tuple, int] = {}
        for _tid, group in tx.groupby("transaction_id")["item_id"]:
            skus = [s for s in group.dropna().tolist() if s]
            for r in (2, 3):
                for combo in combinations(sorted(set(skus)), r):
                    pair_counts[combo] = pair_counts.get(combo, 0) + 1

        top_pairs: List[Dict[str, Any]] = sorted(
            [{"items": list(k), "count": v} for k, v in pair_counts.items()],
            key=lambda x: x["count"],
            reverse=True,
        )[:5]
        kpis["top_sku_pairs"] = top_pairs

    return kpis


class SimulationRunner:
    """High-level orchestrator that runs simulations and streams progress over SSE."""

    async def run_and_stream(self, run_id: str) -> AsyncGenerator[str, None]:
        run = await session_store.async_get_run(run_id)
        if run is None:
            yield self._sse_event(
                "error",
                {"message": f"Run {run_id} not found"},
            )
            return

        # Guard against EventSource auto-reconnects re-running a finished run.
        if run.status in (RunStatus.COMPLETED, RunStatus.FAILED):
            yield self._sse_event("complete", {"run_id": run_id})
            return

        await session_store.async_update_run(
            run_id,
            status=RunStatus.RUNNING,
            started_at=_now_iso(),
        )

        yield self._sse_event(
            "progress",
            {"percent": 0, "message": "Starting simulation..."},
        )

        try:
            config = run.config

            # Extract spawn interval from POS data when available.
            # Only override if the user left spawn_interval at its default (5.0).
            ingestion_profile: Dict[str, Any] | None = None
            if config.pos_data and os.path.isfile(config.pos_data):
                try:
                    ingestion_profile = extract_params(config.pos_data)
                    if config.spawn_interval == 5.0:
                        config = config.copy(
                            update={"spawn_interval": ingestion_profile["spawn_interval_seconds"]}
                        )
                except Exception:
                    pass  # Non-fatal: proceed with user-supplied values

            sim_result: SimulationResult = await asyncio.to_thread(
                run_simulation,
                store_path=config.store_yaml,
                duration_seconds=config.duration_seconds,
                spawn_interval=config.spawn_interval,
                mission_probability=config.mission_probability,
                seed=config.random_seed,
                num_stockers=config.num_stockers,
                num_cashiers=config.num_cashiers,
                auto_stock_tasks=config.auto_stock_tasks,
                auto_register_tasks=config.auto_register_tasks,
            )

            transactions_csv = sim_result.transactions.to_csv(index=False)
            customers_csv = sim_result.customers.to_csv(index=False)

            heatmap_df = cell_heatmap_to_frame(sim_result.simulator.get_cell_heatmap())
            heatmap_data = heatmap_df.to_dict(orient="records")

            # Aggregate traffic data per edge for lightweight frontend consumption.
            edge_totals = heatmap_df.groupby("edge_index")["visits"].sum().reset_index()
            traffic_edges = edge_totals.to_dict(orient="records")
            traffic_kpis: Dict[str, Any] = {}
            if not edge_totals.empty:
                busiest_edge = edge_totals.sort_values("visits", ascending=False).iloc[0]
                traffic_kpis = {
                    "busiest_edge_index": int(busiest_edge["edge_index"]),
                    "busiest_edge_visits": int(busiest_edge["visits"]),
                    "total_traffic_visits": int(heatmap_df["visits"].sum()),
                }

            # Queue metrics are optional depending on the simulator version.
            queue_data = []
            queue_kpis: Dict[str, Any] = {}
            if hasattr(sim_result.simulator, "get_queue_sample_times") and hasattr(
                sim_result.simulator, "get_queue_lengths_history"
            ):
                queue_df = queue_metrics_to_frame(
                    sim_result.simulator.get_queue_sample_times(),
                    sim_result.simulator.get_queue_lengths_history(),
                )
                queue_data = queue_df.to_dict(orient="records")
                if not queue_df.empty:
                    mean_len = float(queue_df["queue_length"].mean())
                    max_len = int(queue_df["queue_length"].max())
                    p95_len = float(queue_df["queue_length"].quantile(0.95))
                    queue_kpis = {
                        "mean_queue_length": mean_len,
                        "max_queue_length": max_len,
                        "p95_queue_length": p95_len,
                    }

            sku_names = _load_sku_names(config.product_csv)
            kpis = _compute_kpis(sim_result, sku_names)
            sku_breakdown = _compute_sku_breakdown(sim_result, sku_names)
            if queue_kpis:
                kpis.update(queue_kpis)
            if traffic_kpis:
                kpis.update(traffic_kpis)

            worker_timeseries: List[Dict[str, Any]] = []
            if hasattr(sim_result.simulator, "get_worker_mood_samples"):
                worker_timeseries = list(sim_result.simulator.get_worker_mood_samples())

            workers_list: List[Dict[str, Any]] | None = None
            if hasattr(sim_result.simulator, "get_workers"):
                workers_list = []
                for w in sim_result.simulator.get_workers():
                    entry = dict(w)
                    if entry.get("current_task") is not None:
                        task = dict(entry["current_task"])
                        task["type"] = str(task["type"]).split(".")[-1]
                        entry["current_task"] = task
                    workers_list.append(entry)

            # Aggregate worker summary KPIs from the final snapshot.
            if workers_list:
                kpis["total_workers"] = len(workers_list)
                kpis["stocker_count"] = sum(1 for w in workers_list if w.get("can_stock") and not w.get("can_serve"))
                kpis["cashier_count"] = sum(1 for w in workers_list if w.get("can_serve") and not w.get("can_stock"))
                efficiency_vals = [w["task_efficiency"] for w in workers_list if "task_efficiency" in w]
                if efficiency_vals:
                    kpis["avg_efficiency"] = round(sum(efficiency_vals) / len(efficiency_vals), 4)

            await session_store.async_update_run(
                run_id,
                status=RunStatus.COMPLETED,
                completed_at=_now_iso(),
                transactions_csv=transactions_csv,
                customers_csv=customers_csv,
                heatmap_data=heatmap_data,
                queue_data=queue_data,
                traffic_edges=traffic_edges,
                kpis=kpis,
                workers=workers_list,
                ingestion_profile=ingestion_profile,
                sku_breakdown=sku_breakdown,
                worker_timeseries=worker_timeseries if worker_timeseries else None,
            )

            yield self._sse_event(
                "progress",
                {"percent": 100, "message": "Simulation complete"},
            )
            yield self._sse_event(
                "complete",
                {"run_id": run_id},
            )
        except Exception as exc:
            await session_store.async_update_run(
                run_id,
                status=RunStatus.FAILED,
                failed_at=_now_iso(),
                error_message=str(exc),
            )
            yield self._sse_event(
                "error",
                {"message": f"Simulation failed: {exc}"},
            )

    @staticmethod
    def _sse_event(event: str, data: Dict[str, Any]) -> str:
        payload = {"event": event, "data": data}
        return f"data: {json.dumps(payload)}\n\n"


simulation_runner = SimulationRunner()

