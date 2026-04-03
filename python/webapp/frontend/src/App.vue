<template>
  <div class="min-h-screen bg-background text-ink">
    <header class="border-b border-rim bg-surface">
      <div class="max-w-6xl mx-auto px-6 py-4 flex items-center justify-between">
        <div>
          <h1 class="text-[18px] font-light tracking-tight text-ink">
            PriceRiot
          </h1>
          <p class="text-[11px] text-ink-ghost tracking-label uppercase mt-0.5">
            Simulation engine
          </p>
        </div>
        <div class="flex items-center gap-2 text-[11px] text-ink-ghost font-mono">
          <span class="w-1.5 h-1.5 rounded-full bg-green"></span>
          headless
        </div>
      </div>
    </header>

    <main class="max-w-6xl mx-auto px-6 py-6 space-y-6">
      <nav class="flex gap-6 border-b border-rim">
        <button
          v-for="tab in tabs"
          :key="tab.id"
          class="pb-2.5 text-sm transition-colors duration-150 border-b-2 -mb-px"
          :class="tab.id === activeTab
            ? 'text-mustard border-mustard'
            : 'text-ink-dim border-transparent hover:text-ink'"
          @click="activeTab = tab.id"
        >
          {{ tab.label }}
        </button>
      </nav>

      <section>
        <KeepAlive>
          <component
            :is="currentComponent"
            @submitted="onSimulationSubmitted"
          />
        </KeepAlive>
      </section>
    </main>
  </div>
</template>

<script setup>
import { computed, ref, onMounted, onBeforeUnmount } from "vue";
import { useTemporalStore } from "./stores/temporal";
import { beaconDeleteIncompleteRuns } from "./api/client";
import RunTab         from "./components/RunTab.vue";
import SimResultsView from "./components/SimResultsView.vue";
import CompareTab     from "./components/CompareTab.vue";
import LayoutTab      from "./components/LayoutTab.vue";

const temporalStore = useTemporalStore();

const tabs = [
  { id: "run",     label: "Run",     component: RunTab         },
  { id: "results", label: "Results", component: SimResultsView },
  { id: "compare", label: "Compare", component: CompareTab     },
  { id: "layout",  label: "Layout",  component: LayoutTab      },
];

const activeTab = ref("run");

const currentComponent = computed(() =>
  tabs.find((t) => t.id === activeTab.value)?.component || RunTab
);

function onSimulationSubmitted() {
  activeTab.value = "results";
}

function handleBeforeUnload() {
  beaconDeleteIncompleteRuns();
}

onMounted(() => {
  temporalStore.fetchDatasets();
  temporalStore.fetchSystemInfo();
  temporalStore.fetchHistory();
  window.addEventListener("beforeunload", handleBeforeUnload);
});

onBeforeUnmount(() => {
  window.removeEventListener("beforeunload", handleBeforeUnload);
});
</script>
