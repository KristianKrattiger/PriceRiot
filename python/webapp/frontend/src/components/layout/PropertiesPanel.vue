<template>
  <div class="flex flex-col h-full min-h-0 overflow-hidden">

    <!-- Header -->
    <div class="px-3 py-2 border-b border-rim shrink-0">
      <p class="text-[10px] uppercase tracking-label text-ink-ghost">Properties</p>
    </div>

    <!-- Multi-selection summary -->
    <div
      v-if="!layoutStore.selection && layoutStore.multiSelection.length > 0"
      class="flex-1 min-h-0 flex flex-col gap-3 p-3 overflow-y-auto"
    >
      <div class="space-y-1">
        <p class="text-[10px] uppercase tracking-label text-ink-ghost">Selection</p>
        <p class="text-sm font-mono text-ink">{{ layoutStore.multiSelection.length }} objects</p>
        <p class="text-[10px] font-mono text-ink-ghost">
          {{ multiSelSummary }}
        </p>
      </div>
      <!-- Bulk edge width -->
      <div v-if="multiEdgeCount > 0" class="space-y-1.5">
        <p class="text-[10px] uppercase tracking-label text-ink-ghost">
          Bulk edit · {{ multiEdgeCount }} edge{{ multiEdgeCount > 1 ? "s" : "" }}
        </p>
        <div class="flex items-center gap-2">
          <label class="text-[10px] font-mono text-ink-ghost shrink-0">Width (ft)</label>
          <input
            v-model.number="bulkEdgeWidth"
            type="number" min="0.1" step="0.5"
            class="flex-1 min-w-0 bg-surface-deep border border-rim px-2 py-1
                   text-[11px] font-mono text-ink focus:outline-none focus:border-deep-teal/60"
          />
          <button
            type="button"
            @click="applyBulkEdgeWidth"
            class="shrink-0 px-2 py-1 text-[10px] font-mono border border-rim text-ink-ghost
                   hover:text-ink hover:bg-surface-hover transition-colors"
          >Apply</button>
        </div>
        <!-- Edge type bulk set -->
        <div class="flex gap-1">
          <button
            type="button"
            @click="layoutStore.bulkUpdateEdges({ edgeType: 'aisle' })"
            class="flex-1 py-1 text-[10px] font-mono border border-rim text-ink-ghost
                   hover:bg-surface-hover transition-colors"
          >→ Aisle</button>
          <button
            type="button"
            @click="layoutStore.bulkUpdateEdges({ edgeType: 'wall' })"
            class="flex-1 py-1 text-[10px] font-mono border border-rim text-ink-ghost
                   hover:bg-surface-hover transition-colors"
          >→ Wall</button>
        </div>
      </div>

      <button
        type="button"
        @click="layoutStore.removeMultiSelected()"
        class="w-full py-1.5 px-3 text-[11px] font-mono border border-red-400/40 text-red-400
               hover:bg-red-400/10 transition-colors"
      >Delete all selected</button>
      <button
        type="button"
        @click="layoutStore.clearMultiSelection()"
        class="w-full py-1.5 px-3 text-[11px] font-mono border border-rim text-ink-ghost
               hover:text-ink hover:bg-surface-hover transition-colors"
      >Clear selection</button>
    </div>

    <!-- No selection -->
    <div
      v-else-if="!layoutStore.selection"
      class="flex-1 min-h-0 flex items-center justify-center p-4 overflow-y-auto"
    >
      <p class="text-[10px] font-mono text-ink-ghost/40 text-center leading-relaxed">
        Select a node, edge,<br>or shelf to edit<br>properties.
      </p>
    </div>

    <!-- Stale / unknown selection -->
    <div
      v-else-if="!hasRenderableSelection"
      class="flex-1 min-h-0 flex flex-col items-center justify-center gap-3 p-4 overflow-y-auto"
    >
      <p class="text-[10px] font-mono text-ink-ghost/50 text-center leading-relaxed">
        Nothing to edit for this selection.<br>
        Try clicking a node, aisle edge, or shelf on the canvas.
      </p>
      <button
        type="button"
        @click="layoutStore.clearSelection()"
        class="py-1.5 px-3 text-[11px] font-mono border border-rim text-ink-ghost
               hover:text-ink hover:bg-surface-hover transition-colors"
      >Clear selection</button>
    </div>

    <!-- Node properties -->
    <div
      v-else-if="layoutStore.selection.type === 'node' && localNode"
      class="flex flex-col flex-1 min-h-0"
    >
      <div class="flex-1 min-h-0 overflow-y-auto px-3 py-3 space-y-3">

        <!-- Type + ID badge -->
        <div class="flex items-center justify-between">
          <span
            class="text-[10px] font-mono px-2 py-0.5 border"
            :style="{ borderColor: nodeColor + '60', color: nodeColor, background: nodeColor + '18' }"
          >{{ localNode.type }}</span>
          <span class="text-[10px] font-mono text-ink-ghost">id {{ localNode.id }}</span>
        </div>

        <!-- Position -->
        <FieldGroup label="Position">
          <div class="grid grid-cols-2 gap-2">
            <PropInput label="X (ft)" v-model.number="localNode.x" @blur="applyNode" />
            <PropInput label="Y (ft)" v-model.number="localNode.y" @blur="applyNode" />
          </div>
        </FieldGroup>

        <!-- Size -->
        <FieldGroup label="Size">
          <div class="grid grid-cols-2 gap-2">
            <PropInput label="Width (ft)"  v-model.number="localNode.width"  @blur="applyNode" />
            <PropInput label="Length (ft)" v-model.number="localNode.length" @blur="applyNode" />
          </div>
        </FieldGroup>

        <!-- Type-specific fields -->
        <FieldGroup v-if="localNode.type === 'Entrance'" label="Entry">
          <PropInput label="Entry rate (cust/s)" v-model.number="localNode.entryRate" @blur="applyNode" />
        </FieldGroup>

        <FieldGroup v-if="localNode.type === 'Exit'" label="Exit">
          <PropInput label="Exit rate" v-model.number="localNode.exitRate" @blur="applyNode" />
        </FieldGroup>

        <FieldGroup v-if="localNode.type === 'Register'" label="Checkout">
          <PropInput label="Service rate (cust/s)" v-model.number="localNode.serviceRate" @blur="applyNode" />
        </FieldGroup>

        <!-- Checkout queue (Register only) -->
        <FieldGroup v-if="localNode.type === 'Register' && localQueue" label="Queue">
          <PropInput label="Processing time (s)" v-model.number="localQueue.processingTime" @blur="applyQueue" />

          <!-- Waypoint list -->
          <div class="mt-2 space-y-1">
            <div
              v-for="(wp, i) in localQueue.waypoints"
              :key="i"
              class="space-y-1 border border-rim/50 p-1.5"
            >
              <div class="flex items-center gap-1 text-[10px] font-mono text-ink-ghost">
                <span class="w-4 shrink-0 text-ink-ghost/50">{{ i + 1 }}</span>
                <span class="flex-1 truncate">{{ wp.x.toFixed(1) }}, {{ wp.y.toFixed(1) }}</span>
                <button
                  :disabled="i === 0"
                  @click="moveWaypointUp(i)"
                  class="px-1 text-ink-ghost/60 hover:text-ink transition-colors disabled:opacity-30"
                >↑</button>
                <button
                  :disabled="i === localQueue.waypoints.length - 1"
                  @click="moveWaypointDown(i)"
                  class="px-1 text-ink-ghost/60 hover:text-ink transition-colors disabled:opacity-30"
                >↓</button>
                <button
                  :disabled="localQueue.waypoints.length <= 1"
                  @click="removeWaypoint(i)"
                  class="px-1 text-danger/60 hover:text-danger transition-colors disabled:opacity-30"
                >✕</button>
              </div>
              <div class="grid grid-cols-2 gap-1">
                <PropInput label="X (ft)" v-model.number="wp.x" @blur="applyWaypoint(i, wp)" />
                <PropInput label="Y (ft)" v-model.number="wp.y" @blur="applyWaypoint(i, wp)" />
              </div>
            </div>
          </div>

          <div class="mt-2 flex gap-1">
            <button
              @click="addWaypoint"
              class="flex-1 py-1 text-[10px] font-mono border border-rim text-ink-ghost
                     hover:text-ink hover:bg-surface-hover transition-colors"
            >+ Add waypoint</button>
            <button
              @click="toggleWaypointEdit"
              class="flex-1 py-1 text-[10px] font-mono border transition-colors"
              :class="waypointEditActive
                ? 'border-tangerine/50 text-tangerine bg-tangerine/10'
                : 'border-rim text-ink-ghost hover:text-ink hover:bg-surface-hover'"
            >{{ waypointEditActive ? '● On canvas' : 'Edit on canvas' }}</button>
          </div>
        </FieldGroup>
        <FieldGroup v-else-if="localNode.type === 'Register'" label="Queue">
          <button
            @click="createQueueForRegister"
            class="w-full py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                   hover:text-ink hover:bg-surface-hover transition-colors"
          >Create queue controls</button>
        </FieldGroup>

        <FieldGroup v-if="localNode.type === 'Junction'" label="Behaviour">
          <PropInput label="Dwell time (s)" v-model.number="localNode.dwellTime" @blur="applyNode" />
        </FieldGroup>

        <!-- Crowd -->
        <FieldGroup label="Crowd">
          <div class="grid grid-cols-2 gap-2">
            <PropInput label="Jam density"    v-model.number="localNode.jamDensity"    @blur="applyNode" />
            <PropInput label="Personal space" v-model.number="localNode.personalSpace" @blur="applyNode" />
          </div>
        </FieldGroup>

      </div>

      <!-- Delete -->
      <div class="px-3 py-2 border-t border-rim shrink-0">
        <button
          @click="deleteNode"
          class="w-full py-1.5 text-[11px] font-mono border border-danger/50 text-danger
                 hover:bg-danger/10 transition-colors"
        >✕ Delete node</button>
      </div>
    </div>

    <!-- Edge properties -->
    <div
      v-else-if="layoutStore.selection.type === 'edge' && localEdge"
      class="flex flex-col flex-1 min-h-0"
    >
      <div class="flex-1 min-h-0 overflow-y-auto px-3 py-3 space-y-3">

        <!-- Header row -->
        <div class="flex items-center justify-between">
          <span class="text-[10px] font-mono text-ink-ghost/60">
            {{ localEdge.nodeA }} → {{ localEdge.nodeB }}
          </span>
          <span class="text-[10px] font-mono text-ink-ghost">id {{ localEdge.id }}</span>
        </div>

        <!-- Geometry -->
        <FieldGroup label="Geometry">
          <div class="space-y-1">
            <label class="block text-[10px] text-ink-ghost/70">Width (ft)</label>
            <div class="flex items-center gap-2">
              <input
                type="range"
                min="0.5"
                max="30"
                step="0.1"
                v-model.number="localEdge.width"
                @input="applyEdge"
                class="flex-1"
              />
              <input
                type="number"
                min="0.5"
                step="0.1"
                v-model.number="localEdge.width"
                @blur="applyEdge"
                class="w-20 bg-surface-deep border border-rim px-2 py-1 text-[11px] font-mono text-ink
                       focus:outline-none focus:border-tangerine transition-colors"
              />
            </div>
          </div>
          <div class="mt-2 flex items-center justify-between text-[11px] font-mono text-ink-ghost">
            <span>Length</span>
            <span>{{ localEdge.length.toFixed(1) }} ft</span>
          </div>
        </FieldGroup>

        <!-- Edge type toggle -->
        <FieldGroup label="Type">
          <div class="flex border border-rim overflow-hidden">
            <button
              @click="setEdgeType('aisle')"
              class="flex-1 py-1.5 text-[11px] font-mono transition-colors"
              :class="localEdge.edgeType === 'aisle'
                ? 'bg-mustard/10 text-mustard'
                : 'text-ink-ghost hover:text-ink hover:bg-surface-hover'"
            >Aisle</button>
            <button
              @click="setEdgeType('wall')"
              class="flex-1 py-1.5 text-[11px] font-mono border-l border-rim transition-colors"
              :class="localEdge.edgeType === 'wall'
                ? 'bg-mustard/10 text-mustard'
                : 'text-ink-ghost hover:text-ink hover:bg-surface-hover'"
            >Wall</button>
          </div>
        </FieldGroup>

        <!-- Shelf depths -->
        <FieldGroup label="Shelves">
          <div v-if="localEdge.shelfLeft > 0 || localEdge.shelfRight > 0" class="space-y-2">
            <div class="grid grid-cols-2 gap-2">
              <div class="space-y-1">
                <PropInput label="Left (ft)"  v-model.number="localEdge.shelfLeft"  @blur="applyEdge" />
                <button
                  v-if="localEdge.shelfLeft > 0"
                  @click="removeShelf('left')"
                  class="w-full py-0.5 text-[10px] font-mono border border-danger/40 text-danger/70
                         hover:bg-danger/10 hover:text-danger transition-colors"
                >✕ left</button>
              </div>
              <div class="space-y-1">
                <PropInput label="Right (ft)" v-model.number="localEdge.shelfRight" @blur="applyEdge" />
                <button
                  v-if="localEdge.shelfRight > 0"
                  @click="removeShelf('right')"
                  class="w-full py-0.5 text-[10px] font-mono border border-danger/40 text-danger/70
                         hover:bg-danger/10 hover:text-danger transition-colors"
                >✕ right</button>
              </div>
            </div>
            <button
              @click="removeShelf('both')"
              class="w-full py-1 text-[10px] font-mono border border-danger/50 text-danger
                     hover:bg-danger/10 transition-colors"
            >✕ Remove all shelves</button>
          </div>
          <p v-else class="text-[10px] font-mono text-ink-ghost/40">No shelves on this edge.</p>
        </FieldGroup>

        <!-- Flow -->
        <FieldGroup label="Direction">
          <div class="flex border border-rim overflow-hidden">
            <button
              v-for="f in directionModes"
              :key="f"
              @click="setDirectionMode(f)"
              class="flex-1 py-1.5 text-[11px] font-mono transition-colors"
              :class="[
                f !== 'bi' ? 'border-l border-rim' : '',
                edgeDirectionMode === f
                  ? 'bg-mustard/10 text-mustard'
                  : 'text-ink-ghost hover:text-ink hover:bg-surface-hover',
              ]"
            >{{ directionLabel[f] }}</button>
          </div>
          <p class="text-[10px] font-mono text-ink-ghost/60">
            Endpoints: {{ localEdge.nodeA }} → {{ localEdge.nodeB }}
          </p>
          <p class="text-[10px] font-mono text-ink-ghost/50">
            {{ directionSummary }}
          </p>
        </FieldGroup>
        <button
          @click="swapEndpoints"
          class="w-full py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                 hover:text-ink hover:bg-surface-hover transition-colors"
        >⇄ Swap endpoints</button>
        <p class="text-[10px] font-mono text-ink-ghost/50">
          Queue waypoint repositioning is available when selecting a Register node.
        </p>

        <!-- Add shelf (click to expand L/R/Both picker) -->
        <div class="pt-1">
          <template v-if="!shelfPickerOpen">
            <button
              @click="shelfPickerOpen = true"
              class="w-full py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                     hover:text-ink hover:bg-surface-hover transition-colors"
            >▬ Add shelf to edge</button>
          </template>
          <template v-else>
            <div class="flex border border-rim overflow-hidden">
              <button @click="pickShelf('left')"
                class="flex-1 py-1.5 text-[11px] font-mono text-ink-ghost
                       hover:text-ink hover:bg-surface-hover transition-colors"
              >Left</button>
              <button @click="pickShelf('right')"
                class="flex-1 py-1.5 text-[11px] font-mono border-l border-rim text-ink-ghost
                       hover:text-ink hover:bg-surface-hover transition-colors"
              >Right</button>
              <button @click="pickShelf('both')"
                class="flex-1 py-1.5 text-[11px] font-mono border-l border-rim text-ink-ghost
                       hover:text-ink hover:bg-surface-hover transition-colors"
              >Both</button>
            </div>
          </template>
        </div>

        <!-- Planogram -->
        <div v-if="localEdge.shelfLeft > 0 || localEdge.shelfRight > 0" class="pt-1">
          <button
            @click="openPlanogram"
            class="w-full py-1.5 text-[11px] font-mono border border-deep-teal/50 text-deep-teal
                   hover:bg-deep-teal/10 transition-colors"
          >▦ Open Planogram</button>
        </div>

      </div>

      <!-- Delete -->
      <div class="px-3 py-2 border-t border-rim shrink-0">
        <button
          @click="deleteEdge"
          class="w-full py-1.5 text-[11px] font-mono border border-danger/50 text-danger
                 hover:bg-danger/10 transition-colors"
        >✕ Delete edge</button>
      </div>
    </div>

    <!-- FreeObject / Shelf properties -->
    <div v-else-if="layoutStore.selection?.type === 'freeObject' && localFO" class="flex flex-col flex-1 min-h-0">
      <div class="flex-1 min-h-0 overflow-y-auto px-3 py-3 space-y-3">

        <div class="flex items-center justify-between">
          <span class="text-[10px] font-mono px-2 py-0.5 border border-mustard/40 text-mustard bg-mustard/10">
            {{ localFO.type }}
          </span>
          <span class="text-[10px] font-mono text-ink-ghost">id {{ localFO.id }}</span>
        </div>

        <FieldGroup label="Position">
          <div class="grid grid-cols-2 gap-2">
            <PropInput label="X (ft)" v-model.number="localFO.x" @blur="applyFO" />
            <PropInput label="Y (ft)" v-model.number="localFO.y" @blur="applyFO" />
          </div>
        </FieldGroup>

        <FieldGroup label="Size">
          <div class="grid grid-cols-2 gap-2">
            <PropInput label="Width (ft)"  v-model.number="localFO.widthFt"  @blur="applyFO" />
            <PropInput label="Depth (ft)"  v-model.number="localFO.lengthFt" @blur="applyFO" />
          </div>
        </FieldGroup>

        <FieldGroup label="Rotation">
          <PropInput label="Angle (°)" v-model.number="localFO.rotation" @blur="applyFO" />
        </FieldGroup>

      </div>

      <div class="px-3 py-2 border-t border-rim shrink-0">
        <button
          @click="deleteFO"
          class="w-full py-1.5 text-[11px] font-mono border border-danger/50 text-danger
                 hover:bg-danger/10 transition-colors"
        >✕ Delete shelf</button>
      </div>
    </div>

  </div>
