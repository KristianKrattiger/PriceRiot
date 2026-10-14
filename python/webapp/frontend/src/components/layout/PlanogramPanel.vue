<template>
  <div class="flex flex-col h-full bg-surface border-l border-rim shadow-xl overflow-hidden">

    <!-- Header -->
    <div class="px-4 py-3 border-b border-rim shrink-0 flex items-center justify-between">
      <div>
        <p class="text-[10px] uppercase tracking-label text-ink-ghost">Planogram</p>
        <p class="text-[12px] font-mono text-ink mt-0.5 truncate max-w-[160px]">
          {{ edge?.label || `Edge ${edgeId}` }}
        </p>
      </div>
      <button
        @click="$emit('close')"
        class="text-[11px] font-mono text-ink-ghost hover:text-ink px-2 py-1 border border-rim
               hover:bg-surface-hover transition-colors"
      >✕</button>
    </div>

    <!-- Side tabs -->
    <div class="flex shrink-0 border-b border-rim">
      <button
        v-for="s in availableSides"
        :key="s"
        @click="activeSide = s"
        class="flex-1 py-2 text-[11px] font-mono transition-colors"
        :class="activeSide === s
          ? 'bg-deep-teal/10 text-deep-teal border-b-2 border-deep-teal'
          : 'text-ink-ghost hover:text-ink hover:bg-surface-hover'"
      >{{ s === 'left' ? 'Left Side' : 'Right Side' }}</button>
    </div>

    <!-- No shelves -->
    <div v-if="!availableSides.length" class="flex-1 flex items-center justify-center p-6">
      <p class="text-[11px] font-mono text-ink-ghost/50 text-center">
        This edge has no shelves.<br>
        <span class="text-[10px]">Add shelves via the properties panel.</span>
      </p>
    </div>

    <template v-else>
      <!-- Controls bar -->
      <div class="px-3 py-2 border-b border-rim shrink-0 flex items-center gap-3">
        <div class="flex items-center gap-1">
          <span class="text-[10px] font-mono text-ink-ghost mr-0.5">Bays</span>
          <button @click="adjustBays(-1)" v-bind="ctrlBtn">−</button>
          <span class="w-6 text-center text-[11px] font-mono text-ink">{{ bayCount }}</span>
          <button @click="adjustBays(1)"  v-bind="ctrlBtn">+</button>
        </div>
        <div class="flex items-center gap-1">
          <span class="text-[10px] font-mono text-ink-ghost mr-0.5">Faces</span>
          <button @click="adjustFaces(-1)" v-bind="ctrlBtn">−</button>
          <span class="w-6 text-center text-[11px] font-mono text-ink">{{ faceCount }}</span>
          <button @click="adjustFaces(1)"  v-bind="ctrlBtn">+</button>
        </div>
        <div class="flex-1" />
        <!-- Selection actions (shown when cells are selected) -->
        <template v-if="selectedCells.size > 0">
          <button
            @click="startFillSelected"
            class="px-2 py-1 text-[10px] font-mono border border-tangerine text-tangerine
                   hover:bg-tangerine/10 transition-colors whitespace-nowrap"
          >Fill {{ selectedCells.size }} selected…</button>
          <button
            @click="clearSelection"
            class="text-[9px] font-mono text-ink-ghost/50 hover:text-ink-ghost transition-colors"
          >✕ clear</button>
        </template>
        <span class="text-[9px] font-mono text-ink-ghost/50">
          {{ filledCount }}/{{ totalCells }} filled
        </span>
        <button
          @click="startFillAll"
          class="px-2 py-1 text-[10px] font-mono border border-rim text-ink-ghost
                 hover:text-ink hover:bg-surface-hover transition-colors whitespace-nowrap"
        >Fill empty…</button>
      </div>

      <!-- No products warning -->
      <div
        v-if="!layoutStore.products.length"
        class="mx-3 mt-2 px-3 py-2 bg-mustard/10 border border-mustard/30 shrink-0"
      >
        <p class="text-[10px] font-mono text-mustard mb-1.5">No product catalog loaded.</p>
        <div class="flex gap-2 items-center">
          <input type="file" accept=".csv" ref="productFileInput" class="hidden" @change="onProductUpload" />
          <button
            @click="$refs.productFileInput.click()"
            :disabled="loadingProducts"
            class="px-2 py-1 text-[10px] font-mono border border-mustard/50 text-mustard
                   hover:bg-mustard/20 transition-colors"
          >{{ loadingProducts ? 'Loading…' : '↑ Upload CSV' }}</button>
          <span v-if="layoutStore.productsFile" class="text-[10px] font-mono text-ink-ghost truncate">
            or <button @click="reloadProducts" class="underline hover:text-ink">
              reload
            </button>
          </span>
        </div>
      </div>

      <!-- ── Grid ────────────────────────────────────────────────────────── -->
      <div class="flex-1 overflow-auto p-2">
        <!--
          Layout:
            col 0          → face label (fixed narrow)
            col 1..bayCount → bay cells (equal-width)
        -->
        <div
          class="grid gap-px"
          :style="gridStyle"
        >
          <!-- Top-left corner -->
          <div class="text-[8px] font-mono text-ink-ghost/30 flex items-end justify-center pb-1">F\B</div>

          <!-- Bay column headers -->
          <div
            v-for="bay in bayCount"
            :key="'bh-' + bay"
            class="text-center text-[9px] font-mono text-ink-ghost/70 pb-1 border-b border-rim/40 relative group/bh"
          >
            Bay {{ bay }}
            <button
              v-if="bayCount > 1"
              @click.stop="copyBayToAll(bay - 1)"
              class="absolute right-0 top-0 opacity-0 group-hover/bh:opacity-100 transition-opacity
                     text-[8px] font-mono text-ink-ghost hover:text-ink px-0.5"
              title="Copy this bay to all other bays"
            >⬌</button>
          </div>

          <!-- Face rows -->
          <template v-for="face in faceCount" :key="'row-' + face">
            <!-- Face label -->
            <div
              class="flex items-center justify-center text-[9px] font-mono text-ink-ghost/60
                     border-r border-rim/40 pr-1 relative group/fl"
            >
              F{{ face }}
              <button
                v-if="faceCount > 1"
                @click.stop="copyFaceToAll(face - 1)"
                class="absolute -right-0.5 opacity-0 group-hover/fl:opacity-100 transition-opacity
                       text-[8px] font-mono text-ink-ghost hover:text-ink"
                title="Copy this face to all other faces"
              >⬇</button>
            </div>

            <!-- Cells for this face, across all bays -->
            <div
              v-for="bay in bayCount"
              :key="`cell-${bay}-${face}`"
              @click="openPicker(bay - 1, face - 1, 0, $event)"
              class="relative cursor-pointer transition-all min-h-[60px] flex flex-col border"
              :class="cellClass(bay - 1, face - 1)"
            >
              <template v-if="getCellProduct(bay - 1, face - 1)">
                <!-- Filled cell -->
                <div class="flex flex-col h-full p-1 gap-0.5">
                  <p class="text-[8px] font-mono text-ink-ghost/50 leading-none">
                    #{{ getCell(bay - 1, face - 1, 0)?.skuId }}
                  </p>
                  <p class="text-[10px] font-mono text-ink leading-tight flex-1 line-clamp-2">
                    {{ getCellProduct(bay - 1, face - 1)?.name || '—' }}
                  </p>
                  <div class="flex items-center justify-between">
                    <span class="text-[8px] font-mono text-ink-ghost/60">
                      qty {{ getCell(bay - 1, face - 1, 0)?.onShelfQty }}
                    </span>
                    <span class="text-[9px] font-mono text-deep-teal/80">
                      ${{ (getCellProduct(bay - 1, face - 1)?.price ?? 0).toFixed(2) }}
                    </span>
                  </div>
                  <!-- Clear button -->
                  <button
                    @click.stop="clearCell(bay - 1, face - 1, 0)"
                    class="absolute top-0.5 right-0.5 text-[8px] font-mono text-danger/40
                           hover:text-danger leading-none opacity-0 group-hover:opacity-100
                           transition-opacity"
                    title="Clear"
                  >✕</button>
                </div>
              </template>
              <template v-else-if="getCell(bay - 1, face - 1, 0)">
                <!-- SKU assigned but not in catalog -->
                <div class="flex flex-col h-full p-1 gap-0.5">
                  <p class="text-[8px] font-mono text-ink-ghost/50 leading-none">
                    #{{ getCell(bay - 1, face - 1, 0)?.skuId }}
                  </p>
                  <p class="text-[9px] font-mono text-ink-ghost/40 leading-tight flex-1">
                    (not in catalog)
                  </p>
                  <button
                    @click.stop="clearCell(bay - 1, face - 1, 0)"
                    class="absolute top-0.5 right-0.5 text-[8px] font-mono text-danger/40 hover:text-danger"
                  >✕</button>
                </div>
              </template>
              <template v-else>
                <!-- Empty cell -->
                <div class="flex-1 flex items-center justify-center">
                  <span class="text-[14px] font-mono text-ink-ghost/20 group-hover:text-ink-ghost/50">+</span>
                </div>
              </template>
            </div>
          </template>
        </div>

        <!-- Summary row -->
        <div class="mt-3 flex items-center gap-3 text-[9px] font-mono text-ink-ghost/40">
          <span>{{ filledCount }} SKU{{ filledCount !== 1 ? 's' : '' }} assigned</span>
          <span>·</span>
          <span>{{ uniqueSkuCount }} unique product{{ uniqueSkuCount !== 1 ? 's' : '' }}</span>
        </div>
      </div>

      <!-- ── SKU Picker drawer ───────────────────────────────────────────── -->
      <div
        v-if="activePicker !== null"
        class="absolute inset-x-0 bottom-0 bg-surface border-t-2 border-deep-teal/50 shadow-2xl z-50 flex flex-col"
        style="max-height: 58%"
      >
        <!-- Picker header -->
        <div class="px-3 py-2 border-b border-rim shrink-0 flex items-center gap-2 bg-deep-teal/5">
          <span class="text-[10px] font-mono text-deep-teal">
            {{ activePicker.fillAll
              ? 'Fill all empty cells'
              : activePicker.fillSelection
                ? `Fill ${selectedCells.size} selected cells`
                : `Bay ${activePicker.bay + 1} · Face ${activePicker.face + 1}` }}
          </span>
          <div class="flex items-center gap-1.5 ml-auto">
            <span class="text-[10px] font-mono text-ink-ghost">qty</span>
            <input
              v-model.number="pickerQty"
              type="number" min="1" step="1"
              class="w-16 bg-surface-deep border border-rim px-2 py-0.5 text-[11px] font-mono
                     text-ink focus:outline-none focus:border-tangerine"
            />
            <button @click="activePicker = null" class="text-ink-ghost hover:text-ink text-[11px] font-mono ml-1">✕</button>
          </div>
        </div>

        <!-- Search -->
        <div class="px-3 py-2 border-b border-rim shrink-0">
          <input
            v-model="pickerSearch"
            ref="pickerSearchInput"
            placeholder="Search SKU, name or category…"
            class="w-full bg-surface-deep border border-rim px-2 py-1.5 text-[11px] font-mono
                   text-ink focus:outline-none focus:border-tangerine"
          />
        </div>

        <!-- Product list -->
        <div class="flex-1 overflow-y-auto">
          <div v-if="!layoutStore.products.length" class="p-4 text-[11px] font-mono text-ink-ghost/50 text-center">
            No products loaded
          </div>
          <button
            v-for="p in filteredProducts"
            :key="p.skuId"
            @click="assignSku(p.skuId)"
            class="w-full text-left px-3 py-2 hover:bg-surface-hover transition-colors
                   border-b border-rim/30 last:border-b-0 flex items-center gap-2"
          >
            <span class="text-[9px] font-mono text-ink-ghost/50 w-14 shrink-0 text-right tabular-nums">
              #{{ p.skuId }}
            </span>
            <span class="text-[11px] font-mono text-ink flex-1 truncate">{{ p.name || '—' }}</span>
            <span v-if="p.category" class="text-[9px] font-mono text-ink-ghost/60 shrink-0 max-w-[56px] truncate">
              {{ p.category }}
            </span>
            <span class="text-[10px] font-mono text-deep-teal shrink-0 tabular-nums">
              ${{ (p.price ?? 0).toFixed(2) }}
            </span>
          </button>
          <div v-if="layoutStore.products.length && !filteredProducts.length"
            class="p-4 text-[11px] font-mono text-ink-ghost/50 text-center"
          >No matches</div>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup>
