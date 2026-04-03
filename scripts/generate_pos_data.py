"""
generate_pos_data.py
====================
Generate synthetic POS transaction data calibrated to a specific store layout.

Store size is inferred from the YAML: physical floor area (node bounding box) and
register count drive the customers-per-day estimate. Product mix is drawn from the
products CSV using popularity weights.

Usage
-----
    python scripts/generate_pos_data.py \\
        --store  examples/cowboy_market.yaml \\
        --products data/raw/cowboy_products.csv \\
        --days   28 \\
        --output data/raw/cowboy_pos.csv

    # Inspect without writing:
    python scripts/generate_pos_data.py --store examples/cowboy_market.yaml --dry-run

Importable API
--------------
    from scripts.generate_pos_data import generate
    df = generate("examples/cowboy_market.yaml", "data/raw/cowboy_products.csv", days=28)
    df.to_csv("data/raw/cowboy_pos.csv", index=False)
"""

from __future__ import annotations

import argparse
import math
import os
import sys
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import List

import numpy as np
import pandas as pd
import yaml


# ---------------------------------------------------------------------------
# Store characterisation
# ---------------------------------------------------------------------------

_DAY_ABBREVS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]


@dataclass
class StoreProfile:
    """Derived characteristics used to set traffic volume."""
    floor_area_m2: float       # physical footprint from node bounding box
    register_count: int        # number of Register-type nodes
    aisle_count: int           # number of non-routing edges (shelf edges)
    customers_per_day: float   # estimated peak-week daily footfall
    mean_basket_size: float    # expected items per transaction
    open_hour: int             # typical/default store opening hour (24h)
    close_hour: int            # typical/default store closing hour (24h)
    # Per-day schedule: indexed 0=Mon…6=Sun, None means closed that day.
    day_schedule: List[tuple[int, int] | None] = None  # type: ignore[assignment]

    def __post_init__(self) -> None:
        if self.day_schedule is None:
            # Default: Mon-Sat using typical hours; Sun closed.
            self.day_schedule = [
                (self.open_hour, self.close_hour) if i < 6 else None
                for i in range(7)
            ]


