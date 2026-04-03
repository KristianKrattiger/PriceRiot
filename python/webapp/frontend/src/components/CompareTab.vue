<template>
  <div class="space-y-6">
    <h2 class="text-[11px] uppercase tracking-label text-ink-ghost font-medium">Compare simulations</h2>

    <!-- Simulation selector -->
    <div class="space-y-3">
      <p class="text-sm text-ink-dim">Select 2–5 completed simulations to compare their aggregate KPIs.</p>

      <div class="max-h-48 overflow-y-auto border border-rim bg-surface-deep divide-y divide-rim">
        <label
          v-for="sim in completedSims"
          :key="sim.sim_id"
          class="flex items-center gap-3 px-3 py-2 cursor-pointer hover:bg-surface-hover transition-colors"
        >
          <input
            type="checkbox"
            class="h-3.5 w-3.5"
            :value="sim.sim_id"
            :disabled="!selectedIds.includes(sim.sim_id) && selectedIds.length >= 5"
            v-model="selectedIds"
          />
          <span class="font-mono text-[11px] text-ink min-w-0 truncate">
            {{ sim.config?.label || "Simulation #" + sim.sim_id }}
            <span class="text-ink-ghost ml-1">· {{ simDescription(sim) }}</span>
          </span>
        </label>
        <p v-if="completedSims.length === 0" class="px-3 py-3 text-[11px] text-ink-ghost">
          No completed simulations yet.
        </p>
      </div>

      <div class="flex items-center gap-4">
        <button
          class="px-4 py-2 text-sm font-medium bg-green text-background
                 hover:bg-green/90 disabled:opacity-40 transition-colors"
          :disabled="selectedIds.length < 2"
          @click="doCompare"
        >
          Compare ({{ selectedIds.length }})
        </button>
        <button
          v-if="comparison"
          class="px-3 py-2 text-[11px] font-mono text-ink-ghost border border-rim hover:bg-surface-hover transition-colors"
          @click="comparison = null; selectedIds = []"
        >Clear</button>
      </div>
    </div>

    <!-- ── Comparison results ────────────────────────────────────────────── -->
    <div v-if="comparison" class="space-y-8">

      <!-- Numeric KPIs -->
      <div class="space-y-2">
        <p class="text-[11px] uppercase tracking-label text-ink-ghost font-medium">Numeric metrics</p>
        <div class="border border-rim overflow-x-auto">
          <table class="min-w-full text-xs">
            <thead class="bg-surface border-b border-rim">
              <tr>
                <th class="px-3 py-2.5 text-left text-[11px] uppercase tracking-label text-ink-ghost font-medium">
                  Metric
                </th>
                <th
                  v-for="sim in comparison.sims"
                  :key="sim.sim_id"
                  class="px-3 py-2.5 text-left text-[11px] uppercase tracking-label text-ink font-mono font-medium"
                >
                  {{ sim.label }}
                </th>
              </tr>
            </thead>
            <tbody class="bg-surface-deep divide-y divide-rim">
              <tr
                v-for="metric in numericMetrics"
                :key="metric.key"
                class="hover:bg-surface-hover transition-colors"
              >
                <td class="px-3 py-2 text-ink-dim">{{ metric.label }}</td>
                <td
                  v-for="sim in comparison.sims"
                  :key="sim.sim_id + metric.key"
                  class="px-3 py-2 font-mono"
                  :class="bestClass(sim.sim_id, metric)"
                >
                  {{ formatMetric(sim.sim_id, metric.key, metric.type) }}
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

      <!-- Categorical KPIs -->
      <div class="space-y-2">
        <p class="text-[11px] uppercase tracking-label text-ink-ghost font-medium">Categorical metrics</p>
        <div class="border border-rim overflow-x-auto">
          <table class="min-w-full text-xs">
            <thead class="bg-surface border-b border-rim">
              <tr>
                <th class="px-3 py-2.5 text-left text-[11px] uppercase tracking-label text-ink-ghost font-medium">
                  Metric
                </th>
                <th
                  v-for="sim in comparison.sims"
                  :key="sim.sim_id"
                  class="px-3 py-2.5 text-left text-[11px] uppercase tracking-label text-ink font-mono font-medium"
                >
                  {{ sim.label }}
                </th>
              </tr>
            </thead>
            <tbody class="bg-surface-deep divide-y divide-rim">
              <tr
                v-for="metric in categoricalMetrics"
                :key="metric.key"
                class="hover:bg-surface-hover transition-colors"
              >
                <td class="px-3 py-2 text-ink-dim">{{ metric.label }}</td>
                <td
                  v-for="sim in comparison.sims"
                  :key="sim.sim_id + metric.key"
                  class="px-3 py-2 font-mono text-ink"
                >
                  {{ getCategorical(sim.sim_id, metric.key) }}
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

    </div>
  </div>
</template>

<script setup>
import { computed, ref } from "vue";
import { useTemporalStore } from "../stores/temporal";
import { formatCurrency, formatNumber } from "../utils/format";

const store = useTemporalStore();

const selectedIds = ref([]);
const comparison  = ref(null);