import { ref, computed, watch, nextTick } from "vue";
import { useLayoutStore } from "../../stores/layout";
import { uploadFile } from "../../api/client";

const props = defineProps({ edgeId: { type: Number, required: true } });
defineEmits(["close"]);

const layoutStore = useLayoutStore();

// ---------------------------------------------------------------------------
// Shared button attrs
// ---------------------------------------------------------------------------
const ctrlBtn = {
  class: "w-5 h-5 flex items-center justify-center border border-rim text-[11px] font-mono text-ink-ghost hover:text-ink hover:bg-surface-hover transition-colors",
};

// ---------------------------------------------------------------------------
// Edge / side / bay / face state
// ---------------------------------------------------------------------------
const edge = computed(() => layoutStore.edgeById(props.edgeId));

const availableSides = computed(() => {
  if (!edge.value) return [];
  const sides = [];
  if (edge.value.shelfLeft  > 0) sides.push("left");
  if (edge.value.shelfRight > 0) sides.push("right");
  return sides;
});

const activeSide = ref("left");

watch(availableSides, (sides) => {
  if (sides.length && !sides.includes(activeSide.value)) {
    activeSide.value = sides[0];
  }
}, { immediate: true });

const planogramEntry = computed(() => {
  const eid = String(props.edgeId);
  return layoutStore.planogram[eid] ?? { bayCountLeft: 1, bayCountRight: 1, cells: [] };
});

