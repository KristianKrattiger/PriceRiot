<template>
  <div
    ref="containerRef"
    class="relative w-full h-full overflow-hidden select-none"
    style="background: #E2DFD1; cursor: v-bind(cursorStyle)"
  >
    <canvas
      ref="canvasRef"
      class="absolute inset-0"
      style="touch-action: none;"
      @wheel.prevent="onWheel"
      @pointerdown="onPointerDown"
      @pointermove="onPointerMove"
      @pointerup="onPointerUp"
      @pointercancel="onPointerUp"
      @contextmenu.prevent="onContextMenu"
    />

    <!-- Node type legend -->
    <div class="absolute top-3 right-3 space-y-1 pointer-events-none">
      <div v-for="item in legend" :key="item.label"
           class="flex items-center gap-1.5 text-[10px] font-mono text-ink-ghost bg-surface/80 px-2 py-0.5 border border-rim">
        <span class="w-2.5 h-2.5 rounded-full inline-block shrink-0" :style="{ background: item.color }" />
        {{ item.label }}
      </div>
    </div>

    <!-- Edge-draw instruction banner -->
    <div
      v-if="isDrawingEdge"
      class="absolute top-3 left-1/2 -translate-x-1/2 flex items-center gap-2
             bg-surface/90 border border-mustard/40 px-4 py-2 text-[11px] font-mono text-mustard pointer-events-none"
    >
      <span class="w-1.5 h-1.5 rounded-full bg-mustard animate-pulse"></span>
      {{ edgeDrawSourceId ? "Click destination node  ·  Esc to cancel" : "Click source node" }}
    </div>

    <!-- "Add aisles" banner for wizard-new layouts -->
    <div
      v-if="layoutStore.wizardNew && !isDrawingEdge"
      class="absolute bottom-8 left-1/2 -translate-x-1/2 flex items-center gap-2
             bg-mustard/10 border border-mustard/40 px-4 py-2 text-[11px] font-mono text-mustard pointer-events-none"
    >
      <span class="w-1.5 h-1.5 rounded-full bg-mustard"></span>
      Entrance → Exit path created. Add interior aisles and shelves to complete your store.
    </div>

    <!-- Multi-selection status badge -->
    <div
      v-if="layoutStore.multiSelection.length > 1"
      class="absolute bottom-8 right-3 flex items-center gap-2
             bg-surface/90 border border-deep-teal/40 px-3 py-1.5 text-[11px] font-mono text-deep-teal pointer-events-none"
    >
      {{ layoutStore.multiSelection.length }} selected · Del to delete · Shift+click to toggle
    </div>

    <!-- Boundary-edit instruction banner -->
    <div
      v-if="layoutStore.activeTool === 'boundary'"
      class="absolute top-3 left-1/2 -translate-x-1/2 flex items-center gap-2
             bg-surface/90 border border-deep-teal/40 px-4 py-2 text-[11px] font-mono text-deep-teal pointer-events-none"
    >
      <span class="w-1.5 h-1.5 rounded-full bg-deep-teal animate-pulse"></span>
      Drag vertices · Click ⊕ to insert · Right-click vertex to remove · Esc to exit
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount, watch } from "vue";
import { useLayoutStore } from "../../stores/layout";
import { sameEntityId } from "../../utils/entityIds.js";

const layoutStore = useLayoutStore();

/** Map node id → node (string keys so number/string ids both resolve). */
function nodeMapByStringId(nodes) {
  const m = new Map();
  for (const n of nodes) m.set(String(n.id), n);
  return m;
}

// ---------------------------------------------------------------------------
// Refs & canvas state
// ---------------------------------------------------------------------------
const containerRef = ref(null);
const canvasRef    = ref(null);
let   ctx          = null;
let   animFrameId  = null;
let   dirty        = true;
let   ro           = null;

// Reactive interaction state (used by computed cursorStyle)
const isPanning            = ref(false);
const isSpaceDown          = ref(false);
const isDraggingNode       = ref(false);
const isDraggingFO         = ref(false);
const isDraggingWaypoint   = ref(false);
const isDraggingBoundary   = ref(false);
const isMarquee            = ref(false);
const hoverNodeId          = ref(null);
const hoverBoundaryVertex  = ref(-1);    // index or -1
const hoverBoundaryMidpt   = ref(-1);    // segment index or -1

// Plain interaction state (canvas-only)
let panStart               = { x: 0, y: 0 };
let panStartVp             = { zoom: 1, panX: 0, panY: 0 };
let dragNodeId             = null;
let dragFOId               = null;
let dragFOOffset           = { x: 0, y: 0 };
let marqueeStartS          = { x: 0, y: 0 };  // screen coords
let marqueeEndS            = { x: 0, y: 0 };
let dragWaypointRegisterId = null;
let dragWaypointIndex      = null;
let dragBoundaryIndex      = -1;
let edgeDrawSourceId       = null;
let edgeDraftEnd           = null;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
const MIN_ZOOM = 0.02;
const MAX_ZOOM = 80;

const COLORS = {
  grid:            "rgba(28,26,20,0.06)",
  boundary:        "#EDEADD",   // store-floor fill (lighter than canvas bg)
  boundaryStroke:  "#8C8880",   // wall stroke
  boundaryOutside: "#D4D0BF",   // area outside store bounds
  aisleFill:      "#EBE7D8",
  aisleStroke:    "#CEC9B6",
  wallFill:       "#D4CFBD",
  wallStroke:     "#A09C8E",
  shelfBand:      "#0A3B4D",
  nodeEntrance:   "#008A31",
  nodeExit:       "#A33025",
  nodeJunction:   "#9A9790",
  nodeRegister:   "#C9980A",
  nodeStockroom:  "#8B5A3C",
  nodeText:       "#F0EDE3",
  selection:      "#6B56D6",
  multiSel:       "#2DA8B8",   // teal — multi-selection ring
  hover:          "#C9980A",
  edgeDraft:      "#6B56D6",
  scaleBar:       "#6A6760",
  waypointFill:   "#C9980A",
  waypointLine:   "rgba(201,152,10,0.5)",
  boundaryVertex: "#6B56D6",
  boundaryMidpt:  "rgba(107,86,214,0.45)",
};

const NODE_TYPE_COLOR = {
  Entrance:  COLORS.nodeEntrance,
  Exit:      COLORS.nodeExit,
  Junction:  COLORS.nodeJunction,
  Register:  COLORS.nodeRegister,
  Stockroom: COLORS.nodeStockroom,
};

const legend = [
  { label: "Entrance",  color: COLORS.nodeEntrance  },
  { label: "Exit",      color: COLORS.nodeExit       },
  { label: "Junction",  color: COLORS.nodeJunction   },
  { label: "Register",  color: COLORS.nodeRegister   },
  { label: "Stockroom", color: COLORS.nodeStockroom  },
];

// ---------------------------------------------------------------------------
// Computed
// ---------------------------------------------------------------------------
const isDrawingEdge = computed(() => layoutStore.activeTool === "draw-edge");