</template>

<script setup>
import { ref, watch, computed } from "vue";
import { useLayoutStore } from "../../stores/layout";
import FieldGroup from "./PropertiesFieldGroup.vue";
import PropInput from "./PropertiesPropInput.vue";

const layoutStore = useLayoutStore();

// ---------------------------------------------------------------------------
// Multi-selection summary + bulk edit
// ---------------------------------------------------------------------------
const multiSelSummary = computed(() => {
  const ms = layoutStore.multiSelection;
  const nodeCount = ms.filter((s) => s.type === "node").length;
  const edgeCount = ms.filter((s) => s.type === "edge").length;
  const foCount   = ms.filter((s) => s.type === "freeObject").length;
  const parts = [];
  if (nodeCount) parts.push(`${nodeCount} node${nodeCount > 1 ? "s" : ""}`);
  if (edgeCount) parts.push(`${edgeCount} edge${edgeCount > 1 ? "s" : ""}`);
  if (foCount)   parts.push(`${foCount} free object${foCount > 1 ? "s" : ""}`);
  return parts.join(", ");
});

const multiEdgeCount = computed(() =>
  layoutStore.multiSelection.filter((s) => s.type === "edge").length
);

const bulkEdgeWidth = ref(10);

function applyBulkEdgeWidth() {
  const w = Number(bulkEdgeWidth.value);
  if (!Number.isFinite(w) || w <= 0) return;
  layoutStore.bulkUpdateEdges({ width: w });
}

