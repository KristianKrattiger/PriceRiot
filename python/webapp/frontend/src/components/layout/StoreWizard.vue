<template>
  <!-- Modal backdrop -->
  <div class="fixed inset-0 z-50 flex items-center justify-center bg-ink/40">
    <div class="bg-surface border border-rim w-full max-w-lg shadow-xl">

      <!-- Header -->
      <div class="flex items-center justify-between px-6 py-4 border-b border-rim">
        <div>
          <h2 class="text-sm font-medium text-ink">New Store Layout</h2>
          <p class="text-[10px] font-mono text-ink-ghost mt-0.5">Step {{ step }} of 3</p>
        </div>
        <button @click="$emit('cancel')" class="text-ink-ghost hover:text-ink transition-colors text-lg leading-none">✕</button>
      </div>

      <!-- Step indicators -->
      <div class="flex border-b border-rim">
        <div
          v-for="s in 3" :key="s"
          class="flex-1 py-2 text-center text-[10px] font-mono uppercase tracking-label transition-colors"
          :class="s === step
            ? 'text-mustard border-b-2 border-mustard -mb-px'
            : s < step
              ? 'text-ink-ghost'
              : 'text-ink-ghost/40'"
        >
          {{ ['Shape & Size', 'Schedule', 'Data Files'][s - 1] }}
        </div>
      </div>

      <!-- ── Step 1: Shape & Size ─────────────────────────────────────────── -->
      <div v-if="step === 1" class="px-6 py-5 space-y-5">

        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Store name</label>
          <input
            v-model="form.name"
            type="text"
            placeholder="e.g. My Market"
            class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                   placeholder:text-ink-ghost/50 focus:outline-none focus:border-tangerine transition-colors"
          />
        </div>

        <div class="space-y-2">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Floor plan shape</label>
          <div class="grid grid-cols-2 gap-2">
            <!-- Rectangle — only enabled shape in Phase 1 -->
            <button
              @click="form.shape = 'rectangle'"
              class="px-3 py-3 text-[11px] font-mono border transition-colors text-left"
              :class="form.shape === 'rectangle'
                ? 'border-mustard bg-mustard/10 text-mustard'
                : 'border-rim text-ink-ghost hover:text-ink hover:bg-surface-hover'"
            >
              <div class="font-medium">Rectangle</div>
              <div class="text-[10px] opacity-70">Standard rectangular footprint</div>
            </button>

            <!-- Phase 2+ shapes — disabled -->
            <button
              v-for="shape in ['L-shape', 'T-shape', 'U-shape', 'Custom polygon']"
              :key="shape"
              disabled
              class="px-3 py-3 text-[11px] font-mono border border-rim text-ink-ghost/30 text-left cursor-not-allowed relative"
            >
              <div class="font-medium">{{ shape }}</div>
              <div class="text-[10px]">Phase 2</div>
            </button>
          </div>
        </div>

        <div class="grid grid-cols-2 gap-4">
          <div class="space-y-1.5">
            <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Width (ft)</label>
            <input
              v-model.number="form.widthFt"
              type="number" min="10" step="1"
              class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors"
            />
          </div>
          <div class="space-y-1.5">
            <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Depth (ft)</label>
            <input
              v-model.number="form.depthFt"
              type="number" min="10" step="1"
              class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                     focus:outline-none focus:border-tangerine transition-colors"
            />
          </div>
        </div>

        <!-- Auto-computed sqft -->
        <p class="text-[11px] font-mono text-ink-ghost">
          ≈ {{ sqFootage.toLocaleString() }} sq ft
          <span v-if="sqFootage > 0" class="ml-2">
            ({{ sizeCategory }})
          </span>
        </p>
      </div>

      <!-- ── Step 2: Schedule ──────────────────────────────────────────────── -->
      <div v-if="step === 2" class="px-6 py-5 space-y-5">

        <div class="space-y-2">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Days open</label>
          <div class="flex flex-wrap gap-1.5">
            <button
              v-for="day in allDays"
              :key="day"
              @click="toggleDay(day)"
              class="px-3 py-1.5 text-[11px] font-mono border transition-colors"
              :class="form.daysOfOperation.includes(day)
                ? 'border-mustard bg-mustard/10 text-mustard'
                : 'border-rim text-ink-ghost hover:text-ink hover:bg-surface-hover'"
            >{{ day }}</button>
          </div>
        </div>

        <div class="space-y-3">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Default hours</label>
          <div class="grid grid-cols-2 gap-4">
            <div class="space-y-1.5">
              <label class="block text-[10px] text-ink-ghost">Open</label>
              <input
                v-model="form.defaultOpen"
                type="time"
                class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                       focus:outline-none focus:border-tangerine transition-colors"
              />
            </div>
            <div class="space-y-1.5">
              <label class="block text-[10px] text-ink-ghost">Close</label>
              <input
                v-model="form.defaultClose"
                type="time"
                class="w-full bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                       focus:outline-none focus:border-tangerine transition-colors"
              />
            </div>
          </div>
        </div>

        <!-- Per-day overrides for open days -->
        <div v-if="form.daysOfOperation.length" class="space-y-2">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Per-day overrides (optional)</label>
          <div
            v-for="day in form.daysOfOperation"
            :key="day"
            class="flex items-center gap-3 text-[11px] font-mono"
          >
            <span class="w-8 text-ink-dim">{{ day }}</span>
            <label class="flex items-center gap-1.5 text-ink-ghost cursor-pointer">
              <input type="checkbox" v-model="form.overrides[day].enabled" />
              Override
            </label>
            <template v-if="form.overrides[day].enabled">
              <input
                v-model="form.overrides[day].open"
                type="time"
                class="bg-surface-deep border border-rim px-2 py-1 font-mono text-ink text-[11px]
                       focus:outline-none focus:border-tangerine"
              />
              <span class="text-ink-ghost">–</span>
              <input
                v-model="form.overrides[day].close"
                type="time"
                class="bg-surface-deep border border-rim px-2 py-1 font-mono text-ink text-[11px]
                       focus:outline-none focus:border-tangerine"
              />
            </template>
          </div>
        </div>
      </div>

      <!-- ── Step 3: Data Files ────────────────────────────────────────────── -->
      <div v-if="step === 3" class="px-6 py-5 space-y-5">
        <p class="text-[11px] text-ink-ghost">
          Both files are optional — you can add them later from the Layout tab.
        </p>

        <!-- POS CSV: pick existing or upload new -->
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">POS data (CSV)</label>
          <div class="flex gap-2">
            <select
              v-model="form.posCsv"
              class="flex-1 bg-surface-deep border border-rim px-3 py-2 text-sm font-mono text-ink
                     focus:outline-none focus:border-tangerine"
            >
              <option value="">None selected</option>
              <option v-for="ds in datasets" :key="ds.filename" :value="ds.filename">
                {{ ds.store_name || ds.filename }}
              </option>
            </select>
            <input type="file" accept=".csv" ref="posCsvInput" class="hidden" @change="onPosCsvUpload" />
            <button
              @click="$refs.posCsvInput.click()"
              :disabled="posUploading"
              class="px-3 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                     hover:bg-surface-hover transition-colors shrink-0 whitespace-nowrap"
            >{{ posUploading ? "Uploading…" : "↑ Upload" }}</button>
          </div>
          <p v-if="posUploadError" class="text-[10px] font-mono text-danger">{{ posUploadError }}</p>
        </div>

        <!-- Product CSV -->
        <div class="space-y-1.5">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Product catalog (CSV, optional)</label>
          <div class="flex gap-2 items-center">
            <input type="file" accept=".csv" ref="productFileInput" class="hidden" @change="onProductUpload" />
            <div
              @click="$refs.productFileInput.click()"
              class="flex-1 bg-surface-deep border border-rim px-3 py-2 text-sm font-mono cursor-pointer
                     hover:border-tangerine transition-colors"
              :class="form.productCsv ? 'text-ink' : 'text-ink-ghost'"
            >
              <span v-if="productUploading" class="text-ink-ghost">Uploading…</span>
              <span v-else>{{ form.productCsv || "No file selected…" }}</span>
            </div>
            <button
              @click="$refs.productFileInput.click()"
              :disabled="productUploading"
              class="px-3 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                     hover:bg-surface-hover transition-colors shrink-0"
            >Browse</button>
          </div>
          <p v-if="productUploadError" class="text-[10px] font-mono text-danger">{{ productUploadError }}</p>
        </div>
      </div>

      <!-- Footer -->
      <div class="flex items-center justify-between px-6 py-4 border-t border-rim bg-surface-deep">
        <button
          v-if="step > 1"
          @click="step--"
          class="px-4 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                 hover:text-ink hover:bg-surface-hover transition-colors"
        >← Back</button>
        <div v-else />

        <div class="flex gap-2">
          <button
            @click="$emit('cancel')"
            class="px-4 py-1.5 text-[11px] font-mono border border-rim text-ink-ghost
                   hover:text-ink hover:bg-surface-hover transition-colors"
          >Cancel</button>

          <button
            v-if="step < 3"
            :disabled="!canProceed"
            @click="step++"
            class="px-4 py-1.5 text-[11px] font-mono border transition-colors"
            :class="canProceed
              ? 'border-mustard bg-mustard/10 text-mustard hover:bg-mustard/20'
              : 'border-rim text-ink-ghost/40 cursor-not-allowed'"
          >Next →</button>

          <button
            v-else
            :disabled="creating"
            @click="finish"
            class="px-4 py-1.5 text-[11px] font-mono border border-mustard
                   bg-mustard/10 text-mustard hover:bg-mustard/20 transition-colors"
          >{{ creating ? "Creating…" : "Create layout →" }}</button>
        </div>
      </div>

    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted } from "vue";
