"""
orchestrator.py
===============
Multi-run simulation orchestrator for PriceRiot.

Wraps the temporal simulation engine (TemporalScheduler / run_temporal_simulation)
in a ``SimRun`` abstraction and a ``RunOrchestrator`` that executes N independent
runs, writes structured outputs, and emits aggregate statistics.

Public API
----------
    from python.analytics.orchestrator import RunOrchestrator, SimulationRangeConfig

    cfg = SimulationRangeConfig(
        store_yaml="examples/cowboy_market.yaml",
        start_date=date(2024, 1, 1),
        end_date=date(2024, 1, 14),
        seed=42,
    )

    orch = RunOrchestrator(cfg, n_runs=5, output_dir="outputs")
    summary = orch.run_all(verbose=True)
    print(summary)
"""

from __future__ import annotations

import json
import logging
import os
import statistics
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict, dataclass
from datetime import date
from typing import List, Optional

import pandas as pd

from python.analytics.temporal import (
    SimulationRangeConfig,
    TemporalSimResult,
    run_temporal_simulation,
)


# ---------------------------------------------------------------------------
# SimRun
# ---------------------------------------------------------------------------

@dataclass
class SimRunOutput:
    """Self-contained result set for one simulation run."""
    run_index: int
    config: SimulationRangeConfig
    result: TemporalSimResult
    output_dir: str
    # Computed after the run
    transaction_count: int = 0
    customer_count: int = 0
    total_revenue: float = 0.0
    peak_period: str = ""
    kpis: dict = None
    sku_breakdown: list = None
    traffic_edges: list = None
    queue_data: list = None
    worker_timeseries: list = None


# ---------------------------------------------------------------------------
# KPI computation helpers
# ---------------------------------------------------------------------------

def _compute_run_kpis(result: TemporalSimResult) -> dict:
    """Derive scalar KPIs from a completed TemporalSimResult."""
    df = result.transactions
    kpis: dict = {}

    if not df.empty:
        if "transaction_id" in df.columns:
            kpis["total_transactions"] = int(df["transaction_id"].nunique())
        if "customer_id" in df.columns:
            kpis["total_customers"] = int(df["customer_id"].nunique())
        if "item_total" in df.columns:
            kpis["total_revenue"] = round(float(df["item_total"].sum()), 2)

        # Basket metrics (one row per unique transaction)
        if "transaction_id" in df.columns and "total_spent" in df.columns:
            per_tx = df.drop_duplicates("transaction_id")["total_spent"]
            kpis["avg_basket_value"] = round(float(per_tx.mean()), 2)
        if "transaction_id" in df.columns:
            kpis["avg_items_per_basket"] = round(
                float(df.groupby("transaction_id").size().mean()), 2
            )
        if "transaction_id" in df.columns and "satisfaction" in df.columns:
            per_tx_sat = df.drop_duplicates("transaction_id")["satisfaction"]
            kpis["avg_satisfaction"] = round(float(per_tx_sat.mean()), 2)

        # Top products
        if "item_name" in df.columns and "quantity" in df.columns:
            qty = df.groupby("item_name")["quantity"].sum()
            kpis["most_bought_product"] = str(qty.idxmax())
        if "item_name" in df.columns and "item_total" in df.columns:
            rev = df.groupby("item_name")["item_total"].sum()
            kpis["highest_revenue_product"] = str(rev.idxmax())

    # Traffic
    if result.cell_heatmap:
        edge_totals = [sum(cells) for cells in result.cell_heatmap]
        kpis["total_traffic_visits"] = int(sum(edge_totals))
        if edge_totals:
            best = max(range(len(edge_totals)), key=lambda i: edge_totals[i])
            kpis["busiest_edge_index"]  = best
            kpis["busiest_edge_visits"] = int(edge_totals[best])

    # Queue stats (computed from full raw sample list)
    raw = result._queue_lengths_all
    if raw:
        kpis["mean_queue_length"] = round(float(sum(raw) / len(raw)), 2)
        kpis["max_queue_length"]  = int(max(raw))
        s = sorted(raw)
        kpis["p95_queue_length"]  = round(float(s[int(0.95 * len(s))]), 2)

    # Worker efficiency
    if result.worker_timeseries:
        effs = [r["task_efficiency"] for r in result.worker_timeseries]
        kpis["mean_worker_efficiency"] = round(float(sum(effs) / len(effs)), 3)

    return kpis


