// Real browser + Backend + worker smoke. Run ONLY against a separate review workspace.
// Requires an already running Chromium --remote-debugging-port, no browser dependency.
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const [frontend, backend, debug, evidence, failureSourceDraft] = process.argv.slice(2);
if (!frontend || !backend || !debug || !evidence) throw new Error("Usage: node verify-composition.mjs <isolated frontend URL> <isolated backend URL> <Chrome debug URL> <evidence dir>");
const api = async (route, body) => {
  const response = await fetch(backend + route, body ? { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) } : undefined);
  const value = await response.json();
  assert(response.ok, JSON.stringify(value)); return value;
};
const tag = Date.now();
const assetId = `studio-review-${tag}`;
await api("/authoring/trajectories/import-csv", { asset_id: assetId, version: 1, name: "工作台联调轨迹（测试资源）",
  content: "track_id,time_s,x_m,y_m,z_m\nN0,0,0,0,-8\nN0,120,0,0,-8\nN1,0,1000,0,-60\nN1,120,1012,0,-60\nN2,0,2000,100,-60\nN2,120,2012,100,-60\n" });
const environments = await api("/environments");
const authoredEnvironment = environments.find(env => env.environment_asset_id !== "reference-shallow-water-v1");
assert(authoredEnvironment, "review requires an actually published authored environment");
const page = await (await fetch(debug + "/json/new?about:blank", { method: "PUT" })).json();
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => { ws.onopen = resolve; });
let id = 0; const pending = new Map();
ws.onmessage = event => { const message = JSON.parse(event.data); if (message.id) { pending.get(message.id)?.(message); pending.delete(message.id); } };
const send = (method, params = {}) => new Promise((resolve, reject) => {
  const current = ++id; const timeout = setTimeout(() => { pending.delete(current); reject(new Error(method + " timed out")); }, 10000);
  pending.set(current, result => { clearTimeout(timeout); result.error ? reject(new Error(JSON.stringify(result.error))) : resolve(result.result); });
  ws.send(JSON.stringify({ id: current, method, params }));
});
const evaluate = async expression => {
  const reply = await send("Runtime.evaluate", { expression, returnByValue: true });
  if (reply.exceptionDetails) throw new Error(JSON.stringify(reply.exceptionDetails));
  return reply.result.value;
};
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const terminal = async (runId, expected = "Completed") => {
  for (let n = 0; n < 150; n++) {
    const run = await api(`/runs/${runId}`);
    if (["Completed", "Failed", "Cancelled"].includes(run.lifecycle)) {
      assert.equal(run.lifecycle, expected, JSON.stringify(run)); return run;
    }
    await pause(200);
  }
  throw Error("Run did not terminate within 30 seconds");
};
const waitFor = async expression => { for (let n = 0; n < 80; n++) { if (await evaluate(expression)) return; await pause(100); } throw new Error("UI timeout: " + expression + "\n" + await evaluate("document.body.innerText")); };
const button = text => evaluate(`[...document.querySelectorAll('button')].find(b=>b.textContent.trim()===${JSON.stringify(text)})?.click()`);
const field = async (label, value) => {
  await evaluate(`(()=>{ const el=[...document.querySelectorAll('label')].find(l=>l.textContent.trim().startsWith(${JSON.stringify(label)}))?.querySelector('input,select'); if(!el) throw Error('missing field'); const proto=el.tagName==='SELECT'?HTMLSelectElement.prototype:HTMLInputElement.prototype; Object.getOwnPropertyDescriptor(proto,'value').set.call(el,${JSON.stringify(value)}); el.dispatchEvent(new Event(el.tagName==='SELECT'?'change':'input',{bubbles:true})); })()`);
  await pause(80);
};
const node = async number => {
  const rect = await evaluate(`(()=>{const r=document.querySelector('[aria-label="选择节点 N${number}"]').getBoundingClientRect();return{x:r.x+14,y:r.y+14}})()`);
  await send("Input.dispatchMouseEvent", { type: "mousePressed", ...rect, button: "left", clickCount: 1 });
  await send("Input.dispatchMouseEvent", { type: "mouseReleased", ...rect, button: "left", clickCount: 1 });
  await waitFor(`!!document.querySelector('[aria-label="绑定轨迹资源"]')`);
};
fs.mkdirSync(evidence, { recursive: true });
const screenshot = async name => { const data = await send("Page.captureScreenshot", { format: "png" }); fs.writeFileSync(path.join(evidence, name), Buffer.from(data.data, "base64")); };

