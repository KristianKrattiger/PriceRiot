/**
 * useLayoutStore — Pinia store for the Layout tab.
 *
 * Coordinate convention: all positions in feet (canvas x/y).
 * The backend layout_serializer handles YAML ↔ JSON translation.
 */
import { defineStore } from "pinia";
import {
  fetchLayouts,
  fetchLayout,
  saveLayout as apiSaveLayout,
  deleteLayout as apiDeleteLayout,
} from "../api/client";
import { sameEntityId } from "../utils/entityIds.js";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Deep-clone only the mutable layout state (for undo snapshots). */
function cloneLayoutState(state) {
  return JSON.parse(
    JSON.stringify({
      nodes:          state.nodes,
      edges:          state.edges,
      shelves:        state.shelves,
      freeObjects:    state.freeObjects,
      planogram:      state.planogram,
      checkoutQueues: state.checkoutQueues,
      boundary:       state.boundary,
    })
  );
}

const MAX_HISTORY = 50;

function toFiniteNumber(value, fallback = 0) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function sanitizeNodePatch(patch = {}) {
  const out = { ...patch };
  if ("id" in out) out.id = Math.trunc(toFiniteNumber(out.id, 0));
  if ("x" in out) out.x = toFiniteNumber(out.x, 0);
  if ("y" in out) out.y = toFiniteNumber(out.y, 0);
  if ("width" in out) out.width = Math.max(0.1, toFiniteNumber(out.width, 6));
  if ("length" in out) out.length = Math.max(0.1, toFiniteNumber(out.length, 6));
  if ("jamDensity" in out) out.jamDensity = clamp(toFiniteNumber(out.jamDensity, 3.5), 0.0, 100.0);
  if ("personalSpace" in out) out.personalSpace = clamp(toFiniteNumber(out.personalSpace, 1.0), 0.0, 20.0);
  if ("dwellTime" in out) out.dwellTime = Math.max(0.0, toFiniteNumber(out.dwellTime, 0.0));
  if ("serviceRate" in out) out.serviceRate = Math.max(0.0, toFiniteNumber(out.serviceRate, 0.0));
  if ("entryRate" in out) out.entryRate = Math.max(0.0, toFiniteNumber(out.entryRate, 0.0));
  if ("exitRate" in out) out.exitRate = Math.max(0.0, toFiniteNumber(out.exitRate, 0.0));
  if ("blockedFraction" in out) out.blockedFraction = clamp(toFiniteNumber(out.blockedFraction, 0.0), 0.0, 1.0);
  if ("angle" in out) out.angle = toFiniteNumber(out.angle, 0.0);
  return out;
}

function sanitizeEdgePatch(patch = {}) {
  const out = { ...patch };
  if ("id" in out) out.id = Math.trunc(toFiniteNumber(out.id, 0));
  if ("nodeA" in out) out.nodeA = Math.trunc(toFiniteNumber(out.nodeA, 0));
  if ("nodeB" in out) out.nodeB = Math.trunc(toFiniteNumber(out.nodeB, 0));
  if ("width" in out) out.width = Math.max(0.1, toFiniteNumber(out.width, 10));
  if ("length" in out) out.length = Math.max(0.1, toFiniteNumber(out.length, 1));
  if ("shelfLeft" in out) out.shelfLeft = Math.max(0.0, toFiniteNumber(out.shelfLeft, 0.0));
  if ("shelfRight" in out) out.shelfRight = Math.max(0.0, toFiniteNumber(out.shelfRight, 0.0));
  if ("freeSpeed" in out) out.freeSpeed = Math.max(0.0, toFiniteNumber(out.freeSpeed, 1.2));
  if ("jamDensity" in out) out.jamDensity = clamp(toFiniteNumber(out.jamDensity, 3.5), 0.0, 100.0);
  if ("blockedFraction" in out) out.blockedFraction = clamp(toFiniteNumber(out.blockedFraction, 0.0), 0.0, 1.0);
  if ("flow" in out) out.flow = out.flow === "bi" ? "bi" : "uni";
  if ("orientation" in out) out.orientation = out.orientation === "bwd" ? "bwd" : "fwd";
  if ("edgeType" in out) out.edgeType = out.edgeType === "wall" ? "wall" : "aisle";
  return out;
}

function sanitizeFreeObjectPatch(patch = {}) {
  const out = { ...patch };
  if ("id" in out) out.id = Math.trunc(toFiniteNumber(out.id, 0));
  if ("x" in out) out.x = toFiniteNumber(out.x, 0);
  if ("y" in out) out.y = toFiniteNumber(out.y, 0);
  if ("rotation" in out) out.rotation = toFiniteNumber(out.rotation, 0);
  if ("widthFt" in out) out.widthFt = Math.max(0.1, toFiniteNumber(out.widthFt, 2));
  if ("lengthFt" in out) out.lengthFt = Math.max(0.1, toFiniteNumber(out.lengthFt, 2));
  return out;
}

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

