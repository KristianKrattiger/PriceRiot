import { defineStore } from "pinia";

export const useSimulationStore = defineStore("simulation", {
  state: () => ({
    runs: [],
    currentRunId: null
  }),
  getters: {
    currentRun(state) {
      return state.runs.find((r) => r.run_id === state.currentRunId) || null;
    }
  },
  actions: {
    setRuns(runs) {
      this.runs = runs;
    },
    addOrUpdateRun(run) {
      const idx = this.runs.findIndex((r) => r.run_id === run.run_id);
      if (idx === -1) {
        this.runs.push(run);
      } else {
        this.runs[idx] = run;
      }
    },
    setCurrentRun(id) {
      this.currentRunId = id;
    }
  }
});

