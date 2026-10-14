<template>
  <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/50 overflow-auto py-8">
    <div class="bg-surface border border-rim shadow-xl w-[560px] max-h-[90vh] flex flex-col">

      <!-- Header -->
      <div class="px-4 py-3 border-b border-rim shrink-0 flex items-center justify-between">
        <div>
          <p class="text-[10px] uppercase tracking-label text-ink-ghost">Run Simulation</p>
          <p class="text-[13px] font-mono text-ink mt-0.5 truncate max-w-[400px]">
            {{ layoutStore.filename || 'Unsaved layout' }}
          </p>
        </div>
        <button
          @click="$emit('cancel')"
          class="text-[11px] font-mono text-ink-ghost hover:text-ink px-2 py-1 border border-rim
                 hover:bg-surface-hover transition-colors shrink-0"
          :disabled="running || saving"
        >✕</button>
      </div>

      <!-- Save warning -->
      <div v-if="layoutStore.isDirty" class="px-4 py-2.5 bg-mustard/10 border-b border-mustard/30 space-y-2 shrink-0">
        <p class="text-[11px] font-mono text-mustard">Layout has unsaved changes — it will be saved before running.</p>
        <div v-if="!layoutStore.filename" class="flex items-center gap-2">
          <label class="text-[10px] text-ink-ghost font-mono shrink-0">Save as:</label>
          <input
            v-model="saveName"
            type="text"
            placeholder="my_store"
            class="flex-1 bg-surface-deep border border-rim px-2 py-1 text-[11px] font-mono text-ink
                   focus:outline-none focus:border-tangerine"
          />
        </div>
      </div>

      <!-- POS params note -->
      <div v-if="posParamsNote" class="px-4 py-2 bg-deep-teal/10 border-b border-deep-teal/20 shrink-0">
        <p class="text-[10px] font-mono text-deep-teal">{{ posParamsNote }}</p>
      </div>

      <!-- Scrollable form body -->
      <div class="flex-1 overflow-y-auto px-4 py-4 space-y-5">

        <!-- Presets -->
        <div class="space-y-2">
          <div class="flex items-center gap-2">
            <select
              class="flex-1 bg-surface-deep border border-rim px-2 py-1.5 text-[11px] font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors"
              @change="onLoadPreset($event.target.value); $event.target.value = ''"
            >
              <option value="" disabled selected>Load preset…</option>
              <option v-for="p in presetsStore.presets" :key="p.id" :value="p.id">{{ p.name }}</option>
            </select>
            <button
              @click="promptSavePreset"
              class="px-3 py-1.5 text-[10px] font-mono border border-rim text-ink-ghost
                     hover:text-ink hover:bg-surface-hover transition-colors shrink-0"
            >Save preset</button>
          </div>
          <div v-if="savePresetPrompt" class="flex items-center gap-2">
            <input
              v-model="presetName"
              type="text"
              placeholder="Preset name…"
              class="flex-1 bg-surface-deep border border-rim px-2 py-1.5 text-[11px] font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors"
              @keydown.enter="confirmSavePreset"
              @keydown.escape="savePresetPrompt = false"
            />
            <button
              @click="confirmSavePreset"
              :disabled="!presetName.trim()"
              class="px-2 py-1.5 text-[10px] font-mono border border-mustard/50 text-mustard
                     hover:bg-mustard/10 transition-colors shrink-0 disabled:opacity-40"
            >Save</button>
            <button @click="savePresetPrompt = false" class="text-ink-ghost hover:text-ink px-1 transition-colors">✕</button>
          </div>
        </div>

        <!-- Label -->
        <div class="space-y-1.5">
          <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Label (optional)</label>
          <input
            v-model="form.label"
            type="text"
            placeholder="e.g. Planogram test A"
            class="w-full bg-surface-deep border border-rim px-2 py-1.5 text-[11px] font-mono text-ink
                   placeholder:text-ink-ghost/40 focus:outline-none focus:border-tangerine transition-colors"
          />
        </div>

        <!-- Temporal preset -->
        <div class="space-y-2 pt-3 border-t border-rim">
          <p class="text-[10px] uppercase tracking-label text-ink-ghost">Date preset</p>
          <div class="flex gap-0 border border-rim">
            <button
              v-for="p in presets"
              :key="p.id"
              class="flex-1 py-1.5 text-[10px] font-medium uppercase tracking-label transition-colors border-r border-rim last:border-r-0"
              :class="form.preset === p.id
                ? 'bg-mustard/20 text-mustard'
                : 'text-ink-ghost hover:text-ink hover:bg-surface-hover'"
              @click="form.preset = p.id"
            >{{ p.label }}</button>
          </div>

          <!-- Contiguous range -->
          <div v-if="form.preset === 'contiguous_range'" class="grid grid-cols-2 gap-3">
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Start</label>
              <input type="date" v-model="form.date_config.start" v-bind="inputClass" />
            </div>
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">End</label>
              <input type="date" v-model="form.date_config.end" v-bind="inputClass" />
            </div>
          </div>

          <!-- Single day N runs -->
          <div v-else-if="form.preset === 'single_day_n_runs'" class="space-y-1">
            <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Date</label>
            <input type="date" v-model="form.date_config.date" v-bind="inputClass" />
          </div>

          <!-- Weekday repeat -->
          <div v-else-if="form.preset === 'weekday_repeat'" class="space-y-3">
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Days of week</label>
              <div class="flex gap-1.5 flex-wrap">
                <button
                  v-for="(day, i) in weekdays"
                  :key="i"
                  class="px-2 py-0.5 text-[10px] font-mono border transition-colors"
                  :class="form.date_config.days_of_week.includes(i)
                    ? 'border-mustard bg-mustard/20 text-mustard'
                    : 'border-rim text-ink-ghost hover:text-ink'"
                  @click="toggleDow(i)"
                >{{ day }}</button>
              </div>
            </div>
            <div class="grid grid-cols-2 gap-3">
              <div class="space-y-1">
                <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Anchor (start)</label>
                <input type="date" v-model="form.date_config.anchor" v-bind="inputClass" />
              </div>
              <div class="space-y-1">
                <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Weeks</label>
                <input type="number" v-model.number="form.date_config.weeks" min="1" step="1" v-bind="inputClass" />
              </div>
            </div>
          </div>

          <!-- Custom days calendar -->
          <div v-else-if="form.preset === 'custom_days'" class="space-y-2">
            <div class="flex items-center justify-between">
              <button class="px-2 py-0.5 text-[10px] font-mono text-ink-ghost border border-rim hover:bg-surface-hover transition-colors" @click="calMonth = prevMonth(calMonth)">‹</button>
              <span class="text-[11px] font-mono text-ink">{{ calMonthLabel }}</span>
              <button class="px-2 py-0.5 text-[10px] font-mono text-ink-ghost border border-rim hover:bg-surface-hover transition-colors" @click="calMonth = nextMonth(calMonth)">›</button>
            </div>
            <div class="grid grid-cols-7 gap-0.5">
              <div v-for="d in ['M','T','W','T','F','S','S']" :key="d + Math.random()"
                class="text-center text-[9px] font-mono text-ink-ghost/50 py-0.5">{{ d }}</div>
              <div
                v-for="(cell, idx) in calCells" :key="idx"
                class="text-center text-[10px] font-mono py-1 cursor-pointer transition-colors"
                :class="cell.date
                  ? (form.date_config.dates.includes(cell.date)
                      ? 'bg-mustard/25 text-mustard border border-mustard/40'
                      : 'text-ink hover:bg-surface-hover border border-transparent')
                  : 'text-transparent cursor-default'"
                @click="cell.date && toggleCustomDate(cell.date)"
              >{{ cell.label }}</div>
            </div>
            <p v-if="form.date_config.dates.length" class="text-[10px] font-mono text-ink-ghost">
              {{ form.date_config.dates.length }} date{{ form.date_config.dates.length !== 1 ? 's' : '' }} selected
            </p>
          </div>
        </div>

        <!-- Time window -->
        <div class="space-y-2 pt-3 border-t border-rim">
          <div class="flex items-center justify-between">
            <p class="text-[10px] uppercase tracking-label text-ink-ghost">Time window</p>
            <label class="flex items-center gap-2 cursor-pointer">
              <input type="checkbox" v-model="useStoreHours" class="h-3 w-3" />
              <span class="text-[10px] uppercase tracking-label text-ink-ghost">Use store hours</span>
            </label>
          </div>
          <div v-if="!useStoreHours" class="grid grid-cols-2 gap-3">
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Open</label>
              <input type="time" v-model="customOpen" step="3600" v-bind="inputClass" />
            </div>
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Close</label>
              <input type="time" v-model="customClose" step="3600" v-bind="inputClass" />
            </div>
          </div>
        </div>

        <!-- Runs & threads -->
        <div class="space-y-2 pt-3 border-t border-rim">
          <p class="text-[10px] uppercase tracking-label text-ink-ghost">Execution</p>
          <div class="grid grid-cols-2 gap-3">
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Number of runs</label>
              <input type="number" v-model.number="form.runs" min="1" step="1" v-bind="inputClass" />
            </div>
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Max threads</label>
              <input type="number" v-model.number="form.max_threads" min="1" step="1" v-bind="inputClass" />
            </div>
          </div>
        </div>

        <!-- Customer behaviour -->
        <div class="space-y-2 pt-3 border-t border-rim">
          <p class="text-[10px] uppercase tracking-label text-ink-ghost">Customer behaviour</p>
          <div class="space-y-1">
            <label class="block text-[10px] font-mono text-ink-ghost/70">
              Mission share: {{ (form.mission_probability * 100).toFixed(0) }}%
            </label>
            <input type="range" v-model.number="form.mission_probability" min="0" max="1" step="0.05" class="w-full" />
            <div class="flex justify-between text-[10px] font-mono text-ink-ghost/60">
              <span>Default {{ ((1 - form.mission_probability) * 100).toFixed(0) }}%</span>
              <span>Mission {{ (form.mission_probability * 100).toFixed(0) }}%</span>
            </div>
          </div>
          <p v-if="extractedParams" class="text-[10px] font-mono text-ink-ghost/80 leading-relaxed">
            POS-derived:
            <template v-if="extractedParams.spawn_interval_seconds != null">
              ~{{ Number(extractedParams.spawn_interval_seconds).toFixed(1) }}s between arrivals
            </template>
            <template v-if="extractedParams.transaction_count != null">
              <span class="text-rim-bright mx-1">·</span>{{ formatCount(extractedParams.transaction_count) }} txns
            </template>
            <template v-if="extractedParams.avg_basket_value != null">
              <span class="text-rim-bright mx-1">·</span>avg basket {{ extractedParams.avg_basket_value.toFixed(2) }}
            </template>
          </p>
          <div class="grid grid-cols-2 gap-3">
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Spawn interval (s)</label>
              <input type="number" v-model.number="form.spawn_interval" min="0.1" step="0.1" v-bind="inputClass" />
            </div>
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Random seed</label>
              <input type="number" v-model.number="form.seed" min="0" step="1" v-bind="inputClass" />
            </div>
          </div>
        </div>

        <!-- Staffing -->
        <div class="space-y-2 pt-3 border-t border-rim">
          <p class="text-[10px] uppercase tracking-label text-ink-ghost">Staffing</p>
          <div class="grid grid-cols-2 gap-3">
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Stockers</label>
              <input type="number" v-model.number="form.num_stockers" min="0" step="1" v-bind="inputClass" />
            </div>
            <div class="space-y-1">
              <label class="block text-[10px] uppercase tracking-label text-ink-ghost">Cashiers</label>
              <input type="number" v-model.number="form.num_cashiers" min="0" step="1" v-bind="inputClass" />
            </div>
          </div>
          <div class="flex flex-col gap-1.5 pt-0.5">
            <label class="flex items-center gap-2 cursor-pointer">
              <input type="checkbox" v-model="form.auto_stock_tasks" class="h-3 w-3" />
              <span class="text-[10px] uppercase tracking-label text-ink-ghost">Auto-stock shelves</span>
            </label>
            <label class="flex items-center gap-2 cursor-pointer">
              <input type="checkbox" v-model="form.auto_register_tasks" class="h-3 w-3" />
              <span class="text-[10px] uppercase tracking-label text-ink-ghost">Auto-open registers</span>
            </label>
          </div>
        </div>

        <p v-if="error" class="text-[11px] font-mono text-danger pt-1">{{ error }}</p>

        <!-- High-workload warning -->
        <div v-if="highWorkloadWarning" class="pt-2 border-t border-rim">
          <p class="text-[11px] font-mono text-mustard leading-relaxed">
            ⚠ This configuration may take a long time. Consider reducing the date range, number of runs, or increasing the spawn interval (currently {{ form.spawn_interval }}s).
          </p>
        </div>
      </div>

      <!-- Footer actions -->
      <div class="px-4 py-3 border-t border-rim shrink-0 flex items-center justify-between gap-2">
        <p class="text-[10px] font-mono text-ink-ghost/50 flex-1 truncate">
          {{ form.runs }} run{{ form.runs !== 1 ? 's' : '' }}
          <template v-if="!useStoreHours"> · custom window</template>
        </p>
        <button
          @click="$emit('cancel')"
          class="px-4 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                 hover:text-ink hover:bg-surface-hover transition-colors"
          :disabled="running || saving"
        >Cancel</button>
        <button
          @click="startRun"
          class="px-4 py-1.5 text-[11px] font-mono border border-deep-teal/50 text-deep-teal
                 hover:bg-deep-teal/10 transition-colors disabled:opacity-40 disabled:cursor-not-allowed"
          :disabled="running || saving || !canRun || (layoutStore.isDirty && !layoutStore.filename && !saveName.trim())"
        >{{ buttonLabel }}</button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, watch } from "vue";
