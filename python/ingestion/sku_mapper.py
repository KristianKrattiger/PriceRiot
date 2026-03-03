"""
Fuzzy-match POS product names to simulation SKUs (data/raw/products.csv).
Output: sku_mapping.json (POS name -> sim SKU ID), flag low-confidence matches.
"""
import json
import os
from typing import Dict, List, Optional, Tuple

import pandas as pd
from rapidfuzz import process

# Minimum score (0-100) to accept a match; below this, flag for manual review
MATCH_THRESHOLD = 70


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


def build_sku_mapping(
    top_products: List[str],
    products_csv_path: Optional[str] = None,
    output_dir: Optional[str] = None,
    threshold: int = MATCH_THRESHOLD,
) -> Tuple[Dict[str, int], List[str]]:
    """
    Match POS product names to simulation SKU IDs using rapidfuzz.
    Returns (mapping dict POS_name -> sku_id, list of unmapped / low-confidence names).
    Writes data/processed/sku_mapping.json and includes unmapped in output for review.
    """
    root = _get_project_root()
    products_path = _resolve_path(products_csv_path or os.path.join("data", "raw", "products.csv"), root)
    if not os.path.isfile(products_path):
        raise FileNotFoundError(f"Products CSV not found: {products_path}")

    out_dir = output_dir or os.path.join(root, "data", "processed")
    out_dir = _resolve_path(out_dir, root) if not os.path.isabs(out_dir or "") else (out_dir or "")
    os.makedirs(out_dir, exist_ok=True)

    products_df = pd.read_csv(products_path)
    sim_names = products_df["name"].astype(str).tolist()
    sim_sku_by_name = dict(zip(products_df["name"].astype(str), products_df["sku"].astype(int)))

    mapping: Dict[str, int] = {}
    unmapped: List[str] = []

    for pos_name in top_products:
        if not pos_name or str(pos_name).strip() == "":
            continue
        pos_name = str(pos_name).strip()
        match = process.extractOne(pos_name, sim_names, score_cutoff=threshold)
        if match:
            sim_name, score, _ = match
            mapping[pos_name] = int(sim_sku_by_name[sim_name])
        else:
            unmapped.append(pos_name)

    out_data = {
        "mapping": mapping,
        "unmapped": unmapped,
        "threshold": threshold,
    }
    out_path = os.path.join(out_dir, "sku_mapping.json")
    with open(out_path, "w") as f:
        json.dump(out_data, f, indent=2)

    return mapping, unmapped
