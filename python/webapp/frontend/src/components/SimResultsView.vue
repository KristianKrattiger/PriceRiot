<template>
  <div class="space-y-6">

    <!-- ── Simulation selector ─────────────────────────────────────────────── -->
    <div class="flex flex-wrap items-center gap-3">
      <h2 class="text-[11px] uppercase tracking-label text-ink-ghost font-medium shrink-0">
        Temporal results
      </h2>
      <select
        v-if="store.simulations.length"
        :value="store.activeSimulation?.sim_id"
        @change="loadSimulation($event.target.value)"
        class="min-w-0 flex-1 bg-surface-deep border border-rim text-ink text-[11px] font-mono
               px-2 py-1.5 focus:outline-none focus:border-violet transition-colors"
      >
        <option v-for="s in store.simulations" :key="s.sim_id" :value="s.sim_id">
          {{ simLabel(s) }}
        </option>
      </select>
      <!-- In-progress indicator -->
      <div v-if="isActive" class="flex items-center gap-1.5 text-[11px] font-mono text-violet">
        <span class="w-1.5 h-1.5 rounded-full bg-violet animate-pulse"></span>
        {{ progressText }}
      </div>
    </div>

    <!-- ── No simulation selected ──────────────────────────────────────────── -->
    <div v-if="!sim" class="py-16 text-center">
      <p class="text-[11px] uppercase tracking-label text-ink-ghost">No simulation selected</p>
      <p class="mt-2 text-sm text-ink-ghost/60">Configure and launch a simulation from the Run tab.</p>
    </div>

    <template v-else>

      <!-- ── Aggregate header ──────────────────────────────────────────────── -->
      <div class="space-y-3">
        <div class="flex items-center gap-4">
          <span class="text-[11px] font-medium uppercase tracking-label text-ink-ghost shrink-0">Aggregate</span>
          <div class="flex-1 border-t border-rim"></div>
          <span
            class="inline-flex items-center px-2 py-0.5 text-[10px] font-mono border"
            :class="statusClass(sim.status)"
          >{{ sim.status }}</span>
        </div>

        <!-- Stat row — core -->
        <div class="grid grid-cols-2 sm:grid-cols-5 gap-3" v-if="agg">
          <div class="bg-surface border border-rim px-3 py-2 space-y-0.5">
            <p class="text-[10px] uppercase tracking-label text-ink-ghost">Transactions</p>
            <p class="text-base font-mono text-ink">{{ fmt(agg.mean_transactions, 1) }}</p>
            <p class="text-[10px] font-mono text-ink-ghost">mean / run</p>
          </div>
          <div class="bg-surface border border-rim px-3 py-2 space-y-0.5">
            <p class="text-[10px] uppercase tracking-label text-ink-ghost">Revenue</p>
            <p class="text-base font-mono text-green">${{ fmt(agg.mean_revenue, 2) }}</p>
            <p class="text-[10px] font-mono text-ink-ghost">mean / run</p>
          </div>
          <div class="bg-surface border border-rim px-3 py-2 space-y-0.5">
            <p class="text-[10px] uppercase tracking-label text-ink-ghost">Customers</p>
            <p class="text-base font-mono text-ink">{{ fmt(agg.mean_customers, 1) }}</p>
            <p class="text-[10px] font-mono text-ink-ghost">mean / run</p>
          </div>
          <div class="bg-surface border border-rim px-3 py-2 space-y-0.5">
            <p class="text-[10px] uppercase tracking-label text-ink-ghost">Peak period</p>
            <p class="text-base font-mono text-ink capitalize">{{ topPeakPeriod }}</p>
            <p class="text-[10px] font-mono text-ink-ghost">most frequent</p>
          </div>
          <div class="bg-surface border border-rim px-3 py-2 space-y-0.5">
            <p class="text-[10px] uppercase tracking-label text-ink-ghost">Wall time</p>
            <p class="text-base font-mono text-ink">{{ formatDuration(sim.elapsed_seconds) }}</p>
            <p class="text-[10px] font-mono text-ink-ghost">{{ agg.n_runs }} run{{ agg.n_runs !== 1 ? "s" : "" }}</p>
          </div>
        </div>

        <!-- Stat row — KPIs derived from per-run means -->
        <div class="grid grid-cols-2 sm:grid-cols-4 gap-3" v-if="aggKpis">
          <div class="bg-surface border border-rim px-3 py-2 space-y-0.5">
            <p class="text-[10px] uppercase tracking-label text-ink-ghost">Avg basket</p>
            <p class="text-base font-mono text-green">${{ fmt(aggKpis.avg_basket_value, 2) }}</p>
            <p class="text-[10px] font-mono text-ink-ghost">mean / run</p>
          </div>
          <div class="bg-surface border border-rim px-3 py-2 space-y-0.5">
            <p class="text-[10px] uppercase tracking-label text-ink-ghost">Items / basket</p>
            <p class="text-base font-mono text-ink">{{ fmt(aggKpis.avg_items_per_basket, 2) }}</p>
            <p class="text-[10px] font-mono text-ink-ghost">mean / run</p>
          </div>
          <div class="bg-surface border border-rim px-3 py-2 space-y-0.5">
            <p class="text-[10px] uppercase tracking-label text-ink-ghost">Mean queue</p>
            <p class="text-base font-mono text-ink">{{ fmt(aggKpis.mean_queue_length, 2) }}</p>
            <p class="text-[10px] font-mono text-ink-ghost">mean / run</p>
          </div>
          <div class="bg-surface border border-rim px-3 py-2 space-y-0.5">
            <p class="text-[10px] uppercase tracking-label text-ink-ghost">Worker efficiency</p>
            <p class="text-base font-mono text-ink">{{ aggKpis.mean_worker_efficiency != null ? (aggKpis.mean_worker_efficiency * 100).toFixed(1) + "%" : "—" }}</p>
            <p class="text-[10px] font-mono text-ink-ghost">mean / run</p>
          </div>
        </div>

        <!-- Partial-result warning -->
        <p v-if="sim.status === 'running'" class="text-[11px] font-mono text-mustard">
          Showing partial results — {{ sim.progress?.completed_runs }}/{{ sim.progress?.total_runs }} runs complete
        </p>
        <p v-if="sim.status === 'failed'" class="text-[11px] font-mono text-danger">
          {{ sim.error_message || "Run failed" }}
        </p>
      </div>

      <!-- ── Transaction volume chart (per-run + mean) ──────────────────────── -->
      <div v-if="chartSeries.length" class="space-y-2">
        <div class="flex items-center gap-4">
          <span class="text-[11px] font-medium uppercase tracking-label text-ink-ghost shrink-0">Volume by run</span>
          <div class="flex-1 border-t border-rim"></div>
        </div>
        <div class="bg-surface border border-rim p-4 overflow-x-auto">
          <svg :viewBox="`0 0 ${chartW} ${chartH}`" class="w-full" style="min-width:320px; height:120px">
            <!-- Grid lines -->
            <line v-for="(v, i) in yTicks" :key="'g'+i"
              :x1="chartPad" :y1="yScale(v)" :x2="chartW - chartPad" :y2="yScale(v)"
              stroke="#CEC9B6" stroke-width="0.5" />
            <!-- Per-run bars -->
            <g v-for="(run, ri) in chartSeries" :key="'r'+ri">
              <rect
                :x="barX(ri)" :y="yScale(run.transactions)"
                :width="barW" :height="chartH - chartPad - yScale(run.transactions)"
                :fill="ri === highlightRun ? '#C9980A44' : '#CEC9B633'"
                :stroke="ri === highlightRun ? '#C9980A' : '#CEC9B6'"
                stroke-width="1"
                class="cursor-pointer"
                @click="drilldown(run.run_index)"
                @mouseenter="highlightRun = ri"
                @mouseleave="highlightRun = null"
              />
              <text
                :x="barX(ri) + barW / 2" :y="yScale(run.transactions) - 4"
                text-anchor="middle" font-size="8" fill="#6A6760" font-family="monospace"
              >{{ run.transactions }}</text>
            </g>
            <!-- Mean line -->
            <line v-if="agg"
              :x1="chartPad" :y1="yScale(agg.mean_transactions)"
              :x2="chartW - chartPad" :y2="yScale(agg.mean_transactions)"
              stroke="#C9980A" stroke-width="1.5" stroke-dasharray="4 3" />
            <!-- X axis labels -->
            <text v-for="(run, ri) in chartSeries" :key="'l'+ri"
              :x="barX(ri) + barW / 2" :y="chartH - 4"
              text-anchor="middle" font-size="8" fill="#9A9790" font-family="monospace"
            >{{ run.run_index }}</text>
          </svg>
          <p class="text-[10px] font-mono text-ink-ghost mt-1">
            — mustard dashes = mean &nbsp;· click bar to inspect run
          </p>
        </div>
      </div>

      <!-- ── Run table ──────────────────────────────────────────────────────── -->
      <div class="space-y-2">
        <div class="flex items-center gap-4">
          <span class="text-[11px] font-medium uppercase tracking-label text-ink-ghost shrink-0">Runs</span>
          <div class="flex-1 border-t border-rim"></div>
        </div>

        <div class="overflow-x-auto">
          <table class="w-full text-[11px] font-mono">
            <thead>
              <tr class="border-b border-rim text-ink-ghost uppercase tracking-label text-[10px]">
                <th class="text-left py-1.5 pr-4 font-medium cursor-pointer" @click="sortBy('run_index')">
                  # <span class="text-ink-ghost/40">{{ sortIndicator('run_index') }}</span>
                </th>
                <th class="text-right py-1.5 pr-4 font-medium cursor-pointer" @click="sortBy('transaction_count')">
                  Txns <span class="text-ink-ghost/40">{{ sortIndicator('transaction_count') }}</span>
                </th>
                <th class="text-right py-1.5 pr-4 font-medium cursor-pointer" @click="sortBy('customer_count')">
                  Customers <span class="text-ink-ghost/40">{{ sortIndicator('customer_count') }}</span>
                </th>
                <th class="text-right py-1.5 pr-4 font-medium cursor-pointer" @click="sortBy('total_revenue')">
                  Revenue <span class="text-ink-ghost/40">{{ sortIndicator('total_revenue') }}</span>
                </th>
                <th class="text-left py-1.5 pr-4 font-medium">Peak</th>
                <th class="text-right py-1.5 font-medium">Duration</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="run in sortedRuns"
                :key="run.run_index"
                class="border-b border-rim/50 cursor-pointer transition-colors duration-100"
                :class="store.drilldownRunIndex === run.run_index
                  ? 'bg-surface-hover border-l-2 border-l-mustard'
                  : 'hover:bg-surface-hover'"
                @click="drilldown(run.run_index)"
              >
                <td class="py-2 pr-4 text-ink">{{ run.run_index }}</td>
                <td class="py-2 pr-4 text-right text-ink">{{ run.transaction_count.toLocaleString() }}</td>
                <td class="py-2 pr-4 text-right text-ink-dim">{{ run.customer_count.toLocaleString() }}</td>
                <td class="py-2 pr-4 text-right text-green">${{ fmt(run.total_revenue, 2) }}</td>
                <td class="py-2 pr-4 capitalize text-ink-dim">{{ run.peak_period }}</td>
                <td class="py-2 text-right text-ink-ghost">{{ formatDuration(run.duration_seconds) }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

    </template>

    <!-- ── Drill-down panel ──────────────────────────────────────────────────── -->
    <transition name="slide">
      <div
        v-if="store.drilldownRunIndex !== null"
        class="fixed top-0 right-0 h-full w-[460px] bg-surface border-l border-rim shadow-xl z-50 flex flex-col"
      >
        <!-- Header -->
        <div class="flex items-center justify-between px-5 py-4 border-b border-rim shrink-0">
          <h3 class="text-[11px] uppercase tracking-label text-rust font-medium">
            Run {{ store.drilldownRunIndex }} — detail
          </h3>
          <button
            class="text-[11px] font-mono text-ink-ghost hover:text-ink transition-colors"
            @click="store.closeDrilldown()"
          >✕ close</button>
        </div>

        <!-- Category tabs -->
        <div class="flex border-b border-rim shrink-0 overflow-x-auto">
          <button
            v-for="cat in drillCategories"
            :key="cat.id"
            class="px-3 py-2 text-[10px] font-medium uppercase tracking-label whitespace-nowrap transition-colors border-b-2 -mb-px"
            :class="drillTab === cat.id
              ? 'text-mustard border-mustard'
              : 'text-ink-ghost border-transparent hover:text-ink-dim'"
            @click="drillTab = cat.id"
          >{{ cat.label }}</button>
        </div>

        <div class="flex-1 overflow-y-auto p-5 space-y-5">
          <div v-if="!store.activeRunDetail" class="text-[11px] text-ink-ghost">Loading…</div>

          <template v-else>
            <!-- ── SUMMARY ── -->
            <template v-if="drillTab === 'summary'">
              <div class="grid grid-cols-2 gap-3">
                <div class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Transactions</p>
                  <p class="text-sm font-mono text-ink">{{ fmt(detail.transaction_count) }}</p>
                </div>
                <div class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Customers</p>
                  <p class="text-sm font-mono text-ink">{{ fmt(detail.customer_count) }}</p>
                </div>
                <div class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Revenue</p>
                  <p class="text-sm font-mono text-green">${{ fmt(detail.total_revenue, 2) }}</p>
                </div>
                <div class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Peak period</p>
                  <p class="text-sm font-mono text-ink capitalize">{{ detail.peak_period || "—" }}</p>
                </div>
              </div>

              <!-- Day breakdown -->
              <div v-if="detail.day_summaries?.length" class="space-y-2">
                <p class="text-[11px] uppercase tracking-label text-ink-ghost">Day breakdown</p>
                <div class="space-y-1 max-h-64 overflow-y-auto">
                  <div
                    v-for="day in detail.day_summaries"
                    :key="day.date"
                    class="flex justify-between items-center px-3 py-1.5 bg-surface-deep border border-rim/60 text-[11px] font-mono"
                  >
                    <span class="text-ink-dim">{{ day.date }}</span>
                    <span class="text-ink">{{ day.transactions }} txns</span>
                    <span class="text-green">${{ fmt(day.revenue, 2) }}</span>
                  </div>
                </div>
              </div>

              <!-- Download -->
              <button
                v-if="detail.transactions_csv_url"
                class="text-[11px] font-mono text-rust hover:text-tangerine transition-colors border border-rim px-3 py-1.5 hover:bg-surface-hover"
                @click="downloadCsv"
              >Download transactions CSV ↓</button>
              <p v-else class="text-[11px] text-ink-ghost">CSV not available</p>
            </template>

            <!-- ── BASKET ── -->
            <template v-if="drillTab === 'basket'">
              <div class="grid grid-cols-2 gap-3">
                <div class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Avg basket value</p>
                  <p class="text-sm font-mono text-green">${{ fmt(kpis.avg_basket_value, 2) }}</p>
                  <p class="text-[10px] text-ink-ghost">per transaction</p>
                </div>
                <div class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Items per basket</p>
                  <p class="text-sm font-mono text-ink">{{ fmt(kpis.avg_items_per_basket, 2) }}</p>
                  <p class="text-[10px] text-ink-ghost">line items</p>
                </div>
                <div v-if="kpis.avg_satisfaction != null" class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Avg satisfaction</p>
                  <p class="text-sm font-mono text-ink">{{ fmt(kpis.avg_satisfaction, 1) }} / 10</p>
                  <p class="text-[10px] text-ink-ghost">customer score</p>
                </div>
                <div class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Total revenue</p>
                  <p class="text-sm font-mono text-green">${{ fmt(kpis.total_revenue, 2) }}</p>
                  <p class="text-[10px] text-ink-ghost">all sales</p>
                </div>
              </div>
            </template>

            <!-- ── PRODUCTS ── -->
            <template v-if="drillTab === 'products'">
              <div class="grid grid-cols-2 gap-3 mb-3">
                <div v-if="kpis.most_bought_product" class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5 col-span-2">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Most purchased</p>
                  <p class="text-sm font-mono text-ink truncate">{{ kpis.most_bought_product }}</p>
                  <p class="text-[10px] text-ink-ghost">by quantity</p>
                </div>
                <div v-if="kpis.highest_revenue_product" class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5 col-span-2">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Top revenue SKU</p>
                  <p class="text-sm font-mono text-ink truncate">{{ kpis.highest_revenue_product }}</p>
                  <p class="text-[10px] text-ink-ghost">by revenue</p>
                </div>
              </div>
              <div v-if="detail.sku_breakdown?.length" class="space-y-1 max-h-80 overflow-y-auto">
                <div class="grid grid-cols-3 text-[10px] uppercase tracking-label text-ink-ghost px-3 py-1 border-b border-rim">
                  <span>Product</span>
                  <span
                    class="text-right cursor-pointer hover:text-ink transition-colors"
                    :class="prodSortKey === 'quantity' ? 'text-ink' : ''"
                    @click="setProdSort('quantity')"
                  >Qty {{ prodSortKey === 'quantity' ? (prodSortDir === 'desc' ? '↓' : '↑') : '' }}</span>
                  <span
                    class="text-right cursor-pointer hover:text-ink transition-colors"
                    :class="prodSortKey === 'revenue' ? 'text-ink' : ''"
                    @click="setProdSort('revenue')"
                  >Revenue {{ prodSortKey === 'revenue' ? (prodSortDir === 'desc' ? '↓' : '↑') : '' }}</span>
                </div>
                <div
                  v-for="row in sortedSkuBreakdown"
                  :key="row.product"
                  class="grid grid-cols-3 text-[11px] font-mono px-3 py-1.5 border-b border-rim/40 hover:bg-surface-hover"
                >
                  <span class="text-ink-dim truncate pr-2">{{ row.product }}</span>
                  <span class="text-right text-ink">{{ row.quantity }}</span>
                  <span class="text-right text-green">${{ fmt(row.revenue, 2) }}</span>
                </div>
              </div>
              <p v-else class="text-[11px] text-ink-ghost">No product data available.</p>
            </template>

            <!-- ── TRAFFIC ── -->
            <template v-if="drillTab === 'traffic'">
              <div class="grid grid-cols-2 gap-3 mb-3">
                <div v-if="kpis.total_traffic_visits != null" class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Total cell visits</p>
                  <p class="text-sm font-mono text-ink">{{ fmt(kpis.total_traffic_visits) }}</p>
                  <p class="text-[10px] text-ink-ghost">aisle traversals</p>
                </div>
                <div v-if="kpis.busiest_edge_index != null" class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Busiest aisle</p>
                  <p class="text-sm font-mono text-ink">Edge {{ kpis.busiest_edge_index }}</p>
                  <p class="text-[10px] text-ink-ghost">{{ fmt(kpis.busiest_edge_visits) }} visits</p>
                </div>
              </div>
              <div v-if="detail.traffic_edges?.length" class="space-y-1 max-h-72 overflow-y-auto">
                <div class="grid grid-cols-2 text-[10px] uppercase tracking-label text-ink-ghost px-3 py-1 border-b border-rim">
                  <span>Edge</span><span class="text-right">Visits</span>
                </div>
                <div
                  v-for="edge in sortedTrafficEdges"
                  :key="edge.edge_index"
                  class="grid grid-cols-2 text-[11px] font-mono px-3 py-1.5 border-b border-rim/40 hover:bg-surface-hover"
                >
                  <span class="text-ink-dim">Edge {{ edge.edge_index }}</span>
                  <span class="text-right text-ink">{{ fmt(edge.total_visits) }}</span>
                </div>
              </div>
              <p v-else class="text-[11px] text-ink-ghost">No traffic data available.</p>
            </template>

            <!-- ── QUEUE ── -->
            <template v-if="drillTab === 'queue'">
              <div class="grid grid-cols-2 gap-3 mb-3">
                <div v-if="kpis.mean_queue_length != null" class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Mean queue</p>
                  <p class="text-sm font-mono text-ink">{{ fmt(kpis.mean_queue_length, 2) }}</p>
                  <p class="text-[10px] text-ink-ghost">avg length</p>
                </div>
                <div v-if="kpis.max_queue_length != null" class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Peak queue</p>
                  <p class="text-sm font-mono text-ink">{{ fmt(kpis.max_queue_length) }}</p>
                  <p class="text-[10px] text-ink-ghost">worst case</p>
                </div>
                <div v-if="kpis.p95_queue_length != null" class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">P95 queue</p>
                  <p class="text-sm font-mono text-ink">{{ fmt(kpis.p95_queue_length, 2) }}</p>
                  <p class="text-[10px] text-ink-ghost">95th pct</p>
                </div>
              </div>
              <QueueCharts :queue-data="detail.queue_data || []" />
            </template>

            <!-- ── STAFF ── -->
            <template v-if="drillTab === 'staff'">
              <div class="grid grid-cols-2 gap-3 mb-3">
                <div v-if="kpis.mean_worker_efficiency != null" class="bg-surface-deep border border-rim px-3 py-2 space-y-0.5 col-span-2">
                  <p class="text-[10px] uppercase tracking-label text-ink-ghost">Mean worker efficiency</p>
                  <p class="text-sm font-mono text-ink">{{ (kpis.mean_worker_efficiency * 100).toFixed(1) }}%</p>
                  <p class="text-[10px] text-ink-ghost">avg across all workers × samples</p>
                </div>
              </div>
              <WorkerMoodChart :worker-timeseries="detail.worker_timeseries || []" />
            </template>
          </template>
        </div>
      </div>
    </transition>

    <!-- Backdrop -->
    <div
      v-if="store.drilldownRunIndex !== null"
      class="fixed inset-0 z-40 bg-ink/10"
      @click="store.closeDrilldown()"
    />
  </div>
</template>

<script setup>
import { computed, ref, watch } from "vue";
import { useTemporalStore } from "../stores/temporal";
import { downloadSimRunCsv } from "../api/client";
import QueueCharts    from "./QueueCharts.vue";
import WorkerMoodChart from "./WorkerMoodChart.vue";

const store = useTemporalStore();

// ── Drilldown category tabs ────────────────────────────────────────────────
const drillTab = ref("summary");
const drillCategories = [
  { id: "summary",  label: "Summary"  },
  { id: "basket",   label: "Basket"   },
  { id: "products", label: "Products" },
  { id: "traffic",  label: "Traffic"  },
  { id: "queue",    label: "Queue"    },
  { id: "staff",    label: "Staff"    },
];

// Reset to summary tab when switching runs
watch(() => store.drilldownRunIndex, () => { drillTab.value = "summary"; });

const detail = computed(() => store.activeRunDetail || {});
const kpis   = computed(() => detail.value.kpis || {});

const sortedTrafficEdges = computed(() =>
  [...(detail.value.traffic_edges || [])].sort((a, b) => b.total_visits - a.total_visits)
);

const sim = computed(() => store.activeSimulation);
const agg = computed(() => sim.value?.aggregate || null);
const isActive = computed(() => sim.value?.status === "queued" || sim.value?.status === "running");

// Aggregate KPIs computed as means across per_runs
const aggKpis = computed(() => {
  const runs = sim.value?.per_runs;
  if (!runs?.length) return null;
  const kpiKeys = ["avg_basket_value", "avg_items_per_basket", "mean_queue_length", "mean_worker_efficiency"];
  const result = {};
  for (const k of kpiKeys) {
    const vals = runs.map((r) => r.kpis?.[k]).filter((v) => v != null && !isNaN(v));
    result[k] = vals.length ? vals.reduce((a, b) => a + b, 0) / vals.length : null;
  }
  return Object.values(result).some((v) => v != null) ? result : null;
});

const progressText = computed(() => {
  const p = sim.value?.progress;
  if (!p) return "Queued";
  return `${p.completed_runs} / ${p.total_runs} runs`;
});

function simLabel(s) {
  const label = s.config?.label ? `${s.config.label} — ` : "";
  const preset = s.config?.preset?.replace(/_/g, " ") || "";
  return `#${s.sim_id} ${label}${preset} · ${s.status}`;
}

function loadSimulation(simId) {
  store.viewSimulation(simId);
  if (sim.value?.status === "complete") {
    store.fetchResults(simId);
  }
}

const topPeakPeriod = computed(() => {
  const votes = agg.value?.peak_period_votes;
  if (!votes || !Object.keys(votes).length) return "—";
  return Object.entries(votes).sort((a, b) => b[1] - a[1])[0][0];
});

function statusClass(s) {
  if (s === "complete")  return "border-tangerine/40 bg-rust/10 text-rust";
  if (s === "running")   return "border-violet/40 bg-violet/10 text-violet";
  if (s === "failed")    return "border-danger/40 bg-danger/10 text-danger";
  return "border-rim text-ink-ghost";
}

function fmt(v, dp = 0) {
  if (v == null) return "—";
  return Number(v).toLocaleString("en-GB", { minimumFractionDigits: dp, maximumFractionDigits: dp });
}

function formatDuration(s) {
  if (s == null) return "—";
  s = Math.round(s);
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60), r = s % 60;
  return r > 0 ? `${m}m ${r}s` : `${m}m`;
}

