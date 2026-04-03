<template>
  <div class="space-y-3" v-if="hasData">
    <h3 class="text-[11px] uppercase tracking-label text-ink-ghost font-medium">
      Traffic by edge
    </h3>
    <div class="border border-rim bg-surface-deep p-4 space-y-2">
      <div
        v-for="edge in sortedEdges"
        :key="edge.edge_index"
        class="flex items-center gap-3 text-[11px]"
      >
        <span class="w-16 font-mono text-ink-ghost shrink-0">
          Edge {{ edge.edge_index }}
        </span>
        <div class="flex-1 h-px bg-rim overflow-hidden relative">
          <div
            class="absolute inset-y-0 left-0 h-full bg-deep-teal"
            :style="{ width: barWidth(edge) }"
          />
        </div>
        <span class="w-10 text-right font-mono text-ink-dim shrink-0">
          {{ edge.visits }}
        </span>
      </div>
    </div>
    <p class="text-[11px] text-ink-ghost">
      Sorted by visit count. Longer bar = more traffic.
    </p>
  </div>
  <div v-else class="text-[11px] text-ink-ghost">
    No traffic data available for this run.
  </div>
</template>

<script setup>
import { computed } from "vue";

const props = defineProps({ trafficEdges: { type: Array, required: true } });

const hasData = computed(
  () => Array.isArray(props.trafficEdges) && props.trafficEdges.length > 0
);

const sortedEdges = computed(() => {
  if (!hasData.value) return [];
  return [...props.trafficEdges]
    .map((e) => ({ edge_index: e.edge_index, visits: Number(e.visits ?? 0) }))
    .filter((e) => Number.isFinite(e.edge_index) && Number.isFinite(e.visits))
    .sort((a, b) => b.visits - a.visits);
});

const maxVisits = computed(() => {
  if (!sortedEdges.value.length) return 0;
  return Math.max(...sortedEdges.value.map((e) => e.visits), 0);
});

function barWidth(edge) {
  if (maxVisits.value <= 0) return "0%";
  return `${Math.max(2, (edge.visits / maxVisits.value) * 100)}%`;
}
</script>