import { useLayoutStore } from "../../stores/layout";
import { useRunPresetsStore } from "../../stores/runPresets";
import axios from "axios";
import { fetchPosParams } from "../../api/client";

const api = axios.create({ baseURL: "/api" });

const layoutStore  = useLayoutStore();
const presetsStore = useRunPresetsStore();
const emit = defineEmits(["run-started", "cancel"]);

// ── Preset UI state ────────────────────────────────────────────────────────
const savePresetPrompt = ref(false);
const presetName       = ref("");

function promptSavePreset() {
  presetName.value = "";
  savePresetPrompt.value = true;
}

function confirmSavePreset() {
  if (!presetName.value.trim()) return;
  presetsStore.savePreset(presetName.value.trim(), form);
  savePresetPrompt.value = false;
  presetName.value = "";
}

function onLoadPreset(presetId) {
  if (!presetId) return;
  presetsStore.applyPreset(presetId, form);
}

// ── Today's ISO date string ────────────────────────────────────────────────
const todayIso = new Date().toISOString().slice(0, 10);

const form = reactive({
  label:               "",
  preset:              "single_day_n_runs",
  date_config: {
    start:        todayIso,
    end:          todayIso,
    date:         todayIso,
    days_of_week: [],
    weeks:        1,
    anchor:       todayIso,
    dates:        [],
  },
  runs:                1,
  max_threads:         1,
  mission_probability: 0.5,
  spawn_interval:      5.0,
  seed:                42,
  num_stockers:        2,
  num_cashiers:        1,
  auto_stock_tasks:    true,
  auto_register_tasks: true,
});

