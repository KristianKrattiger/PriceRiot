<template>
  <div class="space-y-4">
    <!-- Top SKU highlight cards -->
    <div class="grid grid-cols-1 sm:grid-cols-2 gap-3" v-if="hasSku">
      <div class="relative border border-rim bg-surface-deep px-4 py-3 pl-5 space-y-1">
        <div class="absolute left-0 top-0 bottom-0 w-1 bg-tangerine"></div>
        <p class="text-[11px] uppercase tracking-label text-ink-ghost">Most bought product</p>
        <p class="text-sm font-mono text-brick truncate">{{ kpis.most_bought_product ?? "—" }}</p>
        <p class="text-[11px] text-ink-ghost">Highest total quantity sold</p>
      </div>
      <div class="relative border border-rim bg-surface-deep px-4 py-3 pl-5 space-y-1">
        <div class="absolute left-0 top-0 bottom-0 w-1 bg-tangerine"></div>
        <p class="text-[11px] uppercase tracking-label text-ink-ghost">Highest revenue product</p>
        <p class="text-sm font-mono text-brick truncate">{{ kpis.highest_revenue_product ?? "—" }}</p>
        <p class="text-[11px] text-ink-ghost">Highest total item revenue</p>
      </div>
    </div>

    <!-- Co-purchase pairs table -->
    <div v-if="hasPairs">
      <p class="text-[11px] uppercase tracking-label text-ink-ghost mb-2">
        Top co-purchased item sets
      </p>
      <div class="border border-rim overflow-hidden">
        <table class="min-w-full text-xs">
          <thead class="bg-surface text-ink-ghost">
            <tr>
              <th class="px-3 py-2 text-left font-medium tracking-label text-[11px] uppercase w-8">#</th>
              <th class="px-3 py-2 text-left font-medium tracking-label text-[11px] uppercase">Items</th>
              <th class="px-3 py-2 text-right font-medium tracking-label text-[11px] uppercase">Co-purchases</th>
            </tr>
          </thead>
          <tbody class="divide-y divide-rim bg-surface-deep">
            <tr
              v-for="(pair, idx) in kpis.top_sku_pairs"
              :key="idx"
              class="hover:bg-surface-hover transition-colors"
            >
              <td class="px-3 py-2 text-ink-ghost font-mono">{{ idx + 1 }}</td>
              <td class="px-3 py-2">
                <div class="flex flex-wrap gap-1">
                  <span
                    v-for="sku in pair.items"
                    :key="sku"
                    class="inline-block border border-rim bg-surface px-1.5 py-0.5 text-ink-dim font-mono text-[11px]"
                  >
                    {{ sku }}
                  </span>
                </div>
              </td>
              <td class="px-3 py-2 text-right text-ink font-mono font-medium">
                {{ pair.count }}
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <p v-if="!hasSku && !hasPairs" class="text-[11px] text-ink-ghost">
      Product breakdown not available (requires product CSV or item-level transactions).
    </p>
  </div>
</template>

<script setup>
import { computed } from "vue";

const props = defineProps({ kpis: { type: Object, required: true } });

const hasSku = computed(() =>
  props.kpis.most_bought_product != null || props.kpis.highest_revenue_product != null
);

const hasPairs = computed(() =>
  Array.isArray(props.kpis.top_sku_pairs) && props.kpis.top_sku_pairs.length > 0
);
</script>
