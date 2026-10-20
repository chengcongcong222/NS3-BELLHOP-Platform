// Real Chromium + real backend. Seeded inputs only; never intercept API responses.
import fs from "node:fs";
const root = process.argv[2];
const base = "http://127.0.0.1:4197";
const seed = JSON.parse(fs.readFileSync(`${root}/seed.json`));
const draft = process.argv.includes("--bearing") ? seed.bearing_draft_id : seed.draft_id;
const target = (await (await fetch("http://127.0.0.1:9227/json/list")).json()).find(t => t.type === "page");
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise(resolve => ws.addEventListener("open", resolve, { once: true }));
let sequence = 0;
const pending = new Map();
const exceptions = [];
ws.addEventListener("message", event => {
  const message = JSON.parse(event.data);
  if (message.method === "Page.javascriptDialogOpening" && message.params.type === "beforeunload")
    void send("Page.handleJavaScriptDialog", {accept:true});
  if (message.method === "Runtime.exceptionThrown") exceptions.push(message.params);
  if (pending.has(message.id)) {
    const [resolve, reject] = pending.get(message.id);
    pending.delete(message.id);
    message.error ? reject(Error(JSON.stringify(message.error))) : resolve(message.result);
  }
});
const send = (method, params = {}) => new Promise((resolve, reject) => {
  const id = ++sequence;
  pending.set(id, [resolve, reject]);
  ws.send(JSON.stringify({ id, method, params }));
});
const evaluate = async expression => {
  const value = await send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
  if (value.exceptionDetails) throw Error(JSON.stringify(value.exceptionDetails));
  return value.result.value;
};
const wait = async expression => {
  for (let i = 0; i < 100; i++) {
    if (await evaluate(expression)) return;
    await new Promise(resolve => setTimeout(resolve, 200));
  }
  throw Error(`Timed out: ${expression}\n${await evaluate("document.body.innerText")}`);
};
const click = name => evaluate(`(()=>{const e=[...document.querySelectorAll('button')].find(e=>e.textContent.trim()===${JSON.stringify(name)}&&!e.disabled);if(!e)throw Error('Missing button');e.click();})()`);
const screenshot = async name => {
  const data = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
  fs.writeFileSync(`${root}/${name}.png`, Buffer.from(data.data, "base64"));
};
const evidence = { executed_by: "Chromium CDP automation", seeded_inputs_via: "ASGI authoring API; no seeded results", checks: [], exceptions };
try {
  await send("Page.enable"); await send("Runtime.enable");
  await send("Runtime.discardConsoleEntries");
  exceptions.length = 0; // Runtime.enable can replay errors from an earlier page.
  await send("Emulation.setDeviceMetricsOverride", { width: 1600, height: 1000, deviceScaleFactor: 1, mobile: false });
  await send("Page.navigate", { url: `${base}/workspace/experiment?draft=${draft}` });
  await wait(`!![...document.querySelectorAll('select')].find(e=>[...e.options].some(o=>o.value==='Both'))`);
  await evaluate(`(()=>{const e=[...document.querySelectorAll('select')].find(e=>[...e.options].some(o=>o.value==='Both'));e.value='Both';e.dispatchEvent(new Event('change',{bubbles:true}));})()`);
  await screenshot("workspace-before-pair");
  await click("保存并运行");
  await wait("location.pathname.startsWith('/run-groups/group-')");
  const queuedId = await evaluate("location.pathname.split('/').pop()");
  if (process.argv.includes("--background")) {
    const queued = await (await fetch(`http://127.0.0.1:8097/run-groups/${queuedId}`)).json();
    if (queued.members[1].state !== "Queued") throw Error("B was not queued at navigation checkpoint");
    await send("Page.navigate", { url: "about:blank" });
    await new Promise(resolve => setTimeout(resolve, 5500));
    await send("Page.navigate", { url: `${base}/run-groups/${queuedId}` });
    evidence.checks.push({ action: "leave page while B queued; reopen without driving workers", group_id: queuedId });
  }
  if (process.argv.includes("--fault")) {
    await wait(`[...document.querySelectorAll('[role="status"]')].some(e=>e.textContent==='部分完成')`);
    const failed = await (await fetch(`http://127.0.0.1:8097/run-groups/${queuedId}/export`)).json();
    if (failed.members[0].state !== "Completed" || !failed.members[0].result ||
        failed.members[1].state !== "Failed" || failed.members[1].result !== null)
      throw Error("B process failure corrupted A or fabricated B results");
    await click("B 结果");
    await wait("document.body.innerText.includes('该成员尚无可用结果')");
    await screenshot("b-failed-no-fake-results");
    await click("A 结果");
    await wait("!!document.querySelector('.result-analysis-paper')");
    await screenshot("a-retained-after-b-failure");
    evidence.groups = [failed];
    evidence.checks.push({ action: "explicit B process failure; actual A retained; B no fabricated zero", group_id: queuedId });
    evidence.success = !exceptions.length;
    if (exceptions.length) throw Error("Browser reported runtime exceptions");
  } else {
  await wait(`[...document.querySelectorAll('[role="status"]')].some(e=>e.textContent==='已完成')`);
  const first = await evaluate("location.pathname.split('/').pop()");
  evidence.checks.push({ action: "browser save-and-run A+B", group_id: first });
  await click("B 结果");
  await wait("!!document.querySelector('.result-analysis-paper')");
  await screenshot("member-b");
  await click("对照");
  for (const [width, height] of [[1600, 1000], [1440, 900], [1366, 768], [1024, 768], [800, 900]]) {
    await send("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: 1, mobile: false });
    await new Promise(resolve => setTimeout(resolve, 100));
    const layout = await evaluate("({width:innerWidth,scroll:document.documentElement.scrollWidth})");
    if (layout.scroll > layout.width) throw Error(`Horizontal overflow at ${width}`);
    evidence.checks.push({ action: "comparison viewport", width, height, ...layout });
    await screenshot(`comparison-${width}`);
  }
  await click("按原配置重跑 A+B");
  await wait(`location.pathname!=='/run-groups/${first}'`);
  await wait(`[...document.querySelectorAll('[role="status"]')].some(e=>e.textContent==='已完成')`);
  const repeated = await evaluate("location.pathname.split('/').pop()");
  evidence.checks.push({ action: "browser frozen group rerun", group_id: repeated });
  evidence.groups = await Promise.all([first, repeated].map(async id =>
    (await fetch(`http://127.0.0.1:8097/run-groups/${id}/export`)).json()));
  const before = (await (await fetch("http://127.0.0.1:8097/run-groups")).json()).length;
  const original = evidence.groups[1];
  const duplicates = await Promise.all([1, 2].map(() => fetch(`http://127.0.0.1:8097/run-groups/${first}/rerun`, {
    method: "POST", headers: {"Content-Type":"application/json"},
    body: JSON.stringify({request_idempotency_key:original.request_key}),
  }).then(r=>r.json())));
  const after = (await (await fetch("http://127.0.0.1:8097/run-groups")).json()).length;
  if (before !== after || duplicates.some(g=>g.group_id !== repeated)) throw Error("Repeated key duplicated group");
  evidence.checks.push({action:"concurrent resubmit captured UI rerun key",same_group:repeated,additional_groups:after-before});
  if (process.argv.includes("--geometry")) {
    await send("Emulation.setDeviceMetricsOverride", { width: 1600, height: 1000, deviceScaleFactor: 1, mobile: false });
    await send("Page.navigate", {url:`${base}/workspace/experiment?draft=${draft}`});
    await wait("!!document.querySelector('[aria-label=\"选择节点 N0\"]')");
    await evaluate("document.querySelector('[aria-label=\"选择节点 N0\"]').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,pointerId:1}))");
    if (await evaluate("[...document.querySelectorAll('button')].some(e=>e.textContent==='改为跟随场景起点')"))
      await click("改为跟随场景起点");
    await wait("document.querySelector('[aria-label=\"X（m）\"]')?.disabled===false");
    await evaluate("document.querySelector('[aria-label=\"选择节点 N0\"]').scrollIntoView({block:'center'})");
    const oldX = await evaluate("Number(document.querySelector('[aria-label=\"X（m）\"]').value)");
    const point = await evaluate("(()=>{const b=document.querySelector('[aria-label=\"选择节点 N0\"] circle').getBoundingClientRect();return{x:b.x+b.width/2,y:b.y+b.height/2}})()");
    await send("Input.dispatchMouseEvent",{type:"mousePressed",...point,button:"left",buttons:1,clickCount:1});
    await send("Input.dispatchMouseEvent",{type:"mouseMoved",x:point.x+25,y:point.y,button:"left",buttons:1});
    await send("Input.dispatchMouseEvent",{type:"mouseReleased",x:point.x+25,y:point.y,button:"left",buttons:0,clickCount:1});
    const newX = await evaluate("Number(document.querySelector('[aria-label=\"X（m）\"]').value)");
    if (newX===oldX) throw Error("Geometric pointer drag did not change node position");
    await evaluate("(()=>{const e=[...document.querySelectorAll('select')].find(e=>[...e.options].some(o=>o.value==='Both'));e.value='Both';e.dispatchEvent(new Event('change',{bubbles:true}));})()");
    if(process.argv.includes("--coverage")) {
      const beforeRuns=(await(await fetch("http://127.0.0.1:8097/runs")).json()).length;
      await evaluate("(()=>{const e=document.querySelector('[aria-label=\"X（m）\"]');Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(e,'1000000');e.dispatchEvent(new Event('input',{bubbles:true}))})()");
      await click("保存并运行");
      await wait("!!document.querySelector('[role=\"alert\"]') || !!document.querySelector('[aria-label=\"保存校验\"]')");
      const afterRuns=(await(await fetch("http://127.0.0.1:8097/runs")).json()).length;
      if(beforeRuns!==afterRuns)throw Error("Out-of-coverage pair created Runs");
      await screenshot("geometric-invalid-coverage");
      await evaluate(`(()=>{const e=document.querySelector('[aria-label="X（m）"]');Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(e,${JSON.stringify(String(newX))});e.dispatchEvent(new Event('input',{bubbles:true}))})()`);
      evidence.checks.push({action:"invalid geometric coverage rejected before admission; coordinate corrected",additional_runs:0});
    }
    await click("保存并运行");
    await wait("location.pathname.startsWith('/run-groups/group-')");
    await wait(`[...document.querySelectorAll('[role="status"]')].some(e=>e.textContent==='已完成')`);
    const movedId = await evaluate("location.pathname.split('/').pop()");
    const moved = await (await fetch(`http://127.0.0.1:8097/run-groups/${movedId}/export`)).json();
    if (moved.common_snapshot_sha256===evidence.groups[0].common_snapshot_sha256 ||
        moved.workload_sha256!==evidence.groups[0].workload_sha256)
      throw Error("Geometric edit did not update common snapshot or unexpectedly changed COMM workload");
    evidence.groups.push(moved);
    evidence.checks.push({action:"real geometric pointer drag and A+B",old_x:oldX,new_x:newX,
      group_id:movedId,common_snapshot_changed:true,workload_unchanged:true});
    await screenshot("geometric-moved-comparison");
  }
  if (exceptions.length) throw Error("Browser reported runtime exceptions");
  evidence.success = true;
  }
} catch (error) {
  evidence.success = false;
  evidence.failure = String(error);
  await screenshot("failure");
  process.exitCode = 1;
} finally {
  fs.writeFileSync(`${root}/browser-evidence.json`, JSON.stringify(evidence, null, 2));
  console.log(JSON.stringify({ success: evidence.success, checks: evidence.checks, failure: evidence.failure, exceptions }));
  ws.close();
}