// ---------------------------------------------------------------------------
// Local form mirrors
// ---------------------------------------------------------------------------
const localNode  = ref(null);
const localEdge  = ref(null);
const localFO    = ref(null);
const localQueue = ref(null);
const shelfPickerOpen = ref(false);

watch(
  () => layoutStore.selection,
  () => { syncForm(); shelfPickerOpen.value = false; },
  { immediate: true }
);

// Sync when underlying data changes (drag moves, etc.)
watch(() => layoutStore.nodes,          () => { if (layoutStore.selection?.type === "node")       syncForm(); }, { deep: true });
watch(() => layoutStore.edges,          () => { if (layoutStore.selection?.type === "edge")        syncForm(); }, { deep: true });
watch(() => layoutStore.freeObjects,    () => { if (layoutStore.selection?.type === "freeObject") syncForm(); }, { deep: true });
watch(() => layoutStore.checkoutQueues, () => { if (layoutStore.selection?.type === "node")       syncForm(); }, { deep: true });

function syncForm() {
  const sel = layoutStore.selection;
  localNode.value  = null;
  localEdge.value  = null;
  localFO.value    = null;
  localQueue.value = null;
  if (!sel) return;
  if (sel.type === "node") {
    const n = layoutStore.nodeById(sel.id);
    localNode.value = n ? { ...n } : null;
    if (n?.type === "Register") {
      layoutStore.ensureQueueForRegister(n.id);
      const q = layoutStore.checkoutQueues.find(
        (q) => q.registerId === n.id || String(q.registerId) === String(n.id)
      );
      localQueue.value = q ? JSON.parse(JSON.stringify(q)) : null;
    }
  } else if (sel.type === "edge") {
    const e = layoutStore.edgeById(sel.id);
    localEdge.value = e ? { ...e } : null;
  } else if (sel.type === "freeObject") {
    const fo = layoutStore.freeObjects.find(
      (f) => f.id === sel.id || String(f.id) === String(sel.id)
    );
    localFO.value = fo ? { ...fo } : null;
  }
}

