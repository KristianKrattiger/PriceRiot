"""
Clean and standardize raw POS CSV: parse Product list, fix types, explode to line items.
"""
import ast
import os
from typing import Optional

import pandas as pd


def _get_project_root() -> str:
    """Resolve project root (avoid circular import by not using analytics at module load)."""
    try:
        from analytics.core import get_project_root
        return get_project_root()
    except ImportError:
        cur = os.path.dirname(os.path.abspath(__file__))
        for _ in range(5):
            cur = os.path.dirname(cur)
            if not cur:
                break
            candidates = [
                os.path.join(cur, "data", "raw", "products.csv"),
                os.path.join(cur, "README.md"),
                os.path.join(cur, ".git"),
            ]
            if any(os.path.exists(p) for p in candidates):
                return cur
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _resolve_path(path: str, base: Optional[str] = None) -> str:
    if os.path.isabs(path):
        return os.path.normpath(path)
    root = base or _get_project_root()
    return os.path.normpath(os.path.join(root, path))


def load_and_clean(input_path: str, output_dir: Optional[str] = None) -> pd.DataFrame:
    """
    Load raw POS CSV, apply fixes, explode to one row per line item, add derived columns.
    Saves to output_dir/pos_cleaned.csv. Returns the cleaned DataFrame.
    """
    root = _get_project_root()
    resolved_in = _resolve_path(input_path, root)
    if not os.path.isfile(resolved_in):
        raise FileNotFoundError(f"Raw POS file not found: {resolved_in}")

    out_dir = output_dir or os.path.join(root, "data", "processed")
    out_dir = _resolve_path(out_dir, root) if not os.path.isabs(out_dir) else out_dir
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "pos_cleaned.csv")

    df = pd.read_csv(resolved_in)

    # Parse Product (stringified Python list)
    def parse_products(s: str):
        if pd.isna(s) or s == "":
            return []
        try:
            v = ast.literal_eval(s)
            return list(v) if isinstance(v, (list, tuple)) else [v]
        except (ValueError, SyntaxError):
            return []

    df["_product_list"] = df["Product"].astype(str).map(parse_products)
    df["basket_size"] = df["_product_list"].map(len)
    df["Total_Items_raw"] = df["Total_Items"]

    # Date: M/D/YYYY H:MM
    df["Date"] = pd.to_datetime(df["Date"], format="mixed")

    # Discount_Applied: string TRUE/FALSE -> bool
    df["Discount_Applied"] = df["Discount_Applied"].astype(str).str.upper().map({"TRUE": True, "FALSE": False})

    # Promotion: string "None" -> actual None/NaN
    df["Promotion"] = df["Promotion"].replace("None", pd.NA)

    # Explode: one row per (transaction, product) line item
    rows = []
    for _, row in df.iterrows():
        base = {
            "Transaction_ID": row["Transaction_ID"],
            "Date": row["Date"],
            "Customer_Name": row["Customer_Name"],
            "Total_Items_raw": row["Total_Items_raw"],
            "Total_Cost": row["Total_Cost"],
            "Payment_Method": row["Payment_Method"],
            "City": row["City"],
            "Store_Type": row["Store_Type"],
            "Discount_Applied": row["Discount_Applied"],
            "Customer_Category": row["Customer_Category"],
            "Season": row["Season"],
            "Promotion": row["Promotion"],
            "basket_size": row["basket_size"],
        }
        for product in row["_product_list"]:
            rows.append({**base, "product": product})
        if not row["_product_list"]:
            rows.append({**base, "product": None})

    clean = pd.DataFrame(rows)

    # Derived columns
    clean["hour_of_day"] = clean["Date"].dt.hour
    clean["day_of_week"] = clean["Date"].dt.dayofweek
    clean["month"] = clean["Date"].dt.month
    clean["is_weekend"] = clean["day_of_week"].isin([5, 6])

    clean.to_csv(out_path, index=False)
    return clean
