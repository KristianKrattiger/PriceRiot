<template>
  <div class="space-y-6">

    <!-- ── Section 1: Dataset ─────────────────────────────────────────────── -->
    <div class="space-y-3">
      <p class="text-[11px] uppercase tracking-label text-rust font-medium">Dataset</p>

      <div class="space-y-1.5">
        <label class="block text-[11px] uppercase tracking-label text-ink-ghost">
          POS data source
        </label>
        <select
          v-model="cfg.pos_dataset"
          class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                 focus:outline-none focus:border-tangerine transition-colors duration-150"
          @change="onDatasetChange"
        >
          <option value="" disabled>Select a dataset…</option>
          <option v-for="ds in store.datasets" :key="ds.filename" :value="ds.filename">
            {{ ds.store_name }} — {{ ds.filename }}
          </option>
        </select>

        <!-- Inline summary -->
        <p v-if="selectedDs" class="text-[11px] font-mono text-ink-ghost">
          {{ selectedDs.store_name }}
          <span class="text-rim-bright mx-1">·</span>
          {{ datasetDateRange }}
          <span class="text-rim-bright mx-1">·</span>
          {{ formatCount(selectedDs.record_count) }} records
        </p>
        <p v-else-if="store.datasetsLoading" class="text-[11px] text-ink-ghost">Loading datasets…</p>
        <p v-else-if="store.datasets.length === 0" class="text-[11px] text-ink-ghost">
          No datasets found. Upload a POS CSV via the Run tab first.
        </p>
      </div>
    </div>

    <!-- ── Section 2: Preset + Date Config ──────────────────────────────────── -->
    <div class="space-y-3 pt-4 border-t border-rim">
      <p class="text-[11px] uppercase tracking-label text-rust font-medium">Date range</p>

      <!-- Preset tabs -->
      <div class="flex gap-0 border border-rim">
        <button
          v-for="preset in presets"
          :key="preset.id"
          class="flex-1 py-1.5 text-[11px] font-medium uppercase tracking-label transition-colors duration-150 border-r border-rim last:border-r-0"
          :class="cfg.preset === preset.id
            ? 'bg-mustard/20 text-mustard'
            : 'text-ink-ghost hover:text-ink hover:bg-surface-hover'"
          @click="cfg.preset = preset.id"
        >
          {{ preset.label }}
        </button>
      </div>

      <!-- Contiguous range -->
      <div v-if="cfg.preset === 'contiguous_range'" class="grid grid-cols-2 gap-4">
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Start date</label>
          <input
            type="date" v-model="cfg.date_config.start"
            class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                   focus:outline-none focus:border-tangerine transition-colors"
          />
        </div>
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">End date</label>
          <input
            type="date" v-model="cfg.date_config.end"
            class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                   focus:outline-none focus:border-tangerine transition-colors"
          />
        </div>
        <p v-if="activeDayCount !== null" class="col-span-2 text-[11px] font-mono text-ink-ghost">
          ≈ {{ activeDayCount }} active day{{ activeDayCount !== 1 ? "s" : "" }} (based on store schedule)
        </p>
      </div>

      <!-- Single day N runs -->
      <div v-else-if="cfg.preset === 'single_day_n_runs'" class="space-y-1.5">
        <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Date</label>
        <input
          type="date" v-model="cfg.date_config.date"
          class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                 focus:outline-none focus:border-tangerine transition-colors"
        />
        <p class="text-[11px] font-mono text-ink-ghost">1 active day, repeated {{ cfg.runs }} time{{ cfg.runs !== 1 ? "s" : "" }}</p>
      </div>

      <!-- Weekday repeat -->
      <div v-else-if="cfg.preset === 'weekday_repeat'" class="space-y-3">
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Days of week</label>
          <div class="flex gap-1.5 flex-wrap">
            <button
              v-for="(day, i) in weekdays"
              :key="i"
              class="px-2.5 py-1 text-[11px] font-mono border transition-colors duration-150"
              :class="cfg.date_config.days_of_week.includes(i)
                ? 'border-mustard bg-mustard/20 text-mustard'
                : 'border-rim text-ink-ghost hover:border-ink-dim hover:text-ink'"
              @click="toggleDow(i)"
            >
              {{ day }}
            </button>
          </div>
        </div>
        <div class="grid grid-cols-2 gap-4">
          <div class="space-y-1.5">
            <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Anchor (start)</label>
            <input
              type="date" v-model="cfg.date_config.anchor"
              class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors"
            />
          </div>
          <div class="space-y-1.5">
            <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Weeks</label>
            <input
              type="number" v-model.number="cfg.date_config.weeks" min="1" step="1"
              class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors"
            />
          </div>
        </div>
        <p v-if="weekdayPreview" class="text-[11px] font-mono text-ink-ghost">{{ weekdayPreview }}</p>
      </div>

      <!-- Custom days calendar -->
      <div v-else-if="cfg.preset === 'custom_days'" class="space-y-3">
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">
            Select dates
            <span class="ml-2 normal-case font-sans text-ink-ghost/60">click to toggle</span>
          </label>
          <!-- Month navigation -->
          <div class="flex items-center justify-between mb-2">
            <button
              class="px-2 py-0.5 text-[11px] font-mono text-ink-ghost border border-rim
                     hover:bg-surface-hover transition-colors"
              @click="calMonth = prevMonth(calMonth)"
            >‹</button>
            <span class="text-[11px] font-mono text-ink">{{ calMonthLabel }}</span>
            <button
              class="px-2 py-0.5 text-[11px] font-mono text-ink-ghost border border-rim
                     hover:bg-surface-hover transition-colors"
              @click="calMonth = nextMonth(calMonth)"
            >›</button>
          </div>
          <!-- Calendar grid -->
          <div class="grid grid-cols-7 gap-0.5">
            <div v-for="d in ['M','T','W','T','F','S','S']" :key="d + Math.random()"
              class="text-center text-[10px] font-mono text-ink-ghost/50 py-0.5">{{ d }}</div>
            <div v-for="(cell, idx) in calCells" :key="idx"
              class="text-center text-[11px] font-mono py-1 rounded-sm cursor-pointer transition-colors duration-100"
              :class="cell.date
                ? (cfg.date_config.dates.includes(cell.date)
                    ? 'bg-mustard/25 text-mustard border border-mustard/40'
                    : 'text-ink hover:bg-surface-hover border border-transparent')
                : 'text-transparent cursor-default'"
              @click="cell.date && toggleCustomDate(cell.date)"
            >{{ cell.label }}</div>
          </div>
          <p v-if="cfg.date_config.dates.length > 0" class="text-[11px] font-mono text-ink-ghost">
            {{ cfg.date_config.dates.length }} date{{ cfg.date_config.dates.length !== 1 ? "s" : "" }} selected
          </p>
        </div>
      </div>
    </div>

    <!-- ── Section 3: Time Window ─────────────────────────────────────────── -->
    <div class="space-y-3 pt-4 border-t border-rim">
      <div class="flex items-center justify-between">
        <p class="text-[11px] uppercase tracking-label text-rust font-medium">Time window</p>
        <label class="flex items-center gap-2 cursor-pointer">
          <input type="checkbox" v-model="useStoreHours" class="h-3.5 w-3.5" />
          <span class="text-[11px] uppercase tracking-label text-ink-ghost">Use store hours</span>
        </label>
      </div>

      <div v-if="!useStoreHours" class="grid grid-cols-2 gap-4">
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Open</label>
          <input
            type="time" v-model="customOpen" step="3600"
            class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                   focus:outline-none focus:border-tangerine transition-colors"
          />
        </div>
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Close</label>
          <input
            type="time" v-model="customClose" step="3600"
            class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                   focus:outline-none focus:border-tangerine transition-colors"
          />
        </div>
      </div>

      <!-- Period preview bar -->
      <div class="space-y-1">
        <div class="flex h-7 overflow-hidden border border-rim/60">
          <div
            v-for="period in periodBars"
            :key="period.name"
            :style="{ width: period.pct + '%', background: period.color }"
            class="relative flex items-center justify-center overflow-hidden"
            :title="`${period.name}: ${period.hours}`"
          >
            <span class="text-[9px] font-mono uppercase tracking-label text-ink/70 truncate px-1">
              {{ period.name }}
            </span>
          </div>
        </div>
        <div class="flex justify-between text-[10px] font-mono text-ink-ghost/70">
          <span>{{ effectiveOpen }}:00</span>
          <span>{{ effectiveClose }}:00</span>
        </div>
      </div>
    </div>

    <!-- ── Section 4: Run Parameters ─────────────────────────────────────── -->
    <div class="space-y-3 pt-4 border-t border-rim">
      <p class="text-[11px] uppercase tracking-label text-rust font-medium">Run parameters</p>
      <div class="grid grid-cols-2 gap-4">
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Number of runs</label>
          <input
            type="number" v-model.number="cfg.runs" min="1" step="1"
            class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                   focus:outline-none focus:border-tangerine transition-colors"
          />
        </div>
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Max threads</label>
          <input
            type="number" v-model.number="cfg.max_threads" :min="1" :max="store.cpuCount" step="1"
            class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                   focus:outline-none focus:border-tangerine transition-colors"
          />
          <p class="text-[10px] text-ink-ghost/60">Runs execute in parallel up to this limit</p>
        </div>
      </div>

      <div class="space-y-1.5">
        <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Label (optional)</label>
        <input
          type="text" v-model="cfg.label" placeholder="e.g. Christmas week baseline"
          class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                 placeholder:text-ink-ghost/50 focus:outline-none focus:border-tangerine transition-colors"
        />
      </div>
    </div>

    <!-- ── Footer: summary + submit ──────────────────────────────────────── -->
    <div class="pt-4 border-t border-rim space-y-3">
      <p class="text-[11px] font-mono text-ink-ghost">
        <span v-if="activeDayCount !== null">
          {{ activeDayCount }} active day{{ activeDayCount !== 1 ? "s" : "" }}
          <span class="text-rim-bright mx-1">·</span>
        </span>
        {{ cfg.runs }} run{{ cfg.runs !== 1 ? "s" : "" }}
        <span v-if="activeDayCount !== null">
          <span class="text-rim-bright mx-1">·</span>
          {{ activeDayCount * cfg.runs }} total sim-day{{ activeDayCount * cfg.runs !== 1 ? "s" : "" }}
        </span>
      </p>

      <button
        class="w-full py-2.5 text-sm font-medium uppercase tracking-label transition-colors duration-150"
        :class="canSubmit && !store.submitting
          ? 'bg-rust/80 hover:bg-rust text-surface'
          : 'bg-rim text-ink-ghost cursor-not-allowed'"
        :disabled="!canSubmit || store.submitting"
        @click="submit"
      >
        {{ store.submitting ? "Running…" : "Run simulation" }}
      </button>

      <p v-if="submitError" class="text-[11px] text-danger font-mono">{{ submitError }}</p>
    </div>
  </div>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from "vue";