def _compute_sku_breakdown(df) -> list:
    if df.empty or "item_name" not in df.columns:
        return []
    grp = (
        df.groupby("item_name")
        .agg(quantity=("quantity", "sum"), revenue=("item_total", "sum"))
        .reset_index()
        .sort_values("revenue", ascending=False)
    )
    return [
        {"product": row["item_name"], "quantity": int(row["quantity"]), "revenue": round(float(row["revenue"]), 2)}
        for _, row in grp.iterrows()
    ]


def _compute_traffic_edges(cell_heatmap: list) -> list:
    return [
        {"edge_index": i, "total_visits": int(sum(cells))}
        for i, cells in enumerate(cell_heatmap)
        if sum(cells) > 0
    ]


class SimRun:
    """
    Wraps a single temporal simulation run.

    Parameters
    ----------
    config:
        SimulationRangeConfig for this run (already has a per-run seed).
    run_index:
        1-based run index (used in output paths and log prefixes).
    output_dir:
        Root output directory.  Run outputs land in ``{output_dir}/run_{NNN}/``.
    """

    def __init__(
        self,
        config: SimulationRangeConfig,
        run_index: int,
        output_dir: str,
    ) -> None:
        self._config    = config
        self._index     = run_index
        self._base_dir  = output_dir

    @property
    def run_dir(self) -> str:
        return os.path.join(self._base_dir, f"run_{self._index:03d}")

    def execute(self, *, verbose: bool = False) -> SimRunOutput:
        """Run the temporal simulation and persist outputs."""
        os.makedirs(self.run_dir, exist_ok=True)

        result = run_temporal_simulation(self._config, verbose=verbose)

        # Compute rich KPIs from the full result
        kpis         = _compute_run_kpis(result)
        sku_breakdown = _compute_sku_breakdown(result.transactions)
        traffic_edges = _compute_traffic_edges(result.cell_heatmap)

        # Write transactions CSV
        tx_path = os.path.join(self.run_dir, "transactions.csv")
        if not result.transactions.empty:
            result.transactions.to_csv(tx_path, index=False)

        # Compute summary metrics
        tx_count  = int(result.transactions["transaction_id"].nunique()
                        if "transaction_id" in result.transactions.columns
                        else len(result.transactions))
        cust_count = int(result.transactions["customer_id"].nunique()
                         if "customer_id" in result.transactions.columns
                         else 0)
        revenue   = float(result.transactions["item_total"].sum()
                          if "item_total" in result.transactions.columns
                          else 0.0)

        # Peak period: which period name contributed the most revenue across days
        peak_period = ""
        if result.day_summaries:
            # Heuristic: sum transactions by day and label the max period from scheduler
            # (We use the period with highest aggregate intensity from the schedule)
            all_periods: dict[str, float] = {}
            for sd in result.sim_days:
                for sp in sd.spawn_periods:
                    # Lower interval = higher intensity = more revenue
                    intensity = 1.0 / max(sp.spawn_interval, 0.1)
                    all_periods[sp.name] = all_periods.get(sp.name, 0.0) + intensity
            peak_period = max(all_periods, key=all_periods.get) if all_periods else ""

        summary = {
            "run_index":         self._index,
            "seed":              self._config.seed,
            "start_date":        self._config.start_date.isoformat(),
            "end_date":          self._config.end_date.isoformat(),
            "store_yaml":        self._config.store_yaml,
            "transaction_count": tx_count,
            "customer_count":    cust_count,
            "total_revenue":     round(revenue, 2),
            "active_days":       len(result.sim_days),
            "peak_period":       peak_period,
            "day_summaries":     result.day_summaries,
        }
        summary_path = os.path.join(self.run_dir, "summary.json")
        with open(summary_path, "w", encoding="utf-8") as fh:
            json.dump(summary, fh, indent=2)

        return SimRunOutput(
            run_index=self._index,
            config=self._config,
            result=result,
            output_dir=self.run_dir,
            transaction_count=tx_count,
            customer_count=cust_count,
            total_revenue=revenue,
            peak_period=peak_period,
            kpis=kpis,
            sku_breakdown=sku_breakdown,
            traffic_edges=traffic_edges,
            queue_data=result.queue_data,
            worker_timeseries=result.worker_timeseries,
        )


# ---------------------------------------------------------------------------
# Thread-safe per-run logging
# ---------------------------------------------------------------------------

