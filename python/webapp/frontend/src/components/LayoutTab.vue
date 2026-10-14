<template>
  <div class="flex flex-col h-[calc(100vh-130px)] min-h-[500px]">

    <!-- Toolbar -->
    <LayoutToolbar @save="onSaveRequest" @fit="onFit" @run="showRunModal = true" />

    <!-- Run status banner -->
    <div
      v-if="runBanner"
      class="px-4 py-2 border-b border-rim flex items-center gap-3 text-[11px] font-mono shrink-0"
      :class="runBanner.status === 'failed' ? 'bg-danger/10 border-danger/30' : 'bg-surface'"
    >
      <span v-if="runBanner.status === 'running'" class="text-mustard animate-pulse flex items-center gap-1.5">
        <span class="w-1.5 h-1.5 rounded-full bg-mustard inline-block"></span>
        Layout test running…
      </span>
      <template v-else-if="runBanner.status === 'complete'">
        <span class="text-deep-teal">✓ Done</span>
        <span class="text-ink-ghost">·</span>
        <span class="text-ink">{{ runBanner.result?.mean_transactions?.toLocaleString() ?? 0 }} transactions</span>
        <span class="text-ink-ghost">·</span>
        <span class="text-ink">${{ Number(runBanner.result?.mean_revenue ?? 0).toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 }) }} revenue</span>
        <button @click="navigateToResults" class="text-deep-teal underline hover:opacity-80 transition-opacity ml-1">View results →</button>
      </template>
      <span v-else-if="runBanner.status === 'failed'" class="text-danger">
        ✗ Run failed{{ runBanner.error ? ': ' + runBanner.error : '' }}
      </span>
      <button @click="runBanner = null" class="ml-auto text-ink-ghost hover:text-ink transition-colors">✕</button>
    </div>

    <!-- Three-panel body -->
    <div class="flex flex-1 min-h-0">

      <!-- ── Left: Object Palette ──────────────────────────────────────────── -->
      <div class="w-40 shrink-0 border-r border-rim bg-surface flex flex-col">
        <ObjectPalette />
      </div>

      <!-- ── Center: Canvas + Planogram overlay ────────────────────────��───── -->
      <div class="flex-1 min-w-0 relative">

        <!-- Empty state -->
        <div
          v-if="!layoutStore.hasLayout && !layoutStore.isLoading"
          class="absolute inset-0 flex flex-col items-center justify-center gap-6 bg-surface-deep"
        >
          <div class="text-center space-y-1">
            <p class="text-sm text-ink-dim">No layout loaded</p>
            <p class="text-[11px] font-mono text-ink-ghost">
              Create a new store or load an existing layout to begin.
            </p>
          </div>

          <div class="flex gap-3">
            <button
              @click="showWizard = true"
              class="px-5 py-2.5 text-[11px] font-mono border border-mustard
                     bg-mustard/10 text-mustard hover:bg-mustard/20 transition-colors"
            >+ New Layout</button>

            <div class="relative" ref="emptyLoadRef">
              <button
                @click="emptyLoadOpen = !emptyLoadOpen"
                class="px-5 py-2.5 text-[11px] font-mono border border-rim text-ink-ghost
                       hover:text-ink hover:bg-surface-hover transition-colors"
              >↑ Load YAML ▾</button>

              <div
                v-if="emptyLoadOpen"
                class="absolute top-full left-0 mt-1 w-64 bg-surface border border-rim shadow-lg z-50"
              >
                <div v-if="layoutStore.layoutsLoading"
                     class="px-3 py-2 text-[11px] text-ink-ghost font-mono">Loading…</div>
                <div v-else-if="!layoutStore.availableLayouts.length"
                     class="px-3 py-2 text-[11px] text-ink-ghost font-mono">No layouts found</div>
                <button
                  v-for="layout in layoutStore.availableLayouts"
                  :key="layout.filename"
                  @click="quickLoad(layout.filename)"
                  class="w-full text-left px-3 py-2 text-[11px] font-mono text-ink
                         hover:bg-surface-hover transition-colors border-b border-rim/50 last:border-b-0"
                >
                  <span class="block truncate">{{ layout.filename }}</span>
                  <span class="text-[10px] text-ink-ghost">{{ layout.node_count }}n · {{ layout.edge_count }}e</span>
                </button>
                <div class="border-t border-rim">
                  <input type="file" accept=".yaml,.yml" ref="emptyFileInput" class="hidden" @change="onEmptyUpload" />
                  <button
                    @click="$refs.emptyFileInput.click()"
                    class="w-full text-left px-3 py-2 text-[11px] font-mono text-ink-ghost
                           hover:bg-surface-hover transition-colors"
                  >↑ Upload YAML…</button>
                </div>
              </div>
            </div>
          </div>

          <p v-if="layoutStore.loadError" class="text-[11px] font-mono text-danger">
            {{ layoutStore.loadError }}
          </p>
        </div>

        <!-- Loading spinner -->
        <div
          v-else-if="layoutStore.isLoading"
          class="absolute inset-0 flex items-center justify-center bg-surface-deep"
        >
          <div class="flex items-center gap-2 text-[11px] font-mono text-ink-ghost">
            <span class="w-1.5 h-1.5 rounded-full bg-mustard animate-pulse"></span>
            Loading layout…
          </div>
        </div>

        <!-- Canvas -->
        <LayoutCanvas v-else ref="canvasRef" class="absolute inset-0" />

        <!-- Planogram panel — slides over the right portion of the canvas -->
        <PlanogramPanel
          v-if="layoutStore.planogramOpen && layoutStore.selectedEdge"
          :edge-id="layoutStore.selectedEdge.id"
          class="absolute inset-y-0 right-0 w-80 z-20"
          @close="layoutStore.planogramOpen = false"
        />
      </div>

      <!-- ── Right: Properties panel ────────────────────────────────────────── -->
      <div class="w-64 shrink-0 min-h-0 border-l border-rim bg-surface flex flex-col overflow-hidden">
        <PropertiesPanel class="min-h-0 flex-1 flex flex-col overflow-hidden" />
      </div>
    </div>

    <!-- Wizard modal -->
    <StoreWizard
      v-if="showWizard"
      @created="onWizardCreated"
      @cancel="showWizard = false"
    />

    <!-- Save dialog -->
    <SaveLayoutDialog
      v-if="showSaveDialog"
      :current-name="saveDialogName"
      @save="onSaveConfirm"
      @cancel="showSaveDialog = false"
    />

    <!-- Run config modal -->
    <RunConfigModal
      v-if="showRunModal"
      @run-started="onRunStarted"
      @cancel="showRunModal = false"
    />
  </div>
