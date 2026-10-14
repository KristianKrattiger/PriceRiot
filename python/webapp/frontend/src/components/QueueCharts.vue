<template>
  <div class="space-y-3" v-if="hasData">
    <!-- Header + toggles -->
    <div class="flex items-center justify-between gap-2 flex-wrap">
      <h3 class="text-[11px] uppercase tracking-label text-ink-ghost font-medium">Queue length over time</h3>
      <div class="flex items-center gap-2">
        <!-- Day selector (multi-day runs only) -->
        <div v-if="uniqueDates.length > 1" class="flex border border-rim text-[11px] font-mono">
          <button
            class="px-3 py-1 transition-colors"
            :class="dayMode === 'all'
              ? 'bg-surface-hover text-ink'
              : 'bg-surface-deep text-ink-ghost hover:text-ink-dim'"
            @click="dayMode = 'all'"
          >All</button>
          <button
            class="px-3 py-1 transition-colors border-l border-rim"
            :class="dayMode === 'by-day'
              ? 'bg-surface-hover text-ink'
              : 'bg-surface-deep text-ink-ghost hover:text-ink-dim'"
            @click="dayMode = 'by-day'"
          >By day</button>
        </div>
        <!-- Lane view toggle -->
        <div v-if="laneCount > 1" class="flex border border-rim text-[11px] font-mono">
          <button
            class="px-3 py-1 transition-colors"
            :class="viewMode === 'overlay'
              ? 'bg-surface-hover text-ink'
              : 'bg-surface-deep text-ink-ghost hover:text-ink-dim'"
            @click="viewMode = 'overlay'"
          >Overlay</button>
          <button
            class="px-3 py-1 transition-colors border-l border-rim"
            :class="viewMode === 'separate'
              ? 'bg-surface-hover text-ink'
              : 'bg-surface-deep text-ink-ghost hover:text-ink-dim'"
            @click="viewMode = 'separate'"
          >Separate</button>
        </div>
      </div>
    </div>

    <!-- By-day mode: one chart per day, lanes overlaid -->
    <div v-if="dayMode === 'by-day'" class="grid grid-cols-1 sm:grid-cols-2 gap-3">
      <div
        v-for="(date, di) in uniqueDates"
        :key="date"
        :ref="el => { if (el) dayEls[di] = el }"
        class="border border-rim bg-surface-deep w-full overflow-hidden"
        style="height: 200px;"
      />
    </div>

    <!-- Overlay: single chart with all lanes, all days -->
    <div
      v-else-if="viewMode === 'overlay'"
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
const dayMode    = ref("all");
const overlayEl  = ref(null);
const separateEls = ref([]);
const dayEls      = ref([]);

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

/** Sorted unique sim_date strings present in the data. */
const uniqueDates = computed(() => {
  const s = new Set();
  for (const row of props.queueData) {
    if (row.sim_date) s.add(row.sim_date);
  }
  return Array.from(s).sort();
});

/** Map: lane_index → sorted [{time, length}] across all days. */
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

/** Map: sim_date → Map(lane_index → [{time, length}]) — time_s reset relative to day start. */
const perDateLaneSeries = computed(() => {
  // group by date then lane; normalise time_s to 0-based within the day
  const byDate = new Map();
  for (const date of uniqueDates.value) byDate.set(date, new Map());

  for (const row of props.queueData) {
    const date = row.sim_date;
    if (!date || !byDate.has(date)) continue;
    const lane = Number(row.lane_index ?? 0);
    const t    = Number(row.time_s ?? row.time ?? 0);
    const len  = Number(row.queue_length ?? 0);
    if (!Number.isFinite(t) || !Number.isFinite(len)) continue;
    const laneMap = byDate.get(date);
    if (!laneMap.has(lane)) laneMap.set(lane, []);
    laneMap.get(lane).push({ time: t, length: len });
  }

  // Normalise times per date so x-axis shows seconds-into-day
  for (const laneMap of byDate.values()) {
    for (const series of laneMap.values()) {
      series.sort((a, b) => a.time - b.time);
      const t0 = series[0]?.time ?? 0;
      for (const p of series) p.time -= t0;
    }
  }
  return byDate;
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

async function drawByDay() {
  await nextTick();
  if (!hasData.value) return;
  for (let di = 0; di < uniqueDates.value.length; di++) {
    const el = dayEls.value[di];
    if (!el) continue;
    const date    = uniqueDates.value[di];
    const laneMap = perDateLaneSeries.value.get(date) || new Map();
    const lanes   = Array.from(laneMap.keys()).sort((a, b) => a - b);
    const traces  = lanes.map((laneIdx) =>
      buildTrace(laneIdx, laneMap.get(laneIdx) || [], lanes.length > 1)
    );
    Plotly.react(el, traces, {
      ...baseLayout(date),
      showlegend: lanes.length > 1,
    }, plotlyConfig);
  }
}

function drawCurrent() {
  if (dayMode.value === "by-day") {
    dayEls.value = [];
    drawByDay();
  } else if (viewMode.value === "overlay") {
    drawOverlay();
  } else {
    separateEls.value = [];
    drawSeparate();
  }
}

onMounted(() => { if (hasData.value) drawCurrent(); });
watch(() => props.queueData, () => { drawCurrent(); });
watch(viewMode, () => { drawCurrent(); });
watch(dayMode,  () => { drawCurrent(); });

onBeforeUnmount(() => {
  if (overlayEl.value) Plotly.purge(overlayEl.value);
  for (const el of separateEls.value) {
    if (el) Plotly.purge(el);
  }
  for (const el of dayEls.value) {
    if (el) Plotly.purge(el);
  }
});
</script>
