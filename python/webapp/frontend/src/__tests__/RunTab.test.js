import { describe, it, expect, vi, beforeEach } from "vitest";
import { mount } from "@vue/test-utils";
import { createPinia, setActivePinia } from "pinia";

vi.mock("../api/client", () => ({
  fetchPosDatasets: vi.fn(async () => []),
  fetchPosParams: vi.fn(async () => ({
    spawn_interval_seconds: 210.0,
    transaction_count: 1500,
    avg_basket_value: 74.5,
  })),
  uploadFile: vi.fn(async () => ({ filename: "uploaded.yaml" })),
  submitSimulation: vi.fn(async () => ({ sim_id: "42", status: "queued" })),
  pollSimulationStatus: vi.fn(async () => ({ sim_id: "42", status: "complete" })),
  fetchSimulationResults: vi.fn(async () => ({ sim_id: "42", status: "complete" })),
  fetchSimulationRunDetail: vi.fn(async () => ({})),
  fetchSimulationHistory: vi.fn(async () => []),
  fetchSystemInfo: vi.fn(async () => ({ cpu_count: 4 })),
  deleteSimulation: vi.fn(async () => {}),
}));

import RunTab from "../components/RunTab.vue";
import { useTemporalStore } from "../stores/temporal";

describe("RunTab.vue", () => {
  let pinia;

  beforeEach(() => {
    localStorage.clear();
    pinia = createPinia();
    setActivePinia(pinia);
  });

  it("disables submit until dataset, YAML, and valid date range are set", async () => {
    const wrapper = mount(RunTab, { global: { plugins: [pinia] } });
    const submitBtn = wrapper.findAll("button").find((b) => b.text().includes("Run simulation"));
    expect(submitBtn.attributes("disabled")).toBeDefined();

    const store = useTemporalStore();
    store.datasets = [
      {
        filename: "cowboy_pos.csv",
        store_yaml: "cowboy_market.yaml",
        store_name: "Cowboy Market",
        days_of_operation: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat"],
        hours_of_operation: { Mon: { open: 9, close: 19 } },
      },
    ];
    store.selectDataset("cowboy_pos.csv");
    store.activeConfig.date_config.start = "2024-01-01";
    store.activeConfig.date_config.end = "2024-01-07";
    await wrapper.vm.$nextTick();

    expect(submitBtn.attributes("disabled")).toBeUndefined();
    expect(wrapper.text()).toContain("6 active days");
  });

  it("switches date presets cleanly", async () => {
    const wrapper = mount(RunTab, { global: { plugins: [pinia] } });
    const dayBtn = wrapper.findAll("button").find((b) => b.text() === "Day × N");
    await dayBtn.trigger("click");

    const store = useTemporalStore();
    expect(store.activeConfig.preset).toBe("single_day_n_runs");
    expect(wrapper.text()).toContain("1 active day");
  });
});
