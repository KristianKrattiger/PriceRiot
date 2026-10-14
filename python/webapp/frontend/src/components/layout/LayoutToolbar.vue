<template>
  <div class="flex items-center gap-2 px-4 py-2 bg-surface border-b border-rim shrink-0">

    <!-- Load / New -->
    <div class="relative" ref="loadMenuRef">
      <button
        @click="loadMenuOpen = !loadMenuOpen"
        class="flex items-center gap-1.5 px-3 py-1.5 text-[11px] font-mono border border-rim
               text-ink-ghost hover:text-ink hover:bg-surface-hover transition-colors"
      >
        <span>Load</span>
        <span class="text-[9px]">▾</span>
      </button>

      <!-- Dropdown -->
      <div
        v-if="loadMenuOpen"
        class="absolute top-full left-0 mt-1 w-64 bg-surface border border-rim shadow-lg z-50"
      >
        <div class="px-3 py-2 text-[10px] uppercase tracking-label text-ink-ghost border-b border-rim">
          Available Layouts
        </div>

        <div v-if="layoutStore.layoutsLoading" class="px-3 py-2 text-[11px] text-ink-ghost font-mono">
          Loading…
        </div>
        <div v-else-if="!layoutStore.availableLayouts.length" class="px-3 py-2 text-[11px] text-ink-ghost font-mono">
          No layouts found
        </div>
        <button
          v-for="layout in layoutStore.availableLayouts"
          :key="layout.filename"
          @click="loadLayout(layout.filename)"
          class="w-full text-left px-3 py-2 text-[11px] font-mono text-ink
                 hover:bg-surface-hover transition-colors border-b border-rim/50 last:border-b-0"
        >
          <span class="block truncate">{{ layout.filename }}</span>
          <span class="text-[10px] text-ink-ghost">
            {{ layout.node_count }}n · {{ layout.edge_count }}e · {{ layout.units }}
          </span>
        </button>

        <div class="border-t border-rim">
          <input type="file" accept=".yaml,.yml" ref="fileInput" class="hidden" @change="onFileUpload" />
          <button
            @click="$refs.fileInput.click()"
            class="w-full text-left px-3 py-2 text-[11px] font-mono text-ink-ghost
                   hover:bg-surface-hover transition-colors"
          >
            ↑ Upload YAML…
          </button>
        </div>
      </div>
    </div>

    <div class="w-px h-4 bg-rim shrink-0" />

    <!-- Save -->
    <button
      :disabled="!layoutStore.hasLayout"
      @click="$emit('save')"
      class="px-3 py-1.5 text-[11px] font-mono border border-rim transition-colors"
      :class="layoutStore.hasLayout
        ? 'text-ink-ghost hover:text-ink hover:bg-surface-hover'
        : 'text-ink-ghost/40 cursor-not-allowed'"
    >
      Save
      <span v-if="layoutStore.isDirty" class="text-mustard ml-0.5">●</span>
    </button>

    <div class="w-px h-4 bg-rim shrink-0" />

    <!-- Undo / Redo -->
    <button
      :disabled="!layoutStore.canUndo"
      @click="layoutStore.undo()"
      title="Undo (Ctrl+Z)"
      class="px-2 py-1.5 text-[11px] font-mono border border-rim transition-colors"
      :class="layoutStore.canUndo
        ? 'text-ink-ghost hover:text-ink hover:bg-surface-hover'
        : 'text-ink-ghost/30 cursor-not-allowed'"
    >↩</button>
    <button
      :disabled="!layoutStore.canRedo"
      @click="layoutStore.redo()"
      title="Redo (Ctrl+Y)"
      class="px-2 py-1.5 text-[11px] font-mono border border-rim transition-colors"
      :class="layoutStore.canRedo
        ? 'text-ink-ghost hover:text-ink hover:bg-surface-hover'
        : 'text-ink-ghost/30 cursor-not-allowed'"
    >↪</button>

    <div class="w-px h-4 bg-rim shrink-0" />

    <!-- Zoom display -->
    <span class="text-[11px] font-mono text-ink-ghost min-w-[72px] text-center">
      {{ zoomLabel }}
    </span>

    <!-- Fit to content -->
    <button
      :disabled="!layoutStore.hasLayout"
      @click="$emit('fit')"
      title="Fit to content"
      class="px-2 py-1.5 text-[11px] font-mono border border-rim transition-colors"
      :class="layoutStore.hasLayout
        ? 'text-ink-ghost hover:text-ink hover:bg-surface-hover'
        : 'text-ink-ghost/30 cursor-not-allowed'"
    >⊞</button>

    <div class="w-px h-4 bg-rim shrink-0" />

    <!-- Grid toggle -->
    <button
      @click="layoutStore.showGrid = !layoutStore.showGrid"
      class="px-3 py-1.5 text-[11px] font-mono border border-rim transition-colors"
      :class="layoutStore.showGrid
        ? 'bg-deep-teal/10 border-deep-teal/40 text-deep-teal'
        : 'text-ink-ghost hover:text-ink hover:bg-surface-hover'"
    >Grid</button>

    <!-- Snap toggle -->
    <button
      @click="layoutStore.snapToGrid = !layoutStore.snapToGrid"
      class="px-3 py-1.5 text-[11px] font-mono border border-rim transition-colors"
      :class="layoutStore.snapToGrid
        ? 'bg-deep-teal/10 border-deep-teal/40 text-deep-teal'
        : 'text-ink-ghost hover:text-ink hover:bg-surface-hover'"
    >Snap</button>

    <div class="w-px h-4 bg-rim shrink-0" />

    <!-- Data dropdown (POS + product catalog) -->
    <div class="relative" ref="dataMenuRef">
      <button
        :disabled="!layoutStore.hasLayout"
        @click="dataMenuOpen = !dataMenuOpen"
        class="flex items-center gap-1 px-3 py-1.5 text-[11px] font-mono border border-rim transition-colors"
        :class="layoutStore.hasLayout
          ? 'text-ink-ghost hover:text-ink hover:bg-surface-hover'
          : 'text-ink-ghost/30 cursor-not-allowed'"
      >
        <span>Data</span>
        <span v-if="layoutStore.posCsvName || layoutStore.productsFile" class="text-mustard text-[8px]">●</span>
        <span class="text-[9px]">▾</span>
      </button>

      <div
        v-if="dataMenuOpen"
        class="absolute top-full left-0 mt-1 w-72 bg-surface border border-rim shadow-lg z-50"
      >
        <!-- POS CSV -->
        <div class="px-3 py-2 text-[10px] uppercase tracking-label text-ink-ghost border-b border-rim">
          POS Data
        </div>

        <div v-if="layoutStore.posCsvName" class="px-3 py-2 flex items-center gap-2 border-b border-rim">
          <span class="text-[11px] font-mono text-ink flex-1 truncate">{{ layoutStore.posCsvName }}</span>
          <button
            @click="layoutStore.detachPosCsv()"
            class="text-danger/60 hover:text-danger text-[10px] font-mono shrink-0"
          >✕</button>
        </div>

        <template v-else>
          <div v-if="datasetsLoading" class="px-3 py-2 text-[11px] text-ink-ghost font-mono">Loading…</div>
          <button
            v-for="ds in datasets"
            :key="ds.filename"
            @click="attachPos(ds.filename)"
            class="w-full text-left px-3 py-2 text-[11px] font-mono text-ink
                   hover:bg-surface-hover transition-colors border-b border-rim/50 last:border-b-0"
          >
            <span class="block truncate">{{ ds.store_name || ds.filename }}</span>
            <span class="text-[10px] text-ink-ghost">{{ ds.record_count?.toLocaleString() ?? 0 }} records</span>
          </button>
          <div v-if="!datasetsLoading && !datasets.length" class="px-3 py-2 text-[11px] text-ink-ghost font-mono">
            No POS datasets found
          </div>
          <div class="border-t border-rim">
            <input type="file" accept=".csv" ref="posCsvInput" class="hidden" @change="onPosUpload" />
            <button
              @click="$refs.posCsvInput.click()"
              class="w-full text-left px-3 py-2 text-[11px] font-mono text-ink-ghost hover:bg-surface-hover transition-colors"
            >↑ Upload POS CSV…</button>
          </div>
        </template>

        <!-- Product catalog -->
        <div class="px-3 py-2 text-[10px] uppercase tracking-label text-ink-ghost border-y border-rim">
          Product Catalog
        </div>

        <div v-if="layoutStore.productsFile" class="px-3 py-2 flex items-center gap-2">
          <span class="text-[11px] font-mono text-ink flex-1 truncate">{{ layoutStore.productsFile }}</span>
          <span class="text-[10px] text-ink-ghost shrink-0">{{ layoutStore.products.length }} SKUs</span>
          <button
            @click="layoutStore.detachProducts()"
            class="text-danger/60 hover:text-danger text-[10px] font-mono shrink-0"
          >✕</button>
        </div>
        <div v-else>
          <input type="file" accept=".csv" ref="productCsvInput" class="hidden" @change="onProductUpload" />
          <button
            @click="$refs.productCsvInput.click()"
            :disabled="productUploading"
            class="w-full text-left px-3 py-2 text-[11px] font-mono text-ink-ghost hover:bg-surface-hover transition-colors"
          >{{ productUploading ? 'Uploading…' : '↑ Upload catalog CSV…' }}</button>
        </div>
      </div>
    </div>

    <!-- Alignment toolbar — visible when multi-selecting nodes -->
    <template v-if="multiNodeCount >= 2">
      <div class="w-px h-4 bg-rim shrink-0" />
      <span class="text-[10px] font-mono text-ink-ghost/60 shrink-0">Align:</span>
      <button
        v-for="btn in alignButtons"
        :key="btn.key"
        :title="btn.title"
        @click="btn.action()"
        class="px-2 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
               hover:text-ink hover:bg-surface-hover transition-colors"
      >{{ btn.label }}</button>
      <template v-if="multiNodeCount >= 3">
        <div class="w-px h-4 bg-rim shrink-0" />
        <span class="text-[10px] font-mono text-ink-ghost/60 shrink-0">Distribute:</span>
        <button
          title="Distribute horizontally"
          @click="layoutStore.distributeSelectedNodes('x')"
          class="px-2 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                 hover:text-ink hover:bg-surface-hover transition-colors"
        >↔</button>
        <button
          title="Distribute vertically"
          @click="layoutStore.distributeSelectedNodes('y')"
          class="px-2 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                 hover:text-ink hover:bg-surface-hover transition-colors"
        >↕</button>
      </template>
    </template>

    <!-- Spacer -->
    <div class="flex-1" />

    <!-- Filename / dirty indicator -->
    <span v-if="layoutStore.filename" class="text-[11px] font-mono text-ink-dim truncate max-w-[200px]">
      {{ layoutStore.filename }}
    </span>
    <span v-if="layoutStore.isDirty" class="text-[10px] font-mono text-mustard">unsaved</span>

    <div class="w-px h-4 bg-rim shrink-0" />

    <!-- Run (enabled whenever a layout is loaded) -->
    <button
      :disabled="!layoutStore.hasLayout"
      @click="$emit('run')"
      class="px-3 py-1.5 text-[11px] font-mono border transition-colors"
      :class="layoutStore.hasLayout
        ? 'border-deep-teal/50 text-deep-teal hover:bg-deep-teal/10'
        : 'border-rim text-ink-ghost/30 cursor-not-allowed'"
    >▶ Run</button>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount } from "vue";
