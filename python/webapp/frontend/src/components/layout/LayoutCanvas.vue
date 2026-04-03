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
      @contextmenu.prevent
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
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount, watch } from "vue";
import { useLayoutStore } from "../../stores/layout";

const layoutStore = useLayoutStore();

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
const isPanning         = ref(false);
const isSpaceDown       = ref(false);
const isDraggingNode    = ref(false);
const isDraggingFO      = ref(false);
const hoverNodeId       = ref(null);

// Plain interaction state (canvas-only)
let panStart            = { x: 0, y: 0 };
let panStartVp          = { zoom: 1, panX: 0, panY: 0 };
let dragNodeId          = null;
let dragFOId            = null;
let dragFOOffset        = { x: 0, y: 0 };   // pointer offset from FO center at drag start
let edgeDrawSourceId    = null;
let edgeDraftEnd        = null;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
const MIN_ZOOM = 0.02;
const MAX_ZOOM = 80;

const COLORS = {
  grid:           "rgba(28,26,20,0.06)",
  boundary:       "#D8D3C2",
  boundaryStroke: "#6A6760",
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
  hover:          "#C9980A",
  edgeDraft:      "#6B56D6",
  scaleBar:       "#6A6760",
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
  if (isPanning.value || isDraggingNode.value || isDraggingFO.value) return "grabbing";
  if (isSpaceDown.value) return "grab";
  const tool = layoutStore.activeTool;
  if (tool === "select") return hoverNodeId.value !== null ? "grab" : "default";
  if (["junction", "register", "entrance", "exit", "draw-edge", "shelf"].includes(tool)) return "crosshair";
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
    layoutStore.showGrid,
    layoutStore.selection,
    layoutStore.viewport,
  ],
  () => markDirty(),
  { deep: true }
);