// ── Chart ──────────────────────────────────────────────────────────────────
const chartW = 600;
const chartH = 120;
const chartPad = 24;
const highlightRun = ref(null);

const chartSeries = computed(() => {
  if (!sim.value?.per_runs?.length) return [];
  return [...sim.value.per_runs].sort((a, b) => a.run_index - b.run_index);
});

const barW = computed(() => {
  const n = chartSeries.value.length;
  if (!n) return 0;
  const avail = chartW - chartPad * 2;
  return Math.max(4, avail / n - 4);
});

function barX(i) {
  const n = chartSeries.value.length;
  const avail = chartW - chartPad * 2;
  const step = avail / n;
  return chartPad + i * step + (step - barW.value) / 2;
}

const yMax = computed(() => {
  const runs = chartSeries.value;
  if (!runs.length) return 100;
  return Math.max(...runs.map((r) => r.transaction_count), 1) * 1.15;
});

const yTicks = computed(() => {
  const m = yMax.value;
  const step = Math.ceil(m / 4 / 10) * 10;
  return [0, step, step * 2, step * 3];
});

function yScale(v) {
  const usable = chartH - chartPad - 10;
  return chartH - chartPad - (v / yMax.value) * usable;
}

// ── Sorting ────────────────────────────────────────────────────────────────
const sortKey  = ref("run_index");
const sortAsc  = ref(true);

