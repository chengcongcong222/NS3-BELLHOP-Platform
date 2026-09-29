// Real services only. Repeats a nominated review Run; never rewrites its draft.
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
const [frontend, backend, debug, sourceId, output] = process.argv.slice(2);
const get = async route => { const r = await fetch(backend + route); assert(r.ok); return r.json(); };
const page = await (await fetch(debug + "/json/new?about:blank", { method: "PUT" })).json();
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => { ws.onopen = resolve; });
let id = 0; const pending = new Map();
ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id) { pending.get(m.id)?.(m); pending.delete(m.id); } };
const send = (method, params = {}) => new Promise((resolve, reject) => {
  const next = ++id, timer = setTimeout(() => { pending.delete(next); reject(Error(method)); }, 10000);
  pending.set(next, m => { clearTimeout(timer); m.error ? reject(Error(JSON.stringify(m.error))) : resolve(m.result); });
  ws.send(JSON.stringify({ id: next, method, params }));
});
const evaluate = async expression => { const r = await send("Runtime.evaluate", { expression, returnByValue: true }); assert(!r.exceptionDetails, JSON.stringify(r.exceptionDetails)); return r.result.value; };
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const wait = async expression => { for (let n = 0; n < 300; n++) { if (await evaluate(expression)) return; await pause(100); } throw Error("UI timeout: " + expression); };
const button = text => evaluate(`[...document.querySelectorAll('button')].find(b=>b.textContent.trim()===${JSON.stringify(text)})?.click()`);
const screenshot = async name => { const r = await send("Page.captureScreenshot", { format: "png" }); fs.writeFileSync(path.join(output, name), Buffer.from(r.data, "base64")); };
try {
  fs.mkdirSync(output, { recursive: true });
  await send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await send("Page.navigate", { url: frontend + "/runs/" + sourceId });
  await wait("[...document.querySelectorAll('button')].some(b=>b.textContent==='按原配置重新运行'&&!b.disabled)");
  await evaluate(`window.reviewCounts=[]; window.reviewObserver=new MutationObserver(()=>{
    const el=document.querySelector('.studio-product');
    if(el&&location.pathname!==${JSON.stringify("/runs/" + sourceId)}) window.reviewCounts.push({path:location.pathname,presentation:el.dataset.presentation,count:Number(el.dataset.visibleEventCount)});
  }); window.reviewObserver.observe(document.body,{subtree:true,childList:true,attributes:true});`);
  await button("按原配置重新运行");
  await wait(`location.pathname.startsWith('/runs/')&&location.pathname!==${JSON.stringify("/runs/" + sourceId)}&&!!document.querySelector('.studio-product')`);
  const repeatedId = (await evaluate("location.pathname")).split("/").at(-1);
  assert.notEqual(sourceId, repeatedId);
  await screenshot("rerun-progressive-start.png");
  await wait("Number(document.querySelector('.studio-product')?.dataset.visibleEventCount)>3");
  await button("暂停");
  await screenshot("rerun-progressive-events.png");
  const counts = await evaluate("window.reviewCounts");
  assert(counts.some(c => c.count === 0 && c.presentation === "progressive"), "new Run must start with empty displayed history");
  assert(counts.some(c => c.count > 0 && c.presentation === "progressive"));
  await button("完整回放");
  await wait("document.querySelector('.studio-product')?.dataset.presentation==='replay'");
  await evaluate("(()=>{const s=document.querySelector('[aria-label=\"按仿真时间定位\"]');Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(s,'50');s.dispatchEvent(new Event('input',{bubbles:true}));s.dispatchEvent(new Event('change',{bubbles:true}));})()");
  const geometry = await evaluate(`(()=>{
    const plot=[...document.querySelectorAll('.timeline-plot')].map(el=>{const r=el.getBoundingClientRect();return{x:r.x,width:r.width}});
    const plan=document.querySelector('[aria-label="运行画布"]');
    const depth=document.querySelector('[aria-label="节点与海底同坐标剖面"]');
    const planNodes=[...plan.querySelectorAll('[role="button"]')];
    const depthNodes=[...depth.querySelectorAll('[data-node-id]')];
    const pixelX=el=>new DOMPoint(0,0).matrixTransform(el.getScreenCTM()).x;
    const paired=depthNodes.map(n=>({id:n.dataset.nodeId,depthX:pixelX(n),planX:pixelX(planNodes.find(p=>p.getAttribute('aria-label')==='节点 '+n.dataset.nodeId))}));
    return {plot,paired,pageHeight:document.documentElement.scrollHeight,viewport:innerHeight};
  })()`);
  for (const p of geometry.plot) { assert(Math.abs(p.x - geometry.plot[0].x) < 1); assert(Math.abs(p.width - geometry.plot[0].width) < 1); }
  for (const p of geometry.paired) assert(Math.abs(p.depthX - p.planX) < 1, JSON.stringify(p));
  await screenshot("aligned-runtime-timeline.png");
  const original = await get(`/runs/${sourceId}/results`), repeated = await get(`/runs/${repeatedId}/results`);
  assert.deepEqual(repeated.projection, original.projection, "view controls must not alter result");
  const context = await get(`/runs/${repeatedId}/definition-context`), oldContext = await get(`/runs/${sourceId}/definition-context`);
  assert.deepEqual(context.scenario, oldContext.scenario);
  assert.deepEqual(context.experiment, oldContext.experiment);
  fs.writeFileSync(path.join(output,"live-view-proof.json"), JSON.stringify({ sourceId, repeatedId, counts, geometry, unchangedProjection: true, frozenConfigurationPreserved: true }, null, 2));
  console.log(JSON.stringify({ sourceId, repeatedId, geometry, progressiveFromZero: true }));
} finally { ws.close(); await fetch(debug + "/json/close/" + page.id); }
