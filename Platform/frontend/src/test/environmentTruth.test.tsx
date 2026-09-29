import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it, vi } from "vitest";
import { apiClient } from "../api/client";
import { DraftEnvironmentProfile } from "../components/EnvironmentProfile";
import { newEnvironmentDraft } from "../domain/workspace";
import { installApi, renderRoute } from "./render";

it("loading terrain cannot clamp or rescale the sound speed profile", () => {
  const draft = { ...newEnvironmentDraft(), soundSpeedProfile: [{ depthMeters: 0, speedMetersPerSecond: 1500 }, { depthMeters: 175, speedMetersPerSecond: 1490 }] };
  const view = render(<DraftEnvironmentProfile draft={draft} />);
  const points = screen.getByRole("img", { name: "草稿声速剖面" }).querySelector("polyline")!.getAttribute("points");
  view.rerender(<DraftEnvironmentProfile draft={{ ...draft, maximumDepthMeters: 110, bathymetry: [{ rangeMeters: 0, depthMeters: 100 }, { rangeMeters: 1000, depthMeters: 110 }] }} />);
  const profile = screen.getByRole("img", { name: "草稿声速剖面" });
  expect(profile.querySelector("polyline")!.getAttribute("points")).toBe(points);
  expect(profile.textContent).toContain("175");
  expect(profile.textContent).toContain("1490");
});

it("reference data is an explicit fixed location choice, not the online default", async () => {
  installApi({ "/authoring/drafts?kind=environment": { body: [] },
    "/authoring/environment-capabilities": { body: { woa_online_available: true, bellhop_available: true } } });
  renderRoute("/workspace/environment");
  const select = await screen.findByLabelText("数据来源");
  expect((select as HTMLSelectElement).value).toBe("online");
  await userEvent.selectOptions(select, "reference");
  expect((screen.getByLabelText("经度（°）") as HTMLInputElement).value).toBe("110");
  expect((screen.getByLabelText("经度（°）") as HTMLInputElement).disabled).toBe(true);
  expect(screen.getByRole("button", { name: "载入参考声速剖面" })).toBeTruthy();
});

it("reopens exact environment inputs including bottom, beams, location and month", async () => {
  installApi({ "/authoring/drafts?kind=environment": { body: [{ kind: "environment", draft_id: "water", revision: 7,
    name: "保存的海域", created_at: "", updated_at: "", validation_issues: [], payload: {
      acquisition: { latitudeDeg: 20, longitudeDeg: 120, month: 9, useReferenceCache: false },
      bottom: { sound_speed_meters_per_second: 1720, density_grams_per_cubic_centimeter: 1.9, attenuation_db_per_wavelength: .7 },
      bellhop: { beam_count: 301, source_depths_meters: [10, 20], receiver_depths_meters: [15, 25], range_sample_count: 30 },
    } }] } });
  renderRoute("/workspace/environment?draft=water");
  expect((await screen.findByLabelText("经度（°）") as HTMLInputElement).value).toBe("120");
  expect((screen.getByLabelText("月份") as HTMLInputElement).value).toBe("9");
  expect((screen.getByLabelText("海底声速（m/s）") as HTMLInputElement).value).toBe("1720");
  expect((screen.getByLabelText("波束数") as HTMLInputElement).value).toBe("301");
});

it("shows real download progress and cancellation beside the acquisition button", async () => {
  installApi({ "/authoring/drafts?kind=environment": { body: [] },
    "/authoring/environment-capabilities": { body: { woa_online_available: true } } });
  const spy = vi.spyOn(apiClient, "acquireWoaProgress").mockImplementation((_request, progress, signal) => {
    progress({ stage: "downloading", message: "正在下载 NOAA 月度数据", dataset: "温度", file_index: 1, file_count: 2,
      received_bytes: 1048576, total_bytes: 2097152 });
    return new Promise((_resolve, reject) => signal.addEventListener("abort", () => reject(new DOMException("cancel", "AbortError"))));
  });
  renderRoute("/workspace/environment");
  await userEvent.click(await screen.findByRole("button", { name: "获取 WOA 数据" }));
  expect(await screen.findByText("1.0 MB / 2.0 MB")).toBeTruthy();
  expect(screen.getByRole("progressbar").getAttribute("value")).toBe("1048576");
  await userEvent.click(screen.getByRole("button", { name: "取消获取" }));
  expect((await screen.findByRole("alert")).textContent).toContain("已取消获取");
  expect(screen.getByRole("button", { name: "获取 WOA 数据" }).hasAttribute("disabled")).toBe(false);
  spy.mockRestore();
});