const cursorStyle = computed(() => {
  if (isMarquee.value) return "crosshair";
  if (isPanning.value || isDraggingNode.value || isDraggingFO.value ||
      isDraggingWaypoint.value || isDraggingBoundary.value) return "grabbing";
  if (isSpaceDown.value) return "grab";
  const tool = layoutStore.activeTool;
  if (tool === "boundary") {
    if (hoverBoundaryVertex.value >= 0) return "grab";
    if (hoverBoundaryMidpt.value  >= 0) return "copy";
    return "crosshair";
  }
  if (tool === "select") return hoverNodeId.value !== null ? "grab" : "default";
  if (["junction", "register", "entrance", "exit", "stockroom", "draw-edge", "shelf"].includes(tool)) return "crosshair";
  return "default";
});

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
onMounted(() => {
  ctx = canvasRef.value.getContext("2d");

  ro = new ResizeObserver(() => {
    resizeCanvas();
    markDirty();
  });
  ro.observe(containerRef.value);
  resizeCanvas();

  window.addEventListener("keydown", onKeyDown);
  window.addEventListener("keyup",   onKeyUp);

  startLoop();

  // Auto-fit after ResizeObserver fires
  if (layoutStore.hasLayout) {
    setTimeout(() => fitToContent(), 100);
  }
});

onBeforeUnmount(() => {
  if (animFrameId) cancelAnimationFrame(animFrameId);
  if (ro) ro.disconnect();
  window.removeEventListener("keydown", onKeyDown);
  window.removeEventListener("keyup",   onKeyUp);
});

// Redraw on store changes
watch(
  () => [
    layoutStore.nodes,
    layoutStore.edges,
    layoutStore.boundary,
    layoutStore.shelves,
    layoutStore.freeObjects,
    layoutStore.checkoutQueues,
    layoutStore.showGrid,
    layoutStore.selection,
    layoutStore.multiSelection,
    layoutStore.viewport,
    layoutStore.waypointEditRegisterId,
  ],
  () => markDirty(),
  { deep: true }
);

// Reset edge-draw / hover state when tool changes
watch(
  () => layoutStore.activeTool,
  (tool) => {
    if (tool !== "draw-edge") {
      edgeDrawSourceId = null;
      edgeDraftEnd     = null;
    }
    if (tool !== "boundary") {
      hoverBoundaryVertex.value = -1;
      hoverBoundaryMidpt.value  = -1;
    }
    markDirty();
  }
);

// Auto-fit when a new layout loads (triggered by filename change)
watch(
  () => layoutStore.filename,
  () => {
    if (layoutStore.hasLayout) {
      requestAnimationFrame(() => fitToContent());
    }
  }
);

watch(
  () => layoutStore.hasLayout,
  (has) => {
    if (has) requestAnimationFrame(() => fitToContent());
  }
);

// ---------------------------------------------------------------------------
// Canvas sizing (no DPR — avoids accumulated scale transforms)
// ---------------------------------------------------------------------------
function resizeCanvas() {
  const el = containerRef.value;
  if (!el) return;
  const w = el.clientWidth;
  const h = el.clientHeight;
  const c = canvasRef.value;
  c.width  = w;
  c.height = h;
  c.style.width  = w + "px";
  c.style.height = h + "px";
}

// ---------------------------------------------------------------------------
// Render loop
// ---------------------------------------------------------------------------
function startLoop() {
  function loop() {
    if (dirty) { draw(); dirty = false; }
    animFrameId = requestAnimationFrame(loop);
  }
  animFrameId = requestAnimationFrame(loop);
}

function markDirty() { dirty = true; }

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
function draw() {
  const c = canvasRef.value;
  if (!c || !ctx) return;
  const W = c.width;
  const H = c.height;
  const { zoom, panX, panY } = layoutStore.viewport;

  ctx.clearRect(0, 0, W, H);
  // Outside-store area (slightly darker than floor)
  ctx.fillStyle = layoutStore.boundary.length ? COLORS.boundaryOutside : "#E2DFD1";
  ctx.fillRect(0, 0, W, H);

  ctx.save();
  ctx.setTransform(zoom, 0, 0, zoom, panX, panY);

  if (layoutStore.showGrid) drawGrid(W, H, zoom, panX, panY);
  drawBoundary(zoom);
  drawEdges(zoom);
  drawEdgeDraft(zoom);
  drawNodes(zoom);
  drawFreeObjects(zoom);
  drawQueueWaypoints(zoom);

  ctx.restore();

  // Screen-space overlays (drawn after restore, no world transform)
  drawScaleBar(W, H, zoom);
  if (isMarquee.value) drawMarquee();
}

function drawGrid(W, H, zoom, panX, panY) {
  const gs     = layoutStore.gridSizeFt;
  const wLeft  = -panX / zoom;
  const wTop   = -panY / zoom;
  const wRight = (W - panX) / zoom;
  const wBot   = (H - panY) / zoom;

  ctx.save();
  ctx.strokeStyle = COLORS.grid;
  ctx.lineWidth   = 0.5 / zoom;
  ctx.beginPath();
  for (let x = Math.floor(wLeft / gs) * gs; x <= wRight; x += gs) {
    ctx.moveTo(x, wTop); ctx.lineTo(x, wBot);
  }
  for (let y = Math.floor(wTop / gs) * gs; y <= wBot; y += gs) {
    ctx.moveTo(wLeft, y); ctx.lineTo(wRight, y);
  }
  ctx.stroke();
  ctx.restore();
}

function drawBoundary(zoom) {
  const pts = layoutStore.boundary;
  if (!pts.length) return;
  const editMode = layoutStore.activeTool === "boundary";

  ctx.save();
  ctx.beginPath();
  ctx.moveTo(pts[0].x, pts[0].y);
  for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x, pts[i].y);
  ctx.closePath();
  ctx.fillStyle   = COLORS.boundary;
  ctx.fill();
  ctx.strokeStyle = editMode ? COLORS.boundaryVertex : COLORS.boundaryStroke;
  ctx.lineWidth   = (editMode ? 2 : 3) / zoom;
  ctx.stroke();
  ctx.restore();

  if (!editMode) return;

  // Midpoint insert diamonds
  for (let i = 0; i < pts.length; i++) {
    const a   = pts[i], b = pts[(i + 1) % pts.length];
    const mx  = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
    const r   = 3.5 / zoom;
    const hot = hoverBoundaryMidpt.value === i;
    ctx.save();
    ctx.translate(mx, my);
    ctx.rotate(Math.PI / 4);
    ctx.beginPath();
    ctx.rect(-r, -r, r * 2, r * 2);
    ctx.fillStyle   = hot ? COLORS.boundaryVertex : COLORS.boundaryMidpt;
    ctx.fill();
    ctx.strokeStyle = COLORS.boundaryVertex;
    ctx.lineWidth   = 1 / zoom;
    ctx.stroke();
    ctx.restore();
  }

  // Vertex squares
  for (let i = 0; i < pts.length; i++) {
    const p   = pts[i];
    const r   = 4.5 / zoom;
    const hot = hoverBoundaryVertex.value === i;
    ctx.save();
    ctx.beginPath();
    ctx.rect(p.x - r, p.y - r, r * 2, r * 2);
    ctx.fillStyle   = hot ? COLORS.boundaryVertex : "#fff";
    ctx.fill();
    ctx.strokeStyle = COLORS.boundaryVertex;
    ctx.lineWidth   = 1.5 / zoom;
    ctx.stroke();
    ctx.restore();
  }
}