const useStoreHours = ref(true);
const customOpen    = ref("09:00");
const customClose   = ref("21:00");

const running        = ref(false);
const saving         = ref(false);
const error          = ref(null);
const posParamsNote   = ref("");
const extractedParams = ref(null);
const saveName       = ref(
  layoutStore.filename?.replace(/\.(yaml|yml)$/i, "") ?? "my_store"
);

// ── Presets ────────────────────────────────────────────────────────────────
const presets = [
  { id: "single_day_n_runs",  label: "1 day" },
  { id: "contiguous_range",   label: "Range" },
  { id: "weekday_repeat",     label: "Weekly" },
  { id: "custom_days",        label: "Custom" },
];

const weekdays = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];

function toggleDow(i) {
  const arr  = form.date_config.days_of_week;
  const idx  = arr.indexOf(i);
  if (idx === -1) arr.push(i);
  else arr.splice(idx, 1);
}

// ── Calendar (custom_days) ─────────────────────────────────────────────────
const calMonth = ref(todayIso.slice(0, 7));

const calMonthLabel = computed(() => {
  const [y, m] = calMonth.value.split("-");
  return new Date(Number(y), Number(m) - 1, 1)
    .toLocaleString("default", { month: "long", year: "numeric" });
});

const calCells = computed(() => {
  const [y, m] = calMonth.value.split("-").map(Number);
  const first  = new Date(y, m - 1, 1);
  const days   = new Date(y, m, 0).getDate();
  const startDow = (first.getDay() + 6) % 7; // Mon=0
  const cells  = [];
  for (let i = 0; i < startDow; i++) cells.push({ date: null, label: "" });
  for (let d = 1; d <= days; d++) {
    const iso = `${y}-${String(m).padStart(2, "0")}-${String(d).padStart(2, "0")}`;
    cells.push({ date: iso, label: String(d) });
  }
  return cells;
});

