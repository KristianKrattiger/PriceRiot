import { describe, it, expect } from "vitest";
import { mount } from "@vue/test-utils";
import KPICard from "../components/KPICard.vue";
import IngestionProfile from "../components/IngestionProfile.vue";

describe("KPICard.vue", () => {
  it("renders label, value, subtitle, and optional financial styling", () => {
    const wrapper = mount(KPICard, {
      props: {
        label: "Total Revenue",
        value: "$1,234.56",
        sub: "6 open days",
        money: true,
        accent: "#008A31",
      },
    });
    expect(wrapper.text()).toContain("Total Revenue");
    expect(wrapper.text()).toContain("$1,234.56");
    expect(wrapper.text()).toContain("6 open days");
    expect(wrapper.find(".text-green").exists()).toBe(true);
    expect(wrapper.find('[style*="background-color"]').exists()).toBe(true);
  });

  it("omits accent bar and subtitle when not provided", () => {
    const wrapper = mount(KPICard, {
      props: { label: "Transactions", value: "420" },
    });
    expect(wrapper.find(".text-brick").exists()).toBe(true);
    expect(wrapper.find('[style*="background-color"]').exists()).toBe(false);
  });
});

describe("IngestionProfile.vue", () => {
  it("renders nothing when profile is null", () => {
    const wrapper = mount(IngestionProfile, { props: { profile: null } });
    expect(wrapper.text()).toBe("");
  });

  it("formats spawn interval and transaction count when profile is provided", () => {
    const wrapper = mount(IngestionProfile, {
      props: {
        profile: { spawn_interval_seconds: 145.25, transaction_count: 2400 },
      },
    });
    expect(wrapper.text()).toContain("POS data profile");
    expect(wrapper.text()).toContain("145.3s");
    expect(wrapper.text()).toContain("2,400");
  });
});
