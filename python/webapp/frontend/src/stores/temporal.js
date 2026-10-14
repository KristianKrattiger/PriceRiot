import { watch } from "vue";
import { defineStore } from "pinia";
import {
  fetchPosDatasets,
  submitSimulation,
  pollSimulationStatus,
  fetchSimulationResults,
  fetchSimulationRunDetail,
  fetchSimulationHistory,
  fetchSystemInfo,
  deleteSimulation as apiDeleteSimulation,
  deleteAllSimulations as apiDeleteAllSimulations,
} from "../api/client";

export const useTemporalStore = defineStore("temporal", {
  state: () => ({
    // Dataset picker
    datasets: [],
    datasetsLoading: false,

    // Form / active config (mirrors POST payload shape)
    activeConfig: {
      pos_dataset: "",
      store_yaml: "",
      preset: "contiguous_range",
      date_config: {
        start: "",
        end: "",
        date: "",
        days_of_week: [],
        weeks: 4,
        anchor: "",
        dates: [],
      },
      time_window: null,   // null = use store hours
      runs: 4,
      max_threads: 4,
      label: "",
      // Simulation parameters
      seed: 42,
      mission_probability: 0.5,
      spawn_interval: 5.0,
      num_stockers: 2,
      num_cashiers: 1,
      auto_stock_tasks: true,
      auto_register_tasks: true,
      product_csv: null,
    },

    // System
    cpuCount: 4,

    // Submission state
    submitting: false,
    pendingSimId: null,
    pollingTimer: null,

    // History
    simulations: [],
    historyLoading: false,

    // Active/viewed simulation
    activeSimulation: null,
    activeRunDetail: null,
    drilldownRunIndex: null,
  }),

  getters: {
    selectedDataset(state) {
      return state.datasets.find((d) => d.filename === state.activeConfig.pos_dataset) || null;
    },
  },

  actions: {
    _loadPersistedConfig() {
      try {
        const saved = localStorage.getItem("priceriot_sim_config");
        if (saved) {
          const parsed = JSON.parse(saved);
          Object.assign(this.activeConfig, parsed);
        }
      } catch { /* ignore */ }
      // Watch for changes and persist
      watch(
        () => JSON.stringify(this.activeConfig),
        (v) => {
          try { localStorage.setItem("priceriot_sim_config", v); } catch { /* ignore */ }
        },
        { deep: true }
      );
    },

    async fetchDatasets() {
      this._loadPersistedConfig();
      this.datasetsLoading = true;
      try {
        this.datasets = await fetchPosDatasets();
      } finally {
        this.datasetsLoading = false;
      }
    },

    async fetchSystemInfo() {
      try {
        const info = await fetchSystemInfo();
        this.cpuCount = info.cpu_count || 4;
        this.activeConfig.max_threads = this.cpuCount;
      } catch {
        // non-fatal
      }
    },

    selectDataset(filename) {
      const ds = this.datasets.find((d) => d.filename === filename);
      if (!ds) return;
      this.activeConfig.pos_dataset = ds.filename;
      this.activeConfig.store_yaml = ds.store_yaml || "";
    },

    async submitSimulation() {
      this.submitting = true;
      try {
        const payload = this._buildPayload();
        const job = await submitSimulation(payload);
        this.pendingSimId = job.sim_id;
        this.simulations.unshift(job);
        this._startPolling(job.sim_id);
        return job.sim_id;
      } catch (err) {
        this.submitting = false;
        throw err;
      }
    },

    _buildPayload() {
      const cfg = this.activeConfig;
      return {
        pos_dataset:         cfg.pos_dataset,
        store_yaml:          cfg.store_yaml,
        preset:              cfg.preset,
        date_config:         cfg.date_config,
        time_window:         cfg.time_window,
        runs:                cfg.runs,
        max_threads:         cfg.max_threads,
        label:               cfg.label || null,
        seed:                cfg.seed,
        mission_probability: cfg.mission_probability,
        spawn_interval:      cfg.spawn_interval,
        num_stockers:        cfg.num_stockers,
        num_cashiers:        cfg.num_cashiers,
        auto_stock_tasks:    cfg.auto_stock_tasks,
        auto_register_tasks: cfg.auto_register_tasks,
        product_csv:         cfg.product_csv || null,
      };
    },

    _startPolling(simId) {
      if (this.pollingTimer) clearInterval(this.pollingTimer);
      let attempts = 0;
      const MAX_ATTEMPTS = 400; // ~10 min at 1.5s
      this.pollingTimer = setInterval(async () => {
        attempts++;
        try {
          const status = await pollSimulationStatus(simId);
          const idx = this.simulations.findIndex((s) => String(s.sim_id) === String(simId));
          if (idx !== -1) {
            this.simulations[idx] = { ...this.simulations[idx], ...status };
          }
          if (status.status === "complete" || status.status === "failed") {
            clearInterval(this.pollingTimer);
            this.pollingTimer = null;
            this.submitting = false;
            // Fetch full results
            await this.fetchResults(simId);
          }
        } catch (err) {
          // Stop polling on 404 (sim gone after server restart) or hard limit
          const status = err?.response?.status;
          if (status === 404 || status === 410 || attempts >= MAX_ATTEMPTS) {
            clearInterval(this.pollingTimer);
            this.pollingTimer = null;
            this.submitting = false;
            // Mark the local entry as failed so UI shows an error state
            const idx = this.simulations.findIndex((s) => s.sim_id === simId);
            if (idx !== -1) {
              this.simulations[idx] = {
                ...this.simulations[idx],
                status: "failed",
                error_message: status === 404 ? "Simulation lost (server restarted)" : "Polling timed out",
              };
            }
          }
          // other transient errors: keep polling
        }
      }, 1500);
    },

    async fetchResults(simId) {
      const result = await fetchSimulationResults(simId);
      this.activeSimulation = result;
      const idx = this.simulations.findIndex((s) => s.sim_id === simId);
      if (idx !== -1) {
        this.simulations[idx] = result;
      } else {
        this.simulations.unshift(result);
      }
      return result;
    },

    async fetchRunDetail(simId, runIndex) {
      this.drilldownRunIndex = runIndex;
      try {
        this.activeRunDetail = await fetchSimulationRunDetail(simId, runIndex);
      } catch {
        this.activeRunDetail = null;
      }
    },

    closeDrilldown() {
      this.drilldownRunIndex = null;
      this.activeRunDetail = null;
    },

    async fetchHistory() {
      this.historyLoading = true;
      try {
        this.simulations = await fetchSimulationHistory();
      } finally {
        this.historyLoading = false;
      }
    },

    viewSimulation(simId) {
      const sim = this.simulations.find((s) => s.sim_id === simId);
      if (sim) this.activeSimulation = sim;
    },

    async trackNewSim(simId) {
      // Called after an external submission (e.g. from layout editor).
      // Fetches the job, adds it to the list, sets it active, and starts polling.
      try {
        const job = await fetchSimulationResults(simId);
        const idx = this.simulations.findIndex((s) => s.sim_id === simId);
        if (idx !== -1) {
          this.simulations[idx] = job;
        } else {
          this.simulations.unshift(job);
        }
        this.activeSimulation = job;
        if (job.status === "queued" || job.status === "running") {
          this._startPolling(simId);
        }
      } catch {
        // fallback: just refresh history
        await this.fetchHistory();
        const sim = this.simulations.find((s) => s.sim_id === simId);
        if (sim) {
          this.activeSimulation = sim;
          this._startPolling(simId);
        }
      }
    },

    async deleteSimulation(simId) {
      await apiDeleteSimulation(simId);
      this.simulations = this.simulations.filter((s) => s.sim_id !== simId);
      if (this.activeSimulation?.sim_id === simId) {
        this.activeSimulation = this.simulations[0] || null;
      }
    },

    async clearAllSimulations() {
      await apiDeleteAllSimulations();
      this.simulations = [];
      this.activeSimulation = null;
    },
  },
});
