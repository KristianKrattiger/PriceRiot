<template>
  <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
    <div class="space-y-4">
      <h2 class="text-lg font-semibold text-slate-100">
        Run configuration
      </h2>
      <div class="grid grid-cols-2 gap-4">
        <div>
          <label class="block text-sm text-slate-300 mb-1">Duration (s)</label>
          <input
            v-model.number="durationSeconds"
            type="number"
            min="1"
            class="w-full rounded-md bg-slate-900 border border-slate-700 px-3 py-2 text-sm text-slate-100"
          />
        </div>
        <div>
          <label class="block text-sm text-slate-300 mb-1">Spawn interval (s)</label>
          <input
            v-model.number="spawnInterval"
            type="number"
            min="0.1"
            step="0.1"
            class="w-full rounded-md bg-slate-900 border border-slate-700 px-3 py-2 text-sm text-slate-100"
          />
        </div>
        <div>
          <label class="block text-sm text-slate-300 mb-1">
            Mission shopper share
          </label>
          <input
            v-model.number="missionProbability"
            type="range"
            min="0"
            max="1"
            step="0.05"
            class="w-full"
          />
          <div class="mt-1 flex justify-between text-xs text-slate-400">
            <span>
              Default: {{ ((1 - missionProbability) * 100).toFixed(0) }}%
            </span>
            <span>
              Mission: {{ (missionProbability * 100).toFixed(0) }}%
            </span>
          </div>
        </div>
        <div>
          <label class="block text-sm text-slate-300 mb-1">Random seed</label>
          <input
            v-model.number="randomSeed"
            type="number"
            min="0"
            class="w-full rounded-md bg-slate-900 border border-slate-700 px-3 py-2 text-sm text-slate-100"
          />
        </div>
        <div>
          <label class="block text-sm text-slate-300 mb-1">Stockers</label>
          <input
            v-model.number="numStockers"
            type="number"
            min="0"
            class="w-full rounded-md bg-slate-900 border border-slate-700 px-3 py-2 text-sm text-slate-100"
          />
        </div>
        <div>
          <label class="block text-sm text-slate-300 mb-1">Cashiers</label>
          <input
            v-model.number="numCashiers"
            type="number"
            min="0"
            class="w-full rounded-md bg-slate-900 border border-slate-700 px-3 py-2 text-sm text-slate-100"
          />
        </div>
        <div class="flex items-center gap-2 mt-6">
          <input
            id="autoStockTasks"
            type="checkbox"
            v-model="autoStockTasks"
            class="h-4 w-4 rounded border-slate-700 bg-slate-900"
          />
          <label for="autoStockTasks" class="text-sm text-slate-300">
            Auto-stock shelves
          </label>
        </div>
        <div class="flex items-center gap-2 mt-6">
          <input
            id="autoRegisterTasks"
            type="checkbox"
            v-model="autoRegisterTasks"
            class="h-4 w-4 rounded border-slate-700 bg-slate-900"
          />
          <label for="autoRegisterTasks" class="text-sm text-slate-300">
            Auto-open registers
          </label>
        </div>
      </div>

      <div class="space-y-3">
        <FileUpload
          label="Store YAML (required)"
          accept=".yaml,.yml"
          v-model="storeFile"
        />
        <FileUpload
          label="Product CSV (optional)"
          accept=".csv"
          v-model="productFile"
        />
        <FileUpload
          label="POS CSV (optional)"
          accept=".csv"
          v-model="posFile"
        />
      </div>

      <div class="flex gap-3">
        <button
          class="inline-flex items-center px-4 py-2 rounded-md text-sm font-medium
                 bg-emerald-500 text-slate-950 hover:bg-emerald-400 disabled:opacity-50"
          :disabled="!storeFile || isRunning"
          @click="startRun"
        >
          Run headless
        </button>
        <button
          class="inline-flex items-center px-4 py-2 rounded-md text-sm font-medium
                 bg-slate-800 text-slate-200 border border-slate-700 cursor-not-allowed"
          disabled
        >
          Run with visualizer (desktop)
        </button>
      </div>

      <ProgressBar
        v-if="isRunning"
        :percent="progressPercent"
        :message="progressMessage"
      />
    </div>

    <div class="space-y-4">
      <h2 class="text-lg font-semibold text-slate-100">
        Recent runs
      </h2>
      <div class="space-y-2 max-h-80 overflow-y-auto">
        <div
          v-for="run in runs"
          :key="run.run_id"
          class="flex items-center justify-between rounded-lg border px-3 py-2 text-sm"
          :class="run.run_id === currentRunId
            ? 'border-emerald-500 bg-slate-900/70'
            : 'border-slate-800 bg-slate-900/40 hover:border-slate-600'"
        >
          <div>
            <p class="font-medium text-slate-100">
              Run #{{ run.run_id }}
            </p>
            <p class="text-xs text-slate-400">
              {{ run.config.store_yaml.split(/[\\/]/).slice(-1)[0] }}
            </p>
          </div>
          <div class="flex items-center gap-3">
            <span
              class="inline-flex items-center rounded-full px-2 py-0.5 text-[11px] font-medium"
              :class="statusClass(run.status)"
            >
              {{ run.status }}
            </span>
            <button
              class="text-xs text-emerald-400 hover:text-emerald-300"
              @click="$emit('select-run', run.run_id)"
            >
              View
            </button>
          </div>
        </div>
        <p v-if="runs.length === 0" class="text-sm text-slate-400">
          No runs yet. Start one with the form on the left.
        </p>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, ref } from "vue";