</template>

<script setup>
import { ref, watch, onMounted, onBeforeUnmount } from "vue";
import { useLayoutStore } from "../stores/layout";
import { useTemporalStore } from "../stores/temporal";
import { uploadFile } from "../api/client";
import LayoutCanvas    from "./layout/LayoutCanvas.vue";
import LayoutToolbar   from "./layout/LayoutToolbar.vue";
import StoreWizard     from "./layout/StoreWizard.vue";
import SaveLayoutDialog from "./layout/SaveLayoutDialog.vue";
import ObjectPalette   from "./layout/ObjectPalette.vue";
import PropertiesPanel from "./layout/PropertiesPanel.vue";
import RunConfigModal  from "./layout/RunConfigModal.vue";
import PlanogramPanel  from "./layout/PlanogramPanel.vue";

const layoutStore  = useLayoutStore();
const temporalStore = useTemporalStore();
const emit = defineEmits(["navigate"]);

const canvasRef      = ref(null);
const showWizard     = ref(false);
const showSaveDialog = ref(false);
const showRunModal   = ref(false);
const saveDialogName = ref("");
const emptyLoadOpen  = ref(false);
const emptyLoadRef   = ref(null);
const emptyFileInput = ref(null);
const runBanner      = ref(null); // { simId, status: 'running'|'complete'|'failed', result, error }

// ── Wizard ──────────────────────────────────────────────────────────────────
function onWizardCreated(layoutJson, name) {
  showWizard.value = false;
  layoutStore.loadWizardLayout(layoutJson, name);
}

// ── Save ────────────────────────────────────────────────────────────────────
function onSaveRequest() {
  const stem = layoutStore.filename
    ? layoutStore.filename.replace(/\.(yaml|yml)$/i, "")
    : "my_store";
  saveDialogName.value = stem;
  showSaveDialog.value = true;
}

async function onSaveConfirm({ name, overwrite }) {
  showSaveDialog.value = false;
  try {
    await layoutStore.saveLayout(name, overwrite);
    // Keep the RunTab's YAML picker in sync with the (possibly renamed) saved file.
    if (layoutStore.filename) {
      temporalStore.activeConfig.store_yaml = layoutStore.filename;
    }
  } catch (err) {
    console.error("Save failed:", err);
  }
}

// ── Fit ─────────────────────────────────────────────────────────────────────
function onFit() {
  canvasRef.value?.fitToContent();
}

// ── Run modal ────────────────────────────────────────────────────────────────
function onRunStarted(simId) {
  showRunModal.value = false;
  runBanner.value = { simId, status: "running" };
  temporalStore.trackNewSim(simId);
}

// Watch the active simulation for completion so the banner can update
watch(
  () => temporalStore.activeSimulation,
  (sim) => {
    if (!sim || !runBanner.value) return;
    if (String(sim.sim_id) !== String(runBanner.value.simId)) return;
    if (sim.status === "complete" || sim.status === "completed") {
      runBanner.value = { ...runBanner.value, status: "complete", result: sim.aggregate };
    } else if (sim.status === "failed") {
      runBanner.value = { ...runBanner.value, status: "failed", error: sim.error_message };
    }
  },
  { deep: true }
);

function navigateToResults() {
  runBanner.value = null;
  emit("navigate", "results");
}

// ── Quick-load from empty state dropdown ─────────────────────────────────────
async function quickLoad(filename) {
  emptyLoadOpen.value = false;
  await layoutStore.loadLayout(filename);
  temporalStore.activeConfig.store_yaml = filename;
}

async function onEmptyUpload(e) {
  emptyLoadOpen.value = false;
  const file = e.target.files?.[0];
  if (!file) return;
  try {
    const result = await uploadFile(file);
    await layoutStore.fetchAvailableLayouts();
    await layoutStore.loadLayout(result.filename);
  } catch (err) {
    console.error("Upload failed:", err);
  } finally {
    e.target.value = "";
  }
}

// ── Close empty-state dropdown on outside click ──────────────────────────────
function onDocClick(e) {
  if (emptyLoadRef.value && !emptyLoadRef.value.contains(e.target)) {
    emptyLoadOpen.value = false;
  }
}

onMounted(() => {
  document.addEventListener("click", onDocClick);
  layoutStore.fetchAvailableLayouts();
});
onBeforeUnmount(() => document.removeEventListener("click", onDocClick));
</script>
