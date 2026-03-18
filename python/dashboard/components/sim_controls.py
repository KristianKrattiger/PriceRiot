"""
Sidebar controls for Simulation mode: store layout, sliders, Load from POS, Run Simulation, Compare Runs.
"""
import json
import os
import time
from typing import Any, Dict, List, Optional, Tuple

import streamlit as st

from analytics import run_simulation


def _store_layout_options(root: str) -> List[str]:
    examples_dir = os.path.join(root, "examples")
    if not os.path.isdir(examples_dir):
        return ["examples/store_tiny.yaml"]
    yamls = sorted(f for f in os.listdir(examples_dir) if f.endswith(".yaml"))
    return [os.path.join("examples", f) for f in yamls] if yamls else ["examples/store_tiny.yaml"]


def _product_csv_options(root: str) -> List[str]:
    """Enumerate candidate product CSVs under data/raw. Returns relative paths."""
    raw_dir = os.path.join(root, "data", "raw")
    if not os.path.isdir(raw_dir):
        return ["data/raw/products.csv"]
    csvs = sorted(f for f in os.listdir(raw_dir) if f.lower().endswith(".csv"))
    return [os.path.join("data", "raw", f) for f in csvs] if csvs else ["data/raw/products.csv"]


def _load_sim_params(root: str) -> Optional[Dict[str, Any]]:
    path = os.path.join(root, "data", "processed", "sim_params.json")
    if not os.path.isfile(path):
        return None
    with open(path) as f:
        return json.load(f)


def render_sidebar(root: str) -> Tuple[Dict[str, Any], bool, Optional[Any]]:
    """
    Render sidebar controls. Returns (run_params dict, compare_runs bool, latest SimulationResult or None).
    """
    st.sidebar.header("Simulation controls")

    layouts = _store_layout_options(root)
    store_layout = st.sidebar.selectbox("Store layout", layouts, index=0)

    product_csv_options = _product_csv_options(root)
    selected_products_csv = st.sidebar.selectbox("Product CSV", product_csv_options, index=0)
    st.session_state["selected_products_csv"] = selected_products_csv
    st.sidebar.caption(f"Using products from: {selected_products_csv}")

    # Use loaded POS params as defaults if available (set by "Load from POS data")
    loaded = st.session_state.get("loaded_pos_params")
    def_spawn = loaded.get("spawn_interval_seconds", 5.0) if loaded else 5.0
    def_mission = loaded.get("mission_probability", 0.5) if loaded else 0.5
    def_price = loaded.get("price_sensitivity", 0.5) if loaded else 0.5
    def_default = loaded.get("default_probability", def_mission if loaded else 0.5) if loaded else 0.5

    duration = st.sidebar.slider("Duration (seconds)", 60, 3600, 600, 60)
    spawn_default = max(1, min(30, int(def_spawn))) if loaded else 5
    spawn_interval = st.sidebar.slider("Spawn interval (seconds)", 1, 30, spawn_default, 1)
    mission_probability = st.sidebar.slider("Mission probability", 0.0, 1.0, def_mission, 0.05)
    default_probability = st.sidebar.slider("Default probability", 0.0, 1.0, float(def_default), 0.05)
    price_sensitivity = st.sidebar.slider("Price sensitivity", 0.0, 1.0, def_price, 0.05)
    seed = st.sidebar.number_input("Seed (0 = random)", min_value=0, value=0, step=1)

    if st.sidebar.button("Load from POS data"):
        params = _load_sim_params(root)
        if params is None:
            st.sidebar.warning(
                "sim_params.json not found. Run the ingestion pipeline first: "
                "`python -m python.ingestion.run_ingestion --input data/raw/your_pos_file.csv --output data/processed/`"
            )
        else:
            st.session_state["loaded_pos_params"] = params
            st.sidebar.success("Loaded parameters. Sliders updated from sim_params.json.")
            st.rerun()

    compare_runs = st.sidebar.checkbox("Compare runs", value=False)

    # TODO: Include spawn_weight when sending config to FastAPI simulation endpoints.
    run_params = {
        "store_path": store_layout,
        "duration_seconds": float(duration),
        "spawn_interval": float(spawn_interval),
        "mission_probability": mission_probability,
        "spawn_weight": float(default_probability),
        "price_sensitivity": price_sensitivity,
        "seed": int(seed),
    }

    run_result = None
    if st.sidebar.button("Run Simulation"):
        status_box = st.empty()

        steps = [
            "Submitting simulation config...",
            "Initializing agents...",
            "Running simulation...",
            "Collecting results...",
            "Done — loading results",
        ]

        def _render_status(current_step: int) -> None:
            lines = []
            for idx, label in enumerate(steps, start=1):
                if idx < current_step:
                    prefix = '<span style="color:#50fa7b;">✅</span>'
                    color = "#50fa7b"
                elif idx == current_step:
                    prefix = '<span style="color:#ffb86c;">⏳</span>'
                    color = "#ffb86c"
                else:
                    prefix = '<span style="color:#6272a4;">⏳</span>'
                    color = "#6272a4"
                lines.append(f'<div style="color:{color};font-size:0.9rem;">{prefix} {label}</div>')
            status_box.markdown("<br>".join(lines), unsafe_allow_html=True)

        start = time.time()
        try:
            _render_status(1)
            time.sleep(0.1)
            _render_status(2)
            time.sleep(0.1)
            _render_status(3)
            result = run_simulation(
                store_path=store_layout,
                duration_seconds=float(duration),
                spawn_interval=float(spawn_interval),
                mission_probability=mission_probability,
                seed=int(seed),
            )
            _render_status(4)
            elapsed = time.time() - start
            st.sidebar.caption(f"Completed in {elapsed:.1f}s")
            runs: List[Tuple[Dict[str, Any], Any]] = st.session_state.get("sim_runs", [])
            if not compare_runs:
                runs = []
            runs.append((run_params.copy(), result))
            st.session_state["sim_runs"] = runs[-2:] if compare_runs else runs[-1:]
            run_result = result
            _render_status(5)
        except Exception as e:
            status_box.markdown(
                '<div style="color:#ff5555;font-size:0.9rem;">❌ Simulation failed</div>',
                unsafe_allow_html=True,
            )
            st.sidebar.error(str(e))
            run_result = None

    # If we have a stored run from previous click, use it for display
    if run_result is None and st.session_state.get("sim_runs"):
        runs = st.session_state["sim_runs"]
        if isinstance(runs, list) and runs:
            _, run_result = runs[-1]
        else:
            run_result = runs

    return run_params, compare_runs, run_result
