from __future__ import annotations

import asyncio
import json
import os
import sys
from datetime import datetime, timezone
from typing import AsyncGenerator, Dict, Any

import pandas as pd

_script_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _script_dir not in sys.path:
    sys.path.insert(0, _script_dir)

from analytics import SimulationResult, cell_heatmap_to_frame, queue_metrics_to_frame, run_simulation

from .models import RunStatus
from .storage import session_store


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def _compute_kpis(result: SimulationResult) -> Dict[str, Any]:
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

    kpis: Dict[str, Any] = {
        "total_customers": int(total_customers),
        "total_transactions": int(total_transactions),
        "avg_basket_value": avg_basket_value,
        "avg_items_per_basket": avg_items_per_basket,
    }

    if "dwell_time" in cust:
        kpis["avg_dwell_time"] = float(cust["dwell_time"].mean())

    return kpis


class SimulationRunner:
    """High-level orchestrator that runs simulations and streams progress over SSE."""

    async def run_and_stream(self, run_id: str) -> AsyncGenerator[str, None]:
        run = session_store.get_run(run_id)
        if run is None:
            yield self._sse_event(
                "error",
                {"message": f"Run {run_id} not found"},
            )
            return

        session_store.update_run(
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
            if hasattr(sim_result.simulator, "get_queue_times") and hasattr(
                sim_result.simulator, "get_queue_lengths"
            ):
                queue_df = queue_metrics_to_frame(
                    sim_result.simulator.get_queue_times(),
                    sim_result.simulator.get_queue_lengths(),
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

            kpis = _compute_kpis(sim_result)
            if queue_kpis:
                kpis.update(queue_kpis)
            if traffic_kpis:
                kpis.update(traffic_kpis)

            session_store.update_run(
                run_id,
                status=RunStatus.COMPLETED,
                completed_at=_now_iso(),
                transactions_csv=transactions_csv,
                customers_csv=customers_csv,
                heatmap_data=heatmap_data,
                queue_data=queue_data,
                traffic_edges=traffic_edges,
                kpis=kpis,
                workers=list(sim_result.simulator.get_workers()) if hasattr(sim_result.simulator, "get_workers") else None,
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
            session_store.update_run(
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

