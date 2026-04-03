<template>
  <div class="fixed inset-0 z-50 flex items-center justify-center bg-ink/40">
    <div class="bg-surface border border-rim w-full max-w-sm shadow-xl">

      <div class="flex items-center justify-between px-5 py-4 border-b border-rim">
        <h2 class="text-sm font-medium text-ink">Save Layout</h2>
        <button @click="$emit('cancel')" class="text-ink-ghost hover:text-ink transition-colors">✕</button>
      </div>

      <div class="px-5 py-4 space-y-4">
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Filename (without .yaml)</label>
          <input
            v-model="name"
            type="text"
            class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                   focus:outline-none focus:border-tangerine transition-colors"
          />
          <p class="text-[10px] font-mono text-ink-ghost">Will save as {{ name }}.yaml</p>
        </div>

        <div class="flex gap-0 border border-rim">
          <button
            @click="overwrite = true"
            class="flex-1 py-2 text-[11px] font-mono transition-colors"
            :class="overwrite
              ? 'bg-mustard/10 text-mustard'
              : 'text-ink-ghost hover:text-ink hover:bg-surface-hover'"
          >Overwrite</button>
          <button
            @click="overwrite = false"
            class="flex-1 py-2 text-[11px] font-mono border-l border-rim transition-colors"
            :class="!overwrite
              ? 'bg-mustard/10 text-mustard'
              : 'text-ink-ghost hover:text-ink hover:bg-surface-hover'"
          >New version</button>
        </div>

        <p v-if="!overwrite" class="text-[10px] font-mono text-ink-ghost">
          If {{ name }}.yaml already exists, it will be saved as {{ name }}_v2.yaml (auto-incremented).
        </p>
      </div>

      <div class="flex justify-end gap-2 px-5 py-4 border-t border-rim bg-surface-deep">
        <button
          @click="$emit('cancel')"
          class="px-4 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                 hover:text-ink hover:bg-surface-hover transition-colors"
        >Cancel</button>
        <button
          :disabled="!name.trim()"
          @click="confirm"
          class="px-4 py-1.5 text-[11px] font-mono border transition-colors"
          :class="name.trim()
            ? 'border-mustard bg-mustard/10 text-mustard hover:bg-mustard/20'
            : 'border-rim text-ink-ghost/40 cursor-not-allowed'"
        >Save →</button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref } from "vue";

const props = defineProps({ currentName: { type: String, default: "my_store" } });
const emit  = defineEmits(["save", "cancel"]);

const name      = ref(props.currentName);
const overwrite = ref(true);

function confirm() {
  if (!name.value.trim()) return;
  emit("save", { name: name.value.trim(), overwrite: overwrite.value });
}
</script>
