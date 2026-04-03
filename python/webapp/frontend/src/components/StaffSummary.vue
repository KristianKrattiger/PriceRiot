<template>
  <div v-if="hasData" class="space-y-3">
    <div class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 gap-3">
      <div class="relative border border-rim bg-surface-deep px-3 py-2.5 pl-4 space-y-0.5">
        <div class="absolute left-0 top-0 bottom-0 w-1 bg-green"></div>
        <p class="text-[11px] uppercase tracking-label text-ink-ghost">Total workers</p>
        <p class="font-mono text-[20px] font-light text-ink leading-none">{{ kpis.total_workers ?? "—" }}</p>
      </div>
      <div class="relative border border-rim bg-surface-deep px-3 py-2.5 pl-4 space-y-0.5">
        <div class="absolute left-0 top-0 bottom-0 w-1 bg-green/40"></div>
        <p class="text-[11px] uppercase tracking-label text-ink-ghost">Stockers</p>
        <p class="font-mono text-[20px] font-light text-ink leading-none">{{ kpis.stocker_count ?? "—" }}</p>
      </div>
      <div class="relative border border-rim bg-surface-deep px-3 py-2.5 pl-4 space-y-0.5">
        <div class="absolute left-0 top-0 bottom-0 w-1 bg-green/40"></div>
        <p class="text-[11px] uppercase tracking-label text-ink-ghost">Cashiers</p>
        <p class="font-mono text-[20px] font-light text-ink leading-none">{{ kpis.cashier_count ?? "—" }}</p>
      </div>
      <div class="relative border border-rim bg-surface-deep px-3 py-2.5 pl-4 space-y-0.5">
        <div class="absolute left-0 top-0 bottom-0 w-1" :class="efficiencyAccent"></div>
        <p class="text-[11px] uppercase tracking-label text-ink-ghost">Avg efficiency</p>
        <p class="font-mono text-[20px] font-light leading-none" :class="efficiencyColor">
          {{ kpis.avg_efficiency != null ? formatPct(kpis.avg_efficiency) : "—" }}
        </p>
      </div>
    </div>
  </div>
  <p v-else class="text-[11px] text-ink-ghost">No staff data available for this run.</p>
</template>

<script setup>
import { computed } from "vue";

const props = defineProps({ kpis: { type: Object, required: true } });

const hasData = computed(() => props.kpis.total_workers != null);

function formatPct(v) {
  return `${(v * 100).toFixed(0)}%`;
}

// RAG thresholds: green ≥80%, yellow 60–79%, red <60%
function scoreColor(v) {
  if (v == null) return "text-ink";
  if (v >= 0.80) return "text-green";
  if (v >= 0.60) return "text-mustard";
  return "text-danger";
}

function scoreAccent(v) {
  if (v == null) return "bg-rim";
  if (v >= 0.80) return "bg-green";
  if (v >= 0.60) return "bg-mustard";
  return "bg-danger";
}

const efficiencyColor  = computed(() => scoreColor(props.kpis.avg_efficiency));
const efficiencyAccent = computed(() => scoreAccent(props.kpis.avg_efficiency));
</script>