const bayCount = computed(() =>
  activeSide.value === "left"
    ? planogramEntry.value.bayCountLeft
    : planogramEntry.value.bayCountRight
);

const faceCount = computed(() => {
  const key = activeSide.value === "left" ? "faceCountLeft" : "faceCountRight";
  return planogramEntry.value[key] ?? 1;
});

// CSS grid template: narrow face-label col + equal bay cols
const gridStyle = computed(() => ({
  gridTemplateColumns: `28px repeat(${bayCount.value}, minmax(0, 1fr))`,
}));

// ---------------------------------------------------------------------------
// Bay / face adjustments
// ---------------------------------------------------------------------------
function adjustBays(delta) {
  const newCount = Math.max(1, bayCount.value + delta);
  layoutStore.setPlanogramBayCount(props.edgeId, activeSide.value, newCount);
}

function adjustFaces(delta) {
  const newCount = Math.max(1, faceCount.value + delta);
  layoutStore.setPlanogramFaceCount(props.edgeId, activeSide.value, newCount);
}

// ---------------------------------------------------------------------------
// Cell accessors
// ---------------------------------------------------------------------------
function getCell(bay, face, slot) {
  const cells = planogramEntry.value.cells ?? [];
  return cells.find(
    (c) => c.side === activeSide.value && c.bay === bay && c.face === face && c.slot === slot
  ) ?? null;
}