function sortBy(key) {
  if (sortKey.value === key) sortAsc.value = !sortAsc.value;
  else { sortKey.value = key; sortAsc.value = true; }
}
function sortIndicator(key) {
  if (sortKey.value !== key) return "";
  return sortAsc.value ? "↑" : "↓";
}

const sortedRuns = computed(() => {
  if (!sim.value?.per_runs) return [];
  return [...sim.value.per_runs].sort((a, b) => {
    const va = a[sortKey.value], vb = b[sortKey.value];
    if (va == null) return 1;
    if (vb == null) return -1;
    return sortAsc.value ? (va > vb ? 1 : -1) : (va < vb ? 1 : -1);
  });
});

// ── Product sort (drilldown) ────────────────────────────────────────────────
const prodSortKey = ref("revenue");
const prodSortDir = ref("desc");

function setProdSort(key) {
  if (prodSortKey.value === key) {
    prodSortDir.value = prodSortDir.value === "desc" ? "asc" : "desc";
  } else {
    prodSortKey.value = key;
    prodSortDir.value = "desc";
  }
}

const sortedSkuBreakdown = computed(() => {
  const rows = detail.value.sku_breakdown;
  if (!rows?.length) return [];
  const dir = prodSortDir.value === "desc" ? -1 : 1;
  return [...rows].sort((a, b) => dir * ((a[prodSortKey.value] ?? 0) - (b[prodSortKey.value] ?? 0)));
});

// ── Drill-down ─────────────────────────────────────────────────────────────
function drilldown(runIndex) {
  if (!sim.value) return;
  store.fetchRunDetail(sim.value.sim_id, runIndex);
}

function downloadCsv() {
  if (!store.activeRunDetail || !sim.value) return;
  downloadSimRunCsv(sim.value.sim_id, store.drilldownRunIndex);
}
</script>

<style scoped>
.slide-enter-active,
.slide-leave-active {
  transition: transform 0.2s ease;
}
.slide-enter-from,
.slide-leave-to {
  transform: translateX(100%);
}
</style>