function prevMonth(ym) {
  const [y, m] = ym.split("-").map(Number);
  return m === 1 ? `${y - 1}-12` : `${y}-${String(m - 1).padStart(2, "0")}`;
}
function nextMonth(ym) {
  const [y, m] = ym.split("-").map(Number);
  return m === 12 ? `${y + 1}-01` : `${y}-${String(m + 1).padStart(2, "0")}`;
}
function toggleCustomDate(iso) {
  const arr = form.date_config.dates;
  const idx = arr.indexOf(iso);
  if (idx === -1) arr.push(iso);
  else arr.splice(idx, 1);
}

// ── Workload estimate warning ───────────────────────────────────────────────
const highWorkloadWarning = computed(() => {
  const dc = form.date_config;
  let days = 1;
  if (form.preset === "contiguous_range" && dc.start && dc.end) {
    const ms = new Date(dc.end) - new Date(dc.start);
    days = Math.max(1, Math.round(ms / 86400000) + 1);
  } else if (form.preset === "weekday_repeat" && dc.weeks && dc.days_of_week) {
    days = dc.weeks * dc.days_of_week.length;
  } else if (form.preset === "custom_days") {
    days = dc.dates.length;
  }
  const totalSimDays = days * form.runs;
  // Warn when spawn interval is aggressive or date range is large
  return totalSimDays > 14 || form.spawn_interval < 3.0;
});