try {
  await send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await send("Page.navigate", { url: frontend + "/workspace/experiment" });
  await waitFor("!!document.querySelector('.experiment-inspector')");
  await field("实验名称", `一体化工作台联调 ${tag}`);
  await field("场景名称", `三节点真实资源联调 ${tag}`);
  await field("实验环境", authoredEnvironment.environment_asset_id);
  if (process.env.REVIEW_SCENE_DOMAIN === "1") {
    await waitFor("!!document.querySelector('.scene-domain-editor input[type=checkbox]:not(:disabled)')");
    await evaluate("document.querySelector('.scene-domain-editor input[type=checkbox]').click()");
    await field("X 终点（m）", "3000");
    await field("Y 下界（m）", "-300");
    await field("Y 上界（m）", "300");
  }
  await node(0);
  await field("绑定轨迹", JSON.stringify([assetId, 1, "N0"]));
  await field("深度（m）", "8");
  await button("＋ 添加节点");
  await field("绑定轨迹", JSON.stringify([assetId, 1, "N1"]));
  assert.equal(await evaluate("document.querySelector('[aria-label=\"X（m）\"]').value"), "0");
  assert.equal(await evaluate("document.querySelector('[aria-label=\"X（m）\"]').disabled"), false);
  await field("X（m）", "1000"); await field("深度（m）", "60");
  await button("＋ 添加节点");
  // Reuse the SAME path, with an independent node origin.
  await field("绑定轨迹", JSON.stringify([assetId, 1, "N1"]));
  await field("X（m）", "2000"); await field("Y（m）", "100"); await field("深度（m）", "60");
  await screenshot("unified-studio-node.png");
  await button("环境与实验参数"); await button("网络");
  await field("业务负载", "CommunicationOnly");
  await screenshot("unified-studio-network.png");
  await button("保存并运行");
  await waitFor("location.pathname.startsWith('/runs/')");
  const firstId = (await evaluate("location.pathname")).split("/").at(-1);
  const first = await terminal(firstId);
  const firstResult = await api(`/runs/${firstId}/results`);
  await waitFor("!!document.querySelector('[aria-label=\"运行画布\"]') && document.querySelectorAll('.event-lane button').length > 0");
  assert(!(await evaluate("document.body.innerText")).includes("ScenarioNotFound"));
  const firstContext = await api(`/runs/${firstId}/definition-context`);
  assert.equal(firstContext.scenario.scenario_id, first.scenario_id);
  assert.equal(firstContext.experiment.phy.physical_layer, "Channel1Abstract");
  if (process.env.REVIEW_SCENE_DOMAIN === "1") assert.equal(firstContext.scenario.spatial_domain?.model, "ProfileExtrusion");
  await screenshot("mode-a-run-replay.png");
  assert.equal(first.environment_asset_id, authoredEnvironment.environment_asset_id);
  const draft = (await api("/authoring/drafts?kind=experiment")).find(item => item.name === `一体化工作台联调 ${tag}`);
  assert(draft);
  await send("Page.navigate", { url: frontend + "/workspace/experiment?draft=" + draft.draft_id });
  await waitFor("!!document.querySelector('.experiment-inspector')");
  await waitFor("document.querySelector('.unified-experiment-toolbar [role=status]')?.textContent.includes('已保存')");
  await button("物理模式"); await field("仿真模式", "Channel2Waveform");
  await screenshot("unified-studio-channel2.png");
  await button("保存并运行"); await waitFor("location.pathname.startsWith('/runs/')");
  const secondId = (await evaluate("location.pathname")).split("/").at(-1);
  const second = await terminal(secondId);
  const secondResult = await api(`/runs/${secondId}/results`);
  await waitFor("!!document.querySelector('[aria-label=\"运行画布\"]') && document.querySelectorAll('.event-lane button').length > 0");
  const secondContext = await api(`/runs/${secondId}/definition-context`);
  assert.equal(secondContext.experiment.phy.physical_layer, "Channel2Waveform");
  await waitFor("!!document.querySelector('[aria-label=\"深度节点 N1 · 60 m\"]')");
  await waitFor("[...document.querySelectorAll('button')].some(button => button.textContent.trim() === '按原配置重新运行' && !button.disabled)");
  await screenshot("mode-b-run-replay.png");
  await waitFor("[...document.querySelectorAll('button')].some(button => button.textContent.trim() === '查看结果' && !button.disabled)");
  await button("查看结果");
  await waitFor("!!document.querySelector('[role=\"dialog\"] .result-analysis-paper')");
  await screenshot("mode-b-result-drawer.png");
  await button("关闭结果");
  await send("Page.navigate", { url: frontend + `/results/${secondId}` });
  await waitFor("!!document.querySelector('.result-analysis-paper') && document.body.innerText.includes('波形通信')");
  await screenshot("mode-b-result-analysis.png");
  assert.equal(second.environment_asset_id, authoredEnvironment.environment_asset_id);
  assert.notEqual(firstId, secondId);
  assert.equal(first.provenance.physical_layer, "Channel1Abstract");
  assert.equal(second.provenance.physical_layer, "Channel2Waveform");
  assert(Number(firstResult.projection.local_delivery_count) > 0, "Channel1 must demonstrate actual delivery");
  assert(secondResult.visualization.waveform.length > 0, "Channel2 must expose measured decoding, not just Completed");
  for (const result of [firstResult, secondResult]) {
    assert.equal(result.provenance.environment.asset_id, authoredEnvironment.environment_asset_id);
    const positions = result.visualization.positions.filter(position => position.node_id === "1");
    assert(positions.length > 1);
    assert(positions.some(position => position.x_meters > 1000));
    for (const position of positions) assert(Math.abs(position.x_meters - (1000 + Number(position.at_ns) / 1e9 * 0.1)) < 1e-9);
  }
  let failureEvidence = null;
  if (failureSourceDraft) {
    const source = (await api("/authoring/drafts?kind=experiment")).find(item => item.draft_id === failureSourceDraft);
    assert(source, "failure source draft must exist; never substitute a preset");
    const copy = await api("/authoring/drafts", { kind: "experiment", name: `时隙失败回归（${source.name}副本）${tag}`, payload: source.payload });
    const failed = await terminal((await api(`/authoring/experiments/${copy.draft_id}/runs`, {})).run_id, "Failed");
    assert(failed.failure.message.includes("TDMA slot cannot contain"));
    const actions = await api(`/runs/${failed.run_id}/actions`);
    assert.equal(actions.edit.experiment_draft_id, copy.draft_id);
    assert.equal(actions.timing.minimum_slot_duration_ns, "4533333334");
    const eventBody = await (await fetch(backend + `/runs/${failed.run_id}/events`)).text();
    assert(eventBody.includes("event: stream-end"));
    await send("Page.navigate", { url: frontend + `/runs/${failed.run_id}` });
    await waitFor("document.body.innerText.includes('发送时隙太短') && [...document.querySelectorAll('button')].some(b=>b.textContent==='按原配置重新运行'&&!b.disabled)");
    await pause(500);
    assert(!(await evaluate("document.body.innerText")).includes("TransportUnavailable"));
    assert.equal(await evaluate("[...document.querySelectorAll('a')].find(a=>a.textContent==='修改本实验').getAttribute('href')"), `/workspace/experiment?draft=${copy.draft_id}`);
    await screenshot("failed-run-actions.png");
    await button("按原配置重新运行");
    await waitFor(`location.pathname.startsWith('/runs/') && !location.pathname.endsWith('${failed.run_id}')`);
    const repeated = await terminal((await evaluate("location.pathname")).split("/").at(-1), "Failed");
    const oldContext = await api(`/runs/${failed.run_id}/definition-context`), repeatedContext = await api(`/runs/${repeated.run_id}/definition-context`);
    assert.deepEqual(repeatedContext.experiment, oldContext.experiment);
    assert.deepEqual(repeatedContext.scenario, oldContext.scenario);
    await waitFor("[...document.querySelectorAll('a')].some(a=>a.textContent==='修改本实验')");
    await evaluate("[...document.querySelectorAll('a')].find(a=>a.textContent==='修改本实验').click()");
    await waitFor(`location.search.includes('${copy.draft_id}') && [...document.querySelectorAll('input')].some(i=>i.value===${JSON.stringify(copy.name)})`);
    await screenshot("edit-exact-experiment.png");
    const unchanged = (await api("/authoring/drafts?kind=experiment")).find(item => item.draft_id === source.draft_id);
    assert.deepEqual(unchanged, source, "the user's original draft must remain untouched");
    await send("Page.navigate", { url: frontend + "/runs" });
    await waitFor("!!document.querySelector('.experiment-run-group') && document.body.innerText.includes('时隙失败回归')");
    await screenshot("experiment-run-library.png");
    failureEvidence = { source_draft_id: source.draft_id, original_unchanged: true, copied_draft_id: copy.draft_id, failed, repeated, actions };
  }
  const savedScene = (await api("/authoring/drafts?kind=scenario")).find(item => item.draft_id === draft.payload.scenario_id);
  let domainRejection = null;
  if (process.env.REVIEW_SCENE_DOMAIN === "1") {
    const unsafeAsset = `domain-outside-${tag}`;
    await api("/authoring/trajectories/import-csv", { asset_id: unsafeAsset, version: 1,
      name: "整段路径越界反例（测试资源）", content: "track_id,time_s,x_m,y_m,z_m\npath,0,0,0,0\npath,24,2500,0,0\n" });
    const unsafeScene = await api("/authoring/drafts", { kind: "scenario", name: `越界反例 ${tag}`, payload: {
      ...savedScene.payload, trajectory_bindings: savedScene.payload.trajectory_bindings.map(binding => binding.node_id === "1"
        ? { ...binding, asset_id: unsafeAsset, version: 1, track_id: "path" } : binding),
    } });
    const unsafeExperiment = await api("/authoring/drafts", { kind: "experiment", name: `越界反例 ${tag}`, payload: {
      ...draft.payload, scenario_id: unsafeScene.draft_id, scenario_revision: unsafeScene.revision,
    } });
    const before = (await api("/runs")).length;
    const rejected = await fetch(backend + `/authoring/experiments/${unsafeExperiment.draft_id}/runs`, { method: "POST" });
    domainRejection = { status: rejected.status, body: await rejected.json(), initial_x_m: 1000, end_x_m: 3500, x_max_m: 3000 };
    assert.equal(rejected.status, 400);
    assert.equal(domainRejection.body.error.code, "SceneDomainViolation");
    assert(domainRejection.body.error.message.includes("移出"));
    assert.equal((await api("/runs")).length, before, "rejected geometry must not create a Run");
  }
  assert(savedScene.payload.trajectory_bindings.every(binding => binding.asset_id === assetId));
  assert(savedScene.payload.trajectory_bindings.every(binding => binding.placement_mode === "RelativeToNodeStart"));
  assert.equal(savedScene.payload.trajectory_bindings[1].track_id, savedScene.payload.trajectory_bindings[2].track_id);
  for (const result of [firstResult, secondResult]) {
    for (const position of result.visualization.positions.filter(p => p.node_id === "2")) {
      assert(Math.abs(position.x_meters - (2000 + Number(position.at_ns) / 1e9 * 0.1)) < 1e-9);
      assert.equal(position.y_meters, 100);
    }
  }
  fs.writeFileSync(path.join(evidence, "studio-browser-e2e.json"), JSON.stringify({ viewport: [1440, 900],
    trajectory_asset_id: assetId, environment_asset_id: authoredEnvironment.environment_asset_id,
    scenario: savedScene, experiment: await api("/authoring/drafts?kind=experiment"),
    channel1: { run: first, result: firstResult }, channel2: { run: second, result: secondResult }, failureEvidence, domainRejection }, null, 2));
  console.log(JSON.stringify({ Channel1: firstId, Channel2: secondId, status: "Completed", evidence }, null, 2));
} finally { ws.close(); await fetch(debug + "/json/close/" + page.id); }
