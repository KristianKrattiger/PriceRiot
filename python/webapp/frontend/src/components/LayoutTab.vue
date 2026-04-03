<template>
  <div class="flex flex-col h-[calc(100vh-130px)] min-h-[500px]">

    <!-- Toolbar -->
    <LayoutToolbar @save="onSaveRequest" @fit="onFit" />

    <!-- Three-panel body -->
    <div class="flex flex-1 min-h-0">

      <!-- ── Left: Object Palette ──────────────────────────────────────────── -->
      <div class="w-40 shrink-0 border-r border-rim bg-surface flex flex-col">
        <ObjectPalette />
      </div>

      <!-- ── Center: Canvas ─────────────────────────────────────────────────── -->
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
      </div>

      <!-- ── Right: Properties panel ────────────────────────────────────────── -->
      <div class="w-64 shrink-0 border-l border-rim bg-surface flex flex-col">
        <PropertiesPanel />
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
  </div>
</template>

<script setup>
import { ref, onMounted, onBeforeUnmount } from "vue";
import { useLayoutStore } from "../stores/layout";
import { uploadFile } from "../api/client";
import LayoutCanvas    from "./layout/LayoutCanvas.vue";
import LayoutToolbar   from "./layout/LayoutToolbar.vue";
import StoreWizard     from "./layout/StoreWizard.vue";
import SaveLayoutDialog from "./layout/SaveLayoutDialog.vue";
import ObjectPalette   from "./layout/ObjectPalette.vue";
import PropertiesPanel from "./layout/PropertiesPanel.vue";

const layoutStore = useLayoutStore();

const canvasRef      = ref(null);
const showWizard     = ref(false);
const showSaveDialog = ref(false);
const saveDialogName = ref("");
const emptyLoadOpen  = ref(false);
const emptyLoadRef   = ref(null);
const emptyFileInput = ref(null);

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
  } catch (err) {
    console.error("Save failed:", err);
  }
}

// ── Fit ─────────────────────────────────────────────────────────────────────
function onFit() {
  canvasRef.value?.fitToContent();
}

// ── Quick-load from empty state dropdown ─────────────────────────────────────
async function quickLoad(filename) {
  emptyLoadOpen.value = false;
  await layoutStore.loadLayout(filename);
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
