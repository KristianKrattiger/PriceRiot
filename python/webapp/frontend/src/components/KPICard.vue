<template>
  <div class="rounded-xl bg-slate-900/60 border border-slate-800 px-4 py-3">
    <p class="text-xs uppercase tracking-wide text-slate-400">
      {{ label }}
    </p>
    <p class="mt-1 text-xl font-semibold text-slate-50">
      {{ value }}
    </p>
  </div>
</template>

<script setup>
defineProps({
  label: { type: String, required: true },
  value: { type: String, required: true }
});
</script>

