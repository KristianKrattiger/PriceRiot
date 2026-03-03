"""
Derive simulation parameters from cleaned POS data: spawn intervals, basket stats,
customer profiles, checkout/payment.
"""
import json
import os
from typing import Any, Dict, List, Optional

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


# Checkout time proxy by payment method (seconds)
SERVICE_TIME_BY_PAYMENT: Dict[str, float] = {
    "Mobile Payment": 18.0,
    "Credit Card": 28.0,
    "Debit Card": 26.0,
    "Cash": 38.0,
}


def extract_params(cleaned_path: str, output_dir: Optional[str] = None) -> Dict[str, Any]:
    """
    Read cleaned POS CSV, compute spawn/basket/customer/checkout params, write sim_params.json.
    Returns the params dict.
    """
    root = _get_project_root()
    resolved = _resolve_path(cleaned_path, root)
    if not os.path.isfile(resolved):
        raise FileNotFoundError(f"Cleaned POS file not found: {resolved}")

    out_dir = output_dir or os.path.join(root, "data", "processed")
    out_dir = _resolve_path(out_dir, root) if not os.path.isabs(out_dir or "") else (out_dir or "")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "sim_params.json")

    df = pd.read_csv(resolved)
    df["Date"] = pd.to_datetime(df["Date"])

    # Transaction-level view (one row per transaction)
    tx = df.groupby("Transaction_ID").agg(
        Date=("Date", "min"),
        Customer_Category=("Customer_Category", "first"),
        Total_Cost=("Total_Cost", "first"),
        Discount_Applied=("Discount_Applied", "first"),
        Payment_Method=("Payment_Method", "first"),
        basket_size=("basket_size", "first"),
        hour_of_day=("hour_of_day", "first"),
        day_of_week=("day_of_week", "first"),
    ).reset_index()

    tx = tx.sort_values("Date")
    interarrival_seconds = tx["Date"].diff().dt.total_seconds().dropna()
    interarrival_seconds = interarrival_seconds[interarrival_seconds > 0]

    spawn_interval_mean = float(interarrival_seconds.mean()) if len(interarrival_seconds) else 60.0
    spawn_interval_std = float(interarrival_seconds.std()) if len(interarrival_seconds) > 1 else 30.0

    by_hour = tx.groupby("hour_of_day")["Date"].apply(lambda s: s.diff().dt.total_seconds().dropna().mean())
    spawn_interval_by_hour = {str(int(k)): float(v) if pd.notna(v) and v > 0 else spawn_interval_mean for k, v in by_hour.items()}
    for h in range(24):
        if str(h) not in spawn_interval_by_hour:
            spawn_interval_by_hour[str(h)] = spawn_interval_mean

    # Basket
    mean_basket_size = float(tx["basket_size"].mean())
    basket_size_std = float(tx["basket_size"].std()) if len(tx) > 1 else 0.0

    product_counts = df[df["product"].notna()].groupby("product").agg(
        transaction_count=("Transaction_ID", "nunique"),
        total_quantity=("Transaction_ID", "count"),
    ).reset_index()
    product_counts = product_counts.sort_values("transaction_count", ascending=False)
    top_skus_by_tx = product_counts.head(20)["product"].tolist()
    product_counts = product_counts.sort_values("total_quantity", ascending=False)
    top_skus_by_qty = product_counts.head(20)["product"].tolist()
    top_skus = list(dict.fromkeys(top_skus_by_tx + top_skus_by_qty))[:20]

    # Co-occurrence: which products appear together
    tx_products = df[df["product"].notna()].groupby("Transaction_ID")["product"].apply(set).to_dict()
    products_list = product_counts["product"].tolist()
    cooccurrence: Dict[str, Dict[str, int]] = {}
    for i, a in enumerate(products_list):
        cooccurrence[a] = {}
        for b in products_list:
            if a == b:
                continue
            count = sum(1 for prods in tx_products.values() if a in prods and b in prods)
            if count > 0:
                cooccurrence[a][b] = count

    # Customer profiles
    cat_dist = tx["Customer_Category"].value_counts(normalize=True).to_dict()
    customer_profile_distribution = {str(k): float(v) for k, v in cat_dist.items()}

    per_cat = tx.groupby("Customer_Category").agg(
        mean_basket=("basket_size", "mean"),
        mean_spend=("Total_Cost", "mean"),
        discount_rate=("Discount_Applied", "mean"),
    ).reset_index()
    mission_scores = []
    price_sens_scores = []
    for _, row in per_cat.iterrows():
        mission_scores.append(float(row["mean_basket"]) / max(mean_basket_size, 0.1))
        price_sens_scores.append(float(row["discount_rate"]))
    mission_probability = float(pd.Series(mission_scores).mean()) if mission_scores else 0.5
    mission_probability = max(0.0, min(1.0, mission_probability))
    price_sensitivity = float(pd.Series(price_sens_scores).mean()) if price_sens_scores else 0.5
    price_sensitivity = max(0.0, min(1.0, price_sensitivity))

    # Checkout / payment
    payment_dist = tx["Payment_Method"].value_counts(normalize=True).to_dict()
    payment_distribution = {str(k): float(v) for k, v in payment_dist.items()}
    mean_service_time_s = 0.0
    for method, pct in payment_distribution.items():
        mean_service_time_s += pct * SERVICE_TIME_BY_PAYMENT.get(method, 28.0)
    if not payment_distribution:
        mean_service_time_s = 28.5

    params = {
        "spawn_interval_seconds": round(spawn_interval_mean, 2),
        "spawn_interval_std": round(spawn_interval_std, 2),
        "spawn_interval_by_hour": {k: round(v, 2) for k, v in sorted(spawn_interval_by_hour.items(), key=lambda x: int(x[0]))},
        "mean_basket_size": round(mean_basket_size, 2),
        "basket_size_std": round(basket_size_std, 2),
        "mission_probability": round(mission_probability, 2),
        "price_sensitivity": round(price_sensitivity, 2),
        "mean_service_time_s": round(mean_service_time_s, 2),
        "top_skus": top_skus,
        "customer_profile_distribution": customer_profile_distribution,
        "payment_distribution": payment_distribution,
        "cooccurrence": cooccurrence,
    }

    with open(out_path, "w") as f:
        json.dump(params, f, indent=2)

    return params
