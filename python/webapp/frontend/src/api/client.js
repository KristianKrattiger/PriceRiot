import axios from "axios";

const api = axios.create({
  baseURL: "/api"
});

export async function createRun(formData) {
  const { data } = await api.post("/runs", formData, {
    headers: { "Content-Type": "multipart/form-data" }
  });
  return data;
}

export async function listRuns() {
  const { data } = await api.get("/runs");
  return data;
}

export async function getRun(runId) {
  const { data } = await api.get(`/runs/${runId}`);
  return data;
}

export function streamRun(runId, onEvent) {
  const es = new EventSource(`/api/runs/${runId}/stream`);
  es.onmessage = (event) => {
    try {
      const payload = JSON.parse(event.data);
      onEvent(payload);
    } catch {
      // ignore parse errors
    }
  };
  return es;
}

export async function compareRuns(runIds) {
  const { data } = await api.post("/compare", { run_ids: runIds });
  return data;
}

export function downloadCsv(runId, kind) {
  window.open(`/api/runs/${runId}/${kind}.csv`, "_blank");
}

export async function getIngestionProfile(runId) {
  const { data } = await api.get(`/runs/${runId}/profile`);
  return data;
}

export async function deleteAllRuns() {
  const { data } = await api.delete("/runs");
  return data;
}

export async function deleteIncompleteRuns() {
  const { data } = await api.delete("/runs", { params: { incomplete_only: true } });
  return data;
}

// ---------------------------------------------------------------------------
// Simulation Control Plane — Temporal & Multi-Run
// ---------------------------------------------------------------------------

export async function uploadFile(file) {
  const form = new FormData();
  form.append("file", file);
  const { data } = await api.post("/upload", form, {
    headers: { "Content-Type": "multipart/form-data" },
  });
  return data; // { filename: "..." }
}

export async function fetchPosDatasets() {
  const { data } = await api.get("/pos-datasets");
  return data;
}

export async function fetchPosParams(filename) {
  const { data } = await api.get(`/pos-datasets/${encodeURIComponent(filename)}/params`);
  return data; // { spawn_interval_seconds, transaction_count, avg_basket_value }
}

export async function submitSimulation(payload) {
  const { data } = await api.post("/simulations", payload);
  return data;
}

export async function pollSimulationStatus(simId) {
  const { data } = await api.get(`/simulations/${simId}/status`);
  return data;
}

export async function fetchSimulationResults(simId) {
  const { data } = await api.get(`/simulations/${simId}/results`);
  return data;
}

export async function fetchSimulationRunDetail(simId, runIndex) {
  const { data } = await api.get(`/simulations/${simId}/runs/${runIndex}`);
  return data;
}

export async function fetchSimulationHistory() {
  const { data } = await api.get("/simulations");
  return data;
}

export function downloadSimRunCsv(simId, runIndex) {
  window.open(`/api/simulations/${simId}/runs/${runIndex}/transactions.csv`, "_blank");
}

export async function deleteSimulation(simId) {
  await api.delete(`/simulations/${simId}`);
}

export async function fetchSystemInfo() {
  const { data } = await api.get("/system/info");
  return data;
}

export function beaconDeleteIncompleteRuns() {
  // fetch with keepalive:true is fire-and-forget and survives page unload.
  // sendBeacon only supports POST; keepalive fetch supports DELETE.
  try {
    fetch("/api/runs?incomplete_only=true", { method: "DELETE", keepalive: true });
  } catch {
    // Best-effort — silently ignore if the page is already torn down.
  }
}

// ---------------------------------------------------------------------------
// Layout Editor API
// ---------------------------------------------------------------------------

export async function fetchLayouts() {
  const { data } = await api.get("/layouts");
  return data; // [{filename, directory, updated_at, units, node_count, edge_count}]
}

export async function fetchLayout(filename) {
  const { data } = await api.get(`/layouts/${encodeURIComponent(filename)}`);
  return data;
}

export async function saveLayout(name, overwrite, layout) {
  const { data } = await api.post("/layouts/save", { name, overwrite, layout });
  return data; // {filename, stem, directory}
}

export async function deleteLayout(filename) {
  await api.delete(`/layouts/${encodeURIComponent(filename)}`);
}

export async function fetchProducts(csvFilename) {
  const { data } = await api.get("/products", { params: { csv: csvFilename } });
  return data; // [{skuId, name, price, category}]
}

