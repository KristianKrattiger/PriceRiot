"""
Derive simulation parameters from cleaned POS data.

Keeps it simple: compute spawn interval and transaction count from the
transaction timestamps — the only two values the simulator actually consumes.
"""
import os
from typing import Any, Dict, Optional

import pandas as pd


def _get_project_root() -> str:
    try:
        from analytics.core import get_project_root
        return get_project_root()
    except ImportError:
        cur = os.path.dirname(os.path.abspath(__file__))
        for _ in range(5):
            cur = os.path.dirname(cur)
            if not cur:
                break
            if os.path.exists(os.path.join(cur, "README.md")) or os.path.exists(os.path.join(cur, ".git")):
                return cur
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _resolve_path(path: str, base: Optional[str] = None) -> str:
    if os.path.isabs(path):
        return os.path.normpath(path)
    root = base or _get_project_root()
    return os.path.normpath(os.path.join(root, path))


def extract_params(cleaned_path: str, output_dir: Optional[str] = None) -> Dict[str, Any]:
    """
    Read cleaned POS CSV and compute spawn_interval_seconds from transaction timestamps.

    The CSV must have a Date column (parseable by pandas) and a Transaction_ID column.
    Returns a dict with:
      - spawn_interval_seconds: mean seconds between distinct transactions
      - transaction_count: total number of unique transactions observed
    """
    root = _get_project_root()
    resolved = _resolve_path(cleaned_path, root)
    if not os.path.isfile(resolved):
        raise FileNotFoundError(f"Cleaned POS file not found: {resolved}")

    df = pd.read_csv(resolved)

    # Identify the timestamp column (Date or similar)
    date_col = next((c for c in df.columns if c.lower() in ("date", "datetime", "timestamp")), None)
    tx_col = next((c for c in df.columns if "transaction" in c.lower() and "id" in c.lower()), None)

    if date_col is None or tx_col is None:
        # Can't derive timing — return a sensible default
        return {
            "spawn_interval_seconds": 5.0,
            "transaction_count": len(df),
        }

    df[date_col] = pd.to_datetime(df[date_col], errors="coerce")
    df = df.dropna(subset=[date_col])

    # One timestamp per transaction
    tx = (
        df.groupby(tx_col)[date_col]
        .min()
        .reset_index()
        .sort_values(date_col)
    )

    transaction_count = len(tx)

    if transaction_count < 2:
        spawn_interval = 5.0
    else:
        # Compute intra-day inter-arrival times only.
        # Gaps between consecutive transactions on *different* days (overnight gaps)
        # are excluded — they would massively inflate the mean.
        tx["_date"] = pd.to_datetime(tx[date_col]).dt.date
        within_day_gaps = (
            tx.sort_values(date_col)
            .groupby("_date")[date_col]
            .apply(lambda s: s.diff().dt.total_seconds().dropna())
            .reset_index(drop=True)
        )
        valid_gaps = within_day_gaps[within_day_gaps > 0]

        if len(valid_gaps) > 0:
            spawn_interval = float(valid_gaps.mean())
        else:
            # Fallback: total open seconds across days divided by transaction count
            total_seconds = (tx[date_col].max() - tx[date_col].min()).total_seconds()
            spawn_interval = total_seconds / max(1, transaction_count - 1)

    # Clamp to a sane simulation range [1s, 3600s]
    spawn_interval = max(1.0, min(3600.0, spawn_interval))

    # Average basket value: look for a per-transaction total column
    avg_basket_value: Optional[float] = None
    total_col = next(
        (c for c in df.columns if c.lower() in ("tx_total", "total_cost", "total", "basket_total", "amount")),
        None,
    )
    if total_col and tx_col:
        per_tx = df.groupby(tx_col)[total_col].first()
        if len(per_tx) > 0:
            avg_basket_value = round(float(per_tx.mean()), 2)

    return {
        "spawn_interval_seconds": round(spawn_interval, 2),
        "transaction_count": transaction_count,
        "avg_basket_value": avg_basket_value,
    }