/** Lookup product for a given bay/face — normalises SKU type (int vs string). */
function getCellProduct(bay, face) {
  const cell = getCell(bay, face, 0);
  if (!cell?.skuId) return null;
  const id = cell.skuId;
  return layoutStore.products.find(
    (p) => p.skuId === id || String(p.skuId) === String(id)
  ) ?? null;
}

function cellClass(bay, face) {
  const isActive = activePicker.value?.bay === bay &&
                   activePicker.value?.face === face &&
                   !activePicker.value?.fillAll;
  const isSelected = selectedCells.value.has(cellKey(bay, face));
  const cell    = getCell(bay, face, 0);
  const product = getCellProduct(bay, face);
  if (isSelected)        return "border-tangerine bg-tangerine/15 ring-1 ring-tangerine/50";
  if (isActive)          return "border-tangerine bg-tangerine/10 ring-1 ring-tangerine/40";
  if (cell && product)   return "border-deep-teal/40 bg-deep-teal/5 hover:bg-deep-teal/10 group";
  if (cell && !product)  return "border-rim bg-surface-deep hover:bg-surface-hover group";
  return "border-rim/50 bg-surface-deep hover:border-rim hover:bg-surface-hover";
}

function clearCell(bay, face, slot) {
  layoutStore.clearPlanogramCell(props.edgeId, activeSide.value, bay, face, slot);
}

