import { render, screen, waitFor } from "@testing-library/react";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import userEvent from "@testing-library/user-event";
import { expect, it } from "vitest";
import { AcousticPlots, FrozenLossField } from "../components/AcousticPlots";
import { installApi } from "./render";

const plot = { available: true, frequency_hz: 25000, source_depths_m: [10, 20], receiver_depths_m: [30], ranges_m: [500],
  impulse: [{ delay_s: .333, pressure_gain: .01, phase_degrees: 0 }, { delay_s: .335, pressure_gain: .004, phase_degrees: 90 }],
  field: [{ receiver_index: 0, range_index: 0, tl_db: 40 }], arrivals_sha256: "sha-test", tl_definition: "非相干多径能量损失，不是相干声场。",
  ray_plot: { available: false, reason: "此历史环境未保留声线文件。" } };
function show() { render(<QueryClientProvider client={new QueryClient({ defaultOptions: { queries: { retry: false } } })}><AcousticPlots environmentId="env-real" /></QueryClientProvider>); }

it("requests rays only and displays the backend failure reason with a retry action", async () => {
  const fetch = installApi({ "/environments/env-real/acoustic-plots?source_index=0&receiver_index=0&range_index=0&view=rays": {
    status: 500, body: { error: { code: "InvalidAcousticPlot", message: "声线文件校验失败" } },
  } });
  show();
  await screen.findByText("声线文件校验失败");
  expect(screen.queryByRole("img")).toBeNull();
  await userEvent.click(screen.getByRole("button", { name: "重试" }));
  await waitFor(() => expect(fetch.mock.calls).toHaveLength(2));
});

it("freezes loss and impulse tabs while retaining exact source selection", async () => {
  const fetch = installApi({ "/environments/env-real/acoustic-plots?source_index=0&receiver_index=0&range_index=0&view=rays": { body: plot },
    "/environments/env-real/acoustic-plots?source_index=1&receiver_index=0&range_index=0&view=rays": { body: plot } });
  show(); await screen.findByLabelText("发射深度");
  expect(screen.queryByRole("tab")).toBeNull();
  expect(screen.queryByRole("img")).toBeNull();
  await userEvent.selectOptions(screen.getByLabelText("发射深度"), "1");
  await waitFor(() => expect(fetch.mock.calls.some(([url]) => String(url).includes("source_index=1"))).toBe(true));
  expect(screen.getByText("此历史环境未保留声线文件。")).toBeTruthy();
});

it("does not synthesize a channel plot when immutable calculation artifacts are unavailable", async () => {
  installApi({ "/environments/env-real/acoustic-plots?source_index=0&receiver_index=0&range_index=0&view=rays": { body: { available: false, reason: "未保存离线产物" } } });
  show(); await screen.findByText("未保存离线产物");
  expect(screen.queryByRole("img")).toBeNull();
});

it("uses a distance curve for a single receiver depth without filling no-arrival gaps", async () => {
  render(<FrozenLossField data={{ ...plot,
    ranges_m: [100, 200, 300, 400], field: [40, 45, null, 55].map((tl_db, range_index) => ({ receiver_index: 0, range_index, tl_db })) }} />); await screen.findByRole("img", { name: "单深度传输损失距离曲线" });
  const svg = screen.getByRole("img", { name: "单深度传输损失距离曲线" });
  expect(svg.querySelectorAll("circle")).toHaveLength(3);
  expect(svg.querySelectorAll("line")).toHaveLength(1);
  expect(svg.querySelectorAll("rect")).toHaveLength(0);
  expect(svg.textContent).toContain("非相干传输损失 / dB");
  expect(svg.textContent).toContain("300 m · 无到达（不表示零损失）");
  const points = [...svg.querySelectorAll("circle")];
  expect(points.map(p => Number(p.getAttribute("cx")))).toEqual([200, 335, 605]);
  expect(Number(points[0].getAttribute("cy"))).toBeGreaterThan(Number(points[2].getAttribute("cy")));
});