const hasRenderableSelection = computed(() => {
  const sel = layoutStore.selection;
  if (!sel) return false;
  if (sel.type === "node") return localNode.value != null;
  if (sel.type === "edge") return localEdge.value != null;
  if (sel.type === "freeObject") return localFO.value != null;
  return false;
});

// ---------------------------------------------------------------------------
// Node color by type
// ---------------------------------------------------------------------------
const NODE_COLORS = {
  Entrance:  "#008A31",
  Exit:      "#A33025",
  Junction:  "#9A9790",
  Register:  "#C9980A",
  Stockroom: "#8B5A3C",
};
const nodeColor = computed(
  () => NODE_COLORS[localNode.value?.type] ?? NODE_COLORS.Junction
);

// ---------------------------------------------------------------------------
// Apply changes
// ---------------------------------------------------------------------------
function applyNode() {
  if (!localNode.value) return;
  layoutStore.updateNode(localNode.value.id, { ...localNode.value });
}

function applyEdge() {
  if (!localEdge.value) return;
  layoutStore.updateEdge(localEdge.value.id, { ...localEdge.value });
}

function setEdgeType(t) {
  if (!localEdge.value) return;
  localEdge.value.edgeType = t;
  applyEdge();
}

function setDirectionMode(mode) {
  if (!localEdge.value) return;
  layoutStore.setEdgeDirectionMode(localEdge.value.id, mode);
  const e = layoutStore.edgeById(localEdge.value.id);
  if (e) localEdge.value = { ...e };
}

