"""
Simulation result tabs: Overview, Basket & SKUs, Traffic & Heatmap, Queue & Checkout, Compare.
"""
from typing import Any, Dict, List, Optional

import pandas as pd
import streamlit as st

from analytics import cell_heatmap_to_frame, queue_metrics_to_frame


def _get_runs_for_compare() -> List[tuple]:
    runs = st.session_state.get("sim_runs", [])
    if not runs:
        return []
    if isinstance(runs, list):
        return runs[-2:]
    return [(None, runs)]


def _tab_overview(tx: pd.DataFrame, cust: pd.DataFrame) -> None:
    if tx.empty:
        st.info("No transactions. Run a simulation first.")
        return
    if "transaction_id" in tx.columns:
        tx_unique = tx.drop_duplicates(subset=["transaction_id"])
    else:
        tx_unique = tx
    n_tx = len(tx_unique)
    n_cust = tx_unique["customer_id"].nunique() if "customer_id" in tx_unique.columns else 0
    total_revenue = float(tx_unique["total_spent"].sum()) if "total_spent" in tx_unique.columns else 0.0
    if "transaction_id" in tx.columns:
        basket_sizes = tx.groupby("transaction_id").size()
        avg_basket_size = float(basket_sizes.mean()) if len(basket_sizes) else 0.0
    else:
        avg_basket_size = 0.0
    avg_spend_per_cust = total_revenue / n_cust if n_cust else 0.0

    col1, col2, col3, col4, col5 = st.columns(5)
    col1.metric("Total transactions", n_tx)
    col2.metric("Total customers", n_cust)
    col3.metric("Total revenue", f"{total_revenue:.2f}")
    col4.metric("Avg basket size", f"{avg_basket_size:.2f}")
    col5.metric("Avg spend per customer", f"{avg_spend_per_cust:.2f}")

    # Transaction volume over time (binned by minute) - timestamp is HH:MM:SS string
    if "timestamp" in tx_unique.columns:
        tuc = tx_unique.copy()
        tuc["ts"] = pd.to_datetime(tuc["timestamp"], format="%H:%M:%S", errors="coerce")
        if tuc["ts"].notna().any():
            tuc["minute"] = tuc["ts"].dt.hour * 60 + tuc["ts"].dt.minute
            vol = tuc.groupby("minute").size().reset_index(name="count")
            st.subheader("Transaction volume over time")
            st.line_chart(vol.set_index("minute"))

    # Revenue over time
    if "timestamp" in tx_unique.columns and "total_spent" in tx_unique.columns:
        tuc = tx_unique.copy()
        tuc["ts"] = pd.to_datetime(tuc["timestamp"], format="%H:%M:%S", errors="coerce")
        tuc["minute"] = tuc["ts"].dt.hour * 60 + tuc["ts"].dt.minute
        rev = tuc.groupby("minute")["total_spent"].sum().reset_index(name="revenue")
        st.subheader("Revenue over time")
        st.line_chart(rev.set_index("minute"))


def _tab_basket_skus(tx: pd.DataFrame) -> None:
    if tx.empty or "item_name" not in tx.columns:
        st.info("No line-item data for basket/SKU charts.")
        return
    df = tx[tx["item_name"].notna() & (tx["quantity"] > 0)]
    if df.empty:
        st.info("No items in transactions.")
        return

    top_qty = df.groupby("item_name")["quantity"].sum().sort_values(ascending=False).head(10)
    st.subheader("Top 10 SKUs by quantity sold")
    st.bar_chart(top_qty)

    df["revenue"] = df.get("item_total", df["quantity"] * df.get("price_per_unit", 0))
    top_rev = df.groupby("item_name")["revenue"].sum().sort_values(ascending=False).head(10)
    st.subheader("Top 10 SKUs by revenue")
    st.bar_chart(top_rev)

    basket_sizes = tx.groupby("transaction_id").size() if "transaction_id" in tx.columns else pd.Series([len(tx)])
    st.subheader("Basket size distribution")
    st.bar_chart(basket_sizes.value_counts().sort_index())

    if "item_id" in tx.columns and tx["item_id"].notna().any():
        st.caption("Category breakdown: category data not in transaction output; use products.csv for reference.")


def _tab_traffic_heatmap(sim) -> None:
    try:
        cell_counts = sim.get_cell_heatmap()
    except Exception:
        st.info("Heatmap not available for this run.")
        return
    df = cell_heatmap_to_frame(cell_counts)
    if df.empty or df["visits"].sum() == 0:
        st.info("No traffic data.")
        return
    import seaborn as sns
    import matplotlib.pyplot as plt
    pivot = df.pivot(index="edge_index", columns="cell_index", values="visits").fillna(0)
    fig, ax = plt.subplots(figsize=(10, 6))
    sns.heatmap(pivot, ax=ax, cmap="YlOrRd")
    ax.set_title("Traffic heatmap (edge vs cell)")
    st.pyplot(fig)
    plt.close()

    edge_totals = df.groupby("edge_index")["visits"].sum().sort_values(ascending=False).head(15)
    st.subheader("Most congested edges (by visit count)")
    st.bar_chart(edge_totals)


