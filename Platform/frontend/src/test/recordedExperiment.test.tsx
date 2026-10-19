import { fireEvent, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it } from "vitest";
import { installApi, renderRoute } from "./render";
import type { AuthoringDraftDto } from "../api/types";
import { run, result as acceptance4Result } from "./fixtures";
import { physicalMode } from "../domain/resultAnalysis";
import { runPhysicalMode } from "../domain/runtimePresentation";

const source = { manifest_sha256: "manifest-hash", manifest: { asset_id: "blue-1", version: 1,
  name: "Blue 官方录音", source_sha256: "source-hash", citation: "https://doi.org/10.5281/zenodo.21287414", license: "CC-BY-4.0",
  fc: 13000, usable_bandwidth_hz: 5000, duration_s: 52, fs_delay: 9765.625, fs_time: 48.828125,
  element: 0, axis_kind: "ReceiverElement", reference_revision: "pinned" } };
const noise = { manifest_sha256: "noise-hash", manifest: { asset_id: "blue-noise", version: 1, name: "Blue 连续噪声",
  seed: 23, alpha: 2, amplitude_scale: .01, sample_rate_hz: 52000, shape: [624000, 1], center_frequency_hz: 13000,
  bandwidth_hz: 4882.8125, elements: [0] } };

function server(records = new Map<string, AuthoringDraftDto>()) {
  const mock = installApi({ "/recorded-channels": { body: [source] }, "/recorded-noise": { body: [noise] } });
  const base = mock.getMockImplementation()!;
  mock.mockImplementation(async (input, init) => {
    const url = String(input);
    if (url.startsWith("/authoring/drafts" ) && (!init?.method || init.method === "GET"))
      return new Response(JSON.stringify([...records.values()].filter(d => !url.includes("?kind=") || url.endsWith(d.kind))));
    if (url.startsWith("/authoring/drafts") && ["POST", "PUT"].includes(init?.method ?? "")) {
      const b = JSON.parse(String(init!.body));
      const draft = { ...b, draft_id: `${b.kind}-C`, revision: (records.get(b.kind)?.revision ?? 0) + 1,
        created_at: "2026-10-16", updated_at: "2026-10-16", validation_issues: [] };
      records.set(b.kind, draft); return new Response(JSON.stringify(draft));
    }
    if (url === "/authoring/trajectories/import-csv") {
      const b = JSON.parse(String(init!.body));
      return new Response(JSON.stringify({ ...b, tracks: [], checksum_sha256: "trajectory-hash" }));
    }
    if (url === "/authoring/experiments/experiment-C/runs") return new Response(JSON.stringify({ ...run, run_id: "C-run" }));
    return base(input, init);
  });
  return { mock, records };
}

async function chooseRecording() {
  await screen.findByRole("option", { name: "Blue 官方录音 · v1" });
  await userEvent.selectOptions(await screen.findByLabelText("信道资源 / 实验环境"), "Recorded:blue-1");
  await userEvent.click(screen.getByRole("button", { name: "保留其他输入并更换环境" }));
}

it("opens C without a Bellhop environment or fabricated metric geometry", async () => {
  const { mock } = server(); renderRoute("/workspace/experiment?mode=C");
  await screen.findByRole("img", { name: "录音链路画布" });
  expect(screen.queryByLabelText("实验环境")).toBeNull();
  expect(screen.queryByLabelText("深度（m）")).toBeNull();
  // The common selector reads lightweight names only, never Bellhop geometry.
  expect(mock.mock.calls.some(([url]) => String(url) === "/environments")).toBe(false);
  await chooseRecording();
  expect(screen.getByText("接收阵元 1")).toBeTruthy();
  expect(screen.getByText("归一化幅度 · 测量几何未导入")).toBeTruthy();
});

it("saves the exact recording, continuous noise, modem and scene before starting", async () => {
  const { mock, records } = server(); renderRoute("/workspace/experiment?mode=C");
  await chooseRecording();
  await userEvent.selectOptions(screen.getByLabelText("连续噪声"), "blue-noise");
  await userEvent.click(screen.getByText("OSDM 分块与同步"));
  fireEvent.change(screen.getByLabelText("循环前缀"), { target: { value: "96" } });
  await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
  await screen.findByText("已保存");
  expect(records.get("scenario")?.payload.environment_source).toBe("Recorded");
  expect(records.get("scenario")?.payload.trajectory_bindings).toHaveLength(2);
  const p = records.get("experiment")!.payload;
  expect(p.physical_layer).toBe("Channel3Recorded");
  expect(p.scenario_revision).toBe(1);
  expect(p.occupied_bandwidth_hz).toBe(4882.8125);
  expect(p.osdm).toMatchObject({ cyclic_prefix: 96 });
  expect(p.recorded_channel).toMatchObject({ asset_id: "blue-1", noise: { mode: "ContinuousLibrary", asset_id: "blue-noise", version: 1 } });
  await userEvent.click(screen.getByRole("button", { name: "保存并运行" }));
  await waitFor(() => expect(mock.mock.calls.some(([u]) => u === "/authoring/experiments/experiment-C/runs")).toBe(true));
  expect(records.get("experiment")?.payload.scenario_revision).toBe(2);
});

it("rejects missing sources and out-of-coverage windows without API writes", async () => {
  const { mock } = server(); renderRoute("/workspace/experiment?mode=C");
  await userEvent.click(await screen.findByRole("button", { name: "保存并运行" }));
  await screen.findByText("请选择已导入的录音资源。");
  await chooseRecording();
  fireEvent.change(screen.getByLabelText("录音起点（s）"), { target: { value: "50" } });
  await userEvent.click(screen.getByRole("button", { name: "保存并运行" }));
  await screen.findByText("所选片段不够长，请缩短实验或前移录音起点。");
  expect(mock.mock.calls.some(([, init]) => init?.method === "POST")).toBe(false);
});

it("identifies mode C instead of relabeling recorded waveform results as B", () => {
  const r = { ...acceptance4Result, provenance: { physical_layer: "Channel3Recorded" } };
  expect(physicalMode(r)).toBe("C");
  expect(runPhysicalMode(undefined, "Channel3Recorded", [])).toBe("C");
});
