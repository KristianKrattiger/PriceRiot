"""
Unit tests for the POS ingestion pipeline: cleaner, sku_mapper, param_extractor.
"""
from __future__ import annotations

import os
import sys

import pandas as pd
import pytest

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for _p in (_ROOT, os.path.join(_ROOT, "python")):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.insert(0, _p)

from python.ingestion.cleaner import load_and_clean  # noqa: E402
from python.ingestion.param_extractor import extract_params  # noqa: E402
from python.ingestion.sku_mapper import build_sku_mapping  # noqa: E402


def test_cleaner_explodes_product_lists_and_types(tmp_path):
    raw_csv = tmp_path / "raw_pos.csv"
    pd.DataFrame(
        [
            {
                "Transaction_ID": 100,
                "Date": "1/15/2024 10:30",
                "Customer_Name": "Alice",
                "Product": "['Milk', 'Bread']",
                "Total_Items": 2,
                "Total_Cost": 9.50,
                "Payment_Method": "Card",
                "City": "Austin",
                "Store_Type": "Grocery",
                "Discount_Applied": "TRUE",
                "Customer_Category": "Regular",
                "Season": "Winter",
                "Promotion": "None",
            },
            {
                "Transaction_ID": 101,
                "Date": "1/15/2024 11:00",
                "Customer_Name": "Bob",
                "Product": "['Coffee']",
                "Total_Items": 1,
                "Total_Cost": 7.99,
                "Payment_Method": "Cash",
                "City": "Austin",
                "Store_Type": "Grocery",
                "Discount_Applied": "FALSE",
                "Customer_Category": "Casual",
                "Season": "Winter",
                "Promotion": "BOGO",
            },
        ]
    ).to_csv(raw_csv, index=False)

    cleaned = load_and_clean(str(raw_csv), output_dir=str(tmp_path))
    assert (tmp_path / "pos_cleaned.csv").is_file()
    assert len(cleaned) == 3
    assert set(cleaned["product"]) == {"Milk", "Bread", "Coffee"}
    assert cleaned.loc[cleaned["Transaction_ID"] == 100, "Discount_Applied"].all()
    assert pd.isna(cleaned.loc[cleaned["Transaction_ID"] == 100, "Promotion"]).all()
    assert "hour_of_day" in cleaned.columns and "is_weekend" in cleaned.columns


def test_sku_mapper_fuzzy_and_unmapped(tmp_path):
    products_csv = os.path.join(_ROOT, "data", "raw", "cowboy_products.csv")
    mapping, unmapped = build_sku_mapping(
        ["Chuckwagon Beans", "Campfire Coffie", "Completely Unrelated Quantum Widget"],
        products_csv_path=products_csv,
        output_dir=str(tmp_path),
        threshold=70,
    )
    assert mapping["Chuckwagon Beans"] == 1
    assert mapping["Campfire Coffie"] == 6
    assert "Completely Unrelated Quantum Widget" in unmapped
    assert (tmp_path / "sku_mapping.json").is_file()


def test_param_extractor_excludes_overnight_gaps(tmp_path):
    csv_path = tmp_path / "pos.csv"
    # Day 1: 3 txns 120s apart; Day 2 (next morning): 2 txns 120s apart.
    # Overnight gap (~14 hours) must NOT inflate spawn_interval_seconds.
    rows = [
        {"Transaction_ID": 1, "Date": "2024-01-01 10:00:00", "tx_total": 20.0},
        {"Transaction_ID": 2, "Date": "2024-01-01 10:02:00", "tx_total": 30.0},
        {"Transaction_ID": 3, "Date": "2024-01-01 10:04:00", "tx_total": 40.0},
        {"Transaction_ID": 4, "Date": "2024-01-02 09:00:00", "tx_total": 50.0},
        {"Transaction_ID": 5, "Date": "2024-01-02 09:02:00", "tx_total": 60.0},
    ]
    pd.DataFrame(rows).to_csv(csv_path, index=False)

    params = extract_params(str(csv_path))
    assert params["transaction_count"] == 5
    assert params["spawn_interval_seconds"] == pytest.approx(120.0, abs=0.1)
    assert params["avg_basket_value"] == pytest.approx(40.0, abs=0.01)


def test_param_extractor_on_cowboy_pos():
    cowboy_pos = os.path.join(_ROOT, "data", "raw", "cowboy_pos.csv")
    params = extract_params(cowboy_pos)
    assert params["transaction_count"] > 1000
    assert 60.0 <= params["spawn_interval_seconds"] <= 1200.0
    assert params["avg_basket_value"] is not None and params["avg_basket_value"] > 0