function swapEndpoints() {
  if (!localEdge.value) return;
  layoutStore.swapEdgeEndpoints(localEdge.value.id);
  const e = layoutStore.edgeById(localEdge.value.id);
  if (e) localEdge.value = { ...e };
}

// ---------------------------------------------------------------------------
// Edge shelf
// ---------------------------------------------------------------------------
function addShelfToEdge(side = "both") {
  if (!localEdge.value) return;
  layoutStore.addShelfToEdge(localEdge.value.id, side);
  const e = layoutStore.edgeById(localEdge.value.id);
  if (e) localEdge.value = { ...e };
}

function pickShelf(side) {
  addShelfToEdge(side);
  shelfPickerOpen.value = false;
}

function removeShelf(side) {
  if (!localEdge.value) return;
  layoutStore.removeShelfFromEdge(localEdge.value.id, side);
  const e = layoutStore.edgeById(localEdge.value.id);
  if (e) localEdge.value = { ...e };
}

// ---------------------------------------------------------------------------
// FreeObject / shelf
// ---------------------------------------------------------------------------
function applyFO() {
  if (!localFO.value) return;
  layoutStore.updateFreeObject(localFO.value.id, { ...localFO.value });
}

function deleteFO() {
  if (!localFO.value) return;
  layoutStore.removeFreeObject(localFO.value.id);
}

