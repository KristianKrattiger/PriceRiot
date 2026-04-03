<template>
  <div class="space-y-6" v-if="run">

    <!-- ── Run header ────────────────────────────────────────────────────── -->
    <div class="flex flex-wrap items-center justify-between gap-4">
      <div class="flex items-center gap-4 min-w-0">
        <h2 class="text-[11px] uppercase tracking-label text-ink-ghost font-medium shrink-0">Results</h2>
        <select
          :value="store.currentRunId"
          @change="store.setCurrentRun($event.target.value)"
          class="min-w-0 flex-1 bg-surface-deep border border-rim text-ink text-[11px] font-mono
                 px-2 py-1.5 focus:outline-none focus:border-violet transition-colors"
        >
          <option v-for="r in completedRuns" :key="r.run_id" :value="r.run_id">
            Run #{{ r.run_id }} — {{ formatDate(r.completed_at) }}
          </option>
        </select>
      </div>
      <div class="flex gap-2 shrink-0">
        <button
          class="px-3 py-1.5 text-[11px] font-mono border border-rim text-ink-dim
                 hover:bg-surface-hover hover:text-ink transition-colors"
          @click="download('transactions')"
        >
          Transactions CSV
        </button>
        <button
          class="px-3 py-1.5 text-[11px] font-mono border border-rim text-ink-dim
                 hover:bg-surface-hover hover:text-ink transition-colors"
          @click="download('customers')"
        >
          Customers CSV
        </button>
      </div>
    </div>

    <!-- ── Category filter — underline tab style ─────────────────────────── -->
    <div class="flex gap-4 border-b border-rim">
      <button
        v-for="cat in categories"
        :key="cat.id"
        class="pb-2.5 text-[11px] font-medium uppercase tracking-label transition-colors duration-150 border-b-2 -mb-px"
        :class="activeCategory === cat.id
          ? 'text-green border-green'
          : 'text-ink-ghost border-transparent hover:text-ink-dim'"
        @click="activeCategory = cat.id"
      >
        {{ cat.label }}
      </button>
    </div>

    <!-- ── BASKET METRICS ─────────────────────────────────────────────────── -->
    <section v-show="show('basket')" class="space-y-4">
      <SectionHeader label="Basket" />
      <div class="grid grid-cols-2 md:grid-cols-4 gap-3">
        <KPICard
          label="Avg basket value"
          :value="formatCurrency(run.kpis.avg_basket_value)"
          sub="per transaction"
          :money="true"
          accent="#008A31"
        />
        <KPICard
          label="Avg items per basket"
          :value="formatNumber(run.kpis.avg_items_per_basket, 2)"
          sub="line items"
        />
        <KPICard
          v-if="run.kpis.avg_dwell_time != null"
          label="Avg dwell time"
          :value="formatDuration(run.kpis.avg_dwell_time)"
          sub="in store"
        />
      </div>
      <BasketMetrics :kpis="run.kpis" />
    </section>

    <!-- ── TRAFFIC METRICS ───────────────────────────────────────────────── -->
    <section v-show="show('traffic')" class="space-y-4">
      <SectionHeader label="Traffic" />
      <div class="grid grid-cols-2 md:grid-cols-4 gap-3">
        <KPICard
          label="Total customers"
          :value="formatNumber(run.kpis.total_customers)"
          sub="unique visits"
        />
        <KPICard
          label="Total transactions"
          :value="formatNumber(run.kpis.total_transactions)"
          sub="completed checkouts"
        />
        <KPICard
          v-if="run.kpis.total_traffic_visits != null"
          label="Total cell visits"
          :value="formatNumber(run.kpis.total_traffic_visits)"
          sub="aisle traversals"
        />
        <KPICard
          v-if="run.kpis.busiest_edge_index != null"
          label="Busiest aisle"
          :value="`Edge ${run.kpis.busiest_edge_index}`"
          :sub="`${formatNumber(run.kpis.busiest_edge_visits)} visits`"
        />
      </div>
      <TrafficFlowGraph
        v-if="run.traffic_edges && run.traffic_edges.length"
        :traffic-edges="run.traffic_edges"
      />
    </section>

    <!-- ── QUEUE METRICS ──────────────────────────────────────────────────── -->
    <section v-show="show('queue')" class="space-y-4">
      <SectionHeader label="Queue" />
      <div class="grid grid-cols-2 md:grid-cols-3 gap-3">
        <KPICard
          v-if="run.kpis.mean_queue_length != null"
          label="Mean queue length"
          :value="formatNumber(run.kpis.mean_queue_length, 2)"
          sub="avg across registers"
        />
        <KPICard
          v-if="run.kpis.max_queue_length != null"
          label="Peak queue length"
          :value="formatNumber(run.kpis.max_queue_length)"
          sub="worst case"
        />
        <KPICard
          v-if="run.kpis.p95_queue_length != null"
          label="P95 queue length"
          :value="formatNumber(run.kpis.p95_queue_length, 2)"
          sub="95th percentile"
        />
      </div>
      <QueueCharts
        v-if="run.queue_data && run.queue_data.length"
        :queue-data="run.queue_data"
      />
      <p v-else class="text-[11px] text-ink-ghost">
        Queue data not available for this run.
      </p>
    </section>

    <!-- ── STAFF METRICS ──────────────────────────────────────────────────── -->
    <section v-show="show('staff')" class="space-y-4">
      <SectionHeader label="Staff" />
      <StaffSummary :kpis="run.kpis" />
      <WorkerMoodChart :worker-timeseries="run.worker_timeseries || []" />
    </section>

    <!-- ── PRODUCT METRICS ───────────────────────────────────────────────── -->
    <section v-show="show('products')" class="space-y-4">
      <SectionHeader label="Products" />
      <div class="grid grid-cols-2 md:grid-cols-4 gap-3">
        <KPICard
          v-if="run.kpis.total_revenue != null"
          label="Total revenue"
          :value="formatCurrency(run.kpis.total_revenue)"
          sub="all sales"
          :money="true"
          accent="#008A31"
        />
        <KPICard
          v-if="run.kpis.most_bought_product"
          label="Most purchased"
          :value="run.kpis.most_bought_product"
          sub="by quantity"
        />
        <KPICard
          v-if="run.kpis.highest_revenue_product"
          label="Top revenue SKU"
          :value="run.kpis.highest_revenue_product"
          sub="by revenue"
        />
      </div>
      <SkuBreakdown :sku-breakdown="run.sku_breakdown || []" />
    </section>
  </div>

  <!-- Empty state -->
  <div v-else class="py-16 text-center">
    <p class="text-[11px] uppercase tracking-label text-ink-ghost">No run selected</p>
    <p class="mt-2 text-sm text-ink-ghost/60">Start a simulation from the Run tab.</p>
  </div>