import { useTemporalStore } from "../stores/temporal";

const emit = defineEmits(["submitted"]);

const store = useTemporalStore();
const cfg   = store.activeConfig;   // reactive proxy

const submitError = ref("");

// ── Dataset selection ──────────────────────────────────────────────────────
const selectedDs = computed(() => store.selectedDataset);

const datasetDateRange = computed(() => {
  const ds = selectedDs.value;
  if (!ds || (!ds.date_from && !ds.date_to)) return "";
  if (ds.date_from && ds.date_to) {
    const from = ds.date_from.slice(0, 7);
    const to   = ds.date_to.slice(0, 7);
    return from === to ? from : `${from} – ${to}`;
  }
  return ds.date_from || ds.date_to;
});

function onDatasetChange() {
  store.selectDataset(cfg.pos_dataset);
}

function formatCount(n) {
  if (n == null) return "?";
  return n >= 1000 ? (n / 1000).toFixed(1) + "k" : String(n);
}

// ── Presets ────────────────────────────────────────────────────────────────
const presets = [
  { id: "contiguous_range",  label: "Range"    },
  { id: "single_day_n_runs", label: "Day × N"  },
  { id: "weekday_repeat",    label: "Weekdays" },
  { id: "custom_days",       label: "Custom"   },
];

// ── Weekday repeat ─────────────────────────────────────────────────────────
const weekdays = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];

