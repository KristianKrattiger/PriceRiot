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
          <!-- Hidden file input -->
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

    <!-- Save (disabled until layout loaded) -->
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

    <!-- Spacer -->
    <div class="flex-1" />

    <!-- Filename / dirty indicator -->
    <span v-if="layoutStore.filename" class="text-[11px] font-mono text-ink-dim truncate max-w-[200px]">
      {{ layoutStore.filename }}
    </span>
    <span v-if="layoutStore.isDirty" class="text-[10px] font-mono text-mustard">unsaved</span>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount } from "vue";
import { useLayoutStore } from "../../stores/layout";
import { uploadFile } from "../../api/client";

const layoutStore = useLayoutStore();

const emit = defineEmits(["save", "fit"]);

const loadMenuOpen = ref(false);
const loadMenuRef  = ref(null);
const fileInput    = ref(null);

const zoomLabel = computed(() => {
  const z = layoutStore.viewport.zoom;
  return `${z.toFixed(1)} px/ft`;
});

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

// ── Close dropdown on outside click ────────────────────────────────────────
function onDocClick(e) {
  if (loadMenuRef.value && !loadMenuRef.value.contains(e.target)) {
    loadMenuOpen.value = false;
  }
}

onMounted(async () => {
  document.addEventListener("click", onDocClick);
  await layoutStore.fetchAvailableLayouts();
});

onBeforeUnmount(() => {
  document.removeEventListener("click", onDocClick);
});
</script>
