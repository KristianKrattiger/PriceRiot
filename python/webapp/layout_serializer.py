"""
layout_serializer.py — bidirectional conversion between store YAML and frontend layout JSON.

Coordinate convention
---------------------
YAML (C++ engine) : x (depth, left→right), z (width, top↔bottom).
Frontend canvas   : x (horizontal), y (vertical).
Mapping           : frontend.x = yaml.x, frontend.y = yaml.z

Units
-----
New layouts created in the editor store values in feet (units: feet in YAML).
Existing YAMLs without a units key are treated as "legacy" (arbitrary scalars)
and passed through as-is — the C++ engine is scale-agnostic.
"""
from __future__ import annotations

import json
import math
import os
import re
from pathlib import Path
from typing import Any

import yaml


# ---------------------------------------------------------------------------
# Directory helpers
# ---------------------------------------------------------------------------

def layouts_dir(project_root: str) -> str:
    d = os.path.join(project_root, "data", "layouts")
    os.makedirs(d, exist_ok=True)
    return d


def _sidecar_path(yaml_path: str) -> str:
    stem = yaml_path
    for ext in (".yaml", ".yml"):
        if stem.endswith(ext):
            stem = stem[: -len(ext)]
            break
    return stem + ".meta.json"


def _load_sidecar(yaml_path: str) -> list[dict]:
    """Return freeObjects list from companion .meta.json, or []."""
    p = _sidecar_path(yaml_path)
    if os.path.isfile(p):
        with open(p, "r", encoding="utf-8") as f:
            return json.load(f).get("freeObjects", [])
    return []


def _save_sidecar(yaml_path: str, free_objects: list[dict]) -> None:
    p = _sidecar_path(yaml_path)
    with open(p, "w", encoding="utf-8") as f:
        json.dump({"freeObjects": free_objects}, f, indent=2)


# ---------------------------------------------------------------------------
# YAML → JSON  (parse_yaml)
# ---------------------------------------------------------------------------

def _fmt_hour(h: int) -> str:
    return f"{h:02d}:00"