it("shows one loss sample as a visible point, not a zero-height heatmap", async () => {
  render(<FrozenLossField data={plot} />); await screen.findByRole("img", { name: "单深度传输损失距离曲线" });
  const point = screen.getByRole("img", { name: "单深度传输损失距离曲线" }).querySelector("circle")!;
  expect(Number(point.getAttribute("r"))).toBeGreaterThanOrEqual(4);
  expect(Number(point.getAttribute("cy"))).toBe(140);
  expect(point.textContent).toBe("500 m · 40 dB");
});

it("does not draw a loss curve when every distance sample has no arrival", async () => {
  render(<FrozenLossField data={{ ...plot,
    field: [{ receiver_index: 0, range_index: 0, tl_db: null }] }} />); await screen.findByText(/所有距离采样均无到达/);
  expect(screen.getByRole("status").textContent).toContain("所有距离采样均无到达");
  expect(screen.queryByRole("img")).toBeNull();
});

it("retains real distance-depth samples for multiple receiver depths", async () => {
  render(<FrozenLossField data={{ ...plot,
    receiver_depths_m: [30, 60], ranges_m: [100, 200], field: [40, null, 50, 60].map((tl_db, i) => ({ receiver_index: Math.floor(i / 2), range_index: i % 2, tl_db })) }} />); await screen.findByRole("img", { name: "非相干传输损失距离深度图" });
  const svg = screen.getByRole("img", { name: "非相干传输损失距离深度图" });
  expect(svg.querySelectorAll("rect")).toHaveLength(4);
  expect(svg.querySelectorAll('rect[fill="#d8d8d8"]')).toHaveLength(1);
  expect(svg.textContent).toContain("200 m · 深度 30 m · 无到达");
});

it("resets sampling indices when switching to a different immutable environment", async () => {
  const fetch = installApi({
    "/environments/env-real/acoustic-plots?source_index=0&receiver_index=0&range_index=0&view=rays": { body: plot },
    "/environments/env-real/acoustic-plots?source_index=1&receiver_index=0&range_index=0&view=rays": { body: plot },
    "/environments/env-next/acoustic-plots?source_index=0&receiver_index=0&range_index=0&view=rays": { body: { ...plot, source_depths_m: [5] } },
  });
  const client = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  const view = render(<QueryClientProvider client={client}><AcousticPlots environmentId="env-real" /></QueryClientProvider>);
  await screen.findByLabelText("发射深度");
  await userEvent.selectOptions(screen.getByLabelText("发射深度"), "1");
  await waitFor(() => expect(fetch.mock.calls.some(([url]) => String(url).includes("source_index=1"))).toBe(true));
  view.rerender(<QueryClientProvider client={client}><AcousticPlots environmentId="env-next" /></QueryClientProvider>);
  await screen.findByRole("option", { name: "5 m" });
  expect((screen.getByLabelText("发射深度") as HTMLSelectElement).value).toBe("0");
  expect(fetch.mock.calls.filter(([url]) => String(url).includes("env-next")).every(([url]) => String(url).includes("source_index=0"))).toBe(true);
});

