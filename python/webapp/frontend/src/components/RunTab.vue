<template>
  <div class="grid grid-cols-1 lg:grid-cols-2 gap-8">

    <!-- ── LEFT: Configuration ───────────────────────────────────────────── -->
    <div class="space-y-6">
      <h2 class="text-[11px] uppercase tracking-label text-rust font-medium">
        Run configuration
      </h2>

      <!-- ── Identity ── -->
      <div class="space-y-3">
        <p class="text-[11px] uppercase tracking-label text-rust/70">Identity</p>
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">
            Label (optional)
          </label>
          <input
            v-model="cfg.label"
            type="text"
            placeholder="e.g. Christmas week baseline"
            class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                   placeholder:text-ink-ghost/50 focus:outline-none focus:border-tangerine transition-colors duration-150"
          />
        </div>
      </div>

      <!-- ── Data files ── -->
      <div class="space-y-3 pt-4 border-t border-rim">
        <p class="text-[11px] uppercase tracking-label text-rust/70">Data files</p>

        <!-- POS dataset picker + upload -->
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">POS data source</label>
          <div class="flex gap-2">
            <select
              v-model="cfg.pos_dataset"
              class="flex-1 bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors duration-150"
              @change="onDatasetChange"
            >
              <option value="" disabled>Select a dataset…</option>
              <option v-for="ds in store.datasets" :key="ds.filename" :value="ds.filename">
                {{ ds.store_name }} — {{ ds.filename }}
              </option>
            </select>
            <input type="file" accept=".csv" ref="posInput" class="hidden" @change="onPosUpload" />
            <button
              class="px-3 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                     hover:bg-surface-hover transition-colors shrink-0"
              @click="$refs.posInput.click()"
            >{{ uploadingPos ? "Uploading…" : "Upload CSV" }}</button>
          </div>
          <p v-if="selectedDs" class="text-[11px] font-mono text-ink-ghost">
            {{ selectedDs.store_name }}
            <span class="text-rim-bright mx-1">·</span>
            {{ datasetDateRange }}
            <span class="text-rim-bright mx-1">·</span>
            {{ formatCount(selectedDs.record_count) }} records
            <span v-if="extractedParams" class="text-rim-bright mx-1">·</span>
            <span v-if="extractedParams">
              ~{{ extractedParams.spawn_interval_seconds }}s between arrivals
              <span v-if="extractedParams.avg_basket_value">
                · avg basket <span class="text-green">${{ extractedParams.avg_basket_value }}</span>
              </span>
            </span>
          </p>
          <p v-else-if="store.datasetsLoading" class="text-[11px] text-ink-ghost">Loading datasets…</p>
          <p v-else-if="store.datasets.length === 0" class="text-[11px] text-ink-ghost">
            No datasets found. Upload a POS CSV above.
          </p>
        </div>

        <!-- Store layout YAML -->
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Store layout YAML</label>
          <div class="flex gap-2">
            <input type="file" accept=".yaml,.yml" ref="yamlInput" class="hidden" @change="onYamlUpload" />
            <div
              class="flex-1 bg-surface-deep border border-rim px-3 py-2 text-sm font-mono truncate cursor-pointer
                     hover:border-tangerine transition-colors duration-150"
              :class="cfg.store_yaml ? 'text-ink' : 'text-ink-ghost'"
              @click="$refs.yamlInput.click()"
            >{{ cfg.store_yaml || "No file selected…" }}</div>
            <button
              class="px-3 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                     hover:bg-surface-hover transition-colors shrink-0"
              @click="$refs.yamlInput.click()"
            >{{ uploadingYaml ? "Uploading…" : "Upload YAML" }}</button>
          </div>
        </div>

        <!-- Product catalog CSV -->
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Product catalog CSV (optional)</label>
          <div class="flex gap-2">
            <input type="file" accept=".csv" ref="productInput" class="hidden" @change="onProductUpload" />
            <div
              class="flex-1 bg-surface-deep border border-rim px-3 py-2 text-sm font-mono truncate cursor-pointer
                     hover:border-tangerine transition-colors duration-150"
              :class="uploadedProductName ? 'text-ink' : 'text-ink-ghost'"
              @click="$refs.productInput.click()"
            >{{ uploadedProductName || "No file selected…" }}</div>
            <button
              class="px-3 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                     hover:bg-surface-hover transition-colors shrink-0"
              @click="$refs.productInput.click()"
            >{{ uploadingProduct ? "Uploading…" : "Upload CSV" }}</button>
          </div>
        </div>
      </div>

      <!-- ── Temporal Settings ── -->
      <div class="space-y-3 pt-4 border-t border-rim">
        <p class="text-[11px] uppercase tracking-label text-rust/70">Temporal Settings</p>

        <!-- Date range -->
        <div class="space-y-3 pt-3 border-t border-rim/50">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Date range</label>

          <div class="flex gap-0 border border-rim">
            <button
              v-for="preset in presets"
              :key="preset.id"
              class="flex-1 py-1.5 text-[11px] font-medium uppercase tracking-label transition-colors duration-150 border-r border-rim last:border-r-0"
              :class="cfg.preset === preset.id
                ? 'bg-mustard/20 text-mustard'
                : 'text-ink-ghost hover:text-ink hover:bg-surface-hover'"
              @click="cfg.preset = preset.id"
            >{{ preset.label }}</button>
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
            <p class="text-[11px] font-mono text-ink-ghost">
              1 active day, repeated {{ cfg.runs }} time{{ cfg.runs !== 1 ? "s" : "" }}
            </p>
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
                >{{ day }}</button>
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
              <div class="grid grid-cols-7 gap-0.5">
                <div
                  v-for="d in ['M','T','W','T','F','S','S']"
                  :key="d + Math.random()"
                  class="text-center text-[10px] font-mono text-ink-ghost/50 py-0.5"
                >{{ d }}</div>
                <div
                  v-for="(cell, idx) in calCells"
                  :key="idx"
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

        <!-- Time window -->
        <div class="space-y-3 pt-3 border-t border-rim/50">
          <div class="flex items-center justify-between">
            <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Time window</label>
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

        <!-- Run parameters -->
        <div class="space-y-3 pt-3 border-t border-rim/50">
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
        </div>
      </div>

      <!-- ── Customer behaviour ── -->
      <div class="space-y-3 pt-4 border-t border-rim">
        <p class="text-[11px] uppercase tracking-label text-rust/70">Customer behaviour</p>
        <div class="space-y-2">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">
            Mission shopper share
          </label>
          <input
            v-model.number="cfg.mission_probability"
            type="range" min="0" max="1" step="0.05"
            class="w-full"
          />
          <div class="flex justify-between font-mono text-[11px] text-ink-ghost">
            <span>Default {{ ((1 - cfg.mission_probability) * 100).toFixed(0) }}%</span>
            <span>Mission {{ (cfg.mission_probability * 100).toFixed(0) }}%</span>
          </div>
        </div>
        <div class="grid grid-cols-2 gap-4">
          <div class="space-y-1.5">
            <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Spawn interval (s)</label>
            <input
              type="number" v-model.number="cfg.spawn_interval" min="0.1" step="0.5"
              class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors"
            />
          </div>
          <div class="space-y-1.5">
            <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Random seed</label>
            <input
              type="number" v-model.number="cfg.seed" min="0" step="1"
              class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors"
            />
          </div>
        </div>
      </div>

      <!-- ── Staffing ── -->
      <div class="space-y-3 pt-4 border-t border-rim">
        <p class="text-[11px] uppercase tracking-label text-rust/70">Staffing</p>
        <div class="grid grid-cols-2 gap-4">
          <div class="space-y-1.5">
            <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Stockers</label>
            <input
              type="number" v-model.number="cfg.num_stockers" min="0" step="1"
              class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors"
            />
          </div>
          <div class="space-y-1.5">
            <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Cashiers</label>
            <input
              type="number" v-model.number="cfg.num_cashiers" min="0" step="1"
              class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors"
            />
          </div>
        </div>
        <div class="flex flex-col gap-2 pt-1">
          <div class="flex items-center gap-2">
            <input id="autoStockTasks" type="checkbox" v-model="cfg.auto_stock_tasks" class="h-3.5 w-3.5" />
            <label for="autoStockTasks" class="text-[11px] uppercase tracking-label text-ink-ghost cursor-pointer">
              Auto-stock shelves
            </label>
          </div>
          <div class="flex items-center gap-2">
            <input id="autoRegisterTasks" type="checkbox" v-model="cfg.auto_register_tasks" class="h-3.5 w-3.5" />
            <label for="autoRegisterTasks" class="text-[11px] uppercase tracking-label text-ink-ghost cursor-pointer">
              Auto-open registers
            </label>
          </div>
        </div>
      </div>

      <!-- ── Summary + action ── -->
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

    <!-- ── RIGHT: Temporal simulation history ─────────────────────────────── -->
    <div class="space-y-4 lg:sticky lg:top-6 lg:self-start">
      <div class="flex items-center justify-between">
        <h2 class="text-[11px] uppercase tracking-label text-rust font-medium">
          Recent simulations
        </h2>
      </div>

      <div class="space-y-1 max-h-[600px] overflow-y-auto">
        <div
          v-for="sim in store.simulations"
          :key="sim.sim_id"
          class="group flex items-center justify-between px-3 py-2.5 border transition-colors duration-150"
          :class="sim.sim_id === store.activeSimulation?.sim_id
            ? 'border-y border-r border-rim border-l-2 border-l-tangerine bg-surface-hover'
            : 'border-rim hover:bg-surface-hover'"
        >
          <div class="min-w-0 flex-1 cursor-pointer" @click="viewSim(sim.sim_id)">
            <p class="text-sm font-mono text-ink">
              {{ sim.config?.label || "Simulation #" + sim.sim_id }}
            </p>
            <p class="text-[11px] text-ink-ghost truncate">
              {{ simDescription(sim) }}
            </p>
          </div>
          <div class="flex items-center gap-2 shrink-0">
            <span
              class="inline-flex items-center px-2 py-0.5 text-[10px] font-mono border cursor-pointer"
              :class="statusClass(sim.status)"
              @click="viewSim(sim.sim_id)"
            >{{ sim.status }}</span>
            <button
              class="text-[11px] font-mono text-ink-ghost/40 hover:text-danger transition-colors px-1"
              :title="'Delete simulation #' + sim.sim_id"
              @click.stop="deleteSim(sim.sim_id)"
            >×</button>
          </div>
        </div>

        <p v-if="store.simulations.length === 0" class="text-[11px] text-ink-ghost py-4">
          No simulations yet. Configure and run one with the form on the left.
        </p>
      </div>
    </div>

  </div>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from "vue";
