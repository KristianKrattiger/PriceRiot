<template>
  <div class="space-y-3" v-if="hasData">
    <!-- Header + toggle -->
    <div class="flex items-center justify-between">
      <h3 class="text-[11px] uppercase tracking-label text-ink-ghost font-medium">Queue length over time</h3>
      <div v-if="laneCount > 1" class="flex border border-rim text-[11px] font-mono">
        <button
          class="px-3 py-1 transition-colors"
          :class="viewMode === 'overlay'
            ? 'bg-surface-hover text-ink'
            : 'bg-surface-deep text-ink-ghost hover:text-ink-dim'"
          @click="viewMode = 'overlay'"
        >
          Overlay
        </button>
        <button
          class="px-3 py-1 transition-colors border-l border-rim"
          :class="viewMode === 'separate'
            ? 'bg-surface-hover text-ink'
            : 'bg-surface-deep text-ink-ghost hover:text-ink-dim'"
          @click="viewMode = 'separate'"
        >
          Separate
        </button>
      </div>
    </div>

    <!-- Overlay: single chart with all lanes -->
    <div
      v-if="viewMode === 'overlay'"
      ref="overlayEl"
      class="border border-rim bg-surface-deep w-full overflow-hidden"
      style="height: 220px;"
    />

    <!-- Separate: one chart per lane in a responsive grid -->
    <div v-else class="grid grid-cols-1 sm:grid-cols-2 gap-3">
      <div
        v-for="(_, idx) in laneCount"
        :key="idx"
        :ref="el => { if (el) separateEls[idx] = el }"
        class="border border-rim bg-surface-deep w-full overflow-hidden"
        style="height: 180px;"
      />
    </div>
  </div>
  <div v-else class="text-[11px] text-ink-ghost">
    No queue data available for this run.
  </div>
</template>

<script setup>
import { computed, nextTick, onMounted, onBeforeUnmount, ref, watch } from "vue";
import Plotly from "plotly.js-dist-min";

const props = defineProps({
  queueData: { type: Array, required: true },
});

const viewMode   = ref("overlay");
const overlayEl  = ref(null);
const separateEls = ref([]);

// Neutral teal ramp — no semantic color overlap with UI status indicators
const LANE_COLORS = [
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
  Array.isArray(props.queueData) && props.queueData.length > 0
);

const perLaneSeries = computed(() => {
  const map = new Map();
  for (const row of props.queueData) {
    const lane = Number(row.lane_index ?? 0);
    const t    = Number(row.time_s ?? row.time ?? 0);
    const len  = Number(row.queue_length ?? 0);
    if (!Number.isFinite(t) || !Number.isFinite(len)) continue;
    if (!map.has(lane)) map.set(lane, []);
    map.get(lane).push({ time: t, length: len });
  }
  for (const series of map.values())
    series.sort((a, b) => a.time - b.time);
  return map;
});

const laneCount   = computed(() => perLaneSeries.value.size);
const laneIndices = computed(() =>
  Array.from(perLaneSeries.value.keys()).sort((a, b) => a - b)
);

function baseLayout(title) {
  return {
    paper_bgcolor: "transparent",
    plot_bgcolor:  "transparent",
    margin: { l: 38, r: 10, t: title ? 36 : 28, b: 32 },
    font: { color: "#6A6760", size: 11, family: "IBM Plex Mono, ui-monospace, monospace" },
    title: title
      ? { text: title, font: { size: 11, color: "#6A6760" }, x: 0.02, xanchor: "left" }
      : undefined,
    xaxis: {
      title:     { text: "Time (s)", font: { size: 10, color: "#9A9790" }, standoff: 4 },
      gridcolor: "#D8D4C4",
      linecolor: "#CEC9B6",
      tickcolor: "#CEC9B6",
      tickfont:  { size: 10, color: "#9A9790" },
    },
    yaxis: {
      title:     { text: "Queue length", font: { size: 10, color: "#9A9790" }, standoff: 4 },
      gridcolor: "#D8D4C4",
      linecolor: "#CEC9B6",
      tickcolor: "#CEC9B6",
      tickfont:  { size: 10, color: "#9A9790" },
      rangemode: "tozero",
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
    hovermode: "x unified",
  };
}

const plotlyConfig = { displayModeBar: false, responsive: true };

function buildTrace(laneIdx, series, showLegend = true) {
  const color = LANE_COLORS[laneIdx % LANE_COLORS.length];
  return {
    x:    series.map((p) => p.time),
    y:    series.map((p) => p.length),
    type: "scatter",
    mode: "lines",
    name: `Register ${laneIdx + 1}`,
    line: { color, width: 1.5, shape: "hv" },
    showlegend: showLegend,
    hovertemplate: `<b>Register ${laneIdx + 1}</b><br>Time: %{x:.0f}s<br>Length: %{y}<extra></extra>`,
  };
}

async function drawOverlay() {
  await nextTick();
  if (!overlayEl.value || !hasData.value) return;
  const traces = laneIndices.value.map((laneIdx) =>
    buildTrace(laneIdx, perLaneSeries.value.get(laneIdx) || [], laneCount.value > 1)
  );
  Plotly.react(overlayEl.value, traces, { ...baseLayout(null), showlegend: laneCount.value > 1 }, plotlyConfig);
}

async function drawSeparate() {
  await nextTick();
  if (!hasData.value) return;
  for (let i = 0; i < laneIndices.value.length; i++) {
    const el = separateEls.value[i];
    if (!el) continue;
    const laneIdx = laneIndices.value[i];
    const series  = perLaneSeries.value.get(laneIdx) || [];
    Plotly.react(el, [buildTrace(laneIdx, series, false)], baseLayout(`Register ${laneIdx + 1}`), plotlyConfig);
  }
}

function drawCurrent() {
  if (viewMode.value === "overlay") {
    drawOverlay();
  } else {
    separateEls.value = [];
    drawSeparate();
  }
}

onMounted(() => { if (hasData.value) drawCurrent(); });
watch(() => props.queueData, () => { drawCurrent(); });
watch(viewMode, () => { drawCurrent(); });

onBeforeUnmount(() => {
  if (overlayEl.value) Plotly.purge(overlayEl.value);
  for (const el of separateEls.value) {
    if (el) Plotly.purge(el);
  }
});
</script>