function drawEdges(zoom) {
  const nodeMap   = nodeMapByStringId(layoutStore.nodes);
  const selEdgeId = layoutStore.selection?.type === "edge" ? layoutStore.selection.id : null;
  const multiSet  = new Set(
    layoutStore.multiSelection.filter((s) => s.type === "edge").map((s) => String(s.id))
  );
  for (const edge of layoutStore.edges) {
    const isSel   = sameEntityId(edge.id, selEdgeId);
    const isMulti = multiSet.has(String(edge.id));
    drawEdge(edge, nodeMap, zoom, isSel, isMulti);
  }
}

function drawEdge(edge, nodeMap, zoom, isSelected, isMultiSelected = false) {
  const na = nodeMap.get(String(edge.nodeA));
  const nb = nodeMap.get(String(edge.nodeB));
  if (!na || !nb) return;

  const dx = nb.x - na.x, dy = nb.y - na.y;
  const dist = Math.hypot(dx, dy);
  if (dist < 0.001) return;

  const ux = dx / dist, uy = dy / dist;
  const px = -uy,       py =  ux;
  const hw = edge.width / 2;

  const x0 = na.x + px * hw, y0 = na.y + py * hw;
  const x1 = nb.x + px * hw, y1 = nb.y + py * hw;
  const x2 = nb.x - px * hw, y2 = nb.y - py * hw;
  const x3 = na.x - px * hw, y3 = na.y - py * hw;

  const isWall = edge.edgeType === "wall";

  ctx.save();
  ctx.beginPath();
  ctx.moveTo(x0, y0); ctx.lineTo(x1, y1);
  ctx.lineTo(x2, y2); ctx.lineTo(x3, y3);
  ctx.closePath();
  ctx.fillStyle   = isWall ? COLORS.wallFill : COLORS.aisleFill;
  ctx.fill();
  ctx.strokeStyle = isSelected    ? COLORS.selection
                  : isMultiSelected ? COLORS.multiSel
                  : isWall ? COLORS.wallStroke : COLORS.aisleStroke;
  ctx.lineWidth   = (isSelected || isMultiSelected) ? 2 / zoom : 1 / zoom;
  ctx.stroke();

  // Shelf bands with facing arrows
  if (edge.shelfLeft > 0) {
    const sw = edge.shelfLeft;
    ctx.beginPath();
    ctx.moveTo(x0, y0); ctx.lineTo(x1, y1);
    ctx.lineTo(x1 - px * sw, y1 - py * sw);
    ctx.lineTo(x0 - px * sw, y0 - py * sw);
    ctx.closePath();
    ctx.fillStyle = COLORS.shelfBand + "55";
    ctx.fill();
    // Arrow pointing left (into band, i.e. -px direction)
    drawShelfArrow((x0 + x1) / 2 - px * sw / 2, (y0 + y1) / 2 - py * sw / 2, -px, -py, zoom);
  }
  if (edge.shelfRight > 0) {
    const sw = edge.shelfRight;
    ctx.beginPath();
    ctx.moveTo(x3, y3); ctx.lineTo(x2, y2);
    ctx.lineTo(x2 + px * sw, y2 + py * sw);
    ctx.lineTo(x3 + px * sw, y3 + py * sw);
    ctx.closePath();
    ctx.fillStyle = COLORS.shelfBand + "55";
    ctx.fill();
    // Arrow pointing right (+px direction)
    drawShelfArrow((x3 + x2) / 2 + px * sw / 2, (y3 + y2) / 2 + py * sw / 2, px, py, zoom);
  }

  ctx.restore();
}

/** Arrow drawn in a shelf band. nx,ny = facing direction (unit vector). */
function drawShelfArrow(cx, cy, nx, ny, zoom) {
  if (zoom < 1.5) return;
  const len = 2 / zoom;
  const hw  = len * 0.4;
  // Perpendicular to facing direction
  const tx = -ny, ty = nx;
  ctx.save();
  ctx.strokeStyle = COLORS.shelfBand;
  ctx.lineWidth   = 1.2 / zoom;
  ctx.beginPath();
  // Shaft
  ctx.moveTo(cx - nx * len * 0.4, cy - ny * len * 0.4);
  ctx.lineTo(cx + nx * len * 0.4, cy + ny * len * 0.4);
  // Arrowhead
  const tip = { x: cx + nx * len * 0.4, y: cy + ny * len * 0.4 };
  ctx.moveTo(tip.x - nx * hw + tx * hw, tip.y - ny * hw + ty * hw);
  ctx.lineTo(tip.x, tip.y);
  ctx.lineTo(tip.x - nx * hw - tx * hw, tip.y - ny * hw - ty * hw);
  ctx.stroke();
  ctx.restore();
}

/** Dashed preview line while drawing a new edge */
function drawEdgeDraft(zoom) {
  if (edgeDrawSourceId === null || !edgeDraftEnd) return;
  const src = layoutStore.nodes.find((n) => sameEntityId(n.id, edgeDrawSourceId));
  if (!src) return;

  ctx.save();
  ctx.strokeStyle = COLORS.edgeDraft;
  ctx.lineWidth   = 1.5 / zoom;
  ctx.setLineDash([4 / zoom, 3 / zoom]);
  ctx.beginPath();
  ctx.moveTo(src.x, src.y);
  ctx.lineTo(edgeDraftEnd.x, edgeDraftEnd.y);
  ctx.stroke();
  ctx.setLineDash([]);
  ctx.restore();
}

function drawNodes(zoom) {
  const sel     = layoutStore.selection;
  const multiSet = new Set(
    layoutStore.multiSelection.filter((s) => s.type === "node").map((s) => String(s.id))
  );
  for (const node of layoutStore.nodes) {
    const isSelected = sel?.type === "node" && sameEntityId(sel.id, node.id);
    const isHovered  = sameEntityId(hoverNodeId.value, node.id);
    const isEdgeSrc  = sameEntityId(edgeDrawSourceId, node.id);
    const isMulti    = multiSet.has(String(node.id));
    drawNode(node, zoom, isSelected, isHovered, isEdgeSrc, isMulti);
  }
}

function drawNode(node, zoom, isSelected, isHovered, isEdgeSrc, isMultiSelected = false) {
  if (node.type === "Entrance" || node.type === "Exit") {
    drawNodeRect(node, zoom, isSelected, isHovered, isEdgeSrc, isMultiSelected);
  } else {
    drawNodeCircle(node, zoom, isSelected, isHovered, isEdgeSrc, isMultiSelected);
  }
}

