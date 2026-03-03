"""
POS Analysis tabs: Volume Patterns, Basket Analysis, Sim Param Summary.
"""
import json
import os
import subprocess
import sys
from typing import Optional

import pandas as pd
import streamlit as st


def _pos_cleaned_path(root: str) -> str:
    return os.path.join(root, "data", "processed", "pos_cleaned.csv")


def _sim_params_path(root: str) -> str:
    return os.path.join(root, "data", "processed", "sim_params.json")


def _sku_mapping_path(root: str) -> str:
    return os.path.join(root, "data", "processed", "sku_mapping.json")


def _tab_volume_patterns(root: str) -> None:
    path = _pos_cleaned_path(root)
    if not os.path.isfile(path):
        st.warning("pos_cleaned.csv not found. Run the ingestion pipeline first.")
        return
    df = pd.read_csv(path)
    df["Date"] = pd.to_datetime(df["Date"])

    st.subheader("Transactions by hour of day (averaged)")
    by_hour = df.drop_duplicates(subset=["Transaction_ID"]).groupby(df["Date"].dt.hour).size()
    st.bar_chart(by_hour)

    st.subheader("Transactions by day of week")
    by_dow = df.drop_duplicates(subset=["Transaction_ID"]).groupby(df["Date"].dt.dayofweek).size()
    st.bar_chart(by_dow)

    st.subheader("Transactions by month")
    by_month = df.drop_duplicates(subset=["Transaction_ID"]).groupby(df["Date"].dt.month).size()
    st.bar_chart(by_month)

    params_path = _sim_params_path(root)
    if os.path.isfile(params_path):
        with open(params_path) as f:
            params = json.load(f)
        by_hour_params = params.get("spawn_interval_by_hour", {})
        if by_hour_params:
            st.subheader("Derived spawn interval by hour (from sim_params.json)")
            series = pd.Series({int(k): v for k, v in by_hour_params.items()})
            st.line_chart(series)


def _tab_basket_analysis(root: str) -> None:
    path = _pos_cleaned_path(root)
    if not os.path.isfile(path):
        st.warning("pos_cleaned.csv not found. Run the ingestion pipeline first.")
        return
    df = pd.read_csv(path)

    st.subheader("Top 20 products by frequency")
    top20 = df[df["product"].notna()].groupby("product").size().sort_values(ascending=False).head(20)
    st.bar_chart(top20)

    st.subheader("Basket size distribution")
    basket = df.drop_duplicates(subset=["Transaction_ID"])["basket_size"].value_counts().sort_index()
    st.bar_chart(basket)

    st.subheader("Mean spend by customer category")
    tx = df.drop_duplicates(subset=["Transaction_ID"])
    mean_spend = tx.groupby("Customer_Category")["Total_Cost"].mean().sort_values(ascending=False)
    st.bar_chart(mean_spend)

    st.subheader("Discount applied rate by customer category")
    tx = tx.copy()
    tx["_disc"] = tx["Discount_Applied"].astype(str).str.lower().isin(("true", "1", "yes"))
    discount_rate = tx.groupby("Customer_Category")["_disc"].mean().sort_values(ascending=False)
    st.bar_chart(discount_rate)


def _tab_sim_param_summary(root: str) -> None:
    params_path = _sim_params_path(root)
    if not os.path.isfile(params_path):
        st.warning("sim_params.json not found. Run the ingestion pipeline first.")
        return
    with open(params_path) as f:
        params = json.load(f)
    st.subheader("Simulation parameters (sim_params.json)")
    st.json(params)

    mapping_path = _sku_mapping_path(root)
    if os.path.isfile(mapping_path):
        with open(mapping_path) as f:
            mapping_data = json.load(f)
        mapped = len(mapping_data.get("mapping", {}))
        unmapped = len(mapping_data.get("unmapped", []))
        total = mapped + unmapped
        st.subheader("SKU mapping coverage")
        st.metric("Mapped to sim SKU", mapped)
        st.metric("Unmapped (review)", unmapped)
        if total:
            st.metric("Coverage", f"{100 * mapped / total:.1f}%")
    else:
        st.info("sku_mapping.json not found. Run ingestion to generate it.")

    st.subheader("Re-run ingestion")
    raw_placeholder = st.text_input("Path to raw POS CSV (relative to project root)", value="data/raw/your_pos_file.csv")
    if st.button("Run ingestion pipeline"):
        out_dir = os.path.join(root, "data", "processed")
        cmd = [
            sys.executable,
            "-m",
            "python.ingestion.run_ingestion",
            "--input",
            os.path.join(root, raw_placeholder) if not os.path.isabs(raw_placeholder) else raw_placeholder,
            "--output",
            out_dir,
        ]
        with st.spinner("Running ingestion..."):
            try:
                env = {**os.environ, "PYTHONPATH": root}
                result = subprocess.run(
                    cmd,
                    cwd=root,
                    env=env,
                    capture_output=True,
                    text=True,
                    timeout=300,
                )
                if result.returncode == 0:
                    st.success("Ingestion completed.")
                    st.code(result.stdout)
                else:
                    st.error(result.stderr or result.stdout or "Ingestion failed.")
            except subprocess.TimeoutExpired:
                st.error("Ingestion timed out.")
            except Exception as e:
                st.error(str(e))
    st.caption("Or run from terminal: python -m python.ingestion.run_ingestion --input data/raw/your_pos_file.csv --output data/processed/")


def render_all(root: str) -> None:
    tab1, tab2, tab3 = st.tabs(["Volume Patterns", "Basket Analysis", "Sim Param Summary"])
    with tab1:
        _tab_volume_patterns(root)
    with tab2:
        _tab_basket_analysis(root)
    with tab3:
        _tab_sim_param_summary(root)