function toggleDow(i) {
  const arr = cfg.date_config.days_of_week;
  const idx = arr.indexOf(i);
  if (idx === -1) arr.push(i);
  else arr.splice(idx, 1);
}

const weekdayPreview = computed(() => {
  const dow = cfg.date_config.days_of_week;
  const weeks = cfg.date_config.weeks;
  const anchor = cfg.date_config.anchor;
  if (!dow.length || !weeks || !anchor) return null;
  const dayNames = dow.map((i) => weekdays[i]).join(", ");
  const total = dow.length * weeks;
  return `Every ${dayNames} for ${weeks} week${weeks !== 1 ? "s" : ""} starting ${anchor} → ${total} active day${total !== 1 ? "s" : ""}`;
});

// ── Custom days calendar ───────────────────────────────────────────────────
const today = new Date();
const calMonth = ref({ year: today.getFullYear(), month: today.getMonth() }); // 0-indexed month

const calMonthLabel = computed(() => {
  const d = new Date(calMonth.value.year, calMonth.value.month, 1);
  return d.toLocaleDateString("en-GB", { month: "long", year: "numeric" });
});

function prevMonth(m) {
  if (m.month === 0) return { year: m.year - 1, month: 11 };
  return { year: m.year, month: m.month - 1 };
}
function nextMonth(m) {
  if (m.month === 11) return { year: m.year + 1, month: 0 };
  return { year: m.year, month: m.month + 1 };
}

