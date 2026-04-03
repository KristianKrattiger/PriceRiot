<template>
  <div class="flex flex-col h-full">

    <div class="px-3 py-2 border-b border-rim shrink-0">
      <p class="text-[10px] uppercase tracking-label text-ink-ghost">Tools</p>
    </div>

    <div class="flex-1 overflow-y-auto py-2 px-2 space-y-4">

      <!-- View -->
      <div class="space-y-1">
        <p class="px-1 text-[9px] uppercase tracking-label text-ink-ghost/50">View</p>
        <button v-bind="btn('select')" @click="pick('select')">
          <span class="w-4 text-center shrink-0">↖</span> Select
        </button>
      </div>

      <!-- Nodes -->
      <div class="space-y-1">
        <p class="px-1 text-[9px] uppercase tracking-label text-ink-ghost/50">Nodes</p>
        <button v-bind="btn('junction')" @click="pick('junction')">
          <span class="w-4 text-center shrink-0 text-[10px]">◯</span> Junction
        </button>
        <button v-bind="btn('register')" @click="pick('register')">
          <span class="w-4 text-center shrink-0 text-[10px]">▣</span> Register
        </button>
        <button v-bind="btn('entrance')" @click="pick('entrance')">
          <span class="w-4 text-center shrink-0 text-[9px] text-green">▶</span> Entrance
        </button>
        <button v-bind="btn('exit')" @click="pick('exit')">
          <span class="w-4 text-center shrink-0 text-[9px] text-danger">◀</span> Exit
        </button>
      </div>

      <!-- Connections -->
      <div class="space-y-1">
        <p class="px-1 text-[9px] uppercase tracking-label text-ink-ghost/50">Connections</p>
        <button v-bind="btn('draw-edge')" @click="pick('draw-edge')">
          <span class="w-4 text-center shrink-0 text-[10px]">╌</span> Draw Aisle
        </button>
      </div>

      <!-- Fixtures -->
      <div class="space-y-1">
        <p class="px-1 text-[9px] uppercase tracking-label text-ink-ghost/50">Fixtures</p>
        <button v-bind="btn('shelf')" @click="pick('shelf')">
          <span class="w-4 text-center shrink-0 text-[10px]">▬</span> Shelf
        </button>
        <div class="px-2 py-1 text-[9px] font-mono text-ink-ghost/40 leading-relaxed opacity-50 pointer-events-none">
          Endcaps, bins — Ph3
        </div>
      </div>

    </div>

    <!-- Delete -->
    <div class="px-2 py-2 border-t border-rim shrink-0">
      <button
        :disabled="!layoutStore.selection"
        @click="deleteSelected"
        class="w-full flex items-center gap-2 px-2 py-1.5 text-[11px] font-mono border transition-colors"
        :class="layoutStore.selection
          ? 'border-danger/50 text-danger hover:bg-danger/10'
          : 'border-rim text-ink-ghost/30 cursor-not-allowed'"
      >
        <span class="w-4 text-center shrink-0">✕</span> Delete
      </button>
    </div>

  </div>
</template>

<script setup>
import { computed } from "vue";
import { useLayoutStore } from "../../stores/layout";

const layoutStore = useLayoutStore();
const activeTool  = computed(() => layoutStore.activeTool);

function btn(tool) {
  const active = activeTool.value === tool;
  return {
    class: [
      "w-full flex items-center gap-2 px-2 py-1.5 text-[11px] font-mono border transition-colors text-left",
      active
        ? "border-mustard/60 bg-mustard/10 text-mustard"
        : "border-rim text-ink-ghost hover:text-ink hover:bg-surface-hover",
    ],
  };
}

function pick(tool) {
  layoutStore.setActiveTool(tool);
}

function deleteSelected() {
  const sel = layoutStore.selection;
  if (!sel) return;
  if (sel.type === "node")       layoutStore.removeNode(sel.id);
  else if (sel.type === "edge")  layoutStore.removeEdge(sel.id);
  else if (sel.type === "freeObject") layoutStore.removeFreeObject(sel.id);
}
</script>
