<template>
  <div class="space-y-3" v-if="hasData">
    <h3 class="text-sm font-semibold text-slate-200">
      Queue length over time
    </h3>
    <div class="rounded-lg border border-slate-800 bg-slate-900/60 p-3">
      <svg
        v-if="pathD"
        :viewBox="`0 0 ${width} ${height}`"
        class="w-full h-40"
        preserveAspectRatio="none"
      >
        <polyline
          :points="pathD"
          fill="none"
          class="stroke-emerald-400"
          stroke-width="1.5"
        />
      </svg>
      <p v-else class="text-xs text-slate-400">
        Not enough queue samples to draw a chart.
      </p>
      <p class="mt-2 text-xs text-slate-400">
        Aggregated queue length across all lanes over simulation time (seconds).
      </p>
    </div>
  </div>
  <div v-else class="text-xs text-slate-400">
    No queue data available for this run.
  </div>
</template>

<script setup>
import { computed } from "vue";

const props = defineProps({
  queueData: {
    type: Array,
    required: true
  }
});

const width = 100;
const height = 40;

const hasData = computed(() => Array.isArray(props.queueData) && props.queueData.length > 0);

const aggregatedSeries = computed(() => {
  if (!hasData.value) return [];

  const byTime = new Map();
  for (const row of props.queueData) {
    const t = Number(row.time_s ?? row.time ?? 0);
    const len = Number(row.queue_length ?? 0);
    if (!Number.isFinite(t)) continue;
    const prev = byTime.get(t) ?? 0;
    byTime.set(t, prev + (Number.isFinite(len) ? len : 0));
  }

  const entries = Array.from(byTime.entries())
    .map(([time, length]) => ({ time, length }))
    .sort((a, b) => a.time - b.time);

  return entries;
});

const pathD = computed(() => {
  const pts = aggregatedSeries.value;
  if (!pts.length) return "";

  const minTime = pts[0].time;
  const maxTime = pts[pts.length - 1].time;
  const maxLen = Math.max(...pts.map((p) => p.length), 0);

  if (!Number.isFinite(maxTime) || maxTime === minTime || maxLen <= 0) {
    return "";
  }

  const scaleX = (t) => ((t - minTime) / (maxTime - minTime)) * width;
  const scaleY = (v) => height - (v / maxLen) * height;

  return pts
    .map((p) => `${scaleX(p.time)},${scaleY(p.length)}`)
    .join(" ");
});
</script>