function drawNodeCircle(node, zoom, isSelected, isHovered, isEdgeSrc, isMultiSelected = false) {
  const r     = Math.max(node.width, node.length) / 2;
  const color = NODE_TYPE_COLOR[node.type] ?? COLORS.nodeJunction;

  ctx.save();

  if (isEdgeSrc || isSelected) {
    ctx.beginPath();
    ctx.arc(node.x, node.y, r + 4 / zoom, 0, Math.PI * 2);
    ctx.strokeStyle = isEdgeSrc ? COLORS.edgeDraft : COLORS.selection;
    ctx.lineWidth   = 2 / zoom;
    ctx.stroke();
  } else if (isMultiSelected) {
    ctx.beginPath();
    ctx.arc(node.x, node.y, r + 4 / zoom, 0, Math.PI * 2);
    ctx.strokeStyle = COLORS.multiSel;
    ctx.lineWidth   = 2 / zoom;
    ctx.stroke();
  } else if (isHovered) {
    ctx.beginPath();
    ctx.arc(node.x, node.y, r + 3 / zoom, 0, Math.PI * 2);
    ctx.strokeStyle = COLORS.hover + "80";
    ctx.lineWidth   = 2 / zoom;
    ctx.stroke();
  }

  ctx.beginPath();
  ctx.arc(node.x, node.y, r, 0, Math.PI * 2);
  ctx.fillStyle   = color;
  ctx.fill();
  ctx.strokeStyle = (isSelected || isEdgeSrc) ? COLORS.selection : "rgba(0,0,0,0.2)";
  ctx.lineWidth   = 1.5 / zoom;
  ctx.stroke();

  if (zoom >= 3) {
    const fontSize = Math.min(r * 0.7, 14 / zoom);
    ctx.font         = `${fontSize}px IBM Plex Mono, monospace`;
    ctx.fillStyle    = COLORS.nodeText;
    ctx.textAlign    = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(node.type === "Junction" ? node.id.toString() : node.type.substring(0, 3), node.x, node.y);
  }

  ctx.restore();
}

function drawNodeRect(node, zoom, isSelected, isHovered, isEdgeSrc, isMultiSelected = false) {
  const W     = node.width;
  const D     = node.length;
  const color = NODE_TYPE_COLOR[node.type] ?? COLORS.nodeJunction;
  const angle = (node.angle ?? 0) * Math.PI / 180;
  const pad   = 3 / zoom;

  ctx.save();
  ctx.translate(node.x, node.y);
  ctx.rotate(angle);

  // Selection / multi-selection / hover halo
  if (isEdgeSrc || isSelected || isHovered || isMultiSelected) {
    ctx.beginPath();
    ctx.rect(-W / 2 - pad, -D / 2 - pad, W + pad * 2, D + pad * 2);
    ctx.strokeStyle = isEdgeSrc      ? COLORS.edgeDraft
                    : isSelected     ? COLORS.selection
                    : isMultiSelected ? COLORS.multiSel
                    : COLORS.hover + "80";
    ctx.lineWidth   = 2 / zoom;
    ctx.stroke();
  }

  // Body
  ctx.beginPath();
  ctx.rect(-W / 2, -D / 2, W, D);
  ctx.fillStyle   = color;
  ctx.fill();
  ctx.strokeStyle = (isSelected || isEdgeSrc) ? COLORS.selection : "rgba(0,0,0,0.25)";
  ctx.lineWidth   = 1.5 / zoom;
  ctx.stroke();

  // Small arrow showing the entry direction (perpendicular to wall, into store)
  // Arrow points in +y direction (into the store from the wall)
  if (zoom >= 2) {
    const aLen = Math.min(D * 0.5, 3 / zoom);
    const hw   = aLen * 0.4;
    ctx.strokeStyle = COLORS.nodeText + "C0";
    ctx.lineWidth   = 1.5 / zoom;
    ctx.beginPath();
    ctx.moveTo(0, -aLen * 0.3);
    ctx.lineTo(0,  aLen * 0.3);
    ctx.moveTo(-hw, -aLen * 0.05);
    ctx.lineTo(0,   -aLen * 0.3);
    ctx.lineTo(hw,  -aLen * 0.05);
    ctx.stroke();
  }

  // Label
  if (zoom >= 3) {
    const fontSize = Math.min(Math.min(W, D) * 0.35, 12 / zoom);
    ctx.font         = `${fontSize}px IBM Plex Mono, monospace`;
    ctx.fillStyle    = COLORS.nodeText;
    ctx.textAlign    = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(node.type.substring(0, 3), 0, 0);
  }

  ctx.restore();
}

function drawFreeObjects(zoom) {
  for (const fo of layoutStore.freeObjects) {
    if (fo.type === "shelf") drawFreeShelf(fo, zoom);
    else                     drawGenericFO(fo, zoom);
  }
}

function drawQueueWaypoints(zoom) {
  const editId  = layoutStore.waypointEditRegisterId;
  const selNode = layoutStore.selection?.type === "node" ? layoutStore.selection.id : null;
  // Only render when register is selected or in waypoint-edit mode
  const showFor = new Set(
    [editId, selNode].filter((x) => x != null && x !== "").map((x) => String(x))
  );
  if (!showFor.size) return;

  for (const queue of layoutStore.checkoutQueues) {
    if (!showFor.has(String(queue.registerId))) continue;
    const regNode = layoutStore.nodes.find((n) => sameEntityId(n.id, queue.registerId));
    if (!regNode || !queue.waypoints.length) continue;

    // Dashed connector: register → wp[0] → wp[1] → …
    ctx.save();
    ctx.strokeStyle = COLORS.waypointLine;
    ctx.lineWidth   = 1.5 / zoom;
    ctx.setLineDash([4 / zoom, 3 / zoom]);
    ctx.beginPath();
    ctx.moveTo(regNode.x, regNode.y);
    for (const wp of queue.waypoints) ctx.lineTo(wp.x, wp.y);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.restore();

    // Waypoint circles
    const r = 3 / zoom;
    for (let i = 0; i < queue.waypoints.length; i++) {
      const wp  = queue.waypoints[i];
      const hot = sameEntityId(dragWaypointRegisterId, queue.registerId) && dragWaypointIndex === i;
      ctx.save();
      ctx.beginPath();
      ctx.arc(wp.x, wp.y, r, 0, Math.PI * 2);
      ctx.fillStyle   = hot ? "#fff" : COLORS.waypointFill;
      ctx.fill();
      ctx.strokeStyle = COLORS.waypointFill;
      ctx.lineWidth   = 1.5 / zoom;
      ctx.stroke();
      if (zoom >= 4) {
        ctx.font         = `${Math.min(r * 1.2, 10 / zoom)}px IBM Plex Mono, monospace`;
        ctx.fillStyle    = hot ? COLORS.waypointFill : COLORS.nodeText;
        ctx.textAlign    = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(String(i + 1), wp.x, wp.y);
      }
      ctx.restore();
    }
  }
}

function drawGenericFO(fo, zoom) {
  const rot = (fo.rotation ?? 0) * Math.PI / 180;
  const sel = layoutStore.selection;
  const isSel = sel?.type === "freeObject" && sameEntityId(sel.id, fo.id);
  ctx.save();
  ctx.translate(fo.x, fo.y);
  ctx.rotate(rot);
  ctx.strokeStyle = isSel ? COLORS.selection : "#C9980A";
  ctx.lineWidth   = (isSel ? 2 : 1.5) / zoom;
  ctx.setLineDash([3 / zoom, 2 / zoom]);
  ctx.strokeRect(-(fo.widthFt ?? 2) / 2, -(fo.lengthFt ?? 2) / 2, fo.widthFt ?? 2, fo.lengthFt ?? 2);
  ctx.setLineDash([]);
  ctx.restore();
}

