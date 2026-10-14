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
          {{ ['Template & Size', 'Schedule', 'Data Files'][s - 1] }}
        </div>
      </div>

      <!-- ── Step 1: Template & Size ──────────────────────────────────────── -->
      <div v-if="step === 1" class="px-6 py-5 space-y-5">

        <!-- Store name -->
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

        <!-- Template picker -->
        <div class="space-y-2">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Starting template</label>
          <div class="grid grid-cols-2 gap-2">
            <button
              v-for="tpl in TEMPLATES"
              :key="tpl.id"
              @click="pickTemplate(tpl)"
              class="px-3 py-3 text-[11px] font-mono border transition-colors text-left"
              :class="form.templateId === tpl.id
                ? 'border-mustard bg-mustard/10 text-mustard'
                : 'border-rim text-ink-ghost hover:text-ink hover:bg-surface-hover'"
            >
              <div class="flex items-center gap-2 mb-0.5">
                <span class="text-base leading-none">{{ tpl.icon }}</span>
                <span class="font-medium">{{ tpl.label }}</span>
              </div>
              <div class="text-[10px] opacity-70 leading-snug">{{ tpl.description }}</div>
              <div class="text-[10px] mt-1 opacity-50">{{ tpl.defaultW }}×{{ tpl.defaultD }} ft</div>
            </button>
          </div>
        </div>

        <!-- Aisle layout (hidden for blank template) -->
        <div v-if="form.templateId !== 'blank'" class="space-y-2">
          <label class="block text-[11px] uppercase tracking-label text-ink-ghost">Aisle layout</label>
          <div class="grid grid-cols-3 gap-2">
            <button
              v-for="al in AISLE_LAYOUTS"
              :key="al.id"
              @click="form.aisleLayout = al.id"
              class="px-3 py-3 text-[11px] font-mono border transition-colors text-left"
              :class="form.aisleLayout === al.id
                ? 'border-mustard bg-mustard/10 text-mustard'
                : 'border-rim text-ink-ghost hover:text-ink hover:bg-surface-hover'"
            >
              <div class="flex items-center gap-2 mb-0.5">
                <span class="text-base leading-none">{{ al.icon }}</span>
                <span class="font-medium">{{ al.label }}</span>
              </div>
              <div class="text-[10px] opacity-70 leading-snug">{{ al.description }}</div>
            </button>
          </div>
        </div>

        <!-- Dimensions (pre-filled from template, user can override) -->
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
          <p v-if="posPendingName && !posUploadError" class="text-[10px] font-mono text-ink-ghost">
            Uploading {{ posPendingName }}…
          </p>
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
import { TEMPLATES, AISLE_LAYOUTS } from "../../stores/templates";

const emit = defineEmits(["created", "cancel"]);

const temporalStore = useTemporalStore();

const step           = ref(1);
const creating       = ref(false);
const posUploading    = ref(false);
const posUploadError  = ref("");
const posPendingName  = ref("");
const productUploading   = ref(false);
const productUploadError = ref("");

const allDays = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];

const form = reactive({
  name:            "My Store",
  templateId:      "blank",
  aisleLayout:     "parallel",
  widthFt:         TEMPLATES[0].defaultW,
  depthFt:         TEMPLATES[0].defaultD,
  daysOfOperation: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat"],
  defaultOpen:     "09:00",
  defaultClose:    "19:00",
  overrides:       Object.fromEntries(allDays.map((d) => [d, { enabled: false, open: "09:00", close: "19:00" }])),
  posCsv:          "",
  productCsv:      "",
});

function pickTemplate(tpl) {
  form.templateId = tpl.id;
  form.widthFt    = tpl.defaultW;
  form.depthFt    = tpl.defaultD;
}

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
  if (step.value === 1) return form.name.trim().length > 0 && form.widthFt >= 10 && form.depthFt >= 10 && !!form.templateId;
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
  posPendingName.value = file.name;
  try {
    const result = await uploadFile(file);
    // Refresh dataset list then select the uploaded file
    await temporalStore.fetchDatasets();
    form.posCsv = result.filename;
  } catch (err) {
    posUploadError.value = err?.message ?? "Upload failed";
  } finally {
    posUploading.value   = false;
    posPendingName.value = "";
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

    const meta = {
      name:            form.name,
      daysOfOperation: form.daysOfOperation,
      hours,
      productsFile:    form.productCsv || null,
      posCsvName:      form.posCsv || null,
      aisleLayout:     form.aisleLayout,
    };

    // Resolve selected template generator
    const tpl = TEMPLATES.find((t) => t.id === form.templateId) ?? TEMPLATES[0];
    const layout = tpl.build(form.widthFt, form.depthFt, meta);

    emit("created", layout, form.name);
  } finally {
    creating.value = false;
  }
}


onMounted(() => {
  if (!temporalStore.datasets?.length) temporalStore.fetchDatasets();
});
</script>
