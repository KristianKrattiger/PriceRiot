<template>
  <div
    v-if="profile"
    class="relative border border-rim bg-surface-deep p-4 pl-5 space-y-3"
  >
    <div class="absolute left-0 top-0 bottom-0 w-1 bg-violet"></div>

    <h3 class="text-[11px] uppercase tracking-label text-ink-ghost font-medium flex items-center gap-2">
      POS data profile
      <span class="text-ink-ghost/50 font-normal normal-case tracking-normal">applied to this run</span>
    </h3>

    <div class="grid grid-cols-2 gap-x-8 gap-y-2">
      <div v-for="item in fields" :key="item.label" class="flex justify-between gap-2">
        <span class="text-[11px] text-ink-ghost">{{ item.label }}</span>
        <span class="text-[11px] font-mono text-ink">{{ item.value }}</span>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from "vue";

const props = defineProps({ profile: { type: Object, default: null } });

const fields = computed(() => {
  if (!props.profile) return [];
  const p = props.profile;
  return [
    { label: "Spawn interval",   value: p.spawn_interval_seconds != null ? `${p.spawn_interval_seconds.toFixed(1)}s` : "—" },
    { label: "Transactions",     value: p.transaction_count      != null ? p.transaction_count.toLocaleString()        : "—" },
  ];
});
</script>
