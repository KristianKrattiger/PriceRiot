<template>
  <div class="space-y-3" v-if="hasData">
    <h3 class="text-sm font-semibold text-slate-200">
      Traffic by edge
    </h3>
    <div class="rounded-lg border border-slate-800 bg-slate-900/60 p-3 space-y-2">
      <div
        v-for="edge in sortedEdges"
        :key="edge.edge_index"
        class="flex items-center gap-2 text-xs text-slate-200"
      >
        <span class="w-20 text-slate-300">
          Edge {{ edge.edge_index }}
        </span>
        <div class="flex-1 h-2 rounded-full bg-slate-800 overflow-hidden">
          <div
            class="h-full rounded-full bg-sky-400"
            :style="{ width: barWidth(edge) }"
          />
        </div>
        <span class="w-12 text-right text-slate-400">
          {{ edge.visits }}
        </span>
      </div>
    </div>
    <p class="text-xs text-slate-400">
      Edges sorted by total visit count; higher bars indicate more traffic.
    </p>
  </div>
  <div v-else class="text-xs text-slate-400">
    No traffic data available for this run.
  </div>
</template>

<script setup>
import { computed } from "vue";

const props = defineProps({
  trafficEdges: {
    type: Array,
    required: true
  }
});

const hasData = computed(
  () => Array.isArray(props.trafficEdges) && props.trafficEdges.length > 0
);

const sortedEdges = computed(() => {
  if (!hasData.value) return [];

  return [...props.trafficEdges]
    .map((e) => ({
      edge_index: e.edge_index,
      visits: Number(e.visits ?? 0)
    }))
    .filter((e) => Number.isFinite(e.edge_index) && Number.isFinite(e.visits))
    .sort((a, b) => b.visits - a.visits);
});

const maxVisits = computed(() => {
  if (!sortedEdges.value.length) return 0;
  return Math.max(...sortedEdges.value.map((e) => e.visits), 0);
});

function barWidth(edge) {
  if (maxVisits.value <= 0) return "0%";
  const ratio = edge.visits / maxVisits.value;
  return `${Math.max(3, ratio * 100)}%`;
}
</script>