def _make_run_logger(run_index: int, log_path: str) -> logging.Logger:
    """Return a Logger that writes prefixed lines to *log_path*."""
    logger = logging.getLogger(f"priceriot.run_{run_index:03d}")
    logger.setLevel(logging.DEBUG)
    if not logger.handlers:
        fh = logging.FileHandler(log_path, encoding="utf-8")
        fh.setFormatter(logging.Formatter("[run_%(name)s] %(asctime)s %(message)s",
                                          datefmt="%H:%M:%S"))
        logger.addHandler(fh)
    return logger


# ---------------------------------------------------------------------------
# RunOrchestrator
# ---------------------------------------------------------------------------

@dataclass
class AggregateSummary:
    """Statistics aggregated across all completed runs."""
    n_runs: int
    mean_transactions: float
    min_transactions: int
    max_transactions: int
    mean_revenue: float
    min_revenue: float
    max_revenue: float
    mean_customers: float
    peak_period_votes: dict  # period_name -> count of runs that named it peak

    def __str__(self) -> str:
        lines = [
            f"  Runs completed:     {self.n_runs}",
            f"  Transactions — mean: {self.mean_transactions:.1f}  "
            f"min: {self.min_transactions}  max: {self.max_transactions}",
            f"  Revenue      — mean: £{self.mean_revenue:,.2f}  "
            f"min: £{self.min_revenue:,.2f}  max: £{self.max_revenue:,.2f}",
            f"  Customers    — mean: {self.mean_customers:.1f}",
            f"  Peak period votes:  {self.peak_period_votes}",
        ]
        return "\n".join(lines)


