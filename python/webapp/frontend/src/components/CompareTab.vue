<template>
  <div class="space-y-4">
    <h2 class="text-lg font-semibold text-slate-100">
      Compare runs
    </h2>

    <div class="space-y-2">
      <p class="text-sm text-slate-300">
        Select 2–5 completed runs to compare their KPIs.
      </p>
      <div class="max-h-56 overflow-y-auto space-y-1">
        <label
          v-for="run in completedRuns"
          :key="run.run_id"
          class="flex items-center gap-2 text-sm text-slate-200"
        >
          <input
            type="checkbox"
            class="rounded border-slate-600 bg-slate-900"
            :value="run.run_id"
            v-model="selectedIds"
          />
          <span>Run #{{ run.run_id }}</span>
        </label>
      </div>
      <button
        class="mt-2 px-3 py-2 rounded-md text-xs font-medium bg-emerald-500 text-slate-950 hover:bg-emerald-400 disabled:opacity-50"
        :disabled="selectedIds.length < 2"
        @click="doCompare"
      >
        Compare ({{ selectedIds.length }})
      </button>
    </div>

    <div v-if="comparison" class="space-y-4">
      <h3 class="text-md font-semibold text-slate-100">
        KPI comparison
      </h3>
      <div class="overflow-x-auto text-sm">
        <table class="min-w-full border border-slate-800 text-left">
          <thead class="bg-slate-900/70">
            <tr>
              <th class="px-3 py-2 border-b border-slate-800">Metric</th>
              <th
                v-for="run in comparison.runs"
                :key="run.run_id"
                class="px-3 py-2 border-b border-slate-800"
              >
                Run #{{ run.run_id }}
              </th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="metric in metrics" :key="metric.key">
              <td class="px-3 py-2 border-b border-slate-800 text-slate-300">
                {{ metric.label }}
              </td>
              <td
                v-for="run in comparison.runs"
                :key="run.run_id + metric.key"
                class="px-3 py-2 border-b border-slate-800"
              >
                {{ formatMetric(run.run_id, metric.key, metric.type) }}
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, ref } from "vue";
import { useSimulationStore } from "../stores/simulation";
import { compareRuns } from "../api/client";
import { formatCurrency, formatNumber } from "../utils/format";

const store = useSimulationStore();

const selectedIds = ref([]);
const comparison = ref(null);

const completedRuns = computed(() =>
  store.runs.filter((r) => r.status === "completed")
);

const metrics = [
  { key: "total_customers", label: "Total customers", type: "number" },
  { key: "total_transactions", label: "Total transactions", type: "number" },
  { key: "avg_basket_value", label: "Avg basket value", type: "currency" },
  { key: "avg_items_per_basket", label: "Avg items per basket", type: "number2" }
];

async function doCompare() {
  if (selectedIds.value.length < 2) return;
  const data = await compareRuns(selectedIds.value);
  comparison.value = data;
}

function formatMetric(runId, key, type) {
  if (!comparison.value) return "";
  const kpis = comparison.value.kpi_comparison[runId] || {};
  const value = kpis[key];
  if (value == null) return "-";
  if (type === "currency") return formatCurrency(value);
  if (type === "number2") return formatNumber(value, 2);
  return formatNumber(value);
}
</script>

