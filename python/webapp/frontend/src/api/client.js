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

