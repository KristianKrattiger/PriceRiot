"""
Streamlit dashboard: Simulation and POS Analysis modes.
Run from project root: streamlit run python/dashboard/app.py
"""
import os
import sys

# Windows: required for MinGW-compiled .pyd to load (before any simulation/analytics import)
if os.name == "nt":
    os.add_dll_directory(r"C:\MinGW\mingw64\bin")

# Discover project root (same sentinel pattern as analytics/core.py) before importing analytics
def _discover_root() -> str:
    cur = os.path.dirname(os.path.abspath(__file__))
    for _ in range(6):
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


_ROOT = _discover_root()
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)
_python_dir = os.path.join(_ROOT, "python")
if _python_dir not in sys.path:
    sys.path.insert(0, _python_dir)
for _p in [os.path.join(_ROOT, "build"), os.path.join(_ROOT, "build", "Release")]:
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.insert(0, _p)

import streamlit as st

from analytics import get_project_root, run_simulation

from dashboard.components import pos_charts, sim_charts, sim_controls

st.set_page_config(page_title="PriceRiot Dashboard", layout="wide")

root = get_project_root()

# Sidebar: mode and controls
mode = st.sidebar.radio("Mode", ["Simulation", "POS Analysis"], index=0)

if mode == "Simulation":
    run_params, compare_runs, run_result = sim_controls.render_sidebar(root)
    st.sidebar.markdown("---")
    if run_result is not None:
        st.sidebar.success("Simulation finished. View results in the tabs below.")
    sim_charts.render_tabs(run_params, compare_runs, run_result, root)
else:
    pos_charts.render_all(root)
