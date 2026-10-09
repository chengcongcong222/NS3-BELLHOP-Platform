import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { act, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { MemoryRouter } from "react-router-dom";
import { describe, expect, it } from "vitest";
import { AppShell } from "../components/common";
import { systemInfo } from "./fixtures";
import { installApi } from "./render";

function show(client = new QueryClient({ defaultOptions: { queries: { retry: false } } })) {
  render(<QueryClientProvider client={client}><MemoryRouter><AppShell><p>当前页面内容</p></AppShell></MemoryRouter></QueryClientProvider>);
  return client;
}

describe("shell service connection", () => {
  it("shows connecting only while the initial service query is pending", async () => {
    const fetch = installApi({});
    let complete!: (response: Response) => void;
    fetch.mockImplementationOnce(() => new Promise<Response>(resolve => { complete = resolve; }));
    show();
    expect(screen.getByText("正在连接…")).toBeTruthy();
    expect(screen.queryByRole("button", { name: "重试连接" })).toBeNull();
    await act(async () => { complete(new Response(JSON.stringify(systemInfo))); });
    expect(await screen.findByText("仿真服务已连接")).toBeTruthy();
  });

  it("reports failure, retries only the service query and recovers without changing runs", async () => {
    const fetch = installApi({ "/system/info": new Error("connection refused") });
    show();
    expect(await screen.findByText("仿真服务未连接")).toBeTruthy();
    expect(screen.queryByText("正在连接…")).toBeNull();
    expect(screen.queryByText("系统就绪")).toBeNull();
    let complete!: (response: Response) => void;
    fetch.mockImplementationOnce(() => new Promise<Response>(resolve => { complete = resolve; }));
    await userEvent.click(screen.getByRole("button", { name: "重试连接" }));
    expect(screen.getByRole("button", { name: "正在重试…" }).hasAttribute("disabled")).toBe(true);
    expect(screen.queryByText("系统就绪")).toBeNull();
    await act(async () => { complete(new Response(JSON.stringify(systemInfo))); });
    expect(await screen.findByText("仿真服务已连接")).toBeTruthy();
    expect(screen.queryByRole("button", { name: "重试连接" })).toBeNull();
    expect(screen.getByText("当前页面内容")).toBeTruthy();
    expect(fetch.mock.calls).toHaveLength(2);
    for (const [url, init] of fetch.mock.calls) {
      expect(url).toBe("/system/info");
      expect(init?.method ?? "GET").toBe("GET");
    }
  });

  it("does not call stale system information ready after a refresh has failed", async () => {
    installApi({ "/system/info": new Error("connection lost") });
    const client = new QueryClient({ defaultOptions: { queries: { retry: false } } });
    client.setQueryData(["system-info"], systemInfo);
    show(client);
    expect(screen.getByText("仿真服务已连接")).toBeTruthy();
    await act(async () => { await client.invalidateQueries({ queryKey: ["system-info"] }); });
    await waitFor(() => expect(screen.getByText("仿真服务未连接")).toBeTruthy());
    expect(screen.queryByText("仿真服务已连接")).toBeNull();
    expect(screen.queryByText(/系统就绪/)).toBeNull();
    expect(document.querySelectorAll(".service-online")).toHaveLength(0);
    expect(screen.getByRole("button", { name: "重试连接" })).toBeTruthy();
  });
});
