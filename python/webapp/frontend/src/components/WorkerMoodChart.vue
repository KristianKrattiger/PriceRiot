<template>
  <div v-if="hasData" class="space-y-3">
    <h3 class="text-[11px] uppercase tracking-label text-ink-ghost font-medium">Worker efficiency over time</h3>

    <div
      ref="chartEl"
      class="border border-rim bg-surface-deep w-full overflow-hidden"
      style="height: 220px;"
    />
  </div>
  <p v-else class="text-[11px] text-ink-ghost">No worker efficiency data available for this run.</p>
</template>

<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from "vue";
import Plotly from "plotly.js-dist-min";

const props = defineProps({
  workerTimeseries: { type: Array, default: () => [] },
});

const chartEl = ref(null);

// Neutral teal ramp — no semantic color overlap with UI status indicators
const WORKER_COLORS = [
  "#0A3B4D", // deep-teal
  "#1E7A8C", // teal-mid
  "#3D5A7A", // slate-blue
  "#2DA8B8", // teal-light
  "#5C7FA8", // steel-blue
  "#195F6B", // teal-dark-2
  "#4A8FA8", // teal-steel
  "#8AA8C0", // pale-steel
];

const hasData = computed(() =>
  Array.isArray(props.workerTimeseries) && props.workerTimeseries.length > 0
);

const perWorkerSeries = computed(() => {
  const map = new Map();
  for (const row of (props.workerTimeseries || [])) {
    const wid = row.worker_id ?? 0;
    if (!map.has(wid)) map.set(wid, []);
    map.get(wid).push(row);
  }
  for (const series of map.values())
    series.sort((a, b) => a.time - b.time);
  return map;
});

const workerIds = computed(() =>
  Array.from(perWorkerSeries.value.keys()).sort((a, b) => a - b)
);

function buildLayout() {
  return {
    paper_bgcolor: "transparent",
    plot_bgcolor:  "transparent",
    margin: { l: 42, r: 10, t: 28, b: 32 },
    font: { color: "#6A6760", size: 11, family: "IBM Plex Mono, ui-monospace, monospace" },
    xaxis: {
      title:     { text: "Time (s)", font: { size: 10, color: "#9A9790" }, standoff: 4 },
      gridcolor: "#D8D4C4",
      linecolor: "#CEC9B6",
      tickcolor: "#CEC9B6",
      tickfont:  { size: 10, color: "#9A9790" },
    },
    yaxis: {
      title:     { text: "Efficiency", font: { size: 10, color: "#9A9790" }, standoff: 4 },
      gridcolor: "#D8D4C4",
      linecolor: "#CEC9B6",
      tickcolor: "#CEC9B6",
      tickfont:  { size: 10, color: "#9A9790" },
      range: [0, 1.3],
    },
    legend: {
      bgcolor:     "rgba(240,237,227,0.95)",
      bordercolor: "#CEC9B6",
      borderwidth: 1,
      font:        { size: 10, color: "#6A6760" },
      orientation: "h",
      x: 0.5, xanchor: "center",
      y: 1.0, yanchor: "bottom",
    },
    showlegend: workerIds.value.length > 1,
    hovermode:  "x unified",
  };
}

const plotlyConfig = { displayModeBar: false, responsive: true };

async function draw() {
  await nextTick();
  if (!chartEl.value || !hasData.value) return;
  const traces = workerIds.value.map((wid, i) => {
    const series = perWorkerSeries.value.get(wid) || [];
    const color  = WORKER_COLORS[i % WORKER_COLORS.length];
    return {
      x:    series.map((r) => r.time),
      y:    series.map((r) => r.task_efficiency),
      type: "scatter",
      mode: "lines",
      name: `Worker ${wid}`,
      line: { color, width: 1.5, shape: "spline" },
      hovertemplate: `<b>Worker ${wid}</b><br>Time: %{x:.0f}s<br>Efficiency: %{y:.3f}<extra></extra>`,
    };
  });
  Plotly.react(chartEl.value, traces, buildLayout(), plotlyConfig);
}

onMounted(() => { if (hasData.value) draw(); });
watch(() => props.workerTimeseries, draw);
onBeforeUnmount(() => { if (chartEl.value) Plotly.purge(chartEl.value); });
</script>
