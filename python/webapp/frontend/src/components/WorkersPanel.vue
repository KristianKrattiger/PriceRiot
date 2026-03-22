<template>
  <div class="space-y-4">
    <div class="flex items-center justify-between">
      <h2 class="text-lg font-semibold text-slate-100">
        Workers
      </h2>
      <div class="flex items-center gap-3 text-xs text-slate-400">
        <span v-if="!runId">Select a completed run to view workers.</span>
      </div>
    </div>

    <div class="grid grid-cols-1 md:grid-cols-4 gap-4">
      <div>
        <label class="block text-xs text-slate-300 mb-1">Stockers</label>
        <input
          type="number"
          min="0"
          class="w-full rounded-md bg-slate-900 border border-slate-700 px-3 py-1.5 text-xs text-slate-100"
          v-model.number="numStockers"
        />
      </div>
      <div>
        <label class="block text-xs text-slate-300 mb-1">Cashiers</label>
        <input
          type="number"
          min="0"
          class="w-full rounded-md bg-slate-900 border border-slate-700 px-3 py-1.5 text-xs text-slate-100"
          v-model.number="numCashiers"
        />
      </div>
      <div class="flex items-center gap-2 mt-5">
        <input
          id="autoStock"
          type="checkbox"
          v-model="autoStockTasks"
          class="h-3 w-3 rounded border-slate-600 bg-slate-900"
        />
        <label for="autoStock" class="text-xs text-slate-300">
          Auto-stock shelves
        </label>
      </div>
      <div class="flex items-center gap-2 mt-5">
        <input
          id="autoRegister"
          type="checkbox"
          v-model="autoRegisterTasks"
          class="h-3 w-3 rounded border-slate-600 bg-slate-900"
        />
        <label for="autoRegister" class="text-xs text-slate-300">
          Auto-open registers
        </label>
      </div>
    </div>

    <div class="flex items-center gap-3">
      <button
        class="inline-flex items-center px-3 py-1.5 rounded-md text-xs font-medium
               bg-emerald-500 text-slate-950 hover:bg-emerald-400 disabled:opacity-40"
        :disabled="!runId || applyStatus === 'saving'"
        @click="applyWorkerConfig"
      >
        {{ applyStatus === 'saving' ? 'Applying…' : 'Apply to queued run' }}
      </button>
      <span v-if="applyStatus === 'ok'" class="text-xs text-emerald-400">Saved</span>
      <span v-if="applyStatus === 'error'" class="text-xs text-rose-400">{{ applyError }}</span>
    </div>

    <div class="border border-slate-800 rounded-lg overflow-hidden">
      <table class="min-w-full text-xs">
        <thead class="bg-slate-900/70 text-slate-300">
          <tr>
            <th class="px-3 py-2 text-left font-medium">ID</th>
            <th class="px-3 py-2 text-left font-medium">Role</th>
            <th class="px-3 py-2 text-left font-medium">State</th>
            <th class="px-3 py-2 text-left font-medium">Task</th>
            <th class="px-3 py-2 text-left font-medium">Happiness</th>
            <th class="px-3 py-2 text-left font-medium">Efficiency</th>
          </tr>
        </thead>
        <tbody class="divide-y divide-slate-800 bg-slate-950/30">
          <tr v-for="w in workers" :key="w.id">
            <td class="px-3 py-1.5 text-slate-100">
              {{ w.id }}
            </td>
            <td class="px-3 py-1.5">
              <span class="inline-flex items-center rounded-full px-2 py-0.5 bg-slate-800/60 text-[11px] text-slate-200">
                {{ roleLabel(w) }}
              </span>
            </td>
            <td class="px-3 py-1.5 text-slate-200">
              {{ w.current_task ? "Executing" : "Idle" }}
            </td>
            <td class="px-3 py-1.5 text-slate-300">
              <span v-if="w.current_task">
                {{ taskTypeLabel(w.current_task.type) }}
                <span class="text-slate-500">#{{ w.current_task.target_id }}</span>
              </span>
              <span v-else class="text-slate-500">
                —
              </span>
            </td>
            <td class="px-3 py-1.5 text-slate-200">
              {{ w.happiness?.toFixed(2) ?? "—" }}
            </td>
            <td class="px-3 py-1.5 text-slate-200">
              {{ w.task_efficiency?.toFixed(2) ?? "—" }}
            </td>
          </tr>
          <tr v-if="workers.length === 0">
            <td colspan="6" class="px-3 py-4 text-center text-slate-500">
              No worker data for this run yet.
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, ref, watch } from "vue";
import { useSimulationStore } from "../stores/simulation";
import { listWorkers, updateRunWorkers } from "../api/client";

const store = useSimulationStore();

const runId = computed(() => store.currentRunId);

const workers = ref([]);
const numStockers = ref(2);
const numCashiers = ref(1);
const autoStockTasks = ref(true);
const autoRegisterTasks = ref(true);
const applyStatus = ref(""); // '', 'saving', 'ok', 'error'
const applyError = ref("");

async function loadWorkers() {
  if (!runId.value) {
    workers.value = [];
    return;
  }
  try {
    workers.value = await listWorkers(runId.value);
  } catch {
    workers.value = [];
  }
}

function roleLabel(w) {
  if (w.can_stock && w.can_serve) return "Hybrid";
  if (w.can_stock) return "Stocker";
  if (w.can_serve) return "Cashier";
  return "Worker";
}

function taskTypeLabel(t) {
  if (t === "StockShelves" || t === 0) return "Stock shelves";
  if (t === "ProcessRegister" || t === 1) return "Register";
  if (t === "AssistCustomer" || t === 2) return "Assist";
  return String(t);
}

onMounted(() => {
  loadWorkers();
});

async function applyWorkerConfig() {
  if (!runId.value) return;
  applyStatus.value = "saving";
  applyError.value = "";
  try {
    await updateRunWorkers(runId.value, numStockers.value, numCashiers.value);
    applyStatus.value = "ok";
    setTimeout(() => { applyStatus.value = ""; }, 2000);
  } catch (e) {
    applyStatus.value = "error";
    applyError.value = e?.response?.data?.detail ?? "Failed to update";
  }
}

watch(runId, () => {
  loadWorkers();
});
</script>