def parse_yaml(path: str) -> dict:
    """
    Parse a store YAML file and return a frontend-ready layout dict.

    The returned dict has keys:
        units, productsFile, boundary, nodes, edges, shelves,
        planogram, checkoutQueues, schedule, freeObjects
    """
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    # ── Nodes ──────────────────────────────────────────────────────────────
    nodes: list[dict] = []
    for n in data.get("nodes", []):
        nodes.append({
            "id":              int(n["id"]),
            "type":            str(n["type"]),
            "x":               float(n.get("x", 0)),
            "y":               float(n.get("z", 0)),   # z → canvas y
            "width":           float(n.get("width",  3.0)),
            "length":          float(n.get("length", 3.0)),
            "shelfLeft":       float(n.get("shelf_left",  0.0)),
            "shelfRight":      float(n.get("shelf_right", 0.0)),
            "blockedFraction": float(n.get("blocked", 0.0)),
            "personalSpace":   float(n.get("personal_space", 1.0)),
            "jamDensity":      float(n.get("jam_density", 3.5)),
            "dwellTime":       float(n.get("dwell_s", 0.0)),
            "serviceRate":     float(n.get("service_rate", 0.0)),
            "entryRate":       float(n.get("entry_rate", 0.0)),
            "exitRate":        float(n.get("exit_rate", 0.0)),
        })

    # ── Edges ──────────────────────────────────────────────────────────────
    edges: list[dict] = []
    for e in data.get("edges", []):
        edges.append({
            "id":              int(e["id"]),
            "nodeA":           int(e["from"]),
            "nodeB":           int(e["to"]),
            "length":          float(e.get("length", 1.0)),
            "width":           float(e.get("width",  3.0)),
            "shelfLeft":       float(e.get("shelf_left",  0.0)),
            "shelfRight":      float(e.get("shelf_right", 0.0)),
            "freeSpeed":       float(e.get("free_speed",  1.0)),
            "jamDensity":      float(e.get("jam_density", 3.5)),
            "blockedFraction": float(e.get("blocked", 0.0)),
            "flow":            str(e.get("flow",        "bi")),
            "orientation":     str(e.get("orientation", "fwd")),
            # wall/aisle tag — preserved if already present, defaulted to "aisle"
            "edgeType":        str(e.get("edge_type", "aisle")),
        })

    # ── Planogram ──────────────────────────────────────────────────────────
    planogram: dict[str, Any] = {}
    plano_yaml = data.get("planogram", {})
    if isinstance(plano_yaml, dict):
        for eid_str, edata in plano_yaml.get("edges", {}).items():
            cells: list[dict] = []
            for side_key, side_label in (("left_side", "left"), ("right_side", "right")):
                side = edata.get(side_key, {})
                for item in side.get("planogram", []):
                    cells.append({
                        "side":       side_label,
                        "bay":        int(item.get("bay",  0)),
                        "face":       int(item.get("face", 0)),
                        "slot":       int(item.get("slot", 0)),
                        "skuId":      item.get("sku"),
                        "onShelfQty": int(item.get("on_shelf_qty", 0)),
                    })
            planogram[str(eid_str)] = {
                "bayCountLeft":  int(edata.get("left_side",  {}).get("bay_count", 1)),
                "bayCountRight": int(edata.get("right_side", {}).get("bay_count", 1)),
                "cells": cells,
            }

    # ── Checkout queues ────────────────────────────────────────────────────
    checkout_queues: list[dict] = []
    for q in data.get("checkout_queues", []):
        checkout_queues.append({
            "registerId":     int(q["register_id"]),
            "processingTime": float(q.get("processing_time", 5.0)),
            "waypoints":      [{"x": float(w["x"]), "y": float(w["z"])}
                               for w in q.get("waypoints", [])],
        })

    # ── Operating schedule ─────────────────────────────────────────────────
    days_of_op: list[str] = data.get("days_of_operation",
                                     ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat"])
    hours_raw: dict = data.get("hours_of_operation", {})
    hours: dict[str, dict] = {}
    for day, window in hours_raw.items():
        if isinstance(window, dict):
            hours[day] = {
                "open":  _fmt_hour(int(window.get("open",  9))),
                "close": _fmt_hour(int(window.get("close", 19))),
            }

    # ── Boundary — computed from node positions when not stored ────────────
    boundary = _compute_boundary(nodes)

    # ── freeObjects from sidecar ───────────────────────────────────────────
    free_objects = _load_sidecar(path)

    return {
        "units":         data.get("units", "legacy"),
        "productsFile":  data.get("products_file"),
        "boundary":      boundary,
        "nodes":         nodes,
        "edges":         edges,
        "shelves":       [],          # shelves are derived from edge planogram for now
        "planogram":     planogram,
        "checkoutQueues": checkout_queues,
        "schedule": {
            "daysOfOperation": days_of_op,
            "hours": hours,
        },
        "freeObjects":   free_objects,
    }


def _compute_boundary(nodes: list[dict]) -> list[dict]:
    """Derive a bounding-box boundary polygon from node positions + padding."""
    if not nodes:
        return [
            {"x": 0,   "y": 0},
            {"x": 100, "y": 0},
            {"x": 100, "y": 60},
            {"x": 0,   "y": 60},
        ]
    xs = [n["x"] for n in nodes]
    ys = [n["y"] for n in nodes]
    pad = 6.0
    min_x, max_x = min(xs) - pad, max(xs) + pad
    min_y, max_y = min(ys) - pad, max(ys) + pad
    return [
        {"x": min_x, "y": min_y},
        {"x": max_x, "y": min_y},
        {"x": max_x, "y": max_y},
        {"x": min_x, "y": max_y},
    ]


# ---------------------------------------------------------------------------
# JSON → YAML  (to_yaml)
# ---------------------------------------------------------------------------

def to_yaml(layout: dict) -> str:
    """Serialize a frontend layout dict to a store YAML string."""

    def _r(v: float, d: int = 4) -> float:
        return round(float(v), d)

    nodes_out: list[dict] = []
    for n in layout.get("nodes", []):
        nodes_out.append({
            "id":            int(n["id"]),
            "type":          str(n["type"]),
            "x":             _r(n["x"]),
            "z":             _r(n["y"]),          # canvas y → YAML z
            "length":        _r(n.get("length", 3.0)),
            "width":         _r(n.get("width",  3.0)),
            "shelf_left":    _r(n.get("shelfLeft",  0.0)),
            "shelf_right":   _r(n.get("shelfRight", 0.0)),
            "blocked":       _r(n.get("blockedFraction", 0.0)),
            "personal_space": _r(n.get("personalSpace", 1.0)),
            "jam_density":   _r(n.get("jamDensity", 3.5)),
            "dwell_s":       _r(n.get("dwellTime",  0.0)),
            "service_rate":  _r(n.get("serviceRate", 0.0)),
            "agents":        0,
            "entry_rate":    _r(n.get("entryRate", 0.0)),
            "exit_rate":     _r(n.get("exitRate",  0.0)),
        })

    edges_out: list[dict] = []
    for e in layout.get("edges", []):
        edges_out.append({
            "id":          int(e["id"]),
            "from":        int(e["nodeA"]),
            "to":          int(e["nodeB"]),
            "length":      _r(e.get("length", 1.0)),
            "width":       _r(e.get("width",  3.0)),
            "free_speed":  _r(e.get("freeSpeed", 1.0)),
            "jam_density": _r(e.get("jamDensity", 3.5)),
            "blocked":     _r(e.get("blockedFraction", 0.0)),
            "flow":        str(e.get("flow",        "bi")),
            "orientation": str(e.get("orientation", "fwd")),
            "shelf_left":  _r(e.get("shelfLeft",  0.0)),
            "shelf_right": _r(e.get("shelfRight", 0.0)),
            "edge_type":   str(e.get("edgeType", "aisle")),
        })

    # planogram
    plano_edges: dict = {}
    for eid_str, plano in layout.get("planogram", {}).items():
        cells = plano.get("cells", [])
        left_cells  = [c for c in cells if c.get("side") == "left"]
        right_cells = [c for c in cells if c.get("side") == "right"]

        def _cells(cell_list: list) -> list:
            return [
                {
                    "bay":          int(c.get("bay",  0)),
                    "face":         int(c.get("face", 0)),
                    "slot":         int(c.get("slot", 0)),
                    "sku":          c.get("skuId"),
                    "on_shelf_qty": int(c.get("onShelfQty", 20)),
                }
                for c in cell_list
            ]

        plano_edges[eid_str] = {
            "left_side":  {"bay_count": int(plano.get("bayCountLeft",  1)), "planogram": _cells(left_cells)},
            "right_side": {"bay_count": int(plano.get("bayCountRight", 1)), "planogram": _cells(right_cells)},
        }

    # checkout queues
    queues_out: list[dict] = []
    for q in layout.get("checkoutQueues", []):
        queues_out.append({
            "register_id":    int(q["registerId"]),
            "processing_time": float(q.get("processingTime", 5.0)),
            "waypoints":      [{"x": float(w["x"]), "z": float(w["y"])}
                               for w in q.get("waypoints", [])],
        })

    # schedule
    sched    = layout.get("schedule", {})
    days_op  = sched.get("daysOfOperation", ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat"])
    hours_in = sched.get("hours", {})
    hours_out: dict = {}
    for day, window in hours_in.items():
        open_h  = int(str(window.get("open",  "09:00")).split(":")[0])
        close_h = int(str(window.get("close", "19:00")).split(":")[0])
        hours_out[day] = {"open": open_h, "close": close_h}

    doc: dict[str, Any] = {"units": layout.get("units", "feet")}
    if layout.get("productsFile"):
        doc["products_file"] = layout["productsFile"]
    doc["days_of_operation"]  = days_op
    doc["hours_of_operation"] = hours_out
    doc["nodes"] = nodes_out
    doc["edges"] = edges_out
    if plano_edges:
        doc["planogram"] = {"edges": plano_edges}
    if queues_out:
        doc["checkout_queues"] = queues_out

    return yaml.dump(doc, default_flow_style=False, sort_keys=False, allow_unicode=True)


# ---------------------------------------------------------------------------
# Save with versioning
# ---------------------------------------------------------------------------

def auto_version(name: str, layouts_directory: str) -> str:
    """
    Return a filename stem that does not already exist in layouts_directory.
    Strips any existing _vN suffix, then increments: name → name_v2 → name_v3 …
    """
    base = re.sub(r"_v\d+$", "", name)
    candidate = base
    n = 1
    while os.path.isfile(os.path.join(layouts_directory, candidate + ".yaml")):
        n += 1
        candidate = f"{base}_v{n}"
    return candidate


def save_layout(name: str, overwrite: bool, layout: dict,
                layouts_directory: str) -> tuple[str, str]:
    """
    Persist layout to YAML (+ freeObjects sidecar).

    Returns (filename, version_stem).
    If overwrite=False and name.yaml exists, auto-versions.
    """
    stem = name if overwrite else auto_version(name, layouts_directory)
    yaml_path = os.path.join(layouts_directory, stem + ".yaml")
    yaml_text = to_yaml(layout)
    with open(yaml_path, "w", encoding="utf-8") as f:
        f.write(yaml_text)
    if layout.get("freeObjects"):
        _save_sidecar(yaml_path, layout["freeObjects"])
    return stem + ".yaml", stem


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------

def validate(layout: dict) -> list[str]:
    """Return a list of validation error strings. Empty list = valid."""
    errors: list[str] = []
    nodes = layout.get("nodes", [])
    edges = layout.get("edges", [])

    types = [n.get("type") for n in nodes]
    if types.count("Entrance") < 1:
        errors.append("Layout must have at least one Entrance node.")
    if types.count("Exit") < 1:
        errors.append("Layout must have at least one Exit node.")

    # Connectivity via BFS
    node_ids = {n["id"] for n in nodes}
    adj: dict[int, set[int]] = {nid: set() for nid in node_ids}
    for e in edges:
        a, b = e.get("nodeA"), e.get("nodeB")
        if a in adj:
            adj[a].add(b)
        if b in adj:
            adj[b].add(a)

    if node_ids:
        start  = next(iter(node_ids))
        visited: set[int] = set()
        queue  = [start]
        while queue:
            cur = queue.pop()
            if cur in visited:
                continue
            visited.add(cur)
            queue.extend(adj.get(cur, set()) - visited)
        unreachable = node_ids - visited
        if unreachable:
            errors.append(
                f"Disconnected graph: node(s) {sorted(unreachable)} unreachable."
            )

    # Every Register must have a checkout_queue entry
    register_ids     = {n["id"] for n in nodes if n.get("type") == "Register"}
    queue_reg_ids    = {q["registerId"] for q in layout.get("checkoutQueues", [])}
    missing_queues   = register_ids - queue_reg_ids
    if missing_queues:
        errors.append(
            f"Register node(s) {sorted(missing_queues)} have no checkout_queue entry."
        )

    return errors


# ---------------------------------------------------------------------------
# Wizard default layout generator
# ---------------------------------------------------------------------------

def wizard_default_layout(
    name: str,
    width_ft: float,
    depth_ft: float,
    days_of_operation: list[str],
    hours: dict[str, dict],
    products_file: str | None = None,
) -> dict:
    """
    Generate a minimal valid layout from wizard inputs.

    Graph:  Entrance(1) ──wall─→ CenterJunction(2) ──wall─→ Exit(3)
    Both edges are tagged edgeType="wall" so they appear as boundary paths.
    A canvas banner will prompt the user to add interior aisles.
    """
    cx, cy = width_ft / 2, depth_ft / 2          # store centre

    nodes = [
        {
            "id": 1, "type": "Entrance",
            "x": 0.0,       "y": cy,
            "width": 10.0,  "length": 8.0,
            "shelfLeft": 0, "shelfRight": 0,
            "blockedFraction": 0, "personalSpace": 1.0,
            "jamDensity": 3.5, "dwellTime": 0, "serviceRate": 0,
            "entryRate": 1.0, "exitRate": 0.0,
        },
        {
            "id": 2, "type": "Junction",
            "x": cx,        "y": cy,
            "width": 6.0,   "length": 6.0,
            "shelfLeft": 0, "shelfRight": 0,
            "blockedFraction": 0, "personalSpace": 1.0,
            "jamDensity": 3.5, "dwellTime": 0, "serviceRate": 0,
            "entryRate": 0.0, "exitRate": 0.0,
        },
        {
            "id": 3, "type": "Exit",
            "x": width_ft,  "y": cy,
            "width": 10.0,  "length": 8.0,
            "shelfLeft": 0, "shelfRight": 0,
            "blockedFraction": 0, "personalSpace": 1.0,
            "jamDensity": 3.5, "dwellTime": 0, "serviceRate": 0,
            "entryRate": 0.0, "exitRate": 1.0,
        },
    ]

    edges = [
        {
            "id": 1, "nodeA": 1, "nodeB": 2,
            "length": _r2(cx), "width": 12.0,
            "shelfLeft": 0, "shelfRight": 0,
            "freeSpeed": 1.2, "jamDensity": 3.5, "blockedFraction": 0,
            "flow": "bi", "orientation": "fwd", "edgeType": "wall",
        },
        {
            "id": 2, "nodeA": 2, "nodeB": 3,
            "length": _r2(cx), "width": 12.0,
            "shelfLeft": 0, "shelfRight": 0,
            "freeSpeed": 1.2, "jamDensity": 3.5, "blockedFraction": 0,
            "flow": "bi", "orientation": "fwd", "edgeType": "wall",
        },
    ]

    boundary = [
        {"x": 0,         "y": 0},
        {"x": width_ft,  "y": 0},
        {"x": width_ft,  "y": depth_ft},
        {"x": 0,         "y": depth_ft},
    ]

    return {
        "units":          "feet",
        "productsFile":   products_file,
        "boundary":       boundary,
        "nodes":          nodes,
        "edges":          edges,
        "shelves":        [],
        "planogram":      {},
        "checkoutQueues": [],
        "schedule": {
            "daysOfOperation": days_of_operation,
            "hours": hours,
        },
        "freeObjects": [],
        "_wizardNew": True,          # frontend uses this to show the "add aisles" banner
    }


def _r2(v: float) -> float:
    return round(float(v), 2)