// ---------------------------------------------------------------------------
// Checkout queue
// ---------------------------------------------------------------------------
function applyQueue() {
  if (!localQueue.value) return;
  layoutStore.updateCheckoutQueue(localQueue.value.registerId, {
    processingTime: localQueue.value.processingTime,
  });
}

function addWaypoint() {
  if (!localNode.value) return;
  layoutStore.addQueueWaypoint(localNode.value.id);
  syncForm();
}

function removeWaypoint(index) {
  if (!localNode.value) return;
  layoutStore.removeQueueWaypoint(localNode.value.id, index);
  syncForm();
}
function applyWaypoint(index, wp) {
  if (!localNode.value) return;
  layoutStore.updateQueueWaypoint(localNode.value.id, index, { x: wp.x, y: wp.y });
  syncForm();
}
function moveWaypointUp(index) {
  if (!localNode.value) return;
  layoutStore.moveQueueWaypointUp(localNode.value.id, index);
  syncForm();
}
function moveWaypointDown(index) {
  if (!localNode.value) return;
  layoutStore.moveQueueWaypointDown(localNode.value.id, index);
  syncForm();
}
function createQueueForRegister() {
  if (!localNode.value) return;
  layoutStore.ensureQueueForRegister(localNode.value.id);
  syncForm();
}

