<template>
  <div class="w-full">
    <div class="flex justify-between text-xs text-slate-300 mb-1">
      <span>{{ label }}</span>
      <span>{{ percent }}%</span>
    </div>
    <div class="h-2 rounded-full bg-slate-800 overflow-hidden">
      <div
        class="h-full bg-gradient-to-r from-emerald-400 to-emerald-600 transition-all"
        :style="{ width: percent + '%' }"
      ></div>
    </div>
    <p v-if="message" class="mt-1 text-xs text-slate-400">
      {{ message }}
    </p>
  </div>
</template>

<script setup>
defineProps({
  percent: { type: Number, default: 0 },
  message: { type: String, default: "" },
  label: { type: String, default: "Simulation progress" }
});
</script>