class RunOrchestrator:
    """
    Executes N independent SimRun instances and aggregates results.

    When ``max_threads > 1``, runs execute in parallel using a
    ``ThreadPoolExecutor``.  Each ``Simulator`` instance owns all its mutable
    state — there are no shared writes — so parallelism is safe.  The C++
    ``run()`` and ``step()`` bindings release the GIL, giving true CPU
    parallelism.

    Each run uses a seed derived from the base seed: ``seed_i = base_seed + i``.
    This guarantees reproducibility while keeping runs statistically independent.

    Per-run logs are written to ``{output_dir}/run_NNN/run.log`` with every
    line prefixed ``[run_N]``.  Main-thread stdout shows high-level progress.

    Parameters
    ----------
    config:
        Base SimulationRangeConfig.  Per-run seeds override the ``seed`` field.
    n_runs:
        Number of independent simulation runs.
    output_dir:
        Root directory for all run outputs.  Created if absent.
    max_threads:
        Maximum number of concurrent threads.  Defaults to
        ``os.cpu_count()`` (hardware concurrency).  Set to 1 for sequential.
    """

    def __init__(
        self,
        config: SimulationRangeConfig,
        n_runs: int = 1,
        output_dir: str = "outputs",
        max_threads: Optional[int] = None,
    ) -> None:
        if n_runs < 1:
            raise ValueError(f"n_runs must be >= 1, got {n_runs}")
        self._base_config  = config
        self._n_runs       = n_runs
        self._output_dir   = output_dir
        self._max_threads  = max_threads or os.cpu_count() or 1
        # Guards main-thread print statements so parallel output doesn't interleave.
        self._print_lock   = threading.Lock()

    def _make_run_config(self, run_index: int) -> SimulationRangeConfig:
        """Return a config with a per-run seed (base_seed + run_index - 1)."""
        import dataclasses
        return dataclasses.replace(
            self._base_config,
            seed=self._base_config.seed + (run_index - 1),
        )

    def _run_one(self, run_index: int) -> SimRunOutput:
        """Execute a single SimRun; writes its log to run_NNN/run.log."""
        cfg     = self._make_run_config(run_index)
        sim_run = SimRun(cfg, run_index=run_index, output_dir=self._output_dir)
        os.makedirs(sim_run.run_dir, exist_ok=True)

        log_path = os.path.join(sim_run.run_dir, "run.log")
        logger   = _make_run_logger(run_index, log_path)

        logger.info(f"[run_{run_index:03d}] Starting  seed={cfg.seed}  "
                    f"{cfg.start_date} → {cfg.end_date}")

        # Redirect verbose output from run_temporal_simulation into the log file.
        output = sim_run.execute(verbose=False)

        logger.info(f"[run_{run_index:03d}] Completed  "
                    f"txns={output.transaction_count}  "
                    f"rev=£{output.total_revenue:,.2f}  "
                    f"peak={output.peak_period}")

        # Close file handlers explicitly — Windows holds log files open otherwise,
        # which breaks TemporaryDirectory cleanup and parallel-run teardown.
        for h in logger.handlers[:]:
            h.close()
            logger.removeHandler(h)

        return output

    def run_all(self, *, verbose: bool = True) -> "AggregateSummary":
        """
        Execute all N runs (parallel when max_threads > 1) and write outputs.

        Returns
        -------
        AggregateSummary
        """
        os.makedirs(self._output_dir, exist_ok=True)
        effective_threads = min(self._max_threads, self._n_runs)

        if verbose:
            mode = (f"parallel  threads={effective_threads}"
                    if effective_threads > 1 else "sequential")
            with self._print_lock:
                print(f"\n  RunOrchestrator: {self._n_runs} run(s)  [{mode}]")
                print(f"  Output → {self._output_dir}\n")

        outputs: List[SimRunOutput] = [None] * self._n_runs  # type: ignore

        if effective_threads == 1:
            # Sequential path — straightforward, deterministic output order.
            for i in range(1, self._n_runs + 1):
                if verbose:
                    with self._print_lock:
                        print(f"  Starting run {i}/{self._n_runs} ...", flush=True)
                output = self._run_one(i)
                outputs[i - 1] = output
                if verbose:
                    with self._print_lock:
                        print(f"  ✓ run_{i:03d}  "
                              f"{output.transaction_count} txns  "
                              f"£{output.total_revenue:,.2f}  "
                              f"peak={output.peak_period}")
        else:
            # Parallel path — ThreadPoolExecutor.
            futures = {}
            with ThreadPoolExecutor(max_workers=effective_threads) as pool:
                for i in range(1, self._n_runs + 1):
                    future = pool.submit(self._run_one, i)
                    futures[future] = i

                for future in as_completed(futures):
                    i = futures[future]
                    try:
                        output = future.result()
                        outputs[i - 1] = output
                        if verbose:
                            with self._print_lock:
                                print(f"  ✓ run_{i:03d}  "
                                      f"{output.transaction_count} txns  "
                                      f"£{output.total_revenue:,.2f}  "
                                      f"peak={output.peak_period}")
                    except Exception as exc:
                        with self._print_lock:
                            print(f"  ✗ run_{i:03d} FAILED: {exc}")
                        raise

        summary = self._aggregate(outputs)
        self._write_aggregate(summary, outputs)

        if verbose:
            with self._print_lock:
                print(f"\n{'═'*54}")
                print(f"  Aggregate Summary ({self._n_runs} runs)")
                print(f"{'═'*54}")
                print(summary)
                print(f"{'═'*54}\n")

        return summary

    # ── Internal ──────────────────────────────────────────────────────────

    def _aggregate(self, outputs: List[SimRunOutput]) -> AggregateSummary:
        txns    = [o.transaction_count for o in outputs]
        revs    = [o.total_revenue     for o in outputs]
        custs   = [o.customer_count    for o in outputs]
        votes: dict = {}
        for o in outputs:
            votes[o.peak_period] = votes.get(o.peak_period, 0) + 1

        return AggregateSummary(
            n_runs=len(outputs),
            mean_transactions=statistics.mean(txns) if txns else 0.0,
            min_transactions=min(txns) if txns else 0,
            max_transactions=max(txns) if txns else 0,
            mean_revenue=statistics.mean(revs) if revs else 0.0,
            min_revenue=min(revs) if revs else 0.0,
            max_revenue=max(revs) if revs else 0.0,
            mean_customers=statistics.mean(custs) if custs else 0.0,
            peak_period_votes=votes,
        )

    def _write_aggregate(
        self,
        summary: AggregateSummary,
        outputs: List[SimRunOutput],
    ) -> None:
        agg_path = os.path.join(self._output_dir, "aggregate_summary.json")
        data = {
            "n_runs":              summary.n_runs,
            "mean_transactions":   summary.mean_transactions,
            "min_transactions":    summary.min_transactions,
            "max_transactions":    summary.max_transactions,
            "mean_revenue":        round(summary.mean_revenue, 2),
            "min_revenue":         round(summary.min_revenue, 2),
            "max_revenue":         round(summary.max_revenue, 2),
            "mean_customers":      summary.mean_customers,
            "peak_period_votes":   summary.peak_period_votes,
            "runs": [
                {
                    "run_index":   o.run_index,
                    "seed":        o.config.seed,
                    "transactions":o.transaction_count,
                    "customers":   o.customer_count,
                    "revenue":     round(o.total_revenue, 2),
                    "peak_period": o.peak_period,
                    "output_dir":  o.output_dir,
                }
                for o in outputs
            ],
        }
        with open(agg_path, "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=2)