export const useLayoutStore = defineStore("layout", {
  state: () => ({
    // ── File state ────────────────────────────────────────────────────────
    filename:    null,   // "cowboy_market.yaml"
    isDirty:     false,
    lastSavedAt: null,
    isLoading:   false,
    loadError:   null,

    // ── Available layouts (from /api/layouts) ────────────────────────────
    availableLayouts:  [],
    layoutsLoading:    false,

    // ── Store definition ─────────────────────────────────────────────────
    units:       "feet",
    productsFile: null,
    sqFootage:   0,
    shape:       "rectangle",
    boundary:    [],          // [{x, y}] polygon vertices in feet

    // ── Graph topology ───────────────────────────────────────────────────
    nodes:  [],   // [{id, type, x, y, width, length, dwellTime, serviceRate,
                  //   personalSpace, jamDensity, entryRate, exitRate}]
    edges:  [],   // [{id, nodeA, nodeB, width, length, flow, orientation,
                  //   shelfLeft, shelfRight, freeSpeed, blockedFraction, edgeType}]

    // ── Fixtures ─────────────────────────────────────────────────────────
    shelves: [],  // [{id, edgeId, offsetAlongEdge, lengthFt, widthFt, slots, rows, side}]

    // ── freestanding objects (sidecar only) ─────────────────────────────
    freeObjects: [],  // [{id, type, x, y, rotation, widthFt, lengthFt, label}]

    // ── Planogram ────────────────────────────────────────────────────────
    planogram:      {},   // edgeId → {bayCountLeft, bayCountRight, cells:[...]}
    checkoutQueues: [],

    // ── Products (from CSV) ──────────────────────────────────────────────
    products:       [],
    productCsvName: null,
    posCsvName:     null,

    // ── Operating schedule ───────────────────────────────────────────────
    schedule: {
      daysOfOperation: [],
      hours: {},
    },

    // ── Editor state ─────────────────────────────────────────────────────
    selection:    null,     // {type: "node"|"edge"|"shelf"|"freeObject", id}
    multiSelection: [],     // [{type, id}] — shift-click / marquee selection
    activeTool:   "select", // "select"|"shelf"|"endcap"|"bin"|"display"|"register"|"entrance"|"exit"|"boundary"
    planogramOpen: false,
    wizardNew:    false,    // true → show "add aisles" banner
    waypointEditRegisterId: null,  // non-null → canvas renders/drags that register's waypoints

    // ── Viewport ─────────────────────────────────────────────────────────
    viewport:   { zoom: 1.0, panX: 0, panY: 0 },
    showGrid:   true,
    snapToGrid: true,
    gridSizeFt: 1.0,

    // ── Undo / redo (command-pattern — Phase 2 will replace full snapshots) ──
    // Phase 1: simple snapshots; will be replaced with op+inverse in Phase 2
    history:      [],  // array of cloneLayoutState() snapshots
    historyIndex: -1,
    historyBatchDepth: 0,
    historyBatchDirty: false,
  }),

  // ---------------------------------------------------------------------------
  getters: {
    hasLayout:  (state) => state.nodes.length > 0 || state.boundary.length > 0,
    canUndo:    (state) => state.historyIndex > 0,
    canRedo:    (state) => state.historyIndex < state.history.length - 1,
    hasEntrance:(state) => state.nodes.some((n) => n.type === "Entrance"),
    hasExit:    (state) => state.nodes.some((n) => n.type === "Exit"),
    nodeById:   (state) => (id) =>
      state.nodes.find((n) => sameEntityId(n.id, id)) ?? null,
    edgeById:   (state) => (id) =>
      state.edges.find((e) => sameEntityId(e.id, id)) ?? null,
    wallEdges:  (state) => state.edges.filter((e) => e.edgeType === "wall"),
    selectedNode: (state) =>
      state.selection?.type === "node"
        ? state.nodes.find((n) => sameEntityId(n.id, state.selection.id)) ?? null
        : null,
    selectedEdge: (state) =>
      state.selection?.type === "edge"
        ? state.edges.find((e) => sameEntityId(e.id, state.selection.id)) ?? null
        : null,
  },

  // ---------------------------------------------------------------------------
  actions: {
    // ── Available layouts ────────────────────────────────────────────────
    async fetchAvailableLayouts() {
      this.layoutsLoading = true;
      try {
        this.availableLayouts = await fetchLayouts();
      } finally {
        this.layoutsLoading = false;
      }
    },

    // ── Load a layout from the API ───────────────────────────────────────
    async loadLayout(filename) {
      this.isLoading  = true;
      this.loadError  = null;
      try {
        const data = await fetchLayout(filename);
        this._applyLayoutData(data);
        this.filename = filename;
        this.isDirty  = false;
        this._resetHistory();
      } catch (err) {
        this.loadError = err?.message ?? String(err);
        throw err;
      } finally {
        this.isLoading = false;
      }
    },

    /** Apply a raw layout dict (from API or wizard) into store state. */
    _applyLayoutData(data) {
      this.units          = data.units         ?? "feet";
      this.productsFile   = data.productsFile  ?? null;
      this.boundary       = data.boundary      ?? [];
      this.nodes          = (data.nodes ?? []).map((n) => sanitizeNodePatch(n));
      this.edges          = (data.edges ?? []).map((e) => {
        const edge = sanitizeEdgePatch(e);
        // Back-compat with legacy UI values.
        if (e?.flow === "fwd") {
          edge.flow = "uni";
          edge.orientation = "fwd";
        } else if (e?.flow === "rev") {
          edge.flow = "uni";
          edge.orientation = "bwd";
        }
        return edge;
      });
      this.shelves        = (data.shelves ?? []).map((s) => {
        if (!s || typeof s !== "object") return s;
        const o = { ...s };
        if ("id" in o) o.id = Math.trunc(toFiniteNumber(o.id, 0));
        if ("edgeId" in o) o.edgeId = Math.trunc(toFiniteNumber(o.edgeId, 0));
        return o;
      });
      this.freeObjects    = (data.freeObjects ?? []).map((f) => sanitizeFreeObjectPatch(f));
      this.planogram      = data.planogram     ?? {};
      this.checkoutQueues = (data.checkoutQueues ?? []).map((q) => ({
        ...q,
        registerId: Math.trunc(toFiniteNumber(q.registerId, 0)),
        processingTime: Math.max(0.0, toFiniteNumber(q.processingTime, 5.0)),
        waypoints: (q.waypoints ?? [{ x: 0, y: 0 }]).map((wp) => ({
          x: toFiniteNumber(wp.x, 0),
          y: toFiniteNumber(wp.y, 0),
        })),
      }));
      this.schedule       = data.schedule      ?? { daysOfOperation: [], hours: {} };
      this.wizardNew      = data._wizardNew    ?? false;
      this.selection      = null;
      this.planogramOpen  = false;
      this.posCsvName     = data.posCsvName ?? null;
      this.products       = [];
      this.productCsvName = null;
      // Auto-load product catalog if layout has one attached
      if (this.productsFile) this.fetchProducts(this.productsFile);
    },

    /** Load wizard-generated layout (not yet saved to disk). */
    loadWizardLayout(layoutJson, name) {
      this._applyLayoutData(layoutJson);
      this.filename = name ? name + ".yaml" : null;
      this.isDirty  = true;
      this._resetHistory();
    },

    // ── Save ─────────────────────────────────────────────────────────────
    async saveLayout(name, overwrite) {
      const layoutPayload = {
        units:          this.units,
        productsFile:   this.productsFile,
        posCsvName:     this.posCsvName,
        boundary:       this.boundary,
        nodes:          this.nodes,
        edges:          this.edges,
        shelves:        this.shelves,
        freeObjects:    this.freeObjects,
        planogram:      this.planogram,
        checkoutQueues: this.checkoutQueues,
        schedule:       this.schedule,
      };
      const result = await apiSaveLayout(name, overwrite, layoutPayload);
      this.filename    = result.filename;
      this.isDirty     = false;
      this.lastSavedAt = Date.now();
      await this.fetchAvailableLayouts();
      return result;
    },

    async deleteLayout(filename) {
      await apiDeleteLayout(filename);
      if (this.filename === filename) {
        this._clearLayout();
      }
      await this.fetchAvailableLayouts();
    },

    _clearLayout() {
      this.filename       = null;
      this.isDirty        = false;
      this.boundary       = [];
      this.nodes          = [];
      this.edges          = [];
      this.shelves        = [];
      this.freeObjects    = [];
      this.planogram      = {};
      this.checkoutQueues = [];
      this.schedule       = { daysOfOperation: [], hours: {} };
      this.selection      = null;
      this.wizardNew      = false;
      this._resetHistory();
    },

    // ── Viewport ─────────────────────────────────────────────────────────
    setViewport(vp) {
      this.viewport = { ...this.viewport, ...vp };
    },

    // ── Selection ────────────────────────────────────────────────────────
    select(type, id) {
      this.selection = { type, id };
      if (type !== "node") this.waypointEditRegisterId = null;
    },
    clearSelection() {
      this.selection              = null;
      this.multiSelection         = [];
      this.planogramOpen          = false;
      this.waypointEditRegisterId = null;
    },
    setMultiSelection(items) {
      this.multiSelection = items;
      this.selection      = null;
    },
    toggleMultiSelection(type, id) {
      const idx = this.multiSelection.findIndex(
        (s) => s.type === type && sameEntityId(s.id, id)
      );
      if (idx >= 0) {
        this.multiSelection.splice(idx, 1);
      } else {
        this.multiSelection.push({ type, id });
      }
      this.selection = null;
    },
    clearMultiSelection() {
      this.multiSelection = [];
    },
    /** Delete all items in multiSelection in a single undo-able step. */
    removeMultiSelected() {
      if (!this.multiSelection.length) return;
      const toRemove = [...this.multiSelection];
      this.multiSelection = [];
      this.selection = null;
      const edgeIds = toRemove.filter((s) => s.type === "edge").map((s) => s.id);
      const nodeIds = toRemove.filter((s) => s.type === "node").map((s) => s.id);
      const foIds   = toRemove.filter((s) => s.type === "freeObject").map((s) => s.id);
      for (const id of edgeIds) {
        this.edges = this.edges.filter((e) => !sameEntityId(e.id, id));
        delete this.planogram[String(id)];
      }
      for (const id of nodeIds) {
        this.edges = this.edges.filter(
          (e) => !sameEntityId(e.nodeA, id) && !sameEntityId(e.nodeB, id)
        );
        this.nodes = this.nodes.filter((n) => !sameEntityId(n.id, id));
        this.checkoutQueues = this.checkoutQueues.filter(
          (q) => !sameEntityId(q.registerId, id)
        );
        if (sameEntityId(this.waypointEditRegisterId, id)) this.waypointEditRegisterId = null;
      }
      for (const id of foIds) {
        this.freeObjects = this.freeObjects.filter((f) => !sameEntityId(f.id, id));
      }
      this._recordMutation();
    },

    // ── Multi-selection bulk operations ──────────────────────────────────
    /**
     * Align all multi-selected nodes along one axis.
     * axis:   "x" | "y"
     * anchor: "min" | "max" | "center"
     */
    alignSelectedNodes(axis, anchor) {
      const nodeIds = this.multiSelection
        .filter((s) => s.type === "node")
        .map((s) => s.id);
      if (nodeIds.length < 2) return;
      const nodes = this.nodes.filter((n) =>
        nodeIds.some((id) => sameEntityId(n.id, id))
      );
      const vals  = nodes.map((n) => (axis === "x" ? n.x : n.y));
      const target =
        anchor === "min"    ? Math.min(...vals)
        : anchor === "max"  ? Math.max(...vals)
        : vals.reduce((a, b) => a + b, 0) / vals.length;
      this._pushHistory();
      for (const n of nodes) {
        const idx = this.nodes.findIndex((nd) => sameEntityId(nd.id, n.id));
        if (idx === -1) continue;
        this.nodes.splice(idx, 1, {
          ...this.nodes[idx],
          ...(axis === "x" ? { x: parseFloat(target.toFixed(3)) } : { y: parseFloat(target.toFixed(3)) }),
        });
      }
      this._recordMutation();
    },

    /** Distribute multi-selected nodes evenly along one axis. */
    distributeSelectedNodes(axis) {
      const nodeIds = this.multiSelection
        .filter((s) => s.type === "node")
        .map((s) => s.id);
      if (nodeIds.length < 3) return;
      const nodes = this.nodes
        .filter((n) => nodeIds.some((id) => sameEntityId(n.id, id)))
        .slice()
        .sort((a, b) => (axis === "x" ? a.x - b.x : a.y - b.y));
      const min  = axis === "x" ? nodes[0].x : nodes[0].y;
      const max  = axis === "x" ? nodes[nodes.length - 1].x : nodes[nodes.length - 1].y;
      const step = (max - min) / (nodes.length - 1);
      this._pushHistory();
      for (let i = 1; i < nodes.length - 1; i++) {
        const target = parseFloat((min + step * i).toFixed(3));
        const idx    = this.nodes.findIndex((n) => sameEntityId(n.id, nodes[i].id));
        if (idx === -1) continue;
        this.nodes.splice(idx, 1, {
          ...this.nodes[idx],
          ...(axis === "x" ? { x: target } : { y: target }),
        });
      }
      this._recordMutation();
    },

    /** Set a property on all multi-selected edges in one undo step. */
    bulkUpdateEdges(patch) {
      const edgeIds = this.multiSelection
        .filter((s) => s.type === "edge")
        .map((s) => s.id);
      if (!edgeIds.length) return;
      this._pushHistory();
      const sanitized = sanitizeEdgePatch(patch);
      for (const id of edgeIds) {
        const idx = this.edges.findIndex((e) => sameEntityId(e.id, id));
        if (idx === -1) continue;
        this.edges.splice(idx, 1, { ...this.edges[idx], ...sanitized });
      }
      this._recordMutation();
    },

    // ── Tool ─────────────────────────────────────────────────────────────
    setActiveTool(tool) {
      this.activeTool = tool;
      if (tool !== "select") this.selection = null;
    },

    // ── Undo / redo (snapshot-based; replaced by command pattern in Phase 2) ─
    _resetHistory() {
      this.history      = [cloneLayoutState(this)];
      this.historyIndex = 0;
    },

    _pushHistory() {
      // Truncate any redo states
      this.history = this.history.slice(0, this.historyIndex + 1);
      this.history.push(cloneLayoutState(this));
      if (this.history.length > MAX_HISTORY) {
        this.history.shift();
      } else {
        this.historyIndex++;
      }
    },

    _recordMutation() {
      this.isDirty = true;
      if (this.historyBatchDepth > 0) {
        this.historyBatchDirty = true;
        return;
      }
      this._pushHistory();
    },

    beginBatchEdit() {
      this.historyBatchDepth++;
    },

    commitBatchEdit() {
      if (this.historyBatchDepth <= 0) return;
      this.historyBatchDepth--;
      if (this.historyBatchDepth === 0 && this.historyBatchDirty) {
        this.historyBatchDirty = false;
        this._pushHistory();
      }
    },

    undo() {
      if (!this.canUndo) return;
      this.historyIndex--;
      this._applySnapshot(this.history[this.historyIndex]);
    },

    redo() {
      if (!this.canRedo) return;
      this.historyIndex++;
      this._applySnapshot(this.history[this.historyIndex]);
    },

    _applySnapshot(snap) {
      this.nodes          = snap.nodes;
      this.edges          = snap.edges;
      this.shelves        = snap.shelves;
      this.freeObjects    = snap.freeObjects;
      this.planogram      = snap.planogram;
      this.checkoutQueues = snap.checkoutQueues;
      this.boundary       = snap.boundary;
      this.isDirty        = true;
    },

    // ── Mutation helpers ──────────────────────────────────────────────────
    updateNode(id, patch) {
      const idx = this.nodes.findIndex((n) => sameEntityId(n.id, id));
      if (idx === -1) return;
      this.nodes.splice(idx, 1, { ...this.nodes[idx], ...sanitizeNodePatch(patch) });
      this._recordMutation();
    },

    updateEdge(id, patch) {
      const idx = this.edges.findIndex((e) => sameEntityId(e.id, id));
      if (idx === -1) return;
      this.edges.splice(idx, 1, { ...this.edges[idx], ...sanitizeEdgePatch(patch) });
      this._recordMutation();
    },

    setEdgeDirectionMode(id, mode) {
      const idx = this.edges.findIndex((e) => sameEntityId(e.id, id));
      if (idx === -1) return;
      if (mode === "bi") {
        this.edges.splice(idx, 1, { ...this.edges[idx], flow: "bi", orientation: "fwd" });
      } else if (mode === "oneWayReverse") {
        this.edges.splice(idx, 1, { ...this.edges[idx], flow: "uni", orientation: "bwd" });
      } else {
        this.edges.splice(idx, 1, { ...this.edges[idx], flow: "uni", orientation: "fwd" });
      }
      this._recordMutation();
    },

    swapEdgeEndpoints(id) {
      const idx = this.edges.findIndex((e) => sameEntityId(e.id, id));
      if (idx === -1) return;
      const e = this.edges[idx];
      this.edges.splice(idx, 1, { ...e, nodeA: e.nodeB, nodeB: e.nodeA });
      this._recordMutation();
    },

    // ── Graph editing (Phase 2) ───────────────────────────────────────────
    addNode(patch) {
      const id = this.nodes.length ? Math.max(...this.nodes.map((n) => n.id)) + 1 : 1;
      const typeDefaults = {
        Entrance:  { width: 10, length: 8,  entryRate: 1.0, exitRate: 0.0 },
        Exit:      { width: 10, length: 8,  entryRate: 0.0, exitRate: 1.0 },
        Register:  { width: 8,  length: 4,  serviceRate: 0.5 },
        Stockroom: { width: 12, length: 10 },
      };
      const node = {
        id,
        type: "Junction",
        x: 0, y: 0,
        width: 6, length: 6,
        shelfLeft: 0, shelfRight: 0,
        blockedFraction: 0, personalSpace: 1.0,
        jamDensity: 3.5, dwellTime: 0, serviceRate: 0,
        entryRate: 0.0, exitRate: 0.0,
        ...(typeDefaults[patch?.type] ?? {}),
        ...patch,
        id, // ensure id isn't overwritten by patch
      };
      this.nodes.push(sanitizeNodePatch(node));
      // Auto-create checkout queue for Register nodes
      if (node.type === "Register") {
        this.checkoutQueues.push({
          registerId:     id,
          processingTime: 5.0,
          waypoints:      [{ x: parseFloat((node.x + 6).toFixed(3)), y: parseFloat(node.y.toFixed(3)) }],
        });
      }
      this._recordMutation();
      return id;
    },

    addEdge(nodeAId, nodeBId, patch) {
      const id = this.edges.length ? Math.max(...this.edges.map((e) => e.id)) + 1 : 1;
      const na = this.nodes.find((n) => n.id === nodeAId);
      const nb = this.nodes.find((n) => n.id === nodeBId);
      const length = na && nb
        ? parseFloat(Math.hypot(nb.x - na.x, nb.y - na.y).toFixed(2))
        : 10;
      const edge = {
        id, nodeA: nodeAId, nodeB: nodeBId,
        width: 10, length,
        shelfLeft: 0, shelfRight: 0,
        freeSpeed: 1.2, jamDensity: 3.5, blockedFraction: 0,
        flow: "bi", orientation: "fwd", edgeType: "aisle",
        ...patch,
      };
      this.edges.push(sanitizeEdgePatch(edge));
      this._recordMutation();
      return id;
    },

    removeNode(id) {
      this.edges = this.edges.filter((e) => e.nodeA !== id && e.nodeB !== id);
      this.nodes = this.nodes.filter((n) => n.id !== id);
      // Clean up checkout queue if this was a Register
      this.checkoutQueues = this.checkoutQueues.filter((q) => q.registerId !== id);
      if (this.selection?.id === id) this.selection = null;
      if (this.waypointEditRegisterId === id) this.waypointEditRegisterId = null;
      this._recordMutation();
    },

    removeEdge(id) {
      this.edges = this.edges.filter((e) => e.id !== id);
      delete this.planogram[String(id)];
      if (this.selection?.id === id) this.selection = null;
      this._recordMutation();
    },

    /** Move a node without pushing history — call commitMove() on drag end. */
    moveNode(id, x, y, angle = undefined) {
      const idx = this.nodes.findIndex((n) => sameEntityId(n.id, id));
      if (idx === -1) return;
      this.nodes.splice(idx, 1, {
        ...this.nodes[idx],
        x: parseFloat(x.toFixed(3)),
        y: parseFloat(y.toFixed(3)),
        ...(angle !== undefined ? { angle: parseFloat(angle.toFixed(2)) } : {}),
      });
      // Recompute lengths of connected edges
      for (let i = 0; i < this.edges.length; i++) {
        const e = this.edges[i];
        if (sameEntityId(e.nodeA, id) || sameEntityId(e.nodeB, id)) {
          const na = this.nodes.find((n) => sameEntityId(n.id, e.nodeA));
          const nb = this.nodes.find((n) => sameEntityId(n.id, e.nodeB));
          if (na && nb) {
            this.edges.splice(i, 1, {
              ...e,
              length: parseFloat(Math.hypot(nb.x - na.x, nb.y - na.y).toFixed(2)),
            });
          }
        }
      }
      this.isDirty = true;
    },

    /** Call after a drag-move sequence to commit one history entry. */
    commitMove() {
      this._recordMutation();
    },

    // ── Shelf helpers ─────────────────────────────────────────────────────

    /** Mark an existing edge as having shelves on the specified side(s). */
    addShelfToEdge(edgeId, side = "both") {
      const idx = this.edges.findIndex((e) => sameEntityId(e.id, edgeId));
      if (idx === -1) return;
      const e = this.edges[idx];
      const patch = {};
      if (side === "left"  || side === "both") patch.shelfLeft  = e.shelfLeft  > 0 ? e.shelfLeft  : 4;
      if (side === "right" || side === "both") patch.shelfRight = e.shelfRight > 0 ? e.shelfRight : 4;
      this.edges.splice(idx, 1, { ...e, ...patch });
      // Auto-init planogram entry so the panel has something to open
      const eid = String(edgeId);
      if (!this.planogram[eid]) {
        this.planogram[eid] = { bayCountLeft: 1, bayCountRight: 1, cells: [] };
      }
      this._recordMutation();
    },

    /** Remove shelf from a specific side (sets depth to 0). */
    removeShelfFromEdge(edgeId, side) {
      const idx = this.edges.findIndex((e) => sameEntityId(e.id, edgeId));
      if (idx === -1) return;
      const e = this.edges[idx];
      const patch = {};
      if (side === "left"  || side === "both") patch.shelfLeft  = 0;
      if (side === "right" || side === "both") patch.shelfRight = 0;
      const updated = { ...e, ...patch };
      this.edges.splice(idx, 1, updated);
      if ((updated.shelfLeft ?? 0) <= 0 && (updated.shelfRight ?? 0) <= 0) {
        delete this.planogram[String(edgeId)];
      }
      this._recordMutation();
    },

    // ── Free objects ──────────────────────────────────────────────────────

    addFreeShelf(x, y, rotation = 0) {
      const id = this.freeObjects.length
        ? Math.max(...this.freeObjects.map((f) => f.id)) + 1
        : 1;
      this.freeObjects.push(sanitizeFreeObjectPatch({
        id, type: "shelf",
        x, y,
        rotation,
        widthFt:  10,   // length along wall
        lengthFt: 4,    // depth into aisle
        label: `Shelf ${id}`,
      }));
      this._recordMutation();
      return id;
    },

    removeFreeObject(id) {
      this.freeObjects = this.freeObjects.filter((f) => f.id !== id);
      if (this.selection?.type === "freeObject" && this.selection?.id === id) {
        this.selection = null;
      }
      this._recordMutation();
    },

    /** Move without pushing history — call commitMove() on drag end. */
    moveFreeObject(id, x, y) {
      const idx = this.freeObjects.findIndex((f) => sameEntityId(f.id, id));
      if (idx === -1) return;
      this.freeObjects.splice(idx, 1, {
        ...this.freeObjects[idx],
        x: parseFloat(x.toFixed(3)),
        y: parseFloat(y.toFixed(3)),
      });
      this.isDirty = true;
    },

    updateFreeObject(id, patch) {
      const idx = this.freeObjects.findIndex((f) => sameEntityId(f.id, id));
      if (idx === -1) return;
      this.freeObjects.splice(idx, 1, { ...this.freeObjects[idx], ...sanitizeFreeObjectPatch(patch) });
      this._recordMutation();
    },

    // ── Checkout queue helpers ────────────────────────────────────────────

    updateCheckoutQueue(registerId, patch) {
      const idx = this.ensureQueueForRegister(registerId);
      if (idx === -1) return;
      const next = { ...this.checkoutQueues[idx], ...patch };
      next.processingTime = Math.max(0.0, toFiniteNumber(next.processingTime, 5.0));
      this.checkoutQueues.splice(idx, 1, next);
      this._recordMutation();
    },

    ensureQueueForRegister(registerId) {
      const node = this.nodes.find((n) => sameEntityId(n.id, registerId));
      if (!node || node.type !== "Register") return -1;
      let idx = this.checkoutQueues.findIndex((q) => sameEntityId(q.registerId, registerId));
      if (idx >= 0) return idx;
      this.checkoutQueues.push({
        registerId: node.id,
        processingTime: 5.0,
        waypoints: [{ x: parseFloat((node.x + 6).toFixed(3)), y: parseFloat(node.y.toFixed(3)) }],
      });
      this._recordMutation();
      idx = this.checkoutQueues.length - 1;
      return idx;
    },

    addQueueWaypoint(registerId) {
      const idx = this.ensureQueueForRegister(registerId);
      if (idx === -1) return;
      const wps = this.checkoutQueues[idx].waypoints;
      const last = wps[wps.length - 1] ?? { x: 0, y: 0 };
      const node = this.nodes.find((n) => sameEntityId(n.id, registerId));
      // Offset each new waypoint 6ft further from the register along x
      const dx = node ? (last.x - node.x) : -6;
      const dy = node ? (last.y - node.y) : 0;
      const len = Math.hypot(dx, dy) || 6;
      const nx  = parseFloat((last.x + (dx / len) * 6).toFixed(3));
      const ny  = parseFloat((last.y + (dy / len) * 6).toFixed(3));
      this.checkoutQueues.splice(idx, 1, {
        ...this.checkoutQueues[idx],
        waypoints: [...wps, { x: nx, y: ny }],
      });
      this._recordMutation();
    },

    removeQueueWaypoint(registerId, index) {
      const idx = this.ensureQueueForRegister(registerId);
      if (idx === -1) return;
      const wps = this.checkoutQueues[idx].waypoints;
      if (wps.length <= 1) return; // must keep at least one
      this.checkoutQueues.splice(idx, 1, {
        ...this.checkoutQueues[idx],
        waypoints: wps.filter((_, i) => i !== index),
      });
      this._recordMutation();
    },

    /** Move a waypoint without pushing history. */
    moveQueueWaypoint(registerId, index, x, y) {
      const idx = this.ensureQueueForRegister(registerId);
      if (idx === -1) return;
      const wps = this.checkoutQueues[idx].waypoints.map((wp, i) =>
        i === index ? { x: parseFloat(x.toFixed(3)), y: parseFloat(y.toFixed(3)) } : wp
      );
      this.checkoutQueues.splice(idx, 1, { ...this.checkoutQueues[idx], waypoints: wps });
      this.isDirty = true;
    },

    commitQueueWaypointMove() {
      this._recordMutation();
    },

    updateQueueWaypoint(registerId, index, patch) {
      const idx = this.ensureQueueForRegister(registerId);
      if (idx === -1) return;
      const wps = this.checkoutQueues[idx].waypoints;
      if (index < 0 || index >= wps.length) return;
      const wp = {
        x: toFiniteNumber(patch?.x ?? wps[index].x, wps[index].x),
        y: toFiniteNumber(patch?.y ?? wps[index].y, wps[index].y),
      };
      this.checkoutQueues.splice(idx, 1, {
        ...this.checkoutQueues[idx],
        waypoints: wps.map((p, i) => (i === index ? wp : p)),
      });
      this._recordMutation();
    },

    moveQueueWaypointUp(registerId, index) {
      const idx = this.ensureQueueForRegister(registerId);
      if (idx === -1 || index <= 0) return;
      const wps = [...this.checkoutQueues[idx].waypoints];
      [wps[index - 1], wps[index]] = [wps[index], wps[index - 1]];
      this.checkoutQueues.splice(idx, 1, { ...this.checkoutQueues[idx], waypoints: wps });
      this._recordMutation();
    },

    moveQueueWaypointDown(registerId, index) {
      const idx = this.ensureQueueForRegister(registerId);
      if (idx === -1) return;
      const wps = [...this.checkoutQueues[idx].waypoints];
      if (index < 0 || index >= wps.length - 1) return;
      [wps[index], wps[index + 1]] = [wps[index + 1], wps[index]];
      this.checkoutQueues.splice(idx, 1, { ...this.checkoutQueues[idx], waypoints: wps });
      this._recordMutation();
    },

    // ── Boundary helpers ──────────────────────────────────────────────────

    setBoundary(pts) {
      this.boundary = pts.map((p) => ({
        x: parseFloat(p.x.toFixed(3)),
        y: parseFloat(p.y.toFixed(3)),
      }));
      this._recordMutation();
    },

    /** Move a boundary vertex without pushing history. */
    moveBoundaryVertex(index, x, y) {
      if (index < 0 || index >= this.boundary.length) return;
      this.boundary[index] = {
        x: parseFloat(x.toFixed(3)),
        y: parseFloat(y.toFixed(3)),
      };
      this.isDirty = true;
    },

    commitBoundaryMove() {
      this._recordMutation();
    },

    insertBoundaryVertex(afterIndex, x, y) {
      const newPt = { x: parseFloat(x.toFixed(3)), y: parseFloat(y.toFixed(3)) };
      this.boundary.splice(afterIndex + 1, 0, newPt);
      this._recordMutation();
    },

    removeBoundaryVertex(index) {
      if (this.boundary.length <= 3) return; // minimum triangle
      this.boundary.splice(index, 1);
      this._recordMutation();
    },

    // ── Planogram helpers ─────────────────────────────────────────────────

    _ensurePlanogramEntry(edgeId) {
      const eid = String(edgeId);
      if (!this.planogram[eid]) {
        this.planogram[eid] = { bayCountLeft: 1, bayCountRight: 1, cells: [] };
      }
      return eid;
    },

    setPlanogramBayCount(edgeId, side, count) {
      const eid = this._ensurePlanogramEntry(edgeId);
      const key = side === "left" ? "bayCountLeft" : "bayCountRight";
      const prev = this.planogram[eid][key];
      const next = Math.max(1, Math.trunc(toFiniteNumber(count, 1)));
      this.planogram[eid] = { ...this.planogram[eid], [key]: next };
      // Remove cells beyond new count
      if (next < prev) {
        this.planogram[eid].cells = this.planogram[eid].cells.filter(
          (c) => c.side !== side || c.bay < next
        );
      }
      this._recordMutation();
    },

    setPlanogramCell(edgeId, side, bay, face, slot, skuId, qty) {
      const eid = this._ensurePlanogramEntry(edgeId);
      const cells = this.planogram[eid].cells.filter(
        (c) => !(c.side === side && c.bay === bay && c.face === face && c.slot === slot)
      );
      cells.push({
        side,
        bay: Math.max(0, Math.trunc(toFiniteNumber(bay, 0))),
        face: Math.max(0, Math.trunc(toFiniteNumber(face, 0))),
        slot: Math.max(0, Math.trunc(toFiniteNumber(slot, 0))),
        skuId,
        onShelfQty: Math.max(0, Math.trunc(toFiniteNumber(qty ?? 20, 20))),
      });
      this.planogram[eid] = { ...this.planogram[eid], cells };
      this._recordMutation();
    },

    clearPlanogramCell(edgeId, side, bay, face, slot) {
      const eid = String(edgeId);
      if (!this.planogram[eid]) return;
      this.planogram[eid].cells = this.planogram[eid].cells.filter(
        (c) => !(c.side === side && c.bay === bay && c.face === face && c.slot === slot)
      );
      this._recordMutation();
    },

    setPlanogramFaceCount(edgeId, side, count) {
      const eid = this._ensurePlanogramEntry(edgeId);
      const key = side === "left" ? "faceCountLeft" : "faceCountRight";
      const next = Math.max(1, Math.trunc(toFiniteNumber(count, 1)));
      const prev = this.planogram[eid][key] ?? 1;
      this.planogram[eid] = { ...this.planogram[eid], [key]: next };
      if (next < prev) {
        this.planogram[eid].cells = this.planogram[eid].cells.filter(
          (c) => c.side !== side || c.face < next
        );
      }
      this._recordMutation();
    },

    setPlanogramCellsBulk(ops) {
      if (!Array.isArray(ops) || ops.length === 0) return;
      this.beginBatchEdit();
      try {
        for (const op of ops) {
          if (op?.action === "clear") {
            this.clearPlanogramCell(op.edgeId, op.side, op.bay, op.face, op.slot ?? 0);
          } else {
            this.setPlanogramCell(
              op.edgeId,
              op.side,
              op.bay,
              op.face,
              op.slot ?? 0,
              op.skuId,
              op.qty
            );
          }
        }
      } finally {
        this.commitBatchEdit();
      }
    },

    async fetchProducts(csvName) {
      if (!csvName) return;
      try {
        const { default: axios } = await import("axios");
        const { data } = await axios.get(`/api/products?csv=${encodeURIComponent(csvName)}`);
        this.products       = data;
        this.productCsvName = csvName;
      } catch (e) {
        console.warn("fetchProducts failed:", e);
      }
    },

    /** Attach a product catalog CSV to this layout (persists on save). */
    attachProducts(csvName) {
      this.productsFile = csvName;
      this.isDirty      = true;
      if (csvName) this.fetchProducts(csvName);
    },

    /** Detach the product catalog from this layout. */
    detachProducts() {
      this.productsFile   = null;
      this.products       = [];
      this.productCsvName = null;
      this.isDirty        = true;
    },

    /** Attach a POS CSV dataset to this layout (session-only, not persisted). */
    attachPosCsv(csvName) {
      this.posCsvName = csvName;
      this.isDirty    = true;
    },

    /** Detach the POS CSV from this layout. */
    detachPosCsv() {
      this.posCsvName = null;
      this.isDirty    = true;
    },
  },
});
