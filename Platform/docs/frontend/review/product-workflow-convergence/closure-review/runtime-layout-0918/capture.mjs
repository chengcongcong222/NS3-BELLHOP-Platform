// Read-only, real Run evidence. Only a new browser tab is used; no run/resource
// writes, response interception, existing tab navigation or service restarts.
import { writeFile } from "node:fs/promises";
const base = process.env.PLATFORM_REVIEW_URL ?? "http://127.0.0.1:4178";
const run = process.env.PLATFORM_REVIEW_RUN ?? "run-edc4414a2c204d7daa4d88468de24b2e";
const page = await (await fetch("http://127.0.0.1:9229/json/new?about:blank", { method: "PUT" })).json();
const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => socket.addEventListener("open", resolve, { once: true }));
let sequence = 0;
const pending = new Map(), requests = [], errors = [];
socket.addEventListener("message", event => {
  const message = JSON.parse(event.data);
  if (message.method === "Network.requestWillBeSent") requests.push(message.params.request);
  if (message.method === "Runtime.exceptionThrown") errors.push(message.params.exceptionDetails);
  if (!message.id) return;
  const task = pending.get(message.id); pending.delete(message.id);
  message.error ? task.reject(message.error) : task.resolve(message.result);
});
const call = (method, params = {}) => new Promise((resolve, reject) => {
  const id = ++sequence; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params }));
});
const evaluate = async expression => {
  const reply = await call("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
  if (reply.exceptionDetails) throw Error(JSON.stringify(reply.exceptionDetails));
  return reply.result.value;
};
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const captures = [], interactions = [];
try {
  await call("Page.enable"); await call("Network.enable"); await call("Runtime.enable");
  for (const [width, height] of [[2553,1264], [1920,1080], [1440,900], [1366,768], [800,900]]) {
    await call("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: 1, mobile: false });
    await call("Page.navigate", { url: `${base}/runs/${run}` });
    let ready = false;
    for (let i = 0; i < 100; ++i) {
      await pause(100);
      ready = await evaluate('Boolean(document.querySelector(".runtime-workspace .event-stack button"))');
      if (ready) break;
    }
    if (!ready) throw Error("Real Run did not render");
    await evaluate("document.fonts.ready.then(() => true)");
    await pause(700);
    const metrics = await evaluate(`(() => {
      const rect = selector => { const b = document.querySelector(selector).getBoundingClientRect(); return {x:b.x,y:b.y,width:b.width,height:b.height,bottom:b.bottom}; };
      const main = document.querySelector('main');
      const plots = [...document.querySelectorAll('.timeline-plot')].map(x=>{const b=x.getBoundingClientRect();return {x:b.x,width:b.width}});
      const coordinate = element => new DOMPoint(0,0).matrixTransform(element.getScreenCTM()).x;
      const nodeX = [...document.querySelectorAll('.scene-node-depth [data-node-id]')].map(depth=>{
        const plan=document.querySelector('.run-topology [aria-label="节点 '+depth.dataset.nodeId+'"]');
        return {id:depth.dataset.nodeId,planX:coordinate(plan),depthX:coordinate(depth)};
      });
      return {viewport:{width:innerWidth,height:innerHeight},documentHeight:document.documentElement.scrollHeight,
        documentWidth:document.documentElement.scrollWidth, mainScrollHeight:main.scrollHeight,mainClientHeight:main.clientHeight,
        canvas:rect('.studio-surface'),plan:rect('.run-topology'),profile:rect('.scene-node-depth svg'),history:rect('.studio-history'),
        footer:rect('.app-shell>footer'),plots,nodeX,visibleEvents:document.querySelector('.runtime-workspace').dataset.visibleEventCount,
        eventTextOverflow:[...document.querySelectorAll('.event-stack button')].some(button=>button.querySelector('small').getBoundingClientRect().bottom>button.getBoundingClientRect().bottom+1)};
    })()`);
    const shot = await call("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
    await writeFile(new URL(`./runtime-${width}x${height}.png`, import.meta.url), Buffer.from(shot.data, "base64"));
    captures.push(metrics);
    if (width > 1150 && height > 700 && (metrics.mainScrollHeight > metrics.mainClientHeight + 1 || metrics.documentHeight > height + 1)) throw Error(`Desktop page overflow ${width}x${height}`);
    if (metrics.documentWidth > width + 1) throw Error(`Horizontal overflow ${width}`);
    if (!metrics.plots.every(plot => Math.abs(plot.x-metrics.plots[0].x)<1 && Math.abs(plot.width-metrics.plots[0].width)<1)) throw Error("Timeline plots misaligned");
    if (!metrics.nodeX.every(node => Math.abs(node.planX-node.depthX)<1)) throw Error("Plan/profile X coordinates misaligned: " + JSON.stringify(metrics));
    if (metrics.eventTextOverflow) throw Error("Event text overlaps the next row");
  }
  await call("Emulation.setDeviceMetricsOverride", { width: 1920, height: 1080, deviceScaleFactor: 1, mobile: false });
  await evaluate("document.querySelector('.lane-transmission button').click()");
  await pause(300);
  if (!(await evaluate("document.querySelector('.run-topology').classList.contains('phase-transmission')"))) throw Error("Tx selection did not reach the canvas");
  const txShot = await call("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
  await writeFile(new URL("./runtime-tx-selection.png", import.meta.url), Buffer.from(txShot.data, "base64"));
  await evaluate("document.querySelector('.runtime-node-list button').click()");
  await pause(200);
  if (!(await evaluate("Boolean(document.querySelector('[aria-label=\"节点检查器\"]')) && Boolean(document.querySelector('.event-stack button'))"))) throw Error("Node inspector hid the event stack");
  const nodeShot = await call("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
  await writeFile(new URL("./runtime-node-selection.png", import.meta.url), Buffer.from(nodeShot.data, "base64"));
  const expandedHeight = await evaluate("document.querySelector('.run-topology').getBoundingClientRect().height");
  await evaluate("document.querySelector('[aria-label=\"收起深度剖面\"]').click()");
  await pause(200);
  const collapsedHeight = await evaluate("document.querySelector('.run-topology').getBoundingClientRect().height");
  if (collapsedHeight <= expandedHeight) throw Error("Collapsing the profile did not return space to the plan");
  interactions.push({ tx_selection: true, node_and_events_visible_together: true, expandedHeight, collapsedHeight });
  const mutations = requests.filter(request => !["GET", "OPTIONS"].includes(request.method));
  if (mutations.length || errors.length) throw Error(JSON.stringify({ mutations, errors }));
  const report = { captured_at: new Date().toISOString(), base, run, read_only: true, mutations: 0, errors, captures, interactions };
  await writeFile(new URL("./layout-measurements.json", import.meta.url), JSON.stringify(report, null, 2) + "\n");
  process.stdout.write(JSON.stringify(report, null, 2) + "\n");
} finally {
  socket.close(); await fetch(`http://127.0.0.1:9229/json/close/${page.id}`);
}
