import { fireEvent, render, screen } from "@testing-library/react";
import { useState } from "react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import { AlgorithmParameters } from "../components/AlgorithmParameters";
import { algorithmDefaults, algorithmIssues, isAlgorithmCatalog } from "../domain/algorithms";
import { algorithmCatalog } from "./algorithmFixtures";

describe("installed algorithm configuration", () => {
  it("lets a person type a fractional second without losing the decimal point", async () => {
    const algorithm = algorithmCatalog.algorithms[0];
    function Editor() {
      const [value, setValue] = useState(() => algorithmDefaults(algorithm));
      return <><AlgorithmParameters algorithm={algorithm} value={value} onChange={setValue} /><output>{value.parameters.safety_margin_ns}</output></>;
    }
    render(<Editor />);
    await userEvent.click(screen.getByText("算法高级参数"));
    const input = screen.getByLabelText(/额外安全间隔/);
    await userEvent.clear(input);
    await userEvent.type(input, "0.025");
    expect((input as HTMLInputElement).value).toBe("0.025");
    expect(screen.getByRole("status").textContent).toBe("25000000");
  });
  it("keeps integer nanoseconds exact and displays durations in seconds", () => {
    const algorithm = algorithmCatalog.algorithms[0];
    const value = algorithmDefaults(algorithm);
    const onChange = vi.fn();
    render(<AlgorithmParameters algorithm={algorithm} value={value} onChange={onChange} />);
    fireEvent.change(screen.getByLabelText(/额外安全间隔/), { target: { value: "0.000000001" } });
    expect(onChange).toHaveBeenCalledWith({ ...value, parameters: { safety_margin_ns: "1" } });
    expect(screen.queryByLabelText(/时隙长度/)).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "恢复本算法默认参数", hidden: true }));
    expect(onChange).toHaveBeenLastCalledWith(value);
  });

  it("rejects unavailable versions, extra fields and invalid integer text without changing the draft", () => {
    const value = algorithmDefaults(algorithmCatalog.algorithms[0]);
    expect(algorithmIssues(value, algorithmCatalog)).toEqual([]);
    expect(algorithmIssues({ ...value, version: "2.0.0" }, algorithmCatalog)[0]).toContain("不可用");
    expect(algorithmIssues(value, undefined)[0]).toContain("未加载");
    for (const bad of ["01", "-1", "NaN", "1.1", "60000000001"]) expect(algorithmIssues({ ...value, parameters: { safety_margin_ns: bad } }, algorithmCatalog)).toHaveLength(1);
    expect(algorithmIssues({ ...value, parameters: { ...value.parameters, unknown: "0" } }, algorithmCatalog)).toHaveLength(1);
    expect(value.parameters).toEqual({ safety_margin_ns: "0" });
  });

  it("renders a third-party enum and boolean from metadata without algorithm-specific code", () => {
    const algorithm = { ...algorithmCatalog.algorithms[0], id: "third.party", parameters: [
      { ...algorithmCatalog.algorithms[0].parameters[0], id: "order", label: "发送顺序", type: "enum" as const, default: "升序", choices: ["升序", "降序"], unit: "", advanced: false },
      { ...algorithmCatalog.algorithms[0].parameters[0], id: "switch", label: "功能开关", type: "boolean" as const, default: "false", unit: "", advanced: false },
    ] };
    const onChange = vi.fn();
    render(<AlgorithmParameters algorithm={algorithm} value={algorithmDefaults(algorithm)} onChange={onChange} />);
    fireEvent.change(screen.getByLabelText(/发送顺序/), { target: { value: "降序" } });
    expect(onChange.mock.calls[0][0].parameters.order).toBe("降序");
    expect(screen.getByLabelText(/功能开关/)).toBeTruthy();
    expect(isAlgorithmCatalog(algorithmCatalog)).toBe(true);
    expect(isAlgorithmCatalog({ ...algorithmCatalog, algorithms: [{ ...algorithm, parameters: [{ type: "eval" }] }] })).toBe(false);
    expect(isAlgorithmCatalog({ ...algorithmCatalog, algorithms: [{ ...algorithmCatalog.algorithms[0], parameters: [{ ...algorithmCatalog.algorithms[0].parameters[0], min: "broken" }] }] })).toBe(false);
  });
});