import { useTemporalStore } from "../stores/temporal";
import { uploadFile, fetchPosParams } from "../api/client";

const emit = defineEmits(["submitted"]);

const store = useTemporalStore();
const cfg   = store.activeConfig;

const submitError = ref("");

// ── Dataset ────────────────────────────────────────────────────────────────
const selectedDs = computed(() => store.selectedDataset);
const extractedParams = ref(null);

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

async function applyPosParams(filename) {
  try {
    const params = await fetchPosParams(filename);
    extractedParams.value = params;
    if (params.spawn_interval_seconds) {
      cfg.spawn_interval = params.spawn_interval_seconds;
    }
  } catch {
    extractedParams.value = null;
  }
}

function onDatasetChange() {
  store.selectDataset(cfg.pos_dataset);
  if (cfg.pos_dataset) applyPosParams(cfg.pos_dataset);
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
  const { days_of_week: dow, weeks, anchor } = cfg.date_config;
  if (!dow.length || !weeks || !anchor) return null;
  const dayNames = dow.map((i) => weekdays[i]).join(", ");
  const total = dow.length * weeks;
  return `Every ${dayNames} for ${weeks} week${weeks !== 1 ? "s" : ""} starting ${anchor} → ${total} active day${total !== 1 ? "s" : ""}`;
});

