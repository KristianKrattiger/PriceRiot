<template>
  <div v-if="rows.length" class="space-y-2">
    <!-- Sort controls -->
    <div class="flex items-center gap-3 text-[11px] text-ink-ghost">
      <span class="uppercase tracking-label">Sort by</span>
      <div class="flex gap-1">
        <button
          v-for="col in sortCols"
          :key="col.key"
          class="px-2 py-0.5 font-mono transition-colors"
          :class="sortKey === col.key
            ? 'text-green'
            : 'text-ink-ghost hover:text-ink-dim'"
          @click="setSort(col.key)"
        >
          {{ col.label }}
          <span v-if="sortKey === col.key">{{ sortDir === 'desc' ? '↓' : '↑' }}</span>
        </button>
      </div>
    </div>

    <!-- Table -->
    <div class="border border-rim overflow-hidden">
      <table class="w-full text-xs">
        <thead>
          <tr class="bg-surface text-ink-ghost text-left border-b border-rim">
            <th class="px-3 py-2 font-medium text-[11px] uppercase tracking-label">SKU</th>
            <th class="px-3 py-2 font-medium text-[11px] uppercase tracking-label">Name</th>
            <th class="px-3 py-2 font-medium text-[11px] uppercase tracking-label text-right">Qty sold</th>
            <th class="px-3 py-2 font-medium text-[11px] uppercase tracking-label text-right">Revenue</th>
            <th class="px-3 py-2 font-medium text-[11px] uppercase tracking-label w-28">Share</th>
          </tr>
        </thead>
        <tbody class="bg-surface-deep">
          <tr
            v-for="row in sorted"
            :key="row.sku"
            class="border-t border-rim hover:bg-surface-hover transition-colors"
          >
            <td class="px-3 py-2 font-mono text-ink-dim">{{ row.sku }}</td>
            <td class="px-3 py-2 text-ink max-w-[180px] truncate" :title="row.name">{{ row.name }}</td>
            <td class="px-3 py-2 text-right font-mono text-ink-dim">{{ formatQty(row.quantity) }}</td>
            <td class="px-3 py-2 text-right font-mono text-green font-medium">{{ formatCurrency(row.revenue) }}</td>
            <td class="px-3 py-2">
              <div class="flex items-center gap-1.5">
                <div class="flex-1 h-px bg-rim overflow-hidden relative">
                  <div
                    class="absolute inset-y-0 left-0 h-full bg-green"
                    :style="{ width: `${share(row.revenue)}%` }"
                  />
                </div>
                <span class="font-mono text-ink-ghost w-10 text-right text-[10px]">
                  {{ share(row.revenue).toFixed(1) }}%
                </span>
              </div>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
  <p v-else class="text-[11px] text-ink-ghost">No product data available for this run.</p>
</template>

<script setup>
import { computed, ref } from "vue";
import { formatCurrency, formatNumber } from "../utils/format";

const props = defineProps({
  skuBreakdown: { type: Array, default: () => [] },
});

const sortKey = ref("revenue");
const sortDir = ref("desc");

const sortCols = [
  { key: "revenue",  label: "Revenue" },
  { key: "quantity", label: "Qty"     },
  { key: "name",     label: "Name"    },
];

function setSort(key) {
  if (sortKey.value === key) {
    sortDir.value = sortDir.value === "desc" ? "asc" : "desc";
  } else {
    sortKey.value = key;
    sortDir.value = key === "name" ? "asc" : "desc";
  }
}

const rows = computed(() =>
  Array.isArray(props.skuBreakdown) ? props.skuBreakdown : []
);

const totalRevenue = computed(() =>
  rows.value.reduce((sum, r) => sum + (r.revenue || 0), 0)
);

const sorted = computed(() => {
  const copy = [...rows.value];
  const dir  = sortDir.value === "desc" ? -1 : 1;
  copy.sort((a, b) => {
    const av = a[sortKey.value];
    const bv = b[sortKey.value];
    if (typeof av === "string") return dir * av.localeCompare(bv);
    return dir * (av - bv);
  });
  return copy;
});

function share(revenue) {
  if (!totalRevenue.value) return 0;
  return (revenue / totalRevenue.value) * 100;
}

function formatQty(qty) {
  return formatNumber(qty, qty % 1 === 0 ? 0 : 1);
}
</script>
