import { describe, it, expect, vi, beforeEach } from "vitest";

const { mockGet, mockPost, mockDelete } = vi.hoisted(() => ({
  mockGet: vi.fn(),
  mockPost: vi.fn(),
  mockDelete: vi.fn(),
}));

vi.mock("axios", () => ({
  default: {
    create: vi.fn(() => ({
      get: mockGet,
      post: mockPost,
      delete: mockDelete,
    })),
  },
}));

import {
  updateRunWorkers,
  createSweep,
  listSweeps,
  getSweep,
  getIngestionProfile,
  fetchPosParams,
  submitSimulation,
} from "../api/client";

describe("API client", () => {
  beforeEach(() => {
    mockGet.mockReset();
    mockPost.mockReset();
    mockDelete.mockReset();
  });

  it("posts worker configuration updates to /runs/{id}/workers", async () => {
    mockPost.mockResolvedValue({ data: { run_id: "7", status: "queued" } });
    const res = await updateRunWorkers("7", { num_stockers: 3, num_cashiers: 2 });
    expect(mockPost).toHaveBeenCalledWith("/runs/7/workers", {
      num_stockers: 3,
      num_cashiers: 2,
    });
    expect(res.run_id).toBe("7");
  });

  it("creates, lists, and fetches parameter sweeps", async () => {
    mockPost.mockResolvedValue({ data: { sweep_id: "1", status: "complete" } });
    const created = await createSweep({ store_yaml: "cowboy_market.yaml" });
    expect(mockPost).toHaveBeenCalledWith(
      "/sweeps",
      { store_yaml: "cowboy_market.yaml" },
      { params: undefined }
    );
    expect(created.sweep_id).toBe("1");

    mockGet.mockResolvedValue({ data: [{ sweep_id: "1" }] });
    const all = await listSweeps();
    expect(mockGet).toHaveBeenCalledWith("/sweeps");
    expect(all).toHaveLength(1);

    mockGet.mockResolvedValue({ data: { sweep_id: "1", total_points: 4 } });
    const one = await getSweep("1");
    expect(mockGet).toHaveBeenCalledWith("/sweeps/1");
    expect(one.total_points).toBe(4);
  });

  it("fetches ingestion profile and POS params with encoded filenames", async () => {
    mockGet.mockResolvedValue({ data: { spawn_interval_seconds: 120.5 } });
    const profile = await getIngestionProfile("3");
    expect(mockGet).toHaveBeenCalledWith("/runs/3/profile");
    expect(profile.spawn_interval_seconds).toBe(120.5);

    const params = await fetchPosParams("cowboy pos.csv");
    expect(mockGet).toHaveBeenCalledWith("/pos-datasets/cowboy%20pos.csv/params");
    expect(params.spawn_interval_seconds).toBe(120.5);
  });

  it("submits temporal simulation payloads", async () => {
    mockPost.mockResolvedValue({ data: { sim_id: "9" } });
    const res = await submitSimulation({ store_yaml: "cowboy_market.yaml", runs: 2 });
    expect(mockPost).toHaveBeenCalledWith("/simulations", {
      store_yaml: "cowboy_market.yaml",
      runs: 2,
    });
    expect(res.sim_id).toBe("9");
  });
});
