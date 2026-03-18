<template>
  <div class="space-y-4" v-if="run">
    <h2 class="text-lg font-semibold text-slate-100">
      Results for run #{{ run.run_id }}
    </h2>

    <div class="grid grid-cols-2 md:grid-cols-4 gap-3">
      <KPICard label="Total customers" :value="formatNumber(run.kpis.total_customers)" />
      <KPICard label="Total transactions" :value="formatNumber(run.kpis.total_transactions)" />
      <KPICard label="Avg basket value" :value="formatCurrency(run.kpis.avg_basket_value)" />
      <KPICard label="Avg items per basket" :value="formatNumber(run.kpis.avg_items_per_basket, 2)" />
    </div>

    <div class="flex gap-3">
      <button
        class="px-3 py-2 rounded-md text-xs font-medium bg-slate-800 text-slate-100 border border-slate-700 hover:bg-slate-700"
        @click="download('transactions')"
      >
        Download transactions CSV
      </button>
      <button
        class="px-3 py-2 rounded-md text-xs font-medium bg-slate-800 text-slate-100 border border-slate-700 hover:bg-slate-700"
        @click="download('customers')"
      >
        Download customers CSV
      </button>
    </div>

    <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 mt-4">
      <QueueCharts
        v-if="run.queue_data && run.queue_data.length"
        :queue-data="run.queue_data"
      />
      <div v-else class="text-xs text-slate-400">
        Queue data will appear here once available for this run.
      </div>

      <TrafficFlowGraph
        v-if="run.traffic_edges && run.traffic_edges.length"
        :traffic-edges="run.traffic_edges"
      />
      <div v-else class="text-xs text-slate-400">
        Traffic data will appear here once available for this run.
      </div>
    </div>
  </div>
  <div v-else class="text-sm text-slate-400">
    Select a run from the Run tab to see results.
  </div>
</template>

<script setup>
import { computed } from "vue";
import { useSimulationStore } from "../stores/simulation";
import { downloadCsv } from "../api/client";
import KPICard from "./KPICard.vue";
import QueueCharts from "./QueueCharts.vue";
import TrafficFlowGraph from "./TrafficFlowGraph.vue";
import { formatCurrency, formatNumber } from "../utils/format";

const store = useSimulationStore();

const run = computed(() => store.currentRun);

function download(kind) {
  if (!run.value) return;
  downloadCsv(run.value.run_id, `${kind}.csv`);
}
</script>