</template>

<script setup>
import { computed, ref } from "vue";
import { useSimulationStore } from "../stores/simulation";
import { downloadCsv } from "../api/client";
import KPICard         from "./KPICard.vue";
import QueueCharts     from "./QueueCharts.vue";
import TrafficFlowGraph from "./TrafficFlowGraph.vue";
import BasketMetrics   from "./BasketMetrics.vue";
import StaffSummary    from "./StaffSummary.vue";
import SkuBreakdown    from "./SkuBreakdown.vue";
import WorkerMoodChart from "./WorkerMoodChart.vue";
import { formatCurrency, formatNumber } from "../utils/format";

// ── Inline section header ─────────────────────────────────────────────────
const SectionHeader = {
  props: ["label"],
  template: `
    <div class="flex items-center gap-4">
      <span class="text-[11px] font-medium uppercase tracking-label text-ink-ghost shrink-0">{{ label }}</span>
      <div class="flex-1 border-t border-rim"></div>
    </div>
  `,
};

const store = useSimulationStore();
const run   = computed(() => store.currentRun);

const completedRuns = computed(() =>
  store.runs.filter((r) => r.status === "completed")
);

function formatDate(iso) {
  if (!iso) return "—";
  return new Date(iso).toLocaleString(undefined, {
    month: "short", day: "numeric",
    hour: "2-digit", minute: "2-digit",
  });
}

const categories = [
  { id: "all",      label: "All"      },
  { id: "basket",   label: "Basket"   },
  { id: "traffic",  label: "Traffic"  },
  { id: "queue",    label: "Queue"    },
  { id: "staff",    label: "Staff"    },
  { id: "products", label: "Products" },
];

const activeCategory = ref("all");

function show(cat) {
  return activeCategory.value === "all" || activeCategory.value === cat;
}

function formatDuration(seconds) {
  if (seconds == null) return "—";
  const s = Math.round(seconds);
  if (s < 60) return `${s}s`;
  const m   = Math.floor(s / 60);
  const rem = s % 60;
  return rem > 0 ? `${m}m ${rem}s` : `${m}m`;
}

function download(kind) {
  if (!run.value) return;
  downloadCsv(run.value.run_id, `${kind}.csv`);
}
</script>
