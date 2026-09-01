import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it } from "vitest";
import { environment, experiment, run, scenario } from "./fixtures";
import { installApi, renderRoute } from "./render";

const catalog = {
  "/environments": { body: [environment] },
  "/scenarios": { body: [scenario] },
  "/experiments": { body: [experiment] },
};

describe("simulation workbench user journeys", () => {
  it("goes from workbench to case, creates a real Run, and enters monitor", async () => {
    const launched = { ...run, run_id: "case-launched-run", lifecycle: "Running", event_stream_complete: false };
    const fetchMock = installApi({
      ...catalog,
      "/runs": { body: [] },
      "/results": { body: [] },
      "POST /runs": { body: launched },
      "/runs/case-launched-run": { body: launched },
    });
    renderRoute("/");
    expect(await screen.findByText("水声网络数字孪生仿真工作台")).toBeTruthy();
    await userEvent.click(screen.getAllByRole("link", { name: "浅水四节点协同通信与融合" })[0]);
    expect(await screen.findByText("这个案例将如何执行")).toBeTruthy();
    await userEvent.click(screen.getByRole("button", { name: "运行默认实验" }));
    expect(await screen.findByText("网络运行画布")).toBeTruthy();
    expect(document.body.textContent).toContain("case-launched-run");
    const post = fetchMock.mock.calls.find((call) => call[1]?.method === "POST");
    expect(JSON.parse(String(post?.[1]?.body))).toEqual({ experiment_id: experiment.experiment_id, experiment_version: experiment.version });
  });

  it("builds and saves an environment Draft without claiming publication", async () => {
    installApi(catalog);
    renderRoute("/workspace/environment");
    expect(await screen.findByRole("img", { name: "草稿声速剖面" })).toBeTruthy();
    const name = screen.getByLabelText("环境名称");
    await userEvent.clear(name); await userEvent.type(name, "南海试验环境");
    await userEvent.click(screen.getByRole("button", { name: "保存到本机工作区" }));
    expect(screen.getByText("已保存")).toBeTruthy();
    expect(localStorage.getItem("ns3-bellhop-platform.workbench-drafts.v1")).toContain("南海试验环境");
    expect(document.body.textContent).toContain("不会声称已经生成传播损失或 NoArrival 数据");
  });

  it("supports N-node scene editing with canvas and exact coordinate input", async () => {
    installApi(catalog);
    renderRoute("/workspace/scenario");
    expect(await screen.findByRole("img", { name: "可编辑场景画布" })).toBeTruthy();
    const before = scenario.nodes.length;
    await userEvent.click(screen.getByRole("button", { name: "＋ 添加节点" }));
    expect(screen.getAllByText(/^N[0-9]+$/).length).toBeGreaterThanOrEqual(before + 1);
    const x = screen.getByLabelText("X（m）"); await userEvent.clear(x); await userEvent.type(x, "321");
    expect((x as HTMLInputElement).value).toBe("321");
    expect(screen.getByLabelText("节点深度剖面")).toBeTruthy();
  });

  it("edits an experiment Draft but exposes no fake Run action", async () => {
    installApi(catalog);
    renderRoute("/workspace/experiment");
    expect(await screen.findByText("通信与物理层")).toBeTruthy();
    const rate = screen.getByLabelText("通信速率（bit/s）"); await userEvent.clear(rate); await userEvent.type(rate, "120");
    await userEvent.click(screen.getByRole("button", { name: "保存到本机工作区" }));
    expect(localStorage.getItem("ns3-bellhop-platform.workbench-drafts.v1")).toContain('"bitRateBitsPerSecond":"120"');
    expect(screen.queryByRole("button", { name: /开始仿真/ })).toBeNull();
    expect(document.body.textContent).toContain("开始仿真仍需选择已发布实验");
  });
});
