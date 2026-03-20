import os
import sys
from dataclasses import dataclass
from typing import Iterable, List, Optional, Sequence

import pandas as pd


def _discover_project_root(start: Optional[str] = None) -> str:
    """
    Discover the PriceRiot project root by walking upwards from *start*
    (or this file) until a sentinel such as CMakeLists.txt, .git,
    README.md, or data/raw/products.csv is found.
    """
    if start is None:
        start = os.path.abspath(__file__)

    cur = os.path.dirname(start)
    last_sentinel: Optional[str] = None

    while True:
        candidates = [
            os.path.join(cur, "data", "raw", "products.csv"),
            os.path.join(cur, "CMakeLists.txt"),
            os.path.join(cur, "README.md"),
            os.path.join(cur, ".git"),
        ]
        if any(os.path.exists(p) for p in candidates):
            last_sentinel = cur
            # Prefer the first directory that actually owns data/raw/products.csv
            if os.path.exists(os.path.join(cur, "data", "raw", "products.csv")):
                break

        parent = os.path.dirname(cur)
        if not parent or parent == cur:
            break
        cur = parent

    if last_sentinel is None:
        # Fallback to parent of python/ if sentinels are missing
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return last_sentinel


# Project root and build dirs for simulation.pyd
_ROOT = _discover_project_root()


def get_project_root() -> str:
    """Return the project root directory (for ingestion, dashboard, etc.)."""
    return _ROOT
_BUILD = os.path.join(_ROOT, "build")
_RELEASE = os.path.join(_ROOT, "build", "Release")

for _path in (_BUILD, _RELEASE):
    if os.path.isdir(_path) and _path not in sys.path:
        sys.path.insert(0, _path)


try:
    import simulation  # type: ignore[import]
except ImportError as e:  # pragma: no cover - environment-specific
    if "PyInit_simulation" in str(e):
        raise RuntimeError(
            "The simulation module was built for a different Python version. "
            "Run Python with the same interpreter version that was used for the C++ build "
            "(see README for build instructions)."
        ) from e
    raise


@dataclass
class SimulationResult:
    """Container for a single simulation run."""

    transactions: pd.DataFrame
    customers: pd.DataFrame
    simulator: "simulation.Simulator"


def _resolve_store_path(store_path: str) -> str:
    """Resolve store.yaml path relative to project root if not absolute."""
    if os.path.isabs(store_path):
        return os.path.normpath(store_path)
    return os.path.normpath(os.path.join(_ROOT, store_path))


def transactions_to_frame(transactions: Iterable["simulation.Transaction"]) -> pd.DataFrame:
    """
    Convert a list of Transaction objects into a flat line-item DataFrame.

    One row per (transaction, line item) pair. Transactions without items still
    appear once with item_* fields set to null/zero.
    """
    rows: List[dict] = []
    for tx in transactions:
        header = tx.to_dict()
        items = tx.items()
        if not items:
            rows.append(
                {
                    **header,
                    "item_id": None,
                    "item_name": None,
                    "quantity": 0,
                    "price_per_unit": 0.0,
                    "item_total": 0.0,
                }
            )
            continue

        for item in items:
            rows.append({**header, **item.to_dict()})

    return pd.DataFrame(rows)


def customers_to_frame(customers: Iterable["simulation.Customer"]) -> pd.DataFrame:
    """Convert a list of Customer objects into a DataFrame."""
    return pd.DataFrame([c.to_dict() for c in customers])


def run_simulation(
    store_path: str = "store.yaml",
    duration_seconds: float = 3600.0,
    spawn_interval: float = 5.0,
    mission_probability: float = 0.5,
    seed: int = 0,
    dt: float = 1.0 / 60.0,
    num_stockers: int = 2,
    num_cashiers: int = 1,
    auto_stock_tasks: bool = True,
    auto_register_tasks: bool = True,
) -> SimulationResult:
    """
    Run a headless simulation and return results as pandas DataFrames.

    This is a convenience wrapper around simulation.Simulator for analytics.
    """
    store_abs = _resolve_store_path(store_path)
    if not os.path.isfile(store_abs):
        raise FileNotFoundError(f"Store file not found: {store_abs}")

    sim = simulation.Simulator(
        store_abs,
        spawn_interval=spawn_interval,
        mission_probability=mission_probability,
        seed=seed,
    )
    # Apply simple worker configuration if the bindings expose them.
    if hasattr(sim, "set_worker_config"):
        try:
            sim.set_worker_config(
                num_stockers=num_stockers,
                num_cashiers=num_cashiers,
                auto_stock_tasks=auto_stock_tasks,
                auto_register_tasks=auto_register_tasks,
            )
        except TypeError:
            # Older builds may not support keyword args; fall back silently.
            try:
                sim.set_worker_config(num_stockers, num_cashiers, auto_stock_tasks, auto_register_tasks)
            except Exception:
                pass
    sim.run(duration_seconds, dt=dt)

    tx_df = transactions_to_frame(sim.get_transactions())
    cust_df = customers_to_frame(sim.get_customers())
    return SimulationResult(transactions=tx_df, customers=cust_df, simulator=sim)


def run_simulation_to_csv(
    store_path: str = "store.yaml",
    duration_seconds: float = 3600.0,
    spawn_interval: float = 5.0,
    mission_probability: float = 0.5,
    seed: int = 0,
    dt: float = 1.0 / 60.0,
    num_stockers: int = 2,
    num_cashiers: int = 1,
    auto_stock_tasks: bool = True,
    auto_register_tasks: bool = True,
    output_dir: Optional[str] = None,
) -> SimulationResult:
    """
    Run a simulation and persist transactions/customers to CSV.

    Returns the same SimulationResult as run_simulation, after writing:
      - transactions.csv
      - customers.csv
    into output_dir (default: <project-root>/data/processed).
    """
    if output_dir is None:
        output_dir = os.path.join(_ROOT, "data", "processed")
    os.makedirs(output_dir, exist_ok=True)

    result = run_simulation(
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
    )

    result.transactions.to_csv(os.path.join(output_dir, "transactions.csv"), index=False)
    result.customers.to_csv(os.path.join(output_dir, "customers.csv"), index=False)
    return result


def cell_heatmap_to_frame(
    cell_counts: Sequence[Sequence[int]],
) -> pd.DataFrame:
    """
    Convert Simulator.get_cell_heatmap() output into a long-form DataFrame.

    Columns:
      - edge_index
      - cell_index
      - visits
    """
    rows: List[dict] = []
    for edge_idx, edge_counts in enumerate(cell_counts):
        for cell_idx, visits in enumerate(edge_counts):
            rows.append(
                {
                    "edge_index": edge_idx,
                    "cell_index": cell_idx,
                    "visits": int(visits),
                }
            )
    return pd.DataFrame(rows)


def queue_metrics_to_frame(
    times: Sequence[float],
    lengths_per_lane: Sequence[Sequence[int]],
) -> pd.DataFrame:
    """
    Convert queue metrics into a long-form DataFrame.

    Columns:
      - time_s
      - lane_index
      - queue_length
    """
    rows: List[dict] = []
    for lane_idx, series in enumerate(lengths_per_lane):
        for sample_idx, length in enumerate(series):
            if sample_idx >= len(times):
                break
            rows.append(
                {
                    "time_s": float(times[sample_idx]),
                    "lane_index": lane_idx,
                    "queue_length": int(length),
                }
            )
    return pd.DataFrame(rows)

