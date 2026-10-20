// Real UI / API / Worker; no response interception or synthetic results.
import fs from "node:fs";
const root = process.argv[2], base = "http://127.0.0.1:4197", api = "http://127.0.0.1:8097";
const target = (await (await fetch("http://127.0.0.1:9227/json/list")).json()).find(t => t.type === "page");
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise(r => ws.addEventListener("open", r, { once: true }));
let seq = 0;
const pending = new Map(), exceptions = [], evidence = { executed_by: "Chromium CDP; real UI and Worker", checks: [], exceptions };
ws.addEventListener("message", e => {
  const m = JSON.parse(e.data);
  if (m.method === "Runtime.exceptionThrown") exceptions.push(m.params);
  if (pending.has(m.id)) { const [ok, fail] = pending.get(m.id); pending.delete(m.id); m.error ? fail(Error(JSON.stringify(m.error))) : ok(m.result); }
});
const send = (method, params = {}) => new Promise((ok, fail) => { const id = ++seq; pending.set(id, [ok, fail]); ws.send(JSON.stringify({ id, method, params })); });
const ev = async expression => {
  const r = await send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) throw Error(JSON.stringify(r.exceptionDetails));
  return r.result.value;
};
const wait = async expression => {
  for (let i = 0; i < 200; i++) { if (await ev(expression)) return; await new Promise(r => setTimeout(r, 150)); }
  throw Error(`Timeout ${expression}\n${await ev("document.body.innerText")}`);
};
const click = text => ev(`(()=>{const e=[...document.querySelectorAll('button')].find(e=>e.textContent.trim()===${JSON.stringify(text)}&&!e.disabled);if(!e)throw Error('Missing button');e.click()})()`);
const set = (label, value) => ev(`(()=>{const l=[...document.querySelectorAll('label')].find(l=>l.textContent.startsWith(${JSON.stringify(label)}));const e=l?.querySelector('input,select');if(!e)throw Error('Missing field');Object.getOwnPropertyDescriptor(e.tagName==='SELECT'?HTMLSelectElement.prototype:HTMLInputElement.prototype,'value').set.call(e,${JSON.stringify(value)});e.dispatchEvent(new Event(e.tagName==='SELECT'?'change':'input',{bubbles:true}))})()`);
const shot = async name => fs.writeFileSync(`${root}/${name}.png`, Buffer.from((await send("Page.captureScreenshot", { format: "png" })).data, "base64"));
try {
  await send("Page.enable"); await send("Runtime.enable"); exceptions.length = 0;
  await send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await send("Page.navigate", { url: `${base}/workspace/experiment?mode=C` });
  await wait("!!document.querySelector('.recorded-experiment-page')");
  await set("信道资源 / 实验环境", "Recorded:uwa-blue-1-element-0");
  await click("保留其他输入并更换环境");
  const name = `旧单链路页面验证 ${Date.now()}`;
  await set("实验名称", name);
  await set("建模传播时延", "2.048");
  await set("业务周期", "16.384");
  await set("周期数", "1");
  await click("保存实验草稿");
  await wait("[...document.querySelectorAll('[role=status]')].some(e=>e.textContent==='已保存')");
  const draft = (await (await fetch(`${api}/authoring/drafts`)).json()).find(d => d.name === name && d.kind === "experiment");
  if (!draft || draft.payload.recorded_proxy) throw Error("Legacy save converted to proxy");
  // All three family-switch actions, retaining the recorded buffer.
  await click("几何声场网络位置驱动 · Bellhop");
  await click("取消");
  if (!await ev("!!document.querySelector('.recorded-experiment-page')")) throw Error("Cancel changed editor");
  await click("几何声场网络位置驱动 · Bellhop");
  await click("保留当前输入，打开另一类实验");
  await wait("document.querySelector('[aria-label=\"几何声场网络\"]').getAttribute('aria-pressed')==='true'");
  await click("实录信道通信实测 CIR · 逻辑链路");
  await click("保留当前输入，打开另一类实验");
  if (!await ev(`document.querySelector('.recorded-experiment-page input').value===${JSON.stringify(name)}`)) throw Error("Family switch lost input");
  await click("几何声场网络位置驱动 · Bellhop");
  await click("保存当前后切换");
  await wait("document.querySelector('[aria-label=\"几何声场网络\"]').getAttribute('aria-pressed')==='true'");
  evidence.checks.push({ action: "family switch cancel / retain / save", input_retained: true });
  await send("Page.navigate", { url: `${base}/workspace/experiment?draft=${draft.draft_id}` });
  await wait(`document.querySelector('.recorded-experiment-page input')?.value===${JSON.stringify(name)}`);
  await shot("legacy-v4-reopened");
  await set("实验名称", `${name} 已修改`);
  await click("保存并运行");
  await wait("location.pathname.startsWith('/runs/run-')");
  const rid = await ev("location.pathname.split('/').pop()");
  let run;
  for (let i = 0; i < 300; i++) {
    run = await (await fetch(`${api}/runs/${rid}`)).json();
    if (["Completed", "Failed"].includes(run.lifecycle)) break;
    await new Promise(r => setTimeout(r, 150));
  }
  if (run.lifecycle !== "Completed") throw Error(JSON.stringify(run));
  const context = await (await fetch(`${api}/runs/${rid}/definition-context`)).json();
  await shot("legacy-v4-running");
  evidence.checks.push({ action: "C1 v4 save / reopen / modify / actual run", draft_id: draft.draft_id, run_id: rid, context });
  if (exceptions.length) throw Error("Browser exceptions");
  evidence.success = true;
} catch (e) {
  evidence.success = false; evidence.failure = String(e); await shot("failure"); process.exitCode = 1;
} finally {
  fs.writeFileSync(`${root}/browser-evidence.json`, JSON.stringify(evidence, null, 2));
  console.log(JSON.stringify({ success: evidence.success, failure: evidence.failure, checks: evidence.checks, exceptions }));
  ws.close();
}