function drawFreeShelf(fo, zoom) {
  const W   = fo.widthFt  ?? 10;   // along the wall
  const D   = fo.lengthFt ?? 4;    // depth into aisle
  const rot = (fo.rotation ?? 0) * Math.PI / 180;
  const sel = layoutStore.selection;
  const isSel = sel?.type === "freeObject" && sameEntityId(sel.id, fo.id);

  ctx.save();
  ctx.translate(fo.x, fo.y);
  ctx.rotate(rot);

  // Main body
  ctx.beginPath();
  ctx.rect(-W / 2, -D / 2, W, D);
  ctx.fillStyle   = COLORS.aisleFill;
  ctx.fill();
  ctx.strokeStyle = isSel ? COLORS.selection : COLORS.aisleStroke;
  ctx.lineWidth   = (isSel ? 2 : 1) / zoom;
  ctx.stroke();

  // Shelf band — on the +y side (facing direction), depth = min 40% or 1.5ft
  const bandD = Math.min(D * 0.4, 1.5);
  ctx.beginPath();
  ctx.rect(-W / 2, D / 2 - bandD, W, bandD);
  ctx.fillStyle = COLORS.shelfBand + "88";
  ctx.fill();

  // Facing arrow — downward (+y) in local space, shown when zoomed in enough
  if (zoom >= 1.5) {
    const aLen = Math.min(W * 0.25, D * 0.5, 4 / zoom);
    const ay   = 0;
    const hw   = aLen * 0.35;
    ctx.strokeStyle = COLORS.shelfBand;
    ctx.lineWidth   = 1.5 / zoom;
    ctx.beginPath();
    // Shaft
    ctx.moveTo(0, ay - aLen * 0.4);
    ctx.lineTo(0, ay + aLen * 0.4);
    // Arrowhead
    ctx.moveTo(-hw, ay + aLen * 0.05);
    ctx.lineTo(0,   ay + aLen * 0.4);
    ctx.lineTo(hw,  ay + aLen * 0.05);
    ctx.stroke();
  }

  // Selection halo
  if (isSel) {
    const pad = 2 / zoom;
    ctx.beginPath();
    ctx.rect(-W / 2 - pad, -D / 2 - pad, W + pad * 2, D + pad * 2);
    ctx.strokeStyle = COLORS.selection;
    ctx.lineWidth   = 2 / zoom;
    ctx.stroke();
  }

  ctx.restore();
}

/** Scale bar drawn in screen space at bottom-left */
function drawScaleBar(W, H, zoom) {
  if (zoom <= 0) return;
  // Pick a "nice" world distance that gives 60–140px on screen
  const NICE = [0.5, 1, 2, 5, 10, 20, 25, 50, 100, 200, 500, 1000];
  const worldDist = NICE.find((d) => d * zoom >= 60) ?? NICE[NICE.length - 1];
  const barPx = worldDist * zoom;

  const x = 14, y = H - 14;

  ctx.save();
  ctx.setTransform(1, 0, 0, 1, 0, 0); // screen space

  ctx.strokeStyle = COLORS.scaleBar;
  ctx.lineWidth   = 1.5;
  ctx.beginPath();
  ctx.moveTo(x, y);
  ctx.lineTo(x + barPx, y);
  ctx.moveTo(x, y - 4); ctx.lineTo(x, y + 2);
  ctx.moveTo(x + barPx, y - 4); ctx.lineTo(x + barPx, y + 2);
  ctx.stroke();

  ctx.fillStyle    = COLORS.scaleBar;
  ctx.font         = "10px IBM Plex Mono, monospace";
  ctx.textAlign    = "center";
  ctx.textBaseline = "bottom";
  ctx.fillText(`${worldDist} ft`, x + barPx / 2, y - 5);

  ctx.restore();
}

function drawMarquee() {
  const x = Math.min(marqueeStartS.x, marqueeEndS.x);
  const y = Math.min(marqueeStartS.y, marqueeEndS.y);
  const w = Math.abs(marqueeEndS.x - marqueeStartS.x);
  const h = Math.abs(marqueeEndS.y - marqueeStartS.y);
  ctx.save();
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.strokeStyle = COLORS.multiSel;
  ctx.lineWidth   = 1.5;
  ctx.setLineDash([5, 3]);
  ctx.strokeRect(x, y, w, h);
  ctx.fillStyle = COLORS.multiSel + "22";
  ctx.fillRect(x, y, w, h);
  ctx.setLineDash([]);
  ctx.restore();
}

// ---------------------------------------------------------------------------
// Fit to content
// ---------------------------------------------------------------------------
function fitToContent() {
  const c = canvasRef.value;
  if (!c || !c.width || !c.height) return;
  const W = c.width;
  const H = c.height;

  const pts = [
    ...layoutStore.nodes.map((n) => ({ x: n.x, y: n.y })),
    ...layoutStore.boundary,
  ];
  if (!pts.length) return;

  const xs    = pts.map((p) => p.x);
  const ys    = pts.map((p) => p.y);
  const pad   = 8;
  const minX  = Math.min(...xs) - pad;
  const maxX  = Math.max(...xs) + pad;
  const minY  = Math.min(...ys) - pad;
  const maxY  = Math.max(...ys) + pad;
  const worldW = maxX - minX;
  const worldH = maxY - minY;
  if (worldW < 0.1 || worldH < 0.1) return;

  const zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, Math.min(W / worldW, H / worldH) * 0.9));
  const panX = (W - worldW * zoom) / 2 - minX * zoom;
  const panY = (H - worldH * zoom) / 2 - minY * zoom;

  layoutStore.setViewport({ zoom, panX, panY });
  markDirty();
}

defineExpose({ fitToContent });

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------
function screenToWorld(sx, sy) {
  const { zoom, panX, panY } = layoutStore.viewport;
  return [(sx - panX) / zoom, (sy - panY) / zoom];
}

function snap(x, y) {
  if (!layoutStore.snapToGrid) return { x, y };
  const gs = layoutStore.gridSizeFt;
  return { x: Math.round(x / gs) * gs, y: Math.round(y / gs) * gs };
}

/** Clamp world coords to the AABB of the boundary polygon. No-op when no boundary. */
function clampToBoundary(x, y) {
  const pts = layoutStore.boundary;
  if (!pts.length) return { x, y };
  const xs = pts.map((p) => p.x), ys = pts.map((p) => p.y);
  return {
    x: Math.max(Math.min(...xs), Math.min(Math.max(...xs), x)),
    y: Math.max(Math.min(...ys), Math.min(Math.max(...ys), y)),
  };
}

/** Find nearest point on the boundary polygon and return it with the wall angle (degrees). */
function snapToWall(wx, wy) {
  const pts = layoutStore.boundary;
  if (pts.length < 2) return null;
  let bestDist = Infinity, bestX = wx, bestY = wy, bestAngle = 0;
  for (let i = 0; i < pts.length; i++) {
    const a = pts[i], b = pts[(i + 1) % pts.length];
    const abx = b.x - a.x, aby = b.y - a.y;
    const len2 = abx * abx + aby * aby;
    if (len2 === 0) continue;
    const t   = Math.max(0, Math.min(1, ((wx - a.x) * abx + (wy - a.y) * aby) / len2));
    const nx  = a.x + t * abx, ny = a.y + t * aby;
    const d   = Math.hypot(wx - nx, wy - ny);
    if (d < bestDist) {
      bestDist  = d;
      bestX     = nx;
      bestY     = ny;
      bestAngle = Math.atan2(aby, abx) * 180 / Math.PI;
    }
  }
  return { x: bestX, y: bestY, angle: bestAngle };
}