import { useLayoutStore } from "../../stores/layout";
import { useTemporalStore } from "../../stores/temporal";
import { uploadFile } from "../../api/client";

const layoutStore   = useLayoutStore();
const temporalStore = useTemporalStore();

const emit = defineEmits(["save", "fit", "run"]);

const loadMenuOpen  = ref(false);
const loadMenuRef   = ref(null);
const dataMenuOpen  = ref(false);
const dataMenuRef   = ref(null);
const fileInput     = ref(null);
const productUploading = ref(false);

const datasets       = computed(() => temporalStore.datasets ?? []);
const datasetsLoading = computed(() => temporalStore.datasetsLoading);

const zoomLabel = computed(() => {
  const z = layoutStore.viewport.zoom;
  const px = z >= 10 ? Math.round(z) : z >= 1 ? z.toFixed(1) : z.toFixed(2);
  return `1ft = ${px}px`;
});

const multiNodeCount = computed(() =>
  layoutStore.multiSelection.filter((s) => s.type === "node").length
);

const alignButtons = [
  { key: "left",    label: "⊢",  title: "Align left edges",    action: () => layoutStore.alignSelectedNodes("x", "min")    },
  { key: "right",   label: "⊣",  title: "Align right edges",   action: () => layoutStore.alignSelectedNodes("x", "max")    },
  { key: "cx",      label: "⊟",  title: "Center on X axis",    action: () => layoutStore.alignSelectedNodes("x", "center") },
  { key: "top",     label: "⊤",  title: "Align top edges",     action: () => layoutStore.alignSelectedNodes("y", "min")    },
  { key: "bottom",  label: "⊥",  title: "Align bottom edges",  action: () => layoutStore.alignSelectedNodes("y", "max")    },
  { key: "cy",      label: "≡",  title: "Center on Y axis",    action: () => layoutStore.alignSelectedNodes("y", "center") },
];