// ── Validation ─────────────────────────────────────────────────────────────
const canRun = computed(() => {
  const dc = form.date_config;
  switch (form.preset) {
    case "contiguous_range":   return !!(dc.start && dc.end);
    case "single_day_n_runs":  return !!dc.date;
    case "weekday_repeat":     return !!(dc.anchor && dc.weeks && dc.days_of_week.length);
    case "custom_days":        return dc.dates.length > 0;
    default: return false;
  }
});

function formatCount(n) {
  if (n == null) return "?";
  return n >= 1000 ? (n / 1000).toFixed(1) + "k" : String(n);
}

async function applyPosParams(filename) {
  posParamsNote.value = "";
  extractedParams.value = null;
  if (!filename) return;
  try {
    const params = await fetchPosParams(filename);
    extractedParams.value = params;
    if (params.spawn_interval_seconds != null)
      form.spawn_interval = parseFloat(Number(params.spawn_interval_seconds).toFixed(2));
    posParamsNote.value = `Behaviour estimated from ${filename} (spawn + intraday curve when you run)`;
  } catch {
    extractedParams.value = null;
  }
}

watch(
  () => layoutStore.posCsvName,
  (name) => {
    applyPosParams(name);
  },
  { immediate: true }
);

// ── Button label ───────────────────────────────────────────────────────────
const buttonLabel = computed(() => {
  if (saving.value)  return "Saving…";
  if (running.value) return "Starting…";
  return layoutStore.isDirty ? "Save & Run ▶" : "▶ Run";
});

// ── Save gate ──────────────────────────────────────────────────────────────
async function ensureSaved() {
  if (!layoutStore.isDirty) return true;
  saving.value = true;
  error.value  = null;
  try {
    const name = layoutStore.filename
      ? layoutStore.filename.replace(/\.(yaml|yml)$/i, "")
      : saveName.value.trim() || "my_store";
    await layoutStore.saveLayout(name, true);
    return true;
  } catch (err) {
    error.value = err?.response?.data?.detail ?? err?.message ?? "Failed to save layout.";
    return false;
  } finally {
    saving.value = false;
  }
}

// ── Submit ─────────────────────────────────────────────────────────────────
async function startRun() {
  running.value = true;
  error.value   = null;
  try {
    const saved = await ensureSaved();
    if (!saved) return;

    const payload = {
      pos_dataset:         layoutStore.posCsvName || null,
      store_yaml:          layoutStore.filename,
      preset:              form.preset,
      date_config:         { ...form.date_config },
      time_window:         useStoreHours.value ? null : { open: customOpen.value, close: customClose.value },
      runs:                form.runs,
      max_threads:         form.max_threads,
      label:               form.label || null,
      seed:                form.seed,
      mission_probability: form.mission_probability,
      spawn_interval:      form.spawn_interval,
      num_stockers:        form.num_stockers,
      num_cashiers:        form.num_cashiers,
      auto_stock_tasks:    form.auto_stock_tasks,
      auto_register_tasks: form.auto_register_tasks,
      product_csv:         layoutStore.productsFile || null,
    };

    const { data } = await api.post("/simulations", payload);
    emit("run-started", data.sim_id);
  } catch (err) {
    error.value = err?.response?.data?.detail ?? err?.message ?? "Failed to start simulation.";
  } finally {
    running.value = false;
  }
}

const inputClass = {
  class: "w-full bg-surface-deep border border-rim px-2 py-1.5 text-[11px] font-mono text-ink focus:outline-none focus:border-tangerine transition-colors",
};
</script>
