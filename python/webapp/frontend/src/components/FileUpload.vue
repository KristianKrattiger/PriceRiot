<template>
  <div class="space-y-1">
    <label class="block text-sm font-medium text-slate-300">
      {{ label }}
    </label>
    <input
      type="file"
      :accept="accept"
      class="block w-full text-sm text-slate-300
             file:mr-4 file:py-2 file:px-4
             file:rounded-md file:border-0
             file:text-sm file:font-semibold
             file:bg-slate-800 file:text-slate-100
             hover:file:bg-slate-700"
      @change="onChange"
    />
    <p v-if="fileName" class="text-xs text-slate-400 truncate">
      Selected: {{ fileName }}
    </p>
  </div>
</template>

<script setup>
import { computed } from "vue";

const props = defineProps({
  label: { type: String, required: true },
  accept: { type: String, default: "" },
  modelValue: File
});

const emit = defineEmits(["update:modelValue"]);

const fileName = computed(() => props.modelValue?.name || "");

function onChange(event) {
  const [file] = event.target.files;
  emit("update:modelValue", file || null);
}
</script>

