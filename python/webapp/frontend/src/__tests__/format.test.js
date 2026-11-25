import { describe, it, expect } from "vitest";
import { formatNumber, formatCurrency } from "../utils/format";

describe("format utilities", () => {
  it("formats numbers with requested decimal places", () => {
    expect(formatNumber(1234, 0)).toContain("1");
    expect(formatNumber(12.345, 2)).toBe("12.35");
  });

  it("returns '-' for null, undefined, and NaN", () => {
    expect(formatNumber(null)).toBe("-");
    expect(formatNumber(undefined)).toBe("-");
    expect(formatNumber("not-a-number")).toBe("-");
    expect(formatCurrency(null)).toBe("-");
    expect(formatCurrency(NaN)).toBe("-");
  });

  it("formats currency values", () => {
    const out = formatCurrency(42.5, "USD");
    expect(out).toContain("42.50");
    expect(out).toContain("$");
  });
});
