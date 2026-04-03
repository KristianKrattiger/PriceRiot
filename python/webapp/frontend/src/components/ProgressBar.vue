<template>
  <div class="space-y-1.5">
    <div class="flex justify-between text-[11px] font-mono text-ink-ghost">
      <span>{{ label }}</span>
      <span>{{ percent }}%</span>
    </div>
    <div class="h-px bg-rim overflow-hidden relative">
      <div
        class="absolute inset-y-0 left-0 bg-deep-teal transition-all duration-300"
        :style="{ width: percent + '%' }"
      ></div>
    </div>
    <p v-if="message" class="text-[11px] text-ink-ghost">{{ message }}</p>
  </div>
</template>

<script setup>
defineProps({
  percent: { type: Number, default: 0 },
  message: { type: String, default: "" },
  label:   { type: String, default: "Simulation progress" },
});
</script>
