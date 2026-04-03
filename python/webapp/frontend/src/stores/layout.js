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
    activeTool:   "select", // "select"|"shelf"|"endcap"|"bin"|"display"|"register"|"entrance"|"exit"
    planogramOpen: false,
    wizardNew:    false,    // true → show "add aisles" banner

    // ── Viewport ─────────────────────────────────────────────────────────
    viewport:   { zoom: 1.0, panX: 0, panY: 0 },
    showGrid:   true,
    snapToGrid: true,
    gridSizeFt: 1.0,

    // ── Undo / redo (command-pattern — Phase 2 will replace full snapshots) ──
    // Phase 1: simple snapshots; will be replaced with op+inverse in Phase 2
    history:      [],  // array of cloneLayoutState() snapshots
    historyIndex: -1,
  }),

  // ---------------------------------------------------------------------------
  getters: {
    hasLayout:  (state) => state.nodes.length > 0 || state.boundary.length > 0,
    canUndo:    (state) => state.historyIndex > 0,
    canRedo:    (state) => state.historyIndex < state.history.length - 1,
    hasEntrance:(state) => state.nodes.some((n) => n.type === "Entrance"),
    hasExit:    (state) => state.nodes.some((n) => n.type === "Exit"),
    nodeById:   (state) => (id) => state.nodes.find((n) => n.id === id) ?? null,
    edgeById:   (state) => (id) => state.edges.find((e) => e.id === id) ?? null,
    wallEdges:  (state) => state.edges.filter((e) => e.edgeType === "wall"),
    selectedNode: (state) =>
      state.selection?.type === "node"
        ? state.nodes.find((n) => n.id === state.selection.id) ?? null
        : null,
    selectedEdge: (state) =>
      state.selection?.type === "edge"
        ? state.edges.find((e) => e.id === state.selection.id) ?? null
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
      this.nodes          = data.nodes         ?? [];
      this.edges          = data.edges         ?? [];
      this.shelves        = data.shelves        ?? [];
      this.freeObjects    = data.freeObjects   ?? [];
      this.planogram      = data.planogram     ?? {};
      this.checkoutQueues = data.checkoutQueues ?? [];
      this.schedule       = data.schedule      ?? { daysOfOperation: [], hours: {} };
      this.wizardNew      = data._wizardNew    ?? false;
      this.selection      = null;
      this.planogramOpen  = false;
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
    },
    clearSelection() {
      this.selection    = null;
      this.planogramOpen = false;
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
      const idx = this.nodes.findIndex((n) => n.id === id);
      if (idx === -1) return;
      this.nodes[idx] = { ...this.nodes[idx], ...patch };
      this.isDirty = true;
      this._pushHistory();
    },

    updateEdge(id, patch) {
      const idx = this.edges.findIndex((e) => e.id === id);
      if (idx === -1) return;
      this.edges[idx] = { ...this.edges[idx], ...patch };
      this.isDirty = true;
      this._pushHistory();
    },

    // ── Graph editing (Phase 2) ───────────────────────────────────────────
    addNode(patch) {
      const id = this.nodes.length ? Math.max(...this.nodes.map((n) => n.id)) + 1 : 1;
      const typeDefaults = {
        Entrance: { width: 10, length: 8, entryRate: 1.0, exitRate: 0.0 },
        Exit:     { width: 10, length: 8, entryRate: 0.0, exitRate: 1.0 },
        Register: { width: 8,  length: 4, serviceRate: 0.5 },
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
      this.nodes.push(node);
      this.isDirty = true;
      this._pushHistory();
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
      this.edges.push(edge);
      this.isDirty = true;
      this._pushHistory();
      return id;
    },

    removeNode(id) {
      this.edges = this.edges.filter((e) => e.nodeA !== id && e.nodeB !== id);
      this.nodes = this.nodes.filter((n) => n.id !== id);
      if (this.selection?.id === id) this.selection = null;
      this.isDirty = true;
      this._pushHistory();
    },

    removeEdge(id) {
      this.edges = this.edges.filter((e) => e.id !== id);
      if (this.selection?.id === id) this.selection = null;
      this.isDirty = true;
      this._pushHistory();
    },

    /** Move a node without pushing history — call commitMove() on drag end. */
    moveNode(id, x, y) {
      const idx = this.nodes.findIndex((n) => n.id === id);
      if (idx === -1) return;
      this.nodes[idx] = {
        ...this.nodes[idx],
        x: parseFloat(x.toFixed(3)),
        y: parseFloat(y.toFixed(3)),
      };
      // Recompute lengths of connected edges
      for (let i = 0; i < this.edges.length; i++) {
        const e = this.edges[i];
        if (e.nodeA === id || e.nodeB === id) {
          const na = this.nodes.find((n) => n.id === e.nodeA);
          const nb = this.nodes.find((n) => n.id === e.nodeB);
          if (na && nb) {
            this.edges[i] = {
              ...e,
              length: parseFloat(Math.hypot(nb.x - na.x, nb.y - na.y).toFixed(2)),
            };
          }
        }
      }
      this.isDirty = true;
    },

    /** Call after a drag-move sequence to commit one history entry. */
    commitMove() {
      this._pushHistory();
    },

    // ── Shelf helpers ─────────────────────────────────────────────────────

    /** Mark an existing edge as having shelves (set default depth if currently 0). */
    addShelfToEdge(edgeId) {
      const idx = this.edges.findIndex((e) => e.id === edgeId);
      if (idx === -1) return;
      const e = this.edges[idx];
      this.edges[idx] = {
        ...e,
        shelfLeft:  e.shelfLeft  > 0 ? e.shelfLeft  : 4,
        shelfRight: e.shelfRight > 0 ? e.shelfRight : 4,
      };
      this.isDirty = true;
      this._pushHistory();
    },

    // ── Free objects ──────────────────────────────────────────────────────

    addFreeShelf(x, y, rotation = 0) {
      const id = this.freeObjects.length
        ? Math.max(...this.freeObjects.map((f) => f.id)) + 1
        : 1;
      this.freeObjects.push({
        id, type: "shelf",
        x, y,
        rotation,
        widthFt:  10,   // length along wall
        lengthFt: 4,    // depth into aisle
        label: `Shelf ${id}`,
      });
      this.isDirty = true;
      this._pushHistory();
      return id;
    },

    removeFreeObject(id) {
      this.freeObjects = this.freeObjects.filter((f) => f.id !== id);
      if (this.selection?.type === "freeObject" && this.selection?.id === id) {
        this.selection = null;
      }
      this.isDirty = true;
      this._pushHistory();
    },

    /** Move without pushing history — call commitMove() on drag end. */
    moveFreeObject(id, x, y) {
      const idx = this.freeObjects.findIndex((f) => f.id === id);
      if (idx === -1) return;
      this.freeObjects[idx] = {
        ...this.freeObjects[idx],
        x: parseFloat(x.toFixed(3)),
        y: parseFloat(y.toFixed(3)),
      };
      this.isDirty = true;
    },

    updateFreeObject(id, patch) {
      const idx = this.freeObjects.findIndex((f) => f.id === id);
      if (idx === -1) return;
      this.freeObjects[idx] = { ...this.freeObjects[idx], ...patch };
      this.isDirty = true;
      this._pushHistory();
    },
  },
});