// Reset edge-draw state when tool changes
watch(
  () => layoutStore.activeTool,
  (tool) => {
    if (tool !== "draw-edge") {
      edgeDrawSourceId = null;
      edgeDraftEnd     = null;
      markDirty();
    }
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
  ctx.fillStyle = "#E2DFD1";
  ctx.fillRect(0, 0, W, H);

  ctx.save();
  ctx.setTransform(zoom, 0, 0, zoom, panX, panY);

  if (layoutStore.showGrid) drawGrid(W, H, zoom, panX, panY);
  drawBoundary();
  drawEdges(zoom);
  drawEdgeDraft(zoom);
  drawNodes(zoom);
  drawFreeObjects(zoom);

  ctx.restore();

  // Screen-space overlays (drawn after restore, no world transform)
  drawScaleBar(W, H, zoom);
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

function drawBoundary() {
  const pts = layoutStore.boundary;
  if (!pts.length) return;
  ctx.save();
  ctx.beginPath();
  ctx.moveTo(pts[0].x, pts[0].y);
  for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x, pts[i].y);
  ctx.closePath();
  ctx.fillStyle   = COLORS.boundary;
  ctx.fill();
  ctx.strokeStyle = COLORS.boundaryStroke;
  ctx.lineWidth   = 2 / layoutStore.viewport.zoom;
  ctx.stroke();
  ctx.restore();
}

function drawEdges(zoom) {
  const nodeMap = new Map(layoutStore.nodes.map((n) => [n.id, n]));
  const selEdgeId = layoutStore.selection?.type === "edge" ? layoutStore.selection.id : null;
  for (const edge of layoutStore.edges) {
    drawEdge(edge, nodeMap, zoom, edge.id === selEdgeId);
  }
}

function drawEdge(edge, nodeMap, zoom, isSelected) {
  const na = nodeMap.get(edge.nodeA);
  const nb = nodeMap.get(edge.nodeB);
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
  ctx.strokeStyle = isSelected
    ? COLORS.selection
    : isWall ? COLORS.wallStroke : COLORS.aisleStroke;
  ctx.lineWidth   = isSelected ? 2 / zoom : 1 / zoom;
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
  const src = layoutStore.nodes.find((n) => n.id === edgeDrawSourceId);
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
  const sel = layoutStore.selection;
  for (const node of layoutStore.nodes) {
    const isSelected = sel?.type === "node" && sel?.id === node.id;
    const isHovered  = hoverNodeId.value === node.id;
    const isEdgeSrc  = edgeDrawSourceId === node.id;
    drawNode(node, zoom, isSelected, isHovered, isEdgeSrc);
  }
}

function drawNode(node, zoom, isSelected, isHovered, isEdgeSrc) {
  if (node.type === "Entrance" || node.type === "Exit") {
    drawNodeRect(node, zoom, isSelected, isHovered, isEdgeSrc);
  } else {
    drawNodeCircle(node, zoom, isSelected, isHovered, isEdgeSrc);
  }
}

function drawNodeCircle(node, zoom, isSelected, isHovered, isEdgeSrc) {
  const r     = Math.max(node.width, node.length) / 2;
  const color = NODE_TYPE_COLOR[node.type] ?? COLORS.nodeJunction;

  ctx.save();

  if (isEdgeSrc || isSelected) {
    ctx.beginPath();
    ctx.arc(node.x, node.y, r + 4 / zoom, 0, Math.PI * 2);
    ctx.strokeStyle = isEdgeSrc ? COLORS.edgeDraft : COLORS.selection;
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

function drawNodeRect(node, zoom, isSelected, isHovered, isEdgeSrc) {
  const W     = node.width;
  const D     = node.length;
  const color = NODE_TYPE_COLOR[node.type] ?? COLORS.nodeJunction;
  const angle = (node.angle ?? 0) * Math.PI / 180;
  const pad   = 3 / zoom;

  ctx.save();
  ctx.translate(node.x, node.y);
  ctx.rotate(angle);

  // Selection / hover halo
  if (isEdgeSrc || isSelected || isHovered) {
    ctx.beginPath();
    ctx.rect(-W / 2 - pad, -D / 2 - pad, W + pad * 2, D + pad * 2);
    ctx.strokeStyle = isEdgeSrc ? COLORS.edgeDraft
                    : isSelected ? COLORS.selection
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

function drawGenericFO(fo, zoom) {
  const rot = (fo.rotation ?? 0) * Math.PI / 180;
  const sel = layoutStore.selection;
  const isSel = sel?.type === "freeObject" && sel?.id === fo.id;
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
  const isSel = sel?.type === "freeObject" && sel?.id === fo.id;

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
  const nodeMap = new Map(layoutStore.nodes.map((n) => [n.id, n]));
  for (const edge of layoutStore.edges) {
    const na = nodeMap.get(edge.nodeA);
    const nb = nodeMap.get(edge.nodeB);
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

  if (tool === "select") {
    const hitNode = hitTestNode(wx, wy);
    // Test freeObjects before edges so shelves sitting inside an edge can be selected
    const hitFO   = !hitNode ? hitTestFreeObject(wx, wy) : null;
    const hitEdge = !hitNode && !hitFO ? hitTestEdge(wx, wy) : null;

    if (hitNode) {
      layoutStore.select("node", hitNode.id);
      isDraggingNode.value = true;
      dragNodeId           = hitNode.id;
      c.setPointerCapture(e.pointerId);
      e.preventDefault();
    } else if (hitFO) {
      layoutStore.select("freeObject", hitFO.id);
      isDraggingFO.value = true;
      dragFOId           = hitFO.id;
      dragFOOffset       = { x: hitFO.x - wx, y: hitFO.y - wy };
      c.setPointerCapture(e.pointerId);
      e.preventDefault();
    } else if (hitEdge) {
      layoutStore.select("edge", hitEdge.id);
      startPan(e);
    } else {
      layoutStore.clearSelection();
      startPan(e);
    }
    markDirty();

  } else if (["junction", "register", "entrance", "exit"].includes(tool)) {
    const type = tool.charAt(0).toUpperCase() + tool.slice(1);

    // Entrance/Exit snap to nearest wall; others snap to grid
    let x, y, angle = 0;
    if (type === "Entrance" || type === "Exit") {
      const snapped = snapToWall(wx, wy);
      if (snapped) { x = snapped.x; y = snapped.y; angle = snapped.angle; }
      else { const s = snap(wx, wy); x = s.x; y = s.y; }
    } else {
      const s = snap(wx, wy); x = s.x; y = s.y;
    }

    const newId = layoutStore.addNode({ type, x, y, angle });

    // Register → auto-edge to nearest Exit
    if (type === "Register") {
      const exits = layoutStore.nodes.filter((n) => n.type === "Exit" && n.id !== newId);
      if (exits.length) {
        const nearest = exits.reduce((b, n) => {
          const d = Math.hypot(n.x - x, n.y - y);
          return d < b.d ? { node: n, d } : b;
        }, { node: null, d: Infinity }).node;
        if (nearest) layoutStore.addEdge(newId, nearest.id, { edgeType: "aisle", width: 8 });
      }
    }

    // Junction → auto-connect to 2 nearest existing nodes (forms cycles)
    if (type === "Junction") {
      const others = layoutStore.nodes
        .filter((n) => n.id !== newId)
        .map((n) => ({ node: n, d: Math.hypot(n.x - x, n.y - y) }))
        .sort((a, b) => a.d - b.d)
        .slice(0, 2);
      for (const { node } of others) {
        const dup = layoutStore.edges.some(
          (e) => (e.nodeA === newId && e.nodeB === node.id) ||
                 (e.nodeB === newId && e.nodeA === node.id)
        );
        if (!dup) layoutStore.addEdge(newId, node.id, { edgeType: "aisle" });
      }
    }

    layoutStore.setActiveTool("select");

  } else if (tool === "shelf") {
    const { x, y } = snap(wx, wy);
    layoutStore.addFreeShelf(x, y);
    layoutStore.setActiveTool("select");

  } else if (tool === "draw-edge") {
    const hitNode = hitTestNode(wx, wy);
    if (hitNode) {
      if (edgeDrawSourceId === null) {
        edgeDrawSourceId = hitNode.id;
        layoutStore.select("node", hitNode.id);
      } else if (edgeDrawSourceId !== hitNode.id) {
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

  if (isDraggingNode.value && dragNodeId !== null) {
    const { x, y } = snap(wx, wy);
    layoutStore.moveNode(dragNodeId, x, y);
    markDirty();
    return;
  }

  if (isDraggingFO.value && dragFOId !== null) {
    const { x, y } = snap(wx + dragFOOffset.x, wy + dragFOOffset.y);
    layoutStore.moveFreeObject(dragFOId, x, y);
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
    if (newId !== hoverNodeId.value) {
      hoverNodeId.value = newId;
      markDirty();
    }
  }

  // Draft edge endpoint
  if (layoutStore.activeTool === "draw-edge" && edgeDrawSourceId !== null) {
    edgeDraftEnd = { x: wx, y: wy };
    markDirty();
  }
}

function onPointerUp() {
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

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
function onKeyDown(e) {
  // Delete / Backspace — remove selected
  if ((e.code === "Delete" || e.code === "Backspace") && !e.target.matches("input, textarea, select")) {
    const sel = layoutStore.selection;
    if (sel?.type === "node")       layoutStore.removeNode(sel.id);
    else if (sel?.type === "edge")  layoutStore.removeEdge(sel.id);
    else if (sel?.type === "freeObject") layoutStore.removeFreeObject(sel.id);
    return;
  }

  // Escape — cancel edge draw or clear selection
  if (e.code === "Escape") {
    if (edgeDrawSourceId !== null) {
      edgeDrawSourceId = null;
      edgeDraftEnd     = null;
      markDirty();
    }
    layoutStore.clearSelection();
    if (layoutStore.activeTool !== "select") {
      layoutStore.setActiveTool("select");
    }
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
