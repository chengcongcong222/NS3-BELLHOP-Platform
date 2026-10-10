import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { DepthSamplingInput, uniformDepths } from "../components/DepthSamplingInput";
import { automaticSampling, depthSamples, restoreSampling, samplingBudget } from "../domain/environmentSampling";

describe("automatic water-column sampling", () => {
  it("includes endpoints and preserves deterministic output", () => {
    expect(uniformDepths(10, 100, 10, 120)).toEqual([10,20,30,40,50,60,70,80,90,100]);
    expect(uniformDepths(10, 20, 4, 120)).toEqual(uniformDepths(10, 20, 4, 120));
    expect(uniformDepths(10, 10, 1, 120)).toEqual([10]);
  });
  it.each([[0,10,2,100],[1,100,2,100],[20,10,2,100],[10,10,2,100],
    [10,20,1,100],[1,10,0,100],[1,10,65,100],[1,10,2.5,100],[1,NaN,2,100]])("rejects invalid coverage/count %j", (...args) => {
    expect(() => uniformDepths(...args as [number,number,number,number])).toThrow();
  });
  it("derives the range from water depth, not the count limit", () => {
    expect(depthSamples(automaticSampling("3"), 175).points).toEqual([1,87.5,174]);
    expect(depthSamples(automaticSampling("3"), 100).points).toEqual([1,50,99]);
    expect(depthSamples(automaticSampling("1"), 175).points).toEqual([87.5]);
    expect(depthSamples(automaticSampling("3"), 1).points).toEqual([.01,.5,.99]);
    expect(depthSamples(automaticSampling("65"), 175).error).toContain("与水深无关");
    expect(depthSamples(automaticSampling(""), 175).points).toEqual([]);
  });
  it("updates immediately without exposing mandatory start/end/apply controls", () => {
    const change = vi.fn();
    render(<DepthSamplingInput label="发送" value={automaticSampling()} waterDepth={175} onChange={change} />);
    expect(screen.queryByLabelText("发送深度起点（m）")).toBeNull();
    expect(screen.queryByRole("button", {name:"应用均分"})).toBeNull();
    fireEvent.change(screen.getByLabelText("发送深度采样点数"), {target:{value:"4"}});
    expect(change).toHaveBeenCalledWith(automaticSampling("4"));
  });
  it("preserves historical non-uniform arrays until an explicit mode change", () => {
    const change = vi.fn(), original = [8,20,60];
    const restored = restoreSampling(original, undefined, 175);
    render(<DepthSamplingInput label="发送" value={restored} waterDepth={175} onChange={change} />);
    expect(depthSamples(restored, 175).points).toEqual(original);
    expect(change).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole("button", {name:"改为全水深均分"}));
    expect(change).toHaveBeenCalledWith(automaticSampling("3"));
    expect(restoreSampling(original, automaticSampling("3"), 175).mode).toBe("custom");
    expect(restoreSampling([1,87.5,174], automaticSampling("3"), 175).mode).toBe("automatic");
  });
  it("jointly validates the exact Test5 counts instead of accepting each field alone", () => {
    const depths = depthSamples(automaticSampling("64"), 175).points;
    const budget = samplingBudget(depths, depths, 3000);
    expect(budget.cells).toBe(12_288_000);
    expect(budget.errors).toHaveLength(2);
    expect(budget.maxRanges).toBe(48);
    expect(samplingBudget(depths, depths, 1000).errors).toHaveLength(1);
    expect(samplingBudget(depths, depths, 48).errors).toHaveLength(0);
    expect(samplingBudget(depths, depths, 49).errors).toHaveLength(1);
  });
});