// ---------------------------------------------------------------------------
// Copy operations
// ---------------------------------------------------------------------------
function copyBayToAll(srcBay) {
  const fCount = faceCount.value;
  const bCount = bayCount.value;
  const ops = [];
  for (let targetBay = 0; targetBay < bCount; targetBay++) {
    if (targetBay === srcBay) continue;
    for (let f = 0; f < fCount; f++) {
      const src = getCell(srcBay, f, 0);
      if (src) {
        ops.push({
          action: "set",
          edgeId: props.edgeId,
          side: activeSide.value,
          bay: targetBay,
          face: f,
          slot: 0,
          skuId: src.skuId,
          qty: src.onShelfQty,
        });
      } else {
        ops.push({
          action: "clear",
          edgeId: props.edgeId,
          side: activeSide.value,
          bay: targetBay,
          face: f,
          slot: 0,
        });
      }
    }
  }
  layoutStore.setPlanogramCellsBulk(ops);
}

function copyFaceToAll(srcFace) {
  const fCount = faceCount.value;
  const bCount = bayCount.value;
  const ops = [];
  for (let targetFace = 0; targetFace < fCount; targetFace++) {
    if (targetFace === srcFace) continue;
    for (let b = 0; b < bCount; b++) {
      const src = getCell(b, srcFace, 0);
      if (src) {
        ops.push({
          action: "set",
          edgeId: props.edgeId,
          side: activeSide.value,
          bay: b,
          face: targetFace,
          slot: 0,
          skuId: src.skuId,
          qty: src.onShelfQty,
        });
      } else {
        ops.push({
          action: "clear",
          edgeId: props.edgeId,
          side: activeSide.value,
          bay: b,
          face: targetFace,
          slot: 0,
        });
      }
    }
  }
  layoutStore.setPlanogramCellsBulk(ops);
}

// ---------------------------------------------------------------------------
// Multi-select (Ctrl+click)
// ---------------------------------------------------------------------------
const selectedCells = ref(new Set());

function cellKey(bay, face) { return `${bay}-${face}`; }

function toggleSelect(bay, face, event) {
  if (!event.ctrlKey && !event.metaKey) return false;
  event.preventDefault();
  const key = cellKey(bay, face);
  const next = new Set(selectedCells.value);
  if (next.has(key)) next.delete(key);
  else next.add(key);
  selectedCells.value = next;
  return true;
}

function clearSelection() { selectedCells.value = new Set(); }

