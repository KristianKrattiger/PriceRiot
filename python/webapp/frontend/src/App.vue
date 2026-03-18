<template>
  <div class="min-h-screen bg-background text-slate-100">
    <header class="border-b border-slate-800 bg-slate-950/80">
      <div class="max-w-6xl mx-auto px-4 py-4 flex items-center justify-between">
        <div>
          <h1 class="text-xl font-semibold tracking-tight">
            PriceRiot Simulator
          </h1>
          <p class="text-xs text-slate-400">
            Headless runs • live progress • KPIs & comparisons
          </p>
        </div>
      </div>
    </header>

    <main class="max-w-6xl mx-auto px-4 py-6 space-y-4">
      <nav class="flex gap-2 border-b border-slate-800 pb-2">
        <button
          v-for="tab in tabs"
          :key="tab.id"
          class="px-3 py-1.5 text-sm rounded-full"
          :class="tab.id === activeTab
            ? 'bg-slate-100 text-slate-900'
            : 'text-slate-300 hover:bg-slate-800'"
          @click="activeTab = tab.id"
        >
          {{ tab.label }}
        </button>
      </nav>

      <section>
        <KeepAlive>
          <component
            :is="currentComponent"
            @run-complete="onRunComplete"
            @select-run="onSelectRun"
          />
        </KeepAlive>
      </section>
    </main>
  </div>
</template>

<script setup>
import { computed, ref, onMounted } from "vue";
import { useSimulationStore } from "./stores/simulation";
import { listRuns } from "./api/client";
import RunTab from "./components/RunTab.vue";
import ResultsTab from "./components/ResultsTab.vue";
import CompareTab from "./components/CompareTab.vue";

const store = useSimulationStore();

const tabs = [
  { id: "run", label: "Run simulation", component: RunTab },
  { id: "results", label: "Results", component: ResultsTab },
  { id: "compare", label: "Compare", component: CompareTab }
];

const activeTab = ref("run");

const currentComponent = computed(() => {
  return tabs.find((t) => t.id === activeTab.value)?.component || RunTab;
});

async function bootstrapRuns() {
  const data = await listRuns();
  store.setRuns(data);
}

function onRunComplete(runId) {
  store.setCurrentRun(runId);
  activeTab.value = "results";
}

function onSelectRun(runId) {
  store.setCurrentRun(runId);
  activeTab.value = "results";
}

onMounted(() => {
  bootstrapRuns();
});
</script>

