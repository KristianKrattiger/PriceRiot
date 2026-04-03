<template>
  <div class="relative border border-rim bg-surface-deep px-4 py-3 pl-5">
    <!-- Left accent bar — only rendered when accent is explicitly set -->
    <div
      v-if="accent"
      class="absolute left-0 top-0 bottom-0 w-1"
      :style="{ backgroundColor: accent }"
    ></div>

    <p class="text-[11px] uppercase tracking-label text-ink-ghost font-medium">
      {{ label }}
    </p>
    <p
      class="mt-1 font-mono text-[26px] font-light leading-none"
      :class="money ? 'text-green' : 'text-brick'"
    >
      {{ value }}
    </p>
    <p v-if="sub" class="mt-1.5 text-[11px] text-ink-ghost">{{ sub }}</p>
  </div>
</template>

<script setup>
defineProps({
  label:  { type: String, required: true },
  value:  { type: String, required: true },
  sub:    { type: String, default: null },
  money:  { type: Boolean, default: false },
  accent: { type: String, default: "" },  // omit for no bar; "#008A31" for financial
});
</script>
