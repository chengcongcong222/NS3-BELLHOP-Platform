// Real Chromium + real backend. Seeded inputs only; never intercept API responses.
import fs from "node:fs";
const root = process.argv[2];
const base = "http://127.0.0.1:4197";
const draft = JSON.parse(fs.readFileSync(`${root}/seed.json`)).draft_id;
const target = (await (await fetch("http://127.0.0.1:9227/json/list")).json()).find(t => t.type === "page");
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise(resolve => ws.addEventListener("open", resolve, { once: true }));
let sequence = 0;
const pending = new Map();
const exceptions = [];
ws.addEventListener("message", event => {
  const message = JSON.parse(event.data);
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
  if (exceptions.length) throw Error("Browser reported runtime exceptions");
  evidence.success = true;
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