const completedSims = computed(() =>
  store.simulations.filter((s) => s.status === "complete")
);

function simDescription(sim) {
  const preset = sim.config?.preset?.replace(/_/g, " ") || "";
  const runs   = sim.config?.runs;
  const nRuns  = sim.aggregate?.n_runs ?? runs;
  return [preset, nRuns != null ? `${nRuns} run${nRuns !== 1 ? "s" : ""}` : null]
    .filter(Boolean).join(" · ");
}

const numericMetrics = [
  { key: "mean_transactions",    label: "Mean transactions",      type: "number1",  higherIsBetter: true  },
  { key: "mean_revenue",         label: "Mean revenue",           type: "currency", higherIsBetter: true  },
  { key: "mean_customers",       label: "Mean customers",         type: "number1",  higherIsBetter: true  },
  { key: "avg_basket_value",     label: "Avg basket value",       type: "currency", higherIsBetter: true  },
  { key: "avg_items_per_basket", label: "Avg items per basket",   type: "number2",  higherIsBetter: true  },
  { key: "avg_dwell_time",       label: "Avg dwell time (s)",     type: "number2",  higherIsBetter: false },
  { key: "mean_queue_length",    label: "Mean queue length",      type: "number2",  higherIsBetter: false },
  { key: "max_queue_length",     label: "Peak queue length",      type: "number1",  higherIsBetter: false },
  { key: "mean_worker_efficiency", label: "Worker efficiency",    type: "percent",  higherIsBetter: true  },
];

const categoricalMetrics = [
  { key: "peak_period",         label: "Peak period"     },
  { key: "most_bought_product", label: "Most bought SKU" },
];

// ── Build comparison data client-side ──────────────────────────────────────
function _mean(values) {
  const valid = values.filter((v) => v != null && !isNaN(v));
  if (!valid.length) return null;
  return valid.reduce((a, b) => a + b, 0) / valid.length;
}

function _mostCommon(values) {
  if (!values.length) return null;
  const counts = {};
  for (const v of values) counts[v] = (counts[v] || 0) + 1;
  return Object.entries(counts).sort((a, b) => b[1] - a[1])[0][0];
}

function _buildKpis(sim) {
  const agg      = sim.aggregate || {};
  const perRuns  = sim.per_runs  || [];

  // From aggregate (already computed server-side)
  const kpis = {
    mean_transactions: agg.mean_transactions ?? null,
    mean_revenue:      agg.mean_revenue      ?? null,
    mean_customers:    agg.mean_customers    ?? null,
  };

  // Average per-run KPI fields
  const kpiKeys = [
    "avg_basket_value", "avg_items_per_basket", "avg_dwell_time",
    "mean_queue_length", "max_queue_length", "mean_worker_efficiency",
  ];
  for (const k of kpiKeys) {
    kpis[k] = _mean(perRuns.map((r) => r.kpis?.[k] ?? null));
  }

  // Categorical
  kpis.peak_period         = _mostCommon(perRuns.map((r) => r.peak_period).filter(Boolean))
                           || (_topVote(agg.peak_period_votes));
  kpis.most_bought_product = _mostCommon(
    perRuns.map((r) => r.kpis?.most_bought_product).filter(Boolean)
  );

  return kpis;
}

function _topVote(votes) {
  if (!votes || !Object.keys(votes).length) return null;
  return Object.entries(votes).sort((a, b) => b[1] - a[1])[0][0];
}

function doCompare() {
  if (selectedIds.value.length < 2) return;
  const sims = selectedIds.value.map((id) => {
    const sim = store.simulations.find((s) => s.sim_id === id);
    return {
      sim_id: id,
      label: sim?.config?.label ? `${sim.config.label}` : `#${id}`,
      kpis:  _buildKpis(sim),
    };
  });

  const kpiMap = {};
  for (const s of sims) kpiMap[s.sim_id] = s.kpis;

  comparison.value = { sims, kpiMap };
}

// ── Formatting ─────────────────────────────────────────────────────────────
function getKpiValue(simId, key) {
  return comparison.value?.kpiMap[simId]?.[key] ?? null;
}

function formatMetric(simId, key, type) {
  const value = getKpiValue(simId, key);
  if (value == null) return "—";
  if (type === "currency") return formatCurrency(value);
  if (type === "number2")  return formatNumber(value, 2);
  if (type === "number1")  return formatNumber(value, 1);
  if (type === "percent")  return (value * 100).toFixed(1) + "%";
  return formatNumber(value);
}

function getCategorical(simId, key) {
  const value = getKpiValue(simId, key);
  return value != null ? String(value) : "—";
}

function bestClass(simId, metric) {
  if (metric.higherIsBetter == null || !comparison.value) return "text-ink";
  const allValues = comparison.value.sims
    .map((s) => getKpiValue(s.sim_id, metric.key))
    .filter((v) => v != null);
  if (allValues.length < 2) return "text-ink";
  const best = metric.higherIsBetter ? Math.max(...allValues) : Math.min(...allValues);
  const mine = getKpiValue(simId, metric.key);
  return mine === best ? "text-green font-semibold" : "text-ink";
}
</script>