// ── Load from list ──────────────────────────────────────────────────────────
async function loadLayout(filename) {
  loadMenuOpen.value = false;
  await layoutStore.loadLayout(filename);
  emit("fit");
}

// ── Upload YAML ─────────────────────────────────────────────────────────────
async function onFileUpload(e) {
  loadMenuOpen.value = false;
  const file = e.target.files?.[0];
  if (!file) return;
  try {
    const result = await uploadFile(file);
    await layoutStore.fetchAvailableLayouts();
    await layoutStore.loadLayout(result.filename);
    emit("fit");
  } catch (err) {
    console.error("Upload failed:", err);
  } finally {
    e.target.value = "";
  }
}

// ── Data: POS ───────────────────────────────────────────────────────────────
function attachPos(filename) {
  dataMenuOpen.value = false;
  layoutStore.attachPosCsv(filename);
}

async function onPosUpload(e) {
  const file = e.target.files?.[0];
  if (!file) return;
  dataMenuOpen.value = false;
  try {
    const result = await uploadFile(file);
    await temporalStore.fetchDatasets();
    layoutStore.attachPosCsv(result.filename);
  } catch (err) {
    console.error("POS upload failed:", err);
  } finally {
    e.target.value = "";
  }
}

// ── Data: Product catalog ────────────────────────────────────────────────────
async function onProductUpload(e) {
  const file = e.target.files?.[0];
  if (!file) return;
  productUploading.value = true;
  try {
    const result = await uploadFile(file);
    layoutStore.attachProducts(result.filename);
    dataMenuOpen.value = false;
  } catch (err) {
    console.error("Product upload failed:", err);
  } finally {
    productUploading.value = false;
    e.target.value = "";
  }
}

// ── Close dropdowns on outside click ────────────────────────────────────────
function onDocClick(e) {
  if (loadMenuRef.value && !loadMenuRef.value.contains(e.target)) {
    loadMenuOpen.value = false;
  }
  if (dataMenuRef.value && !dataMenuRef.value.contains(e.target)) {
    dataMenuOpen.value = false;
  }
}

onMounted(async () => {
  document.addEventListener("click", onDocClick);
  await Promise.all([
    layoutStore.fetchAvailableLayouts(),
    temporalStore.fetchDatasets(),
  ]);
});

onBeforeUnmount(() => {
  document.removeEventListener("click", onDocClick);
});
</script>