const calCells = computed(() => {
  const { year, month } = calMonth.value;
  const first = new Date(year, month, 1);
  // Monday-start: getDay() returns 0=Sun, so offset = (dow + 6) % 7
  const offset = (first.getDay() + 6) % 7;
  const daysInMonth = new Date(year, month + 1, 0).getDate();
  const cells = [];
  for (let i = 0; i < offset; i++) cells.push({ date: null, label: "" });
  for (let d = 1; d <= daysInMonth; d++) {
    const iso = `${year}-${String(month + 1).padStart(2, "0")}-${String(d).padStart(2, "0")}`;
    cells.push({ date: iso, label: String(d) });
  }
  return cells;
});

function toggleCustomDate(iso) {
  const arr = cfg.date_config.dates;
  const idx = arr.indexOf(iso);
  if (idx === -1) arr.push(iso);
  else arr.splice(idx, 1);
}

// ── Time window ────────────────────────────────────────────────────────────
const useStoreHours = ref(true);
const customOpen  = ref("09:00");
const customClose = ref("21:00");

watch(useStoreHours, (v) => {
  if (v) {
    cfg.time_window = null;
  } else {
    cfg.time_window = { open: customOpen.value, close: customClose.value };
  }
});
watch([customOpen, customClose], ([o, c]) => {
  if (!useStoreHours.value) {
    cfg.time_window = { open: o, close: c };
  }
});

const effectiveOpen = computed(() => {
  if (useStoreHours.value) {
    const ds = selectedDs.value;
    if (ds?.hours_of_operation) {
      const first = Object.values(ds.hours_of_operation)[0];
      if (first?.open != null) return String(first.open).padStart(2, "0");
    }
    return "09";
  }
  return customOpen.value.slice(0, 2);
});

const effectiveClose = computed(() => {
  if (useStoreHours.value) {
    const ds = selectedDs.value;
    if (ds?.hours_of_operation) {
      const first = Object.values(ds.hours_of_operation)[0];
      if (first?.close != null) return String(first.close).padStart(2, "0");
    }
    return "21";
  }
  return customClose.value.slice(0, 2);
});

// Period preview bar — mirrors _PERIOD_BOUNDARIES from temporal.py
const PERIOD_COLORS = {
  morning: "#E2DFD1",  // surface-deep
  midday:  "#CEC9B6",  // rim
  evening: "#C9980A22", // mustard tint
  close:   "#8B5A3C22", // rust tint
};
const PERIOD_TEXT = {
  morning: "#6A6760",
  midday:  "#6A6760",
  evening: "#C9980A",
  close:   "#8B5A3C",
};

