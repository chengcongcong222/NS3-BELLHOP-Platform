import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { DepthSamplingInput, uniformDepths } from "../components/DepthSamplingInput";

describe("explicit uniform depth sampling", () => {
  it("includes endpoints and preserves deterministic output", () => {
    expect(uniformDepths(10, 100, 10, 120)).toEqual([10,20,30,40,50,60,70,80,90,100]);
    expect(uniformDepths(10, 20, 4, 120)).toEqual(uniformDepths(10, 20, 4, 120));
    expect(uniformDepths(10, 10, 1, 120)).toEqual([10]);
  });
  it.each([[0,10,2,100],[1,100,2,100],[20,10,2,100],[10,10,2,100],
    [10,20,1,100],[1,10,0,100],[1,10,65,100],[1,10,2.5,100],[1,NaN,2,100]])("rejects invalid coverage/count %j", (...args) => {
    expect(() => uniformDepths(...args as [number,number,number,number])).toThrow();
  });
  it("keeps historical non-uniform points until the user applies a new grid", () => {
    const change = vi.fn();
    render(<DepthSamplingInput label="发送" value="8, 20, 60" waterDepth={100} onChange={change} />);
    expect(screen.getByText(/保留原分布/)).toBeTruthy();
    fireEvent.change(screen.getByLabelText("发送深度采样点数"), {target:{value:"4"}});
    expect(change).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole("button", {name:"应用均分"}));
    expect(change).toHaveBeenCalledWith(uniformDepths(8,60,4,100).join(", "));
    expect(screen.getByRole("status").textContent).toContain("均分已应用");
  });
  it("does not overwrite the last valid grid on an invalid edit", () => {
    const change = vi.fn();
    render(<DepthSamplingInput label="接收" value="8, 60" waterDepth={100} onChange={change} />);
    fireEvent.change(screen.getByLabelText("接收深度终点（m）"), {target:{value:"100"}});
    fireEvent.click(screen.getByRole("button", {name:"应用均分"}));
    expect(change).not.toHaveBeenCalled();
    expect(screen.getByRole("alert").textContent).toContain("海底");
  });
});