/** Max auto-connect radius derived from the store boundary (40% of the shorter side). */
function autoConnectMaxDist() {
  const pts = layoutStore.boundary;
  if (!pts.length) return 40;
  const xs = pts.map((p) => p.x), ys = pts.map((p) => p.y);
  const w = Math.max(...xs) - Math.min(...xs);
  const h = Math.max(...ys) - Math.min(...ys);
  return 0.4 * Math.min(w, h);
}

/**
 * Auto-connect a newly placed node to up to 2 nearby nodes using an angle-diversity
 * heuristic: connect to the nearest node, then add a 2nd only if it points in a
 * meaningfully different direction (>45°). Falls back to absolute nearest if nothing
 * is within the distance cap.
 */
function autoConnectNode(newId, x, y, edgePatch = {}) {
  const maxDist = autoConnectMaxDist();
  const allOthers = layoutStore.nodes
    .filter((n) => n.id !== newId)
    .map((n) => ({ node: n, d: Math.hypot(n.x - x, n.y - y) }))
    .sort((a, b) => a.d - b.d);

  const inRange = allOthers.filter(({ d }) => d <= maxDist);
  const pool = inRange.length ? inRange : allOthers.slice(0, 1);

  const chosen = [];
  for (const { node } of pool) {
    if (chosen.length === 0) { chosen.push(node); continue; }
    if (chosen.length >= 2) break;
    const a1 = Math.atan2(chosen[0].y - y, chosen[0].x - x);
    const a2 = Math.atan2(node.y - y, node.x - x);
    let diff = Math.abs(a1 - a2);
    if (diff > Math.PI) diff = 2 * Math.PI - diff;
    if (diff >= Math.PI / 4) chosen.push(node);
  }

  for (const node of chosen) {
    const dup = layoutStore.edges.some(
      (e) =>
        (sameEntityId(e.nodeA, newId) && sameEntityId(e.nodeB, node.id)) ||
        (sameEntityId(e.nodeB, newId) && sameEntityId(e.nodeA, node.id))
    );
    if (!dup) layoutStore.addEdge(newId, node.id, edgePatch);
  }
}

// ---------------------------------------------------------------------------
// Hit testing (world space)
// ---------------------------------------------------------------------------
function hitTestNode(wx, wy) {
  const zoom = layoutStore.viewport.zoom;
  const pad  = 4 / zoom;
  for (const node of layoutStore.nodes) {
    if (node.type === "Entrance" || node.type === "Exit") {
      // Rotated rectangle test
      const angle = (node.angle ?? 0) * Math.PI / 180;
      const dx = wx - node.x, dy = wy - node.y;
      const lx = dx * Math.cos(-angle) - dy * Math.sin(-angle);
      const ly = dx * Math.sin(-angle) + dy * Math.cos(-angle);
      if (Math.abs(lx) <= node.width / 2 + pad && Math.abs(ly) <= node.length / 2 + pad) return node;
    } else {
      const r   = Math.max(node.width, node.length) / 2;
      const tol = Math.max(r, 6 / zoom);
      if (Math.hypot(wx - node.x, wy - node.y) <= tol) return node;
    }
  }
  return null;
}

function hitTestEdge(wx, wy) {
  const nodeMap = nodeMapByStringId(layoutStore.nodes);
  for (const edge of layoutStore.edges) {
    const na = nodeMap.get(String(edge.nodeA));
    const nb = nodeMap.get(String(edge.nodeB));
    if (!na || !nb) continue;
    if (ptSegDist(wx, wy, na.x, na.y, nb.x, nb.y) <= edge.width / 2) return edge;
  }
  return null;
}

function ptSegDist(px, py, ax, ay, bx, by) {
  const abx = bx - ax, aby = by - ay;
  const apx = px - ax, apy = py - ay;
  const len2 = abx * abx + aby * aby;
  const t    = len2 > 0 ? Math.max(0, Math.min(1, (apx * abx + apy * aby) / len2)) : 0;
  return Math.hypot(apx - t * abx, apy - t * aby);
}

function hitTestQueueWaypoint(wx, wy) {
  const editId  = layoutStore.waypointEditRegisterId;
  const selNode = layoutStore.selection?.type === "node" ? layoutStore.selection.id : null;
  const zoom    = layoutStore.viewport.zoom;
  const tol     = 6 / zoom;
  for (const queue of layoutStore.checkoutQueues) {
    if (!sameEntityId(queue.registerId, editId) && !sameEntityId(queue.registerId, selNode)) continue;
    for (let i = 0; i < queue.waypoints.length; i++) {
      const wp = queue.waypoints[i];
      if (Math.hypot(wx - wp.x, wy - wp.y) <= tol) {
        return { registerId: queue.registerId, index: i };
      }
    }
  }
  return null;
}

function hitTestBoundaryVertex(wx, wy) {
  const zoom = layoutStore.viewport.zoom;
  const tol  = 6 / zoom;
  const pts  = layoutStore.boundary;
  for (let i = 0; i < pts.length; i++) {
    if (Math.hypot(wx - pts[i].x, wy - pts[i].y) <= tol) return i;
  }
  return -1;
}

function hitTestBoundaryMidpoint(wx, wy) {
  const zoom = layoutStore.viewport.zoom;
  const tol  = 5 / zoom;
  const pts  = layoutStore.boundary;
  for (let i = 0; i < pts.length; i++) {
    const a  = pts[i], b = pts[(i + 1) % pts.length];
    const mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
    if (Math.hypot(wx - mx, wy - my) <= tol) return i;
  }
  return -1;
}

function hitTestFreeObject(wx, wy) {
  // Test in reverse order so topmost (last drawn) is selected first
  for (let i = layoutStore.freeObjects.length - 1; i >= 0; i--) {
    const fo  = layoutStore.freeObjects[i];
    const W   = fo.widthFt  ?? 2;
    const D   = fo.lengthFt ?? 2;
    const rot = (fo.rotation ?? 0) * Math.PI / 180;
    // Transform world point to object local space
    const dx  = wx - fo.x, dy = wy - fo.y;
    const cos = Math.cos(-rot), sin = Math.sin(-rot);
    const lx  = dx * cos - dy * sin;
    const ly  = dx * sin + dy * cos;
    if (Math.abs(lx) <= W / 2 && Math.abs(ly) <= D / 2) return fo;
  }
  return null;
}

// ---------------------------------------------------------------------------
// Zoom (scroll wheel)
// ---------------------------------------------------------------------------
function onWheel(e) {
  const c    = canvasRef.value;
  const rect = c.getBoundingClientRect();
  const mx   = e.clientX - rect.left;
  const my   = e.clientY - rect.top;

  const factor  = e.deltaY < 0 ? 1.15 : 1 / 1.15;
  const { zoom, panX, panY } = layoutStore.viewport;
  const newZoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, zoom * factor));

  const wx = (mx - panX) / zoom;
  const wy = (my - panY) / zoom;
  layoutStore.setViewport({ zoom: newZoom, panX: mx - wx * newZoom, panY: my - wy * newZoom });
  markDirty();
}