function startFillSelected() {
  if (!selectedCells.value.size) return;
  pickerSearch.value = "";
  pickerQty.value    = 20;
  activePicker.value = { bay: -1, face: -1, slot: 0, fillSelection: true };
  nextTick(() => pickerSearchInput.value?.focus());
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
const totalCells = computed(() => bayCount.value * faceCount.value);

const filledCount = computed(() => {
  const cells = planogramEntry.value.cells ?? [];
  return cells.filter((c) => c.side === activeSide.value).length;
});

const uniqueSkuCount = computed(() => {
  const cells = planogramEntry.value.cells ?? [];
  return new Set(
    cells.filter((c) => c.side === activeSide.value).map((c) => String(c.skuId))
  ).size;
});

// ---------------------------------------------------------------------------
// SKU picker
// ---------------------------------------------------------------------------
const activePicker      = ref(null);
const pickerSearch      = ref("");
const pickerQty         = ref(20);
const pickerSearchInput = ref(null);

function openPicker(bay, face, slot, event) {
  // Ctrl/Meta+click → multi-select toggle, don't open picker
  if (event && (event.ctrlKey || event.metaKey)) {
    toggleSelect(bay, face, event);
    return;
  }
  // Toggle off if same cell clicked again
  if (activePicker.value?.bay === bay && activePicker.value?.face === face && !activePicker.value?.fillAll) {
    activePicker.value = null;
    return;
  }
  const existing     = getCell(bay, face, slot);
  pickerSearch.value = "";
  pickerQty.value    = existing?.onShelfQty ?? 20;
  activePicker.value = { bay, face, slot };
  nextTick(() => pickerSearchInput.value?.focus());
}

const filteredProducts = computed(() => {
  const q = pickerSearch.value.trim().toLowerCase();
  if (!q) return layoutStore.products.slice(0, 100);
  return layoutStore.products.filter(
    (p) =>
      String(p.skuId).includes(q) ||
      (p.name     ?? "").toLowerCase().includes(q) ||
      (p.category ?? "").toLowerCase().includes(q)
  ).slice(0, 100);
});

function assignSku(skuId) {
  if (!activePicker.value) return;
  const ops = [];
  if (activePicker.value.fillAll) {
    for (let b = 0; b < bayCount.value; b++) {
      for (let f = 0; f < faceCount.value; f++) {
        if (!getCell(b, f, 0)) {
          ops.push({
            action: "set",
            edgeId: props.edgeId,
            side: activeSide.value,
            bay: b,
            face: f,
            slot: 0,
            skuId,
            qty: pickerQty.value,
          });
        }
      }
    }
  } else if (activePicker.value.fillSelection) {
    for (const key of selectedCells.value) {
      const [b, f] = key.split("-").map(Number);
      ops.push({
        action: "set",
        edgeId: props.edgeId,
        side: activeSide.value,
        bay: b,
        face: f,
        slot: 0,
        skuId,
        qty: pickerQty.value,
      });
    }
    clearSelection();
  } else {
    const { bay, face, slot } = activePicker.value;
    ops.push({
      action: "set",
      edgeId: props.edgeId,
      side: activeSide.value,
      bay,
      face,
      slot,
      skuId,
      qty: pickerQty.value,
    });
  }
  layoutStore.setPlanogramCellsBulk(ops);
  activePicker.value = null;
}

function startFillAll() {
  pickerSearch.value = "";
  pickerQty.value    = 20;
  activePicker.value = { bay: -1, face: -1, slot: 0, fillAll: true };
  nextTick(() => pickerSearchInput.value?.focus());
}

// ---------------------------------------------------------------------------
// Products loading
// ---------------------------------------------------------------------------
const loadingProducts = ref(false);

async function onProductUpload(e) {
  const file = e.target.files?.[0];
  if (!file) return;
  loadingProducts.value = true;
  try {
    const result = await uploadFile(file);
    layoutStore.attachProducts(result.filename);
  } finally {
    loadingProducts.value = false;
    e.target.value = "";
  }
}

async function reloadProducts() {
  if (!layoutStore.productsFile) return;
  loadingProducts.value = true;
  try {
    await layoutStore.fetchProducts(layoutStore.productsFile);
  } finally {
    loadingProducts.value = false;
  }
}

watch(
  () => layoutStore.productsFile,
  (f) => { if (f && !layoutStore.products.length) layoutStore.fetchProducts(f); },
  { immediate: true }
);
</script>
