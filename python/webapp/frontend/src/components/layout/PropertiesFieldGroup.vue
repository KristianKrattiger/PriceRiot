<template>
  <div class="space-y-1.5">
    <p class="text-[9px] uppercase tracking-label text-ink-ghost/50">{{ label }}</p>
    <slot />
  </div>
</template>

<script setup>
defineProps({
  label: { type: String, default: "" },
});
</script>
