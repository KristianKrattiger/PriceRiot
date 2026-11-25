import { describe, it, expect, vi, beforeEach } from "vitest";
import { setActivePinia, createPinia } from "pinia";

vi.mock("../api/client", () => ({
  fetchPosDatasets: vi.fn(async () => [
    {
      filename: "cowboy_pos.csv",
      store_yaml: "cowboy_market.yaml",
      store_name: "Cowboy Market",
      days_of_operation: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat"],
      hours_of_operation: { Mon: { open: 9, close: 19 } },
    },
  ]),
  submitSimulation: vi.fn(async () => ({ sim_id: "11", status: "queued" })),
  pollSimulationStatus: vi.fn(async () => ({ sim_id: "11", status: "complete" })),
  fetchSimulationResults: vi.fn(async () => ({ sim_id: "11", status: "complete" })),
  fetchSimulationRunDetail: vi.fn(async () => ({ run_index: 1 })),
  fetchSimulationHistory: vi.fn(async () => []),
  fetchSystemInfo: vi.fn(async () => ({ cpu_count: 8 })),
  deleteSimulation: vi.fn(async () => {}),
}));

import { useTemporalStore } from "../stores/temporal";

describe("useTemporalStore", () => {
  beforeEach(() => {
    localStorage.clear();
    setActivePinia(createPinia());
  });

  it("loads datasets and selects matching store YAML", async () => {
    const store = useTemporalStore();
    await store.fetchDatasets();
    expect(store.datasets).toHaveLength(1);

    store.selectDataset("cowboy_pos.csv");
    expect(store.activeConfig.pos_dataset).toBe("cowboy_pos.csv");
    expect(store.activeConfig.store_yaml).toBe("cowboy_market.yaml");
    expect(store.selectedDataset?.store_name).toBe("Cowboy Market");
  });

  it("builds submission payload matching backend schema", () => {
    const store = useTemporalStore();
    store.activeConfig.pos_dataset = "cowboy_pos.csv";
    store.activeConfig.store_yaml = "cowboy_market.yaml";
    store.activeConfig.runs = 3;
    store.activeConfig.num_stockers = 4;
    store.activeConfig.num_cashiers = 2;

    const payload = store._buildPayload();
    expect(payload.pos_dataset).toBe("cowboy_pos.csv");
    expect(payload.store_yaml).toBe("cowboy_market.yaml");
    expect(payload.runs).toBe(3);
    expect(payload.num_stockers).toBe(4);
    expect(payload.num_cashiers).toBe(2);
    expect(payload.label).toBeNull();
  });

  it("updates system CPU count and removes deleted simulations", async () => {
    const store = useTemporalStore();
    await store.fetchSystemInfo();
    expect(store.cpuCount).toBe(8);
    expect(store.activeConfig.max_threads).toBe(8);

    store.simulations = [
      { sim_id: "1", status: "complete" },
      { sim_id: "2", status: "complete" },
    ];
    store.viewSimulation("1");
    expect(store.activeSimulation?.sim_id).toBe("1");

    await store.deleteSimulation("1");
    expect(store.simulations).toHaveLength(1);
    expect(store.activeSimulation?.sim_id).toBe("2");
  });
});
