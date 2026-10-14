/**
 * useRunPresetsStore — save/load named run parameter presets in localStorage.
 *
 * A preset is a snapshot of the temporal activeConfig fields that control
 * simulation behaviour (dates, runs, spawn params, staffing).  It is keyed
 * by a user-supplied name and survives page reloads.
 */
import { defineStore } from "pinia";

const STORAGE_KEY = "priceriot_run_presets";

function loadFromStorage() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    return raw ? JSON.parse(raw) : [];
  } catch {
    return [];
  }
}

function saveToStorage(presets) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(presets));
  } catch { /* quota exceeded — ignore */ }
}

/** Fields captured in a preset (excludes file references which are layout-specific). */
const PRESET_FIELDS = [
  "preset",
  "date_config",
  "time_window",
  "runs",
  "max_threads",
  "seed",
  "mission_probability",
  "spawn_interval",
  "num_stockers",
  "num_cashiers",
  "auto_stock_tasks",
  "auto_register_tasks",
];

export const useRunPresetsStore = defineStore("runPresets", {
  state: () => ({
    presets: loadFromStorage(),
  }),

  actions: {
    /** Save current config fields as a named preset. Returns the new preset id. */
    savePreset(name, config) {
      const id = Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
      const snapshot = {};
      for (const key of PRESET_FIELDS) {
        if (key in config) {
          snapshot[key] = JSON.parse(JSON.stringify(config[key]));
        }
      }
      // Remove old preset with same name if exists
      this.presets = this.presets.filter((p) => p.name !== name);
      this.presets.unshift({ id, name, snapshot, savedAt: new Date().toISOString() });
      saveToStorage(this.presets);
      return id;
    },

    /** Apply a preset onto a reactive config object. */
    applyPreset(presetId, config) {
      const preset = this.presets.find((p) => p.id === presetId);
      if (!preset) return;
      for (const [key, val] of Object.entries(preset.snapshot)) {
        if (key === "date_config" && typeof val === "object") {
          Object.assign(config.date_config, val);
        } else {
          config[key] = JSON.parse(JSON.stringify(val));
        }
      }
    },

    deletePreset(presetId) {
      this.presets = this.presets.filter((p) => p.id !== presetId);
      saveToStorage(this.presets);
    },
  },
});
