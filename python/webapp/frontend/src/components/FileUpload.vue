<template>
  <div class="space-y-1.5">
    <p class="text-[11px] uppercase tracking-label text-ink-ghost font-medium">{{ label }}</p>

    <div
      class="border border-dashed border-violet px-4 py-5 text-center cursor-pointer transition-colors duration-200"
      :class="[
        isDragging ? 'border-mustard bg-mustard/5' : 'hover:border-mustard hover:bg-surface-hover',
        fileName ? 'bg-surface' : 'bg-surface-deep',
      ]"
      @click="$refs.input.click()"
      @dragover.prevent="isDragging = true"
      @dragleave.prevent="isDragging = false"
      @drop.prevent="onDrop"
    >
      <p v-if="fileName" class="text-[11px] font-mono text-rust truncate">{{ fileName }}</p>
      <p v-else class="text-[11px] text-ink-ghost">
        Drop file or <span class="text-ink-dim underline underline-offset-2">browse</span>
      </p>
    </div>

    <input
      ref="input"
      type="file"
      :accept="accept"
      class="sr-only"
      @change="onChange"
    />
  </div>
</template>

<script setup>
import { computed, ref } from "vue";

const props = defineProps({
  label:      { type: String, required: true },
  accept:     { type: String, default: "" },
  modelValue: File,
});

const emit = defineEmits(["update:modelValue"]);

const isDragging = ref(false);
const fileName   = computed(() => props.modelValue?.name || "");

function onChange(event) {
  const [file] = event.target.files;
  emit("update:modelValue", file || null);
}

function onDrop(event) {
  isDragging.value = false;
  const [file] = event.dataTransfer.files;
  if (file) emit("update:modelValue", file);
}
</script>