// ---------------------------------------------------------------------------
// Pointer — main interaction dispatcher
// ---------------------------------------------------------------------------
function onPointerDown(e) {
  if (e.button !== 0 && e.button !== 1) return;

  const c    = canvasRef.value;
  const rect = c.getBoundingClientRect();
  const sx   = e.clientX - rect.left;
  const sy   = e.clientY - rect.top;
  const [wx, wy] = screenToWorld(sx, sy);

  // Middle-click or Space+left always pans
  if (e.button === 1 || (e.button === 0 && isSpaceDown.value)) {
    startPan(e);
    return;
  }

  const tool = layoutStore.activeTool;

  if (tool === "boundary") {
    const vIdx = hitTestBoundaryVertex(wx, wy);
    const mIdx = vIdx < 0 ? hitTestBoundaryMidpoint(wx, wy) : -1;
    if (vIdx >= 0) {
      isDraggingBoundary.value = true;
      dragBoundaryIndex        = vIdx;
      c.setPointerCapture(e.pointerId);
      e.preventDefault();
    } else if (mIdx >= 0) {
      const a = layoutStore.boundary[mIdx];
      const b = layoutStore.boundary[(mIdx + 1) % layoutStore.boundary.length];
      const mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
      layoutStore.insertBoundaryVertex(mIdx, mx, my);
      // Immediately start dragging the new vertex (now at mIdx+1)
      isDraggingBoundary.value = true;
      dragBoundaryIndex        = mIdx + 1;
      c.setPointerCapture(e.pointerId);
      e.preventDefault();
    }
    markDirty();
    return;
  }

  if (tool === "select") {
    // Check waypoint hit first (when register selected or waypoint-edit active)
    const hitWP = hitTestQueueWaypoint(wx, wy);
    if (hitWP) {
      isDraggingWaypoint.value   = true;
      dragWaypointRegisterId     = hitWP.registerId;
      dragWaypointIndex          = hitWP.index;
      c.setPointerCapture(e.pointerId);
      e.preventDefault();
      markDirty();
      return;
    }

    const hitNode = hitTestNode(wx, wy);
    // Test freeObjects before edges so shelves sitting inside an edge can be selected
    const hitFO   = !hitNode ? hitTestFreeObject(wx, wy) : null;
    const hitEdge = !hitNode && !hitFO ? hitTestEdge(wx, wy) : null;

    if (e.shiftKey) {
      // Shift+click: toggle items in multi-selection
      if (hitNode) {
        layoutStore.toggleMultiSelection("node", hitNode.id);
      } else if (hitFO) {
        layoutStore.toggleMultiSelection("freeObject", hitFO.id);
      } else if (hitEdge) {
        layoutStore.toggleMultiSelection("edge", hitEdge.id);
      } else {
        // Shift+drag on empty: start rubber-band marquee
        isMarquee.value = true;
        marqueeStartS   = { x: sx, y: sy };
        marqueeEndS     = { x: sx, y: sy };
        c.setPointerCapture(e.pointerId);
        e.preventDefault();
      }
      markDirty();
      return;
    }

    if (hitNode) {
      layoutStore.clearMultiSelection();
      layoutStore.select("node", hitNode.id);
      isDraggingNode.value = true;
      dragNodeId           = hitNode.id;
      c.setPointerCapture(e.pointerId);
      e.preventDefault();
    } else if (hitFO) {
      layoutStore.clearMultiSelection();
      layoutStore.select("freeObject", hitFO.id);
      isDraggingFO.value = true;
      dragFOId           = hitFO.id;
      dragFOOffset       = { x: hitFO.x - wx, y: hitFO.y - wy };
      c.setPointerCapture(e.pointerId);
      e.preventDefault();
    } else if (hitEdge) {
      layoutStore.clearMultiSelection();
      layoutStore.select("edge", hitEdge.id);
      startPan(e);
    } else {
      layoutStore.clearSelection();
      startPan(e);
    }
    markDirty();

  } else if (["junction", "register", "entrance", "exit", "stockroom"].includes(tool)) {
    const type = tool.charAt(0).toUpperCase() + tool.slice(1);

    // Entrance/Exit/Stockroom snap to nearest wall; others snap to grid then clamp to boundary
    let x, y, angle = 0;
    if (type === "Entrance" || type === "Exit" || type === "Stockroom") {
      const snapped = snapToWall(wx, wy);
      if (snapped) { x = snapped.x; y = snapped.y; angle = snapped.angle; }
      else { const s = snap(wx, wy); x = s.x; y = s.y; }
    } else {
      const s = snap(wx, wy);
      const c = clampToBoundary(s.x, s.y);
      x = c.x; y = c.y;
    }

    const newId = layoutStore.addNode({ type, x, y, angle });

    if (type === "Register")  autoConnectNode(newId, x, y, { edgeType: "aisle", width: 8 });
    if (type === "Junction")  autoConnectNode(newId, x, y, { edgeType: "aisle" });
    if (type === "Stockroom") autoConnectNode(newId, x, y, { edgeType: "aisle" });

    layoutStore.setActiveTool("select");

  } else if (tool === "shelf") {
    const snapped = snap(wx, wy);
    const { x, y } = clampToBoundary(snapped.x, snapped.y);
    layoutStore.addFreeShelf(x, y);
    layoutStore.setActiveTool("select");

  } else if (tool === "draw-edge") {
    const hitNode = hitTestNode(wx, wy);
    if (hitNode) {
      if (edgeDrawSourceId === null) {
        edgeDrawSourceId = hitNode.id;
        layoutStore.select("node", hitNode.id);
      } else if (!sameEntityId(edgeDrawSourceId, hitNode.id)) {
        layoutStore.addEdge(edgeDrawSourceId, hitNode.id);
        edgeDrawSourceId = null;
        edgeDraftEnd     = null;
        layoutStore.setActiveTool("select");
      }
      markDirty();
    }
  }
}

function startPan(e) {
  isPanning.value = true;
  panStart        = { x: e.clientX, y: e.clientY };
  panStartVp      = { ...layoutStore.viewport };
  canvasRef.value.setPointerCapture(e.pointerId);
  e.preventDefault();
}