it("changes actual ray source and filters only computed launch angles", async () => {
  const withRays = { ...plot, ray_plot: { available: true, source_depth_m: 10, frequency_hz: 25000, sha256: "ray-sha",
    rays: [{ angle_degrees: -10, points: [[0, 10], [500, 30]] }] } };
  const deep = { ...withRays, ray_plot: { ...withRays.ray_plot, source_depth_m: 20,
    rays: [{ angle_degrees: -10, points: [[0, 20], [500, 40]] }, { angle_degrees: 10, points: [[0, 20], [500, 50]] }] } };
  const fetch = installApi({ "/environments/env-real/acoustic-plots?source_index=0&receiver_index=0&range_index=0&view=rays": { body: withRays },
    "/environments/env-real/acoustic-plots?source_index=1&receiver_index=0&range_index=0&view=rays": { body: deep } });
  show(); await screen.findByRole("img", { name: "Bellhop 真实声线路径" });
  expect(screen.getByRole("img", { name: "Bellhop 真实声线路径" }).querySelectorAll("polyline")).toHaveLength(1);
  const shallowPoints = screen.getByRole("img", { name: "Bellhop 真实声线路径" }).querySelector("polyline")!.getAttribute("points");
  expect(screen.getByText(/发射深度 10 m · 25 kHz/)).toBeTruthy();
  await userEvent.selectOptions(screen.getByLabelText("发射深度"), "1");
  await screen.findByText(/发射深度 20 m · 25 kHz/);
  expect(fetch.mock.calls.some(([url]) => String(url).includes("source_index=1"))).toBe(true);
  const deepPlot = screen.getByRole("img", { name: "Bellhop 真实声线路径" });
  expect(deepPlot.querySelectorAll("polyline")).toHaveLength(2);
  expect(deepPlot.querySelector("polyline")!.getAttribute("points")).not.toBe(shallowPoints);
  const calls = fetch.mock.calls.length;
  await userEvent.selectOptions(screen.getByLabelText("发射角（已计算声线）"), "10");
  expect(screen.getByRole("img", { name: "Bellhop 真实声线路径" }).querySelectorAll("polyline")).toHaveLength(1);
  expect(screen.getByRole("img", { name: "Bellhop 真实声线路径" }).textContent).toContain("发射角 10°");
  expect(fetch.mock.calls).toHaveLength(calls);
  await userEvent.click(screen.getByText("计算说明与数据来源"));
  expect(screen.getByText("声线数据 SHA256 ray-sha")).toBeTruthy();
});

it("does not show first-source rays under a different selected depth on a legacy server", async () => {
  const legacy = { ...plot, ray_plot: { available: true, source_depth_m: 10, frequency_hz: 25000, sha256: "old-ray",
    rays: [{ angle_degrees: -10, points: [[0, 10], [500, 30]] }] } };
  installApi({ "/environments/env-real/acoustic-plots?source_index=0&receiver_index=0&range_index=0&view=rays": { body: legacy },
    "/environments/env-real/acoustic-plots?source_index=1&receiver_index=0&range_index=0&view=rays": { body: legacy } });
  show(); await screen.findByRole("img", { name: "Bellhop 真实声线路径" });
  await userEvent.selectOptions(screen.getByLabelText("发射深度"), "1");
  await screen.findByText(/未保存发射深度 20 m 的声线/);
  expect(screen.queryByRole("img")).toBeNull();
  expect(screen.getByLabelText("发射深度")).toBeTruthy();
  expect(screen.queryByLabelText("发射角（已计算声线）")).toBeNull();
});

it("keeps fixed axes when filtering computed rays", async () => {
  const rays = { available: true, source_depth_m: 10, frequency_hz: 25000, sha256: "rays",
    rays: [{ angle_degrees: -10, points: [[0,10],[500,80]] }, { angle_degrees: 10, points: [[0,10],[100,30]] }] };
  installApi({ "/environments/env-real/acoustic-plots?source_index=0&receiver_index=0&range_index=0&view=rays": { body: { ...plot, ray_plot: rays } } });
  show(); const svg = await screen.findByRole("img", {name:"Bellhop 真实声线路径"});
  const axes = svg.querySelector('.acoustic-axes')!.textContent;
  const points = svg.querySelectorAll('polyline')[1].getAttribute('points');
  await userEvent.selectOptions(screen.getByLabelText("发射角（已计算声线）"), "10");
  expect(svg.querySelector('.acoustic-axes')!.textContent).toBe(axes);
  expect(svg.querySelector('polyline')!.getAttribute('points')).toBe(points);
  expect(screen.getByText('颜色区分发射方向，不表示声强。')).toBeTruthy();
});