import { uploadFile } from "../../api/client";
import { useTemporalStore } from "../../stores/temporal";

const emit = defineEmits(["created", "cancel"]);

const temporalStore = useTemporalStore();

const step           = ref(1);
const creating       = ref(false);
const posUploading   = ref(false);
const posUploadError = ref("");
const productUploading   = ref(false);
const productUploadError = ref("");

const allDays = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];

const form = reactive({
  name:            "My Store",
  shape:           "rectangle",
  widthFt:         120,
  depthFt:         80,
  daysOfOperation: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat"],
  defaultOpen:     "09:00",
  defaultClose:    "19:00",
  overrides:       Object.fromEntries(allDays.map((d) => [d, { enabled: false, open: "09:00", close: "19:00" }])),
  posCsv:          "",
  productCsv:      "",
});

const datasets = computed(() => temporalStore.datasets ?? []);

const sqFootage = computed(() => Math.round(form.widthFt * form.depthFt));

const sizeCategory = computed(() => {
  const sf = sqFootage.value;
  if (sf < 2000)  return "kiosk/corner store";
  if (sf < 10000) return "small grocery";
  if (sf < 40000) return "neighborhood market";
  if (sf < 80000) return "supermarket";
  if (sf < 150000) return "large supermarket";
  return "warehouse/big-box";
});

