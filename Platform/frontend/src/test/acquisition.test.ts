import { expect, it, vi } from "vitest";
import { acquireWithProgress } from "../api/acquisition";

it("reads byte progress across split chunks and returns only a complete profile", async () => {
  const encode = new TextEncoder();
  const result = { sound_speed_profile: [{ depth_meters: 0 }, { depth_meters: 20 }] };
  const data = JSON.stringify({ type: "progress", stage: "downloading", received_bytes: 100, total_bytes: 200 }) + "\n" +
    JSON.stringify({ type: "heartbeat" }) + "\n" + JSON.stringify({ type: "result", result }) + "\n";
  vi.stubGlobal("fetch", vi.fn().mockResolvedValue(new Response(new ReadableStream({ start(controller) {
    controller.enqueue(encode.encode(data.slice(0, 37)));
    controller.enqueue(encode.encode(data.slice(37))); controller.close();
  } }))));
  const progress = vi.fn();
  expect(await acquireWithProgress("/stream", {}, progress, new AbortController().signal)).toEqual(result);
  expect(progress.mock.calls[0][0].received_bytes).toBe(100);
});

it.each([
  [JSON.stringify({ type: "error", message: "下载超时" }) + "\n", "下载超时"],
  [JSON.stringify({ type: "heartbeat" }) + "\n", "连接已中断"],
])("does not hang or substitute data when acquisition fails", async (body, message) => {
  vi.stubGlobal("fetch", vi.fn().mockResolvedValue(new Response(body)));
  await expect(acquireWithProgress("/stream", {}, vi.fn(), new AbortController().signal)).rejects.toThrow(message);
});

it("explains an outdated backend rather than silently using a non-progress request", async () => {
  vi.stubGlobal("fetch", vi.fn().mockResolvedValue(new Response("", { status: 404 })));
  await expect(acquireWithProgress("/stream", {}, vi.fn(), new AbortController().signal)).rejects.toThrow("后端版本过旧");
});
