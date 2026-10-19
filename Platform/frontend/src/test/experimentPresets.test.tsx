import { useState } from "react";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { it, expect } from "vitest";
import { OsdmSettings } from "../components/OsdmSettings";
import type { OsdmConfig } from "../domain/experimentPresets";
import { isExperimentPresets } from "../domain/experimentPresets";
import { installApi } from "./render";

const historical: OsdmConfig = { block_length: 128, vector_length: 8, cyclic_prefix: 32,
  constellation: "Bpsk", minimum_sync_correlation: .1, maximum_sync_search: 128 };
function setup(value: OsdmConfig | null = null, allow = true) {
  function Harness() {
    const [current, set] = useState(value);
    return <><OsdmSettings layer="Channel2Waveform" value={current} onChange={set} allowInitialPreset={allow} /><output data-testid="saved-values">{JSON.stringify(current)}</output></>;
  }
  render(<QueryClientProvider client={new QueryClient({ defaultOptions: { queries: { retry: false } } })}><Harness /></QueryClientProvider>);
}
it("uses the complete versioned preset without opening research settings", async () => {
  installApi({}); setup();
  await screen.findByText("Bellhop OSDM 标准配置 · v1");
  expect(document.querySelector("details")?.open).toBe(false);
  expect(JSON.parse(screen.getByTestId("saved-values").textContent!)).toEqual({ ...historical,
    block_length: 256, vector_length: 16, cyclic_prefix: 64, constellation: "Qpsk", minimum_sync_correlation: .05 });
});
it("never replaces historical values; closing settings does not reset customization", async () => {
  installApi({}); setup(historical, false);
  await screen.findByText("恢复该版本预设");
  expect(JSON.parse(screen.getByTestId("saved-values").textContent!)).toEqual(historical);
  await userEvent.click(screen.getByText("OSDM 分块与同步"));
  fireEvent.change(screen.getByLabelText("循环前缀"), { target: { value: "48" } });
  await userEvent.click(screen.getByText("OSDM 分块与同步"));
  expect(JSON.parse(screen.getByTestId("saved-values").textContent!).cyclic_prefix).toBe(48);
  await userEvent.click(screen.getByText("OSDM 分块与同步"));
  await userEvent.click(screen.getByText("恢复该版本预设"));
  await screen.findByText("Bellhop OSDM 标准配置 · v1");
  expect(JSON.parse(screen.getByTestId("saved-values").textContent!).cyclic_prefix).toBe(64);
});
it("does not populate an incomplete historical OSDM configuration", async () => {
  installApi({}); setup(null, false);
  await screen.findByText("恢复该版本预设");
  expect(screen.getByTestId("saved-values").textContent).toBe("null");
});
it("does not invent a frontend fallback when presets are unavailable", async () => {
  const calls = installApi({ "/authoring/experiment-presets": { status: 503, body: {} } }); setup();
  await waitFor(() => expect(calls.mock.calls.length).toBeGreaterThan(0));
  expect(screen.getByTestId("saved-values").textContent).toBe("null");
  expect(isExperimentPresets([{ id: "bad", version: 1, name: "bad", physical_layer: "Channel2Waveform", osdm: {} }])).toBe(false);
});