const canProceed = computed(() => {
  if (step.value === 1) return form.name.trim().length > 0 && form.widthFt >= 10 && form.depthFt >= 10;
  if (step.value === 2) return form.daysOfOperation.length > 0;
  return true;
});

function toggleDay(day) {
  const idx = form.daysOfOperation.indexOf(day);
  if (idx >= 0) form.daysOfOperation.splice(idx, 1);
  else          form.daysOfOperation.push(day);
}

async function onPosCsvUpload(e) {
  const file = e.target.files?.[0];
  if (!file) return;
  posUploading.value   = true;
  posUploadError.value = "";
  try {
    const result = await uploadFile(file);
    // Refresh dataset list then select the uploaded file
    await temporalStore.fetchDatasets();
    form.posCsv = result.filename;
  } catch (err) {
    posUploadError.value = err?.message ?? "Upload failed";
  } finally {
    posUploading.value = false;
    e.target.value = "";
  }
}

async function onProductUpload(e) {
  const file = e.target.files?.[0];
  if (!file) return;
  // Show filename immediately for feedback, then upload in background
  form.productCsv          = file.name;
  productUploading.value   = true;
  productUploadError.value = "";
  try {
    const result = await uploadFile(file);
    form.productCsv = result.filename;
  } catch (err) {
    productUploadError.value = err?.message ?? "Upload failed";
    form.productCsv = "";
  } finally {
    productUploading.value = false;
    e.target.value = "";
  }
}

