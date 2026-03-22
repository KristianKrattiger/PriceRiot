<template>
  <div
    v-if="profile"
    class="rounded-lg border border-sky-500/30 bg-sky-950/30 p-4 space-y-3"
  >
    <h3 class="text-sm font-semibold text-sky-300 flex items-center gap-2">
      <span>POS data profile</span>
      <span class="text-xs font-normal text-sky-400/70">(applied to this run)</span>
    </h3>

    <div class="grid grid-cols-2 gap-x-6 gap-y-2 text-sm">
      <div>
        <span class="text-slate-400">Spawn interval</span>
        <span class="ml-2 font-mono text-slate-100">
          {{ profile.spawn_interval_seconds?.toFixed(1) }}s
        </span>
      </div>
      <div>
        <span class="text-slate-400">Mean basket</span>
        <span class="ml-2 font-mono text-slate-100">
          {{ profile.mean_basket_size?.toFixed(1) }} items
        </span>
      </div>
      <div>
        <span class="text-slate-400">Mission shoppers</span>
        <span class="ml-2 font-mono text-slate-100">
          {{ ((profile.mission_probability ?? 0) * 100).toFixed(0) }}%
        </span>
      </div>
      <div>
        <span class="text-slate-400">Price sensitivity</span>
        <span class="ml-2 font-mono text-slate-100">
          {{ ((profile.price_sensitivity ?? 0) * 100).toFixed(0) }}%
        </span>
      </div>
      <div>
        <span class="text-slate-400">Avg service time</span>
        <span class="ml-2 font-mono text-slate-100">
          {{ profile.mean_service_time_s?.toFixed(1) }}s
        </span>
      </div>
    </div>

    <div v-if="profile.top_skus?.length">
      <p class="text-xs text-slate-400 mb-1">Top SKUs</p>
      <div class="flex flex-wrap gap-1">
        <span
          v-for="sku in profile.top_skus.slice(0, 10)"
          :key="sku"
          class="rounded bg-slate-800 px-2 py-0.5 text-xs text-slate-300 font-mono"
        >
          {{ sku }}
        </span>
      </div>
    </div>
  </div>
</template>

<script setup>
defineProps({
  profile: {
    type: Object,
    default: null,
  },
});
</script>