// ── Custom days calendar ───────────────────────────────────────────────────
const today    = new Date();
const calMonth = ref({ year: today.getFullYear(), month: today.getMonth() });

const calMonthLabel = computed(() => {
  const d = new Date(calMonth.value.year, calMonth.value.month, 1);
  return d.toLocaleDateString("en-GB", { month: "long", year: "numeric" });
});

function prevMonth(m) {
  return m.month === 0 ? { year: m.year - 1, month: 11 } : { year: m.year, month: m.month - 1 };
}
function nextMonth(m) {
  return m.month === 11 ? { year: m.year + 1, month: 0 } : { year: m.year, month: m.month + 1 };
}

const calCells = computed(() => {
  const { year, month } = calMonth.value;
  const first = new Date(year, month, 1);
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
const customOpen    = ref("09:00");
const customClose   = ref("21:00");

watch(useStoreHours, (v) => {
  cfg.time_window = v ? null : { open: customOpen.value, close: customClose.value };
});
watch([customOpen, customClose], ([o, c]) => {
  if (!useStoreHours.value) cfg.time_window = { open: o, close: c };
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

const PERIOD_COLORS = {
  morning: "#E2DFD1",
  midday:  "#CEC9B6",
  evening: "#C9980A22",
  close:   "#8B5A3C22",
};

const periodBars = computed(() => {
  const oh = parseInt(effectiveOpen.value, 10);
  const ch = parseInt(effectiveClose.value, 10);
  if (isNaN(oh) || isNaN(ch) || ch <= oh) return [];
  const total = ch - oh;
  const raw = [
    { name: "morning", lo: null, hi: 11.0 },
    { name: "midday",  lo: 11.0, hi: 14.0 },
    { name: "evening", lo: 14.0, hi: 17.0 },
    { name: "close",   lo: 17.0, hi: null  },
  ];
  return raw.map((p) => {
    const start = p.lo == null ? oh : Math.max(p.lo, oh);
    const end   = p.hi == null ? ch : Math.min(p.hi, ch);
    if (end <= start) return null;
    return {
      name:  p.name,
      pct:   ((end - start) / total) * 100,
      hours: `${String(Math.floor(start)).padStart(2, "0")}:00–${String(Math.floor(end)).padStart(2, "00")}:00`,
      color: PERIOD_COLORS[p.name] || "#E2DFD1",
    };
  }).filter(Boolean);
});

// ── Active day count ───────────────────────────────────────────────────────
const _DOW_MAP = { Mon: 0, Tue: 1, Wed: 2, Thu: 3, Fri: 4, Sat: 5, Sun: 6 };

const activeDayCount = computed(() => {
  const p = cfg.preset;
  if (p === "single_day_n_runs") return 1;
  if (p === "weekday_repeat") {
    const { days_of_week: dow, weeks } = cfg.date_config;
    if (!dow.length || !weeks) return null;
    return dow.length * weeks;
  }
  if (p === "custom_days") return cfg.date_config.dates.length || null;
  const { start, end } = cfg.date_config;
  if (!start || !end || end < start) return null;
  const ds  = selectedDs.value;
  const dop = ds?.days_of_operation || [];
  const operatingDow = new Set(dop.map((d) => _DOW_MAP[d]).filter((n) => n !== undefined));
  if (operatingDow.size === 0) [0, 1, 2, 3, 4, 5].forEach((d) => operatingDow.add(d));
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
  if (cfg.preset === "contiguous_range")  return !!(cfg.date_config.start && cfg.date_config.end && cfg.date_config.end >= cfg.date_config.start);
  if (cfg.preset === "single_day_n_runs") return !!cfg.date_config.date;
  if (cfg.preset === "weekday_repeat")    return !!(cfg.date_config.anchor && cfg.date_config.weeks && cfg.date_config.days_of_week.length);
  if (cfg.preset === "custom_days")       return cfg.date_config.dates.length > 0;
  return false;
});

// ── Keyboard shortcut: Cmd/Ctrl+Enter ─────────────────────────────────────
function onKeydown(e) {
  if ((e.metaKey || e.ctrlKey) && e.key === "Enter" && canSubmit.value && !store.submitting) submit();
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

// ── File uploads ──────────────────────────────────────────────────────────
const uploadingPos       = ref(false);
const uploadingYaml      = ref(false);
const uploadingProduct   = ref(false);
const uploadedProductName = ref("");

async function onPosUpload(e) {
  const file = e.target.files?.[0];
  if (!file) return;
  uploadingPos.value = true;
  try {
    const { filename } = await uploadFile(file);
    // Refresh datasets so the new file appears, then select it
    await store.fetchDatasets();
    store.selectDataset(filename);
    cfg.pos_dataset = filename;
    await applyPosParams(filename);
  } catch {
    // non-fatal
  } finally {
    uploadingPos.value = false;
    e.target.value = "";
  }
}

async function onYamlUpload(e) {
  const file = e.target.files?.[0];
  if (!file) return;
  uploadingYaml.value = true;
  try {
    const { filename } = await uploadFile(file);
    cfg.store_yaml = filename;
  } catch {
    // non-fatal
  } finally {
    uploadingYaml.value = false;
    e.target.value = "";
  }
}

async function onProductUpload(e) {
  const file = e.target.files?.[0];
  if (!file) return;
  uploadingProduct.value = true;
  try {
    const { filename } = await uploadFile(file);
    cfg.product_csv = filename;
    uploadedProductName.value = filename;
  } catch {
    // non-fatal
  } finally {
    uploadingProduct.value = false;
    e.target.value = "";
  }
}

// ── History ────────────────────────────────────────────────────────────────
function viewSim(simId) {
  store.viewSimulation(simId);
  emit("submitted", simId);
}

async function deleteSim(simId) {
  try {
    await store.deleteSimulation(simId);
  } catch {
    // non-fatal
  }
}

function simDescription(sim) {
  const preset = sim.config?.preset?.replace(/_/g, " ") || "";
  const runs   = sim.config?.runs;
  return [preset, runs != null ? `${runs} run${runs !== 1 ? "s" : ""}` : null].filter(Boolean).join(" · ");
}

function statusClass(status) {
  if (status === "complete") return "border-tangerine/40 bg-rust/10 text-rust";
  if (status === "running")  return "border-violet/40 bg-violet/10 text-violet";
  if (status === "failed")   return "border-danger/40 bg-danger/10 text-danger";
  return "border-rim text-ink-ghost";
}
</script>