def _infer_store_profile(yaml_path: str) -> StoreProfile:
    """Parse store YAML and derive traffic/basket parameters."""
    with open(yaml_path, "r", encoding="utf-8") as f:
        root = yaml.safe_load(f)

    nodes = root.get("nodes", [])
    edges = root.get("edges", [])

    # ── Physical footprint ────────────────────────────────────────────────
    xs = [float(n.get("x", 0)) for n in nodes if "x" in n]
    zs = [float(n.get("z", 0)) for n in nodes if "z" in n]
    if len(xs) < 2 or len(zs) < 2:
        floor_area_m2 = 100.0
    else:
        # Add a margin equal to half the mean node width to the span
        widths = [float(n.get("width", 2)) for n in nodes]
        margin = max(2.0, float(np.mean(widths)) / 2)
        x_span = (max(xs) - min(xs)) + margin * 2
        z_span = (max(zs) - min(zs)) + margin * 2
        floor_area_m2 = max(20.0, x_span * z_span)

    # ── Node type counts ──────────────────────────────────────────────────
    type_counts: dict[str, int] = {}
    for n in nodes:
        t = str(n.get("type", "Junction"))
        type_counts[t] = type_counts.get(t, 0) + 1

    register_count = type_counts.get("Register", 0) or 1

    # Aisle edges: edges that are not purely entrance→junction or junction→exit
    entrance_ids = {n["id"] for n in nodes if str(n.get("type", "")) == "Entrance"}
    exit_ids     = {n["id"] for n in nodes if str(n.get("type", "")) == "Exit"}
    aisle_count  = sum(
        1 for e in edges
        if e.get("from") not in entrance_ids and e.get("to") not in exit_ids
    )
    aisle_count = max(1, aisle_count)

    # ── Customers/day heuristic ───────────────────────────────────────────
    # Reference: a 300 m² store with 1 register ≈ 60 customers/day.
    # Scale by sqrt(area/300) so larger stores grow sub-linearly, then
    # apply a register multiplier (more tills → more throughput capacity).
    base_customers = 60.0 * math.sqrt(floor_area_m2 / 300.0)
    register_factor = 0.7 + 0.3 * register_count   # 1 → 1.0×, 2 → 1.3×, 3 → 1.6×
    customers_per_day = max(10.0, base_customers * register_factor)

    # ── Basket size heuristic ─────────────────────────────────────────────
    # More aisles → more browsing opportunity → slightly larger baskets.
    mean_basket_size = 2.5 + 0.15 * min(aisle_count, 20)

    # ── Operating schedule from YAML (preferred) or area-based fallback ──
    # Build per-day schedule: list of (open_hour, close_hour) or None.
    day_schedule: list = [None] * 7

    hop = root.get("hours_of_operation") or {}  # {DayAbbrev: {open: int, close: int}}
    dop = root.get("days_of_operation") or []    # [DayAbbrev, ...]

    if dop:
        open_days = set(dop)
    else:
        # Fall back: Mon-Sat from area tier (emit warning only if neither key present)
        if not hop:
            import warnings
            warnings.warn(
                f"[generate_pos_data] '{yaml_path}' has no days_of_operation / "
                "hours_of_operation — using area-based heuristic.",
                stacklevel=2,
            )
        open_days = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}

    # Determine default hours (area-based fallback used when no hop entry)
    if floor_area_m2 < 150:
        default_open, default_close = 9, 17
    elif floor_area_m2 < 500:
        default_open, default_close = 9, 19
    else:
        default_open, default_close = 8, 21

    for i, abbr in enumerate(_DAY_ABBREVS):
        if abbr not in open_days:
            day_schedule[i] = None
            continue
        entry = hop.get(abbr, {}) if isinstance(hop, dict) else {}
        o = int(entry.get("open",  default_open))
        c = int(entry.get("close", default_close))
        if c <= o:
            raise ValueError(
                f"[generate_pos_data] Invalid hours for {abbr}: close ({c}) must be > open ({o})"
            )
        day_schedule[i] = (o, c)

    # Typical hours = widest open window across operating days (for POS generation)
    open_windows = [w for w in day_schedule if w is not None]
    if open_windows:
        open_hour  = min(w[0] for w in open_windows)
        close_hour = max(w[1] for w in open_windows)
    else:
        open_hour, close_hour = default_open, default_close

    return StoreProfile(
        floor_area_m2=round(floor_area_m2, 1),
        register_count=register_count,
        aisle_count=aisle_count,
        customers_per_day=round(customers_per_day, 1),
        mean_basket_size=round(mean_basket_size, 2),
        open_hour=open_hour,
        close_hour=close_hour,
        day_schedule=day_schedule,
    )


# ---------------------------------------------------------------------------
# Intraday traffic distribution
# ---------------------------------------------------------------------------

# Two-peak Gaussian mixture: lunch rush + after-work rush.
# Weights and positions tuned for a general retail store.
_PEAKS = [
    {"mu": 11.5, "sigma": 1.2, "weight": 0.40},  # morning/lunch rush
    {"mu": 16.5, "sigma": 1.4, "weight": 0.60},  # after-work rush
]

# Day-of-week multipliers (Mon=0 … Sun=6)
_DOW_MULTIPLIER = [0.85, 0.85, 0.90, 0.95, 1.10, 1.40, 0.80]


def _intraday_pdf(hour: float) -> float:
    """Unnormalised probability density at a given decimal hour."""
    total = 0.0
    for p in _PEAKS:
        total += p["weight"] * math.exp(-0.5 * ((hour - p["mu"]) / p["sigma"]) ** 2)
    return total


def _sample_arrival_times(
    date: datetime,
    n_customers: int,
    open_hour: int,
    close_hour: int,
    rng: np.random.Generator,
) -> List[datetime]:
    """Return n_customers datetime objects drawn from the intraday distribution."""
    # Build a piecewise CDF over the open window (1-minute resolution).
    minutes = np.arange(open_hour * 60, close_hour * 60)
    pdf = np.array([_intraday_pdf(m / 60.0) for m in minutes])
    pdf /= pdf.sum()

    chosen_minutes = rng.choice(minutes, size=n_customers, replace=True, p=pdf)
    # Jitter within the minute
    jitter = rng.integers(0, 60, size=n_customers)
    return [
        date + timedelta(minutes=int(m), seconds=int(s))
        for m, s in zip(chosen_minutes, jitter)
    ]