def _tab_queue_checkout(sim) -> None:
    try:
        times = sim.get_queue_sample_times()
        lengths = sim.get_queue_lengths_history()
    except Exception:
        st.info("Queue metrics not available.")
        return
    if not times or not lengths:
        st.info("No queue history.")
        return
    df = queue_metrics_to_frame(times, lengths)
    if df.empty:
        st.info("No queue data.")
        return

    col1, col2, col3 = st.columns(3)
    col1.metric("Mean queue length", f"{df['queue_length'].mean():.2f}")
    col2.metric("Max queue length", int(df["queue_length"].max()))
    col3.metric("95th percentile", f"{df['queue_length'].quantile(0.95):.1f}")

    st.subheader("Queue length over time per lane")
    wide = df.pivot(index="time_s", columns="lane_index", values="queue_length").fillna(0)
    wide.columns = [f"Lane {c}" for c in wide.columns]
    st.line_chart(wide)
    st.caption("Payment method distribution not in current sim transaction output.")


def _tab_compare(runs: List[tuple]) -> None:
    if len(runs) < 2:
        st.info("Enable 'Compare runs' and run at least two simulations.")
        return
    (p1, r1), (p2, r2) = runs[-2], runs[-1]
    t1, t2 = r1.transactions, r2.transactions
    c1, c2 = r1.customers, r2.customers

    st.subheader("Side-by-side KPIs")
    def kpis(tx, cust):
        if tx.empty:
            return 0, 0, 0.0, 0.0, 0.0
        u = tx.drop_duplicates(subset=["transaction_id"]) if "transaction_id" in tx.columns else tx
        n_tx = len(u)
        n_cust = u["customer_id"].nunique() if "customer_id" in u.columns else 0
        rev = u["total_spent"].sum() if "total_spent" in u.columns else 0
        bs = tx.groupby("transaction_id").size() if "transaction_id" in tx.columns else pd.Series([len(tx)])
        return n_tx, n_cust, rev, float(bs.mean()) if len(bs) else 0, rev / n_cust if n_cust else 0
    n1, c1n, rev1, bs1, sp1 = kpis(t1, c1)
    n2, c2n, rev2, bs2, sp2 = kpis(t2, c2)

    col1, col2 = st.columns(2)
    with col1:
        st.markdown("**Run A**")
        st.json(p1 or {})
        st.metric("Transactions", n1)
        st.metric("Revenue", f"{rev1:.2f}")
    with col2:
        st.markdown("**Run B**")
        st.json(p2 or {})
        st.metric("Transactions", n2)
        st.metric("Revenue", f"{rev2:.2f}")

    st.subheader("Overlaid transaction volume")
    if not t1.empty and not t2.empty and "timestamp" in t1.columns and "timestamp" in t2.columns:
        u1 = t1.drop_duplicates(subset=["transaction_id"]).copy()
        u2 = t2.drop_duplicates(subset=["transaction_id"]).copy()
        u1["ts"] = pd.to_datetime(u1["timestamp"], format="%H:%M:%S", errors="coerce")
        u2["ts"] = pd.to_datetime(u2["timestamp"], format="%H:%M:%S", errors="coerce")
        u1["minute"] = u1["ts"].dt.hour * 60 + u1["ts"].dt.minute
        u2["minute"] = u2["ts"].dt.hour * 60 + u2["ts"].dt.minute
        v1 = u1.groupby("minute").size().reset_index(name="Run A")
        v2 = u2.groupby("minute").size().reset_index(name="Run B")
        merged = v1.merge(v2, on="minute", how="outer").fillna(0)
        st.line_chart(merged.set_index("minute"))


def render_tabs(
    run_params: Dict[str, Any],
    compare_runs: bool,
    run_result: Optional[Any],
    root: str,
) -> None:
    if run_result is None:
        runs = _get_runs_for_compare()
        if not runs:
            st.info("Use the sidebar to run a simulation.")
            return
        _, run_result = runs[-1]

    runs = _get_runs_for_compare()
    tab_names = ["Overview", "Basket & SKUs", "Traffic & Heatmap", "Queue & Checkout"]
    if compare_runs and len(runs) >= 2:
        tab_names.append("Compare")
    tabs = st.tabs(tab_names)

    with tabs[0]:
        _tab_overview(run_result.transactions, run_result.customers)
    with tabs[1]:
        _tab_basket_skus(run_result.transactions)
    with tabs[2]:
        _tab_traffic_heatmap(run_result.simulator)
    with tabs[3]:
        _tab_queue_checkout(run_result.simulator)
    if len(tabs) > 4:
        with tabs[4]:
            _tab_compare(runs)