function onPointerMove(e) {
  const c    = canvasRef.value;
  const rect = c.getBoundingClientRect();
  const sx   = e.clientX - rect.left;
  const sy   = e.clientY - rect.top;
  const [wx, wy] = screenToWorld(sx, sy);

  if (isMarquee.value) {
    marqueeEndS = { x: sx, y: sy };
    markDirty();
    return;
  }

  if (isDraggingWaypoint.value && dragWaypointRegisterId !== null) {
    const snapped = snap(wx, wy);
    layoutStore.moveQueueWaypoint(dragWaypointRegisterId, dragWaypointIndex, snapped.x, snapped.y);
    markDirty();
    return;
  }

  if (isDraggingBoundary.value && dragBoundaryIndex >= 0) {
    const snapped = snap(wx, wy);
    layoutStore.moveBoundaryVertex(dragBoundaryIndex, snapped.x, snapped.y);
    markDirty();
    return;
  }

  if (isDraggingNode.value && dragNodeId !== null) {
    const dragNode = layoutStore.nodes.find((n) => sameEntityId(n.id, dragNodeId));
    if (dragNode && (dragNode.type === "Entrance" || dragNode.type === "Exit" || dragNode.type === "Stockroom")) {
      const wall = snapToWall(wx, wy);
      if (wall) {
        layoutStore.moveNode(dragNodeId, wall.x, wall.y, wall.angle);
      }
    } else {
      const snapped = snap(wx, wy);
      const clamped = clampToBoundary(snapped.x, snapped.y);
      layoutStore.moveNode(dragNodeId, clamped.x, clamped.y);
    }
    markDirty();
    return;
  }

  if (isDraggingFO.value && dragFOId !== null) {
    const snapped  = snap(wx + dragFOOffset.x, wy + dragFOOffset.y);
    const clamped  = clampToBoundary(snapped.x, snapped.y);
    layoutStore.moveFreeObject(dragFOId, clamped.x, clamped.y);
    markDirty();
    return;
  }

  if (isPanning.value) {
    layoutStore.setViewport({
      zoom: panStartVp.zoom,
      panX: panStartVp.panX + (e.clientX - panStart.x),
      panY: panStartVp.panY + (e.clientY - panStart.y),
    });
    markDirty();
    return;
  }

  // Hover tracking (select tool)
  if (layoutStore.activeTool === "select") {
    const hit = hitTestNode(wx, wy);
    const newId = hit?.id ?? null;
    if (!sameEntityId(newId, hoverNodeId.value)) {
      hoverNodeId.value = newId;
      markDirty();
    }
  }

  // Hover tracking (boundary tool)
  if (layoutStore.activeTool === "boundary") {
    const vIdx = hitTestBoundaryVertex(wx, wy);
    const mIdx = vIdx < 0 ? hitTestBoundaryMidpoint(wx, wy) : -1;
    let changed = false;
    if (vIdx !== hoverBoundaryVertex.value) { hoverBoundaryVertex.value = vIdx; changed = true; }
    if (mIdx !== hoverBoundaryMidpt.value)  { hoverBoundaryMidpt.value  = mIdx; changed = true; }
    if (changed) markDirty();
  }

  // Draft edge endpoint
  if (layoutStore.activeTool === "draw-edge" && edgeDrawSourceId !== null) {
    edgeDraftEnd = { x: wx, y: wy };
    markDirty();
  }
}

/** Convert marquee screen rect to world rect and collect contained items. */
function finishMarquee() {
  const [x0, y0] = screenToWorld(marqueeStartS.x, marqueeStartS.y);
  const [x1, y1] = screenToWorld(marqueeEndS.x,   marqueeEndS.y);
  const minX = Math.min(x0, x1), maxX = Math.max(x0, x1);
  const minY = Math.min(y0, y1), maxY = Math.max(y0, y1);

  // Skip tiny accidental drags (< 5px in screen space)
  if (Math.abs(marqueeEndS.x - marqueeStartS.x) < 5 &&
      Math.abs(marqueeEndS.y - marqueeStartS.y) < 5) return;

  const items = [];
  for (const node of layoutStore.nodes) {
    if (node.x >= minX && node.x <= maxX && node.y >= minY && node.y <= maxY) {
      items.push({ type: "node", id: node.id });
    }
  }
  const nodeMap = nodeMapByStringId(layoutStore.nodes);
  for (const edge of layoutStore.edges) {
    const na = nodeMap.get(String(edge.nodeA));
    const nb = nodeMap.get(String(edge.nodeB));
    if (!na || !nb) continue;
    // Include edge if its midpoint is inside the marquee
    const mx = (na.x + nb.x) / 2, my = (na.y + nb.y) / 2;
    if (mx >= minX && mx <= maxX && my >= minY && my <= maxY) {
      items.push({ type: "edge", id: edge.id });
    }
  }
  if (items.length) layoutStore.setMultiSelection(items);
}

function onPointerUp() {
  if (isMarquee.value) {
    isMarquee.value = false;
    finishMarquee();
    markDirty();
    return;
  }
  if (isDraggingWaypoint.value) {
    layoutStore.commitQueueWaypointMove();
    isDraggingWaypoint.value   = false;
    dragWaypointRegisterId     = null;
    dragWaypointIndex          = null;
  }
  if (isDraggingBoundary.value) {
    layoutStore.commitBoundaryMove();
    isDraggingBoundary.value = false;
    dragBoundaryIndex        = -1;
  }
  if (isDraggingNode.value) {
    layoutStore.commitMove();
    isDraggingNode.value = false;
    dragNodeId           = null;
  }
  if (isDraggingFO.value) {
    layoutStore.commitMove();
    isDraggingFO.value = false;
    dragFOId           = null;
  }
  isPanning.value = false;
}

function onContextMenu(e) {
  if (layoutStore.activeTool !== "boundary") return;
  e.preventDefault();
  const c    = canvasRef.value;
  const rect = c.getBoundingClientRect();
  const [wx, wy] = screenToWorld(e.clientX - rect.left, e.clientY - rect.top);
  const vIdx = hitTestBoundaryVertex(wx, wy);
  if (vIdx >= 0) layoutStore.removeBoundaryVertex(vIdx);
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
function onKeyDown(e) {
  // Delete / Backspace — remove selected
  if ((e.code === "Delete" || e.code === "Backspace") && !e.target.matches("input, textarea, select")) {
    if (layoutStore.multiSelection.length > 0) {
      layoutStore.removeMultiSelected();
    } else {
      const sel = layoutStore.selection;
      if (sel?.type === "node")       layoutStore.removeNode(sel.id);
      else if (sel?.type === "edge")  layoutStore.removeEdge(sel.id);
      else if (sel?.type === "freeObject") layoutStore.removeFreeObject(sel.id);
    }
    return;
  }

  // Escape — cancel edge draw / boundary mode, or clear multi-selection, or clear selection
  if (e.code === "Escape") {
    if (isMarquee.value) {
      isMarquee.value = false;
      markDirty();
      return;
    }
    if (edgeDrawSourceId !== null) {
      edgeDrawSourceId = null;
      edgeDraftEnd     = null;
    }
    hoverBoundaryVertex.value = -1;
    hoverBoundaryMidpt.value  = -1;
    layoutStore.clearSelection();
    if (layoutStore.activeTool !== "select") {
      layoutStore.setActiveTool("select");
    }
    markDirty();
    return;
  }

  // Ctrl+Z / Ctrl+Y
  if (e.ctrlKey && e.code === "KeyZ") { layoutStore.undo(); return; }
  if (e.ctrlKey && (e.code === "KeyY" || (e.shiftKey && e.code === "KeyZ"))) {
    layoutStore.redo(); return;
  }

  // Space = pan mode
  if (e.code === "Space" && !e.target.matches("input, textarea, select")) {
    if (!isSpaceDown.value) { isSpaceDown.value = true; markDirty(); }
    e.preventDefault();
  }
}

function onKeyUp(e) {
  if (e.code === "Space") {
    isSpaceDown.value = false;
    if (isPanning.value) isPanning.value = false;
    markDirty();
  }
}
</script>