# ---------------------------------------------------------------------------
# Transaction generation
# ---------------------------------------------------------------------------

def _load_products(products_csv: str) -> pd.DataFrame:
    df = pd.read_csv(products_csv)
    df.columns = [c.strip().lower() for c in df.columns]
    required = {"sku", "name", "price"}
    missing = required - set(df.columns)
    if missing:
        raise ValueError(f"products CSV missing columns: {missing}")
    if "popularity" not in df.columns:
        df["popularity"] = 1.0
    if "category" not in df.columns:
        df["category"] = "General"
    df["popularity"] = pd.to_numeric(df["popularity"], errors="coerce").fillna(0.5)
    df["price"]      = pd.to_numeric(df["price"],      errors="coerce").fillna(0.0)
    df = df[df["price"] > 0].reset_index(drop=True)
    return df


_CUSTOMER_TYPES = ["Regular", "Bulk", "Casual", "Tourist"]
_CUSTOMER_WEIGHTS = [0.50, 0.20, 0.20, 0.10]

# Basket-size multipliers per customer type (applied to mean_basket_size)
_BASKET_MULTIPLIER = {"Regular": 1.0, "Bulk": 2.2, "Casual": 0.6, "Tourist": 0.8}


def _sample_basket(
    products: pd.DataFrame,
    mean_size: float,
    customer_type: str,
    rng: np.random.Generator,
) -> List[dict]:
    """Return a list of line-item dicts for one transaction."""
    adjusted_mean = mean_size * _BASKET_MULTIPLIER[customer_type]
    # Negative binomial: mean=adjusted_mean, dispersion k=3
    k = 3.0
    p = k / (k + adjusted_mean)
    n_items = int(rng.negative_binomial(int(max(1, k)), p)) + 1
    n_items = max(1, min(n_items, 20))

    weights = products["popularity"].values.astype(float)
    weights /= weights.sum()
    chosen_idx = rng.choice(len(products), size=n_items, replace=True, p=weights)

    # Aggregate duplicate SKUs into quantities
    qty_map: dict[int, int] = {}
    for idx in chosen_idx:
        sku = int(products.iloc[idx]["sku"])
        qty_map[sku] = qty_map.get(sku, 0) + 1

    items = []
    for sku, qty in qty_map.items():
        row = products[products["sku"] == sku].iloc[0]
        items.append({
            "sku":          sku,
            "product_name": row["name"],
            "category":     row["category"],
            "quantity":     qty,
            "unit_price":   float(row["price"]),
            "line_total":   round(float(row["price"]) * qty, 2),
        })
    return items


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def generate(
    store_yaml: str,
    products_csv: str,
    days: int = 28,
    start_date: str | None = None,
    seed: int = 42,
) -> tuple[pd.DataFrame, StoreProfile]:
    """
    Generate synthetic POS data for *store_yaml* over *days* calendar days.

    Returns
    -------
    (df, profile)
        df      : DataFrame with one row per line item
        profile : StoreProfile used to calibrate the generation
    """
    rng = np.random.default_rng(seed)

    profile = _infer_store_profile(store_yaml)
    products = _load_products(products_csv)

    base_date = datetime.strptime(start_date, "%Y-%m-%d") if start_date \
                else datetime(2024, 1, 1)

    rows: list[dict] = []
    tx_id = 1

    for day_offset in range(days):
        date = base_date + timedelta(days=day_offset)
        dow  = date.weekday()  # 0=Mon, 6=Sun

        # Respect days_of_operation: skip closed days.
        day_window = profile.day_schedule[dow] if profile.day_schedule else None
        if day_window is None:
            continue

        multiplier = _DOW_MULTIPLIER[dow]

        # Poisson-sample actual customer count for today
        n_today = int(rng.poisson(profile.customers_per_day * multiplier))
        n_today = max(0, n_today)

        arrivals = _sample_arrival_times(
            date, n_today, day_window[0], day_window[1], rng
        )

        customer_types = rng.choice(
            _CUSTOMER_TYPES, size=n_today, p=_CUSTOMER_WEIGHTS
        )

        for arrival, ctype in zip(arrivals, customer_types):
            items = _sample_basket(products, profile.mean_basket_size, ctype, rng)
            tx_total = sum(i["line_total"] for i in items)
            for item in items:
                rows.append({
                    "Transaction_ID":    tx_id,
                    "Date":              arrival.strftime("%Y-%m-%d %H:%M:%S"),
                    "customer_type":     ctype,
                    "sku":               item["sku"],
                    "product_name":      item["product_name"],
                    "category":          item["category"],
                    "quantity":          item["quantity"],
                    "unit_price":        item["unit_price"],
                    "line_total":        item["line_total"],
                    "tx_total":          round(tx_total, 2),
                })
            tx_id += 1

    df = pd.DataFrame(rows)
    return df, profile


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _find_project_root() -> str:
    cur = os.path.dirname(os.path.abspath(__file__))
    for _ in range(5):
        if os.path.exists(os.path.join(cur, ".git")) or \
           os.path.exists(os.path.join(cur, "README.md")):
            return cur
        cur = os.path.dirname(cur)
    return os.path.dirname(os.path.abspath(__file__))