const waypointEditActive = computed(
  () => layoutStore.waypointEditRegisterId === localNode.value?.id
);

function toggleWaypointEdit() {
  if (!localNode.value) return;
  layoutStore.waypointEditRegisterId =
    waypointEditActive.value ? null : localNode.value.id;
}

// ---------------------------------------------------------------------------
// Planogram
// ---------------------------------------------------------------------------
function openPlanogram() {
  if (!localEdge.value) return;
  layoutStore.planogramOpen = true;
}

const directionModes = ["bi", "oneWayForward", "oneWayReverse"];
const directionLabel = {
  bi: "Bidirectional",
  oneWayForward: "One-way A→B",
  oneWayReverse: "One-way B→A",
};
const edgeDirectionMode = computed(() => {
  if (!localEdge.value) return "bi";
  if (localEdge.value.flow === "bi") return "bi";
  return localEdge.value.orientation === "bwd" ? "oneWayReverse" : "oneWayForward";
});
const directionSummary = computed(() => {
  if (!localEdge.value) return "";
  if (edgeDirectionMode.value === "bi") return "Agents can traverse this edge in both directions.";
  if (edgeDirectionMode.value === "oneWayForward")
    return "Agents can traverse only from nodeA to nodeB.";
  return "Agents can traverse only from nodeB to nodeA.";
});

// ---------------------------------------------------------------------------
// Delete
// ---------------------------------------------------------------------------
function deleteNode() {
  if (!localNode.value) return;
  layoutStore.removeNode(localNode.value.id);
}
function deleteEdge() {
  if (!localEdge.value) return;
  layoutStore.removeEdge(localEdge.value.id);
}
</script>
