<template>
  <div class="flex flex-col h-full">

    <!-- Header -->
    <div class="px-3 py-2 border-b border-rim shrink-0">
      <p class="text-[10px] uppercase tracking-label text-ink-ghost">Properties</p>
    </div>

    <!-- No selection -->
    <div
      v-if="!layoutStore.selection"
      class="flex-1 flex items-center justify-center p-4"
    >
      <p class="text-[10px] font-mono text-ink-ghost/40 text-center leading-relaxed">
        Select a node, edge,<br>or shelf to edit<br>properties.
      </p>
    </div>

    <!-- Node properties -->
    <div v-else-if="layoutStore.selection.type === 'node' && localNode" class="flex flex-col flex-1 min-h-0">
      <div class="flex-1 overflow-y-auto px-3 py-3 space-y-3">

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
    <div v-else-if="layoutStore.selection.type === 'edge' && localEdge" class="flex flex-col flex-1 min-h-0">
      <div class="flex-1 overflow-y-auto px-3 py-3 space-y-3">

        <!-- Header row -->
        <div class="flex items-center justify-between">
          <span class="text-[10px] font-mono text-ink-ghost/60">
            {{ localEdge.nodeA }} → {{ localEdge.nodeB }}
          </span>
          <span class="text-[10px] font-mono text-ink-ghost">id {{ localEdge.id }}</span>
        </div>

        <!-- Geometry -->
        <FieldGroup label="Geometry">
          <PropInput label="Width (ft)" v-model.number="localEdge.width" @blur="applyEdge" />
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

        <!-- Shelf depths (only meaningful for wall edges) -->
        <FieldGroup label="Shelf depth (ft)">
          <div class="grid grid-cols-2 gap-2">
            <PropInput label="Left"  v-model.number="localEdge.shelfLeft"  @blur="applyEdge" />
            <PropInput label="Right" v-model.number="localEdge.shelfRight" @blur="applyEdge" />
          </div>
        </FieldGroup>

        <!-- Flow -->
        <FieldGroup label="Flow">
          <div class="flex border border-rim overflow-hidden">
            <button
              v-for="f in ['bi', 'fwd', 'rev']"
              :key="f"
              @click="setFlow(f)"
              class="flex-1 py-1.5 text-[11px] font-mono transition-colors"
              :class="[
                f !== 'bi' ? 'border-l border-rim' : '',
                localEdge.flow === f
                  ? 'bg-mustard/10 text-mustard'
                  : 'text-ink-ghost hover:text-ink hover:bg-surface-hover',
              ]"
            >{{ f }}</button>
          </div>
        </FieldGroup>

        <!-- Add shelf shortcut -->
        <div class="pt-1">
          <button
            @click="addShelfToEdge"
            class="w-full py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                   hover:text-ink hover:bg-surface-hover transition-colors"
          >▬ Add shelf to edge</button>
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
      <div class="flex-1 overflow-y-auto px-3 py-3 space-y-3">

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

        <p class="text-[10px] font-mono text-ink-ghost/50 leading-relaxed">
          {{ localFO.widthFt }}ft × {{ localFO.lengthFt }}ft · Drag to reposition.
        </p>

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

const layoutStore = useLayoutStore();

// ---------------------------------------------------------------------------
// Local form mirrors
// ---------------------------------------------------------------------------
const localNode = ref(null);
const localEdge = ref(null);
const localFO   = ref(null);

watch(
  () => layoutStore.selection,
  () => syncForm(),
  { immediate: true }
);

// Sync when underlying data changes (drag moves, etc.)
watch(() => layoutStore.nodes,       () => { if (layoutStore.selection?.type === "node")       syncForm(); }, { deep: true });
watch(() => layoutStore.freeObjects, () => { if (layoutStore.selection?.type === "freeObject") syncForm(); }, { deep: true });

function syncForm() {
  const sel = layoutStore.selection;
  localNode.value = null;
  localEdge.value = null;
  localFO.value   = null;
  if (!sel) return;
  if (sel.type === "node") {
    const n = layoutStore.nodeById(sel.id);
    localNode.value = n ? { ...n } : null;
  } else if (sel.type === "edge") {
    const e = layoutStore.edgeById(sel.id);
    localEdge.value = e ? { ...e } : null;
  } else if (sel.type === "freeObject") {
    const fo = layoutStore.freeObjects.find((f) => f.id === sel.id);
    localFO.value = fo ? { ...fo } : null;
  }
}

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

function setFlow(f) {
  if (!localEdge.value) return;
  localEdge.value.flow = f;
  applyEdge();
}

// ---------------------------------------------------------------------------
// Edge shelf
// ---------------------------------------------------------------------------
function addShelfToEdge() {
  if (!localEdge.value) return;
  layoutStore.addShelfToEdge(localEdge.value.id);
  // Sync to show updated depth values
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

<!-- Shared micro-components used only in this file -->
<script>
const FieldGroup = {
  props: { label: String },
  template: `
    <div class="space-y-1.5">
      <p class="text-[9px] uppercase tracking-label text-ink-ghost/50">{{ label }}</p>
      <slot />
    </div>
  `,
};

const PropInput = {
  props: { label: String, modelValue: [Number, String] },
  emits: ["update:modelValue", "blur"],
  template: `
    <div class="space-y-0.5">
      <label class="block text-[10px] text-ink-ghost/70">{{ label }}</label>
      <input
        :value="modelValue"
        @input="$emit('update:modelValue', $event.target.valueAsNumber ?? $event.target.value)"
        @blur="$emit('blur')"
        type="number"
        step="any"
        class="w-full bg-surface-deep border border-rim px-2 py-1 text-[11px] font-mono text-ink
               focus:outline-none focus:border-tangerine transition-colors"
      />
    </div>
  `,
};

export default { components: { FieldGroup, PropInput } };
</script>