async function finish() {
  creating.value = true;
  try {
    // Build hours dict
    const hours = {};
    for (const day of form.daysOfOperation) {
      const ov = form.overrides[day];
      hours[day] = ov.enabled
        ? { open: ov.open, close: ov.close }
        : { open: form.defaultOpen, close: form.defaultClose };
    }

    // Build the layout JSON locally (mirrors layout_serializer.wizard_default_layout)
    const layout = buildWizardLayout(form.name, form.widthFt, form.depthFt,
                                     form.daysOfOperation, hours,
                                     form.productCsv || null);
    emit("created", layout, form.name);
  } finally {
    creating.value = false;
  }
}

/** Mirror of layout_serializer.wizard_default_layout — built client-side. */
function buildWizardLayout(name, widthFt, depthFt, daysOfOperation, hours, productsFile) {
  const cx = widthFt / 2;
  const cy = depthFt / 2;
  return {
    units:        "feet",
    productsFile: productsFile,
    shape:        "rectangle",
    boundary: [
      { x: 0,       y: 0       },
      { x: widthFt, y: 0       },
      { x: widthFt, y: depthFt },
      { x: 0,       y: depthFt },
    ],
    nodes: [
      {
        id: 1, type: "Entrance",
        x: 0,       y: cy,
        width: 10,  length: 8,
        shelfLeft: 0, shelfRight: 0,
        blockedFraction: 0, personalSpace: 1.0,
        jamDensity: 3.5, dwellTime: 0, serviceRate: 0,
        entryRate: 1.0, exitRate: 0.0,
      },
      {
        id: 2, type: "Junction",
        x: cx,      y: cy,
        width: 6,   length: 6,
        shelfLeft: 0, shelfRight: 0,
        blockedFraction: 0, personalSpace: 1.0,
        jamDensity: 3.5, dwellTime: 0, serviceRate: 0,
        entryRate: 0.0, exitRate: 0.0,
      },
      {
        id: 3, type: "Exit",
        x: widthFt, y: cy,
        width: 10,  length: 8,
        shelfLeft: 0, shelfRight: 0,
        blockedFraction: 0, personalSpace: 1.0,
        jamDensity: 3.5, dwellTime: 0, serviceRate: 0,
        entryRate: 0.0, exitRate: 1.0,
      },
    ],
    edges: [
      {
        id: 1, nodeA: 1, nodeB: 2,
        length: parseFloat(cx.toFixed(2)), width: 12,
        shelfLeft: 0, shelfRight: 0,
        freeSpeed: 1.2, jamDensity: 3.5, blockedFraction: 0,
        flow: "bi", orientation: "fwd", edgeType: "wall",
      },
      {
        id: 2, nodeA: 2, nodeB: 3,
        length: parseFloat(cx.toFixed(2)), width: 12,
        shelfLeft: 0, shelfRight: 0,
        freeSpeed: 1.2, jamDensity: 3.5, blockedFraction: 0,
        flow: "bi", orientation: "fwd", edgeType: "wall",
      },
    ],
    shelves:        [],
    freeObjects:    [],
    planogram:      {},
    checkoutQueues: [],
    schedule: { daysOfOperation, hours },
    _wizardNew: true,
  };
}

onMounted(() => {
  if (!temporalStore.datasets?.length) temporalStore.fetchDatasets();
});
</script>
