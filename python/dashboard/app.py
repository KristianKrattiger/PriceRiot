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

st.set_page_config(page_title="PriceRiot: Retail Twin Engine", layout="wide")

THEME_CSS = """
<style>
:root {
  --pr-bg: #282a36;
  --pr-surface: #1e1f29;
  --pr-text-primary: #f8f8f2;
  --pr-text-muted: #6272a4;
  --pr-accent-purple: #bd93f9;
  --pr-accent-cyan: #8be9fd;
  --pr-green: #50fa7b;
  --pr-orange: #ffb86c;
  --pr-red: #ff5555;
}

html, body, [data-testid="stAppViewContainer"], .stApp {
  background-color: var(--pr-bg) !important;
  color: var(--pr-text-primary) !important;
}

section[data-testid="stSidebar"] {
  background-color: var(--pr-surface) !important;
}

section[data-testid="stSidebar"] * {
  color: var(--pr-text-primary) !important;
}

div.block-container {
  padding-top: 1rem;
}

/* Buttons */
button[kind="primary"], .stButton>button {
  background-color: var(--pr-accent-purple) !important;
  color: var(--pr-bg) !important;
  border-radius: 0.5rem;
  border: 1px solid var(--pr-accent-purple) !important;
}

button[kind="primary"]:hover, .stButton>button:hover {
  background-color: var(--pr-accent-cyan) !important;
  border-color: var(--pr-accent-cyan) !important;
}

/* Metrics */
div[data-testid="stMetric"] {
  background-color: var(--pr-surface);
  padding: 0.75rem 1rem;
  border-radius: 0.75rem;
  border: 1px solid rgba(189, 147, 249, 0.4);
}

div[data-testid="stMetric"] label, div[data-testid="stMetric"] span {
  color: var(--pr-text-primary) !important;
}

/* Sliders */
div[role="slider"] {
  background: linear-gradient(90deg, var(--pr-accent-purple), var(--pr-accent-cyan));
}

/* Status colors */
.pr-success {
  color: var(--pr-green);
}

.pr-warning {
  color: var(--pr-orange);
}

.pr-error {
  color: var(--pr-red);
}

.pr-muted {
  color: var(--pr-text-muted);
}

/* Header */
.pr-header {
  background-color: var(--pr-surface);
  padding: 1.25rem 1.5rem 1rem 1.5rem;
  border-radius: 0.75rem;
  border-bottom: 2px solid var(--pr-accent-purple);
  margin-bottom: 1.0rem;
}

.pr-header-title {
  font-size: 1.8rem;
  font-weight: 800;
  margin: 0;
  color: var(--pr-text-primary);
}

.pr-header-subtitle {
  margin: 0.25rem 0 0 0;
  font-size: 0.95rem;
  color: var(--pr-text-muted);
}
</style>
"""

st.markdown(THEME_CSS, unsafe_allow_html=True)

root = get_project_root()

# Sidebar: ASCII branding then mode and controls
SIDEBAR_ASCII = """
<pre style="
  font-family: ui-monospace, monospace;
  font-size: 0.75rem;
  line-height: 1.2;
  color: #bd93f9;
  margin: 0 0 0.5rem 0;
  white-space: pre;
  overflow-x: auto;
">PriceRiot</pre>
"""
st.sidebar.markdown(SIDEBAR_ASCII, unsafe_allow_html=True)
st.sidebar.markdown("---")
mode = st.sidebar.radio("Mode", ["Simulation", "POS Analysis"], index=0)

if mode == "Simulation":
    run_params, compare_runs, run_result = sim_controls.render_sidebar(root)
    st.sidebar.markdown("---")
    if run_result is not None:
        st.sidebar.success("Simulation finished. View results in the tabs below.")
    sim_charts.render_tabs(run_params, compare_runs, run_result, root)
else:
    pos_charts.render_all(root)