const periodBars = computed(() => {
  const oh = parseInt(effectiveOpen.value, 10);
  const ch = parseInt(effectiveClose.value, 10);
  if (isNaN(oh) || isNaN(ch) || ch <= oh) return [];

  const total = ch - oh;
  const raw = [
    { name: "morning", lo: null,  hi: 11.0 },
    { name: "midday",  lo: 11.0,  hi: 14.0 },
    { name: "evening", lo: 14.0,  hi: 17.0 },
    { name: "close",   lo: 17.0,  hi: null  },
  ];

  return raw
    .map((p) => {
      const start = p.lo == null ? oh : Math.max(p.lo, oh);
      const end   = p.hi == null ? ch : Math.min(p.hi, ch);
      if (end <= start) return null;
      const duration = end - start;
      return {
        name:  p.name,
        pct:   (duration / total) * 100,
        hours: `${String(Math.floor(start)).padStart(2,"0")}:00–${String(Math.floor(end)).padStart(2,"0")}:00`,
        color: PERIOD_COLORS[p.name] || "#E2DFD1",
      };
    })
    .filter(Boolean);
});

// ── Active day count ───────────────────────────────────────────────────────
const _DOW_MAP = { Mon: 0, Tue: 1, Wed: 2, Thu: 3, Fri: 4, Sat: 5, Sun: 6 };

const activeDayCount = computed(() => {
  const p = cfg.preset;

  if (p === "single_day_n_runs") return 1;

  if (p === "weekday_repeat") {
    const dow = cfg.date_config.days_of_week;
    const weeks = cfg.date_config.weeks;
    if (!dow.length || !weeks) return null;
    return dow.length * weeks;
  }

  if (p === "custom_days") {
    return cfg.date_config.dates.length || null;
  }

  // contiguous_range — estimate using store's days_of_operation
  const start = cfg.date_config.start;
  const end   = cfg.date_config.end;
  if (!start || !end || end < start) return null;

  const ds  = selectedDs.value;
  const dop = ds?.days_of_operation || [];
  const operatingDow = new Set(dop.map((d) => _DOW_MAP[d]).filter((n) => n !== undefined));
  if (operatingDow.size === 0) {
    // Fallback: assume Mon–Sat (6 days)
    [0, 1, 2, 3, 4, 5].forEach((d) => operatingDow.add(d));
  }

  let count = 0;
  const cur = new Date(start);
  const fin = new Date(end);
  while (cur <= fin) {
    if (operatingDow.has(cur.getDay() === 0 ? 6 : cur.getDay() - 1)) count++;
    cur.setDate(cur.getDate() + 1);
  }
  return count;
});

// ── Validation ─────────────────────────────────────────────────────────────
const canSubmit = computed(() => {
  if (!cfg.pos_dataset || !cfg.store_yaml) return false;
  if (cfg.preset === "contiguous_range") return !!(cfg.date_config.start && cfg.date_config.end && cfg.date_config.end >= cfg.date_config.start);
  if (cfg.preset === "single_day_n_runs") return !!cfg.date_config.date;
  if (cfg.preset === "weekday_repeat")    return !!(cfg.date_config.anchor && cfg.date_config.weeks && cfg.date_config.days_of_week.length);
  if (cfg.preset === "custom_days")       return cfg.date_config.dates.length > 0;
  return false;
});

// ── Keyboard shortcut: Cmd/Ctrl+Enter ─────────────────────────────────────
function onKeydown(e) {
  if ((e.metaKey || e.ctrlKey) && e.key === "Enter" && canSubmit.value && !store.submitting) {
    submit();
  }
}
onMounted(() => window.addEventListener("keydown", onKeydown));
onBeforeUnmount(() => window.removeEventListener("keydown", onKeydown));

// ── Submit ─────────────────────────────────────────────────────────────────
async function submit() {
  submitError.value = "";
  try {
    const simId = await store.submitSimulation();
    emit("submitted", simId);
  } catch (err) {
    submitError.value = err?.response?.data?.detail || err?.message || "Submission failed";
  }
}
</script>