import { useSimulationStore } from "../stores/simulation";
import { createRun, listRuns, streamRun } from "../api/client";
import FileUpload from "./FileUpload.vue";
import ProgressBar from "./ProgressBar.vue";

const emit = defineEmits(["run-complete", "select-run"]);

const store = useSimulationStore();

const durationSeconds = ref(600);
const spawnInterval = ref(5);
const missionProbability = ref(0.5);
const randomSeed = ref(0);
const numStockers = ref(2);
const numCashiers = ref(1);
const autoStockTasks = ref(true);
const autoRegisterTasks = ref(true);

const storeFile = ref(null);
const productFile = ref(null);
const posFile = ref(null);

const isRunning = ref(false);
const progressPercent = ref(0);
const progressMessage = ref("");
let eventSource = null;

const runs = computed(() => store.runs);
const currentRunId = computed(() => store.currentRunId);

function statusClass(status) {
  if (status === "completed") {
    return "bg-emerald-500/20 text-emerald-300 border border-emerald-500/40";
  }
  if (status === "running") {
    return "bg-sky-500/20 text-sky-300 border border-sky-500/40";
  }
  if (status === "failed") {
    return "bg-rose-500/20 text-rose-300 border border-rose-500/40";
  }
  return "bg-slate-700/40 text-slate-200 border border-slate-500/40";
}

async function refreshRuns() {
  const data = await listRuns();
  store.setRuns(data);
}

async function startRun() {
  if (!storeFile.value) return;
  isRunning.value = true;
  progressPercent.value = 0;
  progressMessage.value = "Starting simulation...";

  const form = new FormData();
  form.append("duration_seconds", String(durationSeconds.value));
  form.append("spawn_interval", String(spawnInterval.value));
  form.append("mission_probability", String(missionProbability.value));
  form.append("random_seed", String(randomSeed.value));
  form.append("num_stockers", String(numStockers.value));
  form.append("num_cashiers", String(numCashiers.value));
  form.append("auto_stock_tasks", String(autoStockTasks.value));
  form.append("auto_register_tasks", String(autoRegisterTasks.value));
  form.append("store_file", storeFile.value);
  if (productFile.value) form.append("product_file", productFile.value);
  if (posFile.value) form.append("pos_file", posFile.value);

  try {
    const run = await createRun(form);
    store.addOrUpdateRun(run);
    store.setCurrentRun(run.run_id);
    listenToStream(run.run_id);
  } catch (e) {
    console.error(e);
    isRunning.value = false;
    progressMessage.value = "Failed to start run";
  } finally {
    await refreshRuns();
  }
}

function listenToStream(runId) {
  if (eventSource) {
    eventSource.close();
  }
  eventSource = streamRun(runId, (payload) => {
    if (payload.event === "progress") {
      progressPercent.value = payload.data.percent ?? 0;
      progressMessage.value = payload.data.message ?? "";
    } else if (payload.event === "complete") {
      isRunning.value = false;
      progressMessage.value = "Simulation complete";
      emit("run-complete", runId);
      refreshRuns();
    } else if (payload.event === "error") {
      isRunning.value = false;
      progressMessage.value = payload.data.message || "Error during simulation";
    }
  });
}
</script>