def main() -> None:
    root = _find_project_root()

    parser = argparse.ArgumentParser(
        description="Generate synthetic POS data calibrated to a store YAML."
    )
    parser.add_argument(
        "--store", required=True,
        help="Path to store YAML (e.g. examples/cowboy_market.yaml)"
    )
    parser.add_argument(
        "--products", required=True,
        help="Path to products CSV (e.g. data/raw/cowboy_products.csv)"
    )
    parser.add_argument(
        "--days", type=int, default=28,
        help="Number of calendar days to generate (default: 28)"
    )
    parser.add_argument(
        "--start-date", default=None,
        help="Start date YYYY-MM-DD (default: 2024-01-01)"
    )
    parser.add_argument(
        "--seed", type=int, default=42,
        help="Random seed for reproducibility (default: 42)"
    )
    parser.add_argument(
        "--output", default=None,
        help="Output CSV path. Defaults to data/raw/<store_stem>_pos.csv"
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Print store profile and row count without writing the file"
    )
    args = parser.parse_args()

    store_path    = args.store    if os.path.isabs(args.store)    else os.path.join(root, args.store)
    products_path = args.products if os.path.isabs(args.products) else os.path.join(root, args.products)

    if not os.path.isfile(store_path):
        sys.exit(f"Store YAML not found: {store_path}")
    if not os.path.isfile(products_path):
        sys.exit(f"Products CSV not found: {products_path}")

    df, profile = generate(
        store_yaml=store_path,
        products_csv=products_path,
        days=args.days,
        start_date=args.start_date,
        seed=args.seed,
    )

    tx_count = df["Transaction_ID"].nunique()
    spawn_interval = (args.days * (profile.close_hour - profile.open_hour) * 3600) / max(1, tx_count)

    print("\n-- Store profile --")
    print(f"  Floor area:        {profile.floor_area_m2:.0f} m2")
    print(f"  Registers:         {profile.register_count}")
    print(f"  Aisles:            {profile.aisle_count}")
    print(f"  Customers/day:     {profile.customers_per_day:.0f} (estimated)")
    print(f"  Mean basket size:  {profile.mean_basket_size:.1f} items")
    print("  Schedule:")
    for i, abbr in enumerate(_DAY_ABBREVS):
        win = profile.day_schedule[i] if profile.day_schedule else None
        if win:
            print(f"    {abbr}: {win[0]:02d}:00 - {win[1]:02d}:00")
        else:
            print(f"    {abbr}: closed")
    print("\n-- Generated data --")
    print(f"  Days generated:    {args.days}")
    print(f"  Transactions:      {tx_count:,}")
    print(f"  Line items:        {len(df):,}")
    print(f"  Implied spawn interval: {spawn_interval:.1f}s")

    if args.dry_run:
        print("\n[dry-run] No file written.")
        return

    if args.output:
        out_path = args.output if os.path.isabs(args.output) else os.path.join(root, args.output)
    else:
        stem = os.path.splitext(os.path.basename(store_path))[0]
        out_path = os.path.join(root, "data", "raw", f"{stem}_pos.csv")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    df.to_csv(out_path, index=False)
    print(f"\n  Written -> {os.path.relpath(out_path, root)}")


if __name__ == "__main__":
    main()
