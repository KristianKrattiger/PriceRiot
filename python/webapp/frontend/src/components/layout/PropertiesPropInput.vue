<template>
  <div class="space-y-0.5">
    <label class="block text-[10px] text-ink-ghost/70">{{ label }}</label>
    <input
      :value="modelValue"
      type="number"
      step="any"
      class="w-full bg-surface-deep border border-rim px-2 py-1 text-[11px] font-mono text-ink
             focus:outline-none focus:border-tangerine transition-colors"
      @input="onInput"
      @blur="$emit('blur')"
    />
  </div>
</template>

<script setup>
defineProps({
  label: { type: String, default: "" },
  modelValue: { type: [Number, String], default: undefined },
});

const emit = defineEmits(["update:modelValue", "blur"]);

function onInput(e) {
  const v = e.target.valueAsNumber ?? e.target.value;
  emit("update:modelValue", v);
}
</script>
