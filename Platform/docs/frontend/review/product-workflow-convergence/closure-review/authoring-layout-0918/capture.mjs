// Existing authored experiment, read-only browser validation. No save/run calls.
import { writeFile } from "node:fs/promises";
const base = process.env.PLATFORM_REVIEW_URL ?? "http://127.0.0.1:4178";
const draft = process.env.PLATFORM_REVIEW_DRAFT ?? "experiment-f1e890b99dec";
const page = await (await fetch("http://127.0.0.1:9229/json/new?about:blank", { method: "PUT" })).json();
const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => socket.addEventListener("open", resolve, { once: true }));
let sequence = 0;
const pending = new Map(), requests = [], errors = [], captures = [];
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
const screenshot = async name => {
  const shot = await call("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
  await writeFile(new URL(`./${name}.png`, import.meta.url), Buffer.from(shot.data, "base64"));
};
try {
  await call("Page.enable"); await call("Network.enable"); await call("Runtime.enable");
  for (const [width, height] of [[2553,1264], [1920,1080], [1440,900], [1366,768], [800,900]]) {
    await call("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: 1, mobile: false });
    await call("Page.navigate", { url: `${base}/workspace/experiment?draft=${draft}` });
    let ready = false;
    for (let i = 0; i < 100; ++i) {
      await pause(100);
      ready = await evaluate('Boolean(document.querySelector(".authoring-layout .depth-terrain-line"))');
      if (ready) break;
    }
    if (!ready) throw Error("Existing authored scene/terrain did not render");
    await evaluate("document.fonts.ready.then(() => true)"); await pause(400);
    const metrics = await evaluate(`(() => {
      const rect = selector => {const b=document.querySelector(selector).getBoundingClientRect();return {x:b.x,y:b.y,width:b.width,height:b.height,bottom:b.bottom}};
      const main=document.querySelector('main'), canvas=document.querySelector('.studio-canvas');
      const coordinate = element => new DOMPoint(0,0).matrixTransform(element.getScreenCTM()).x;
      const nodeX=[...document.querySelectorAll('.scene-node-depth [data-node-id]')].map(depth=>{
        const plan=document.querySelector('[aria-label="选择节点 N'+depth.dataset.nodeId+'"]');
        return {id:depth.dataset.nodeId,planX:coordinate(plan),depthX:coordinate(depth)};
      });
      return {viewport:{width:innerWidth,height:innerHeight},documentHeight:document.documentElement.scrollHeight,
        documentWidth:document.documentElement.scrollWidth,mainScrollHeight:main.scrollHeight,mainClientHeight:main.clientHeight,
        canvas:rect('.studio-canvas'),plan:rect('.studio-canvas>svg'),profile:rect('.scene-node-depth svg'),
        sidebar:rect('.studio-tools'),inspector:rect('.property-panel'),
        canvasScrollHeight:canvas.scrollHeight,canvasClientHeight:canvas.clientHeight,
        profileViewBox:document.querySelector('.scene-node-depth svg').getAttribute('viewBox'),nodeX,
        nodeGlyph:rect('.draggable-node .uuv-node-glyph')};
    })()`);
    await screenshot(`experiment-${width}x${height}`); captures.push(metrics);
    if (width > 1150 && height > 700 && (metrics.mainScrollHeight > metrics.mainClientHeight + 1 || metrics.documentHeight > height + 1)) throw Error("Desktop page overflows: " + JSON.stringify(metrics));
    if (metrics.canvasScrollHeight > metrics.canvasClientHeight + 1) throw Error("Canvas content clipped: " + JSON.stringify(metrics));
    if (metrics.documentWidth > width + 1) throw Error("Horizontal overflow");
    if (!metrics.nodeX.every(node=>Math.abs(node.planX-node.depthX)<1)) throw Error("Plan/profile X misalignment: " + JSON.stringify(metrics));
    if (Math.abs(metrics.nodeGlyph.width - 42) > 2) throw Error("Node icons scale with window width");
    if (Math.abs(metrics.profile.height - 190) > 1) throw Error("Depth profile is vertically compressed");
  }
  await call("Emulation.setDeviceMetricsOverride", { width:1920,height:1080,deviceScaleFactor:1,mobile:false });
  await evaluate(`document.querySelector('[aria-label="选择节点 N1"]').dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}))`);
  await pause(400);
  if (!(await evaluate("Boolean(document.querySelector('[aria-label=\"绑定轨迹资源\"]'))"))) throw Error("Node inspector missing");
  await screenshot("experiment-node-inspector");
  await evaluate("[...document.querySelectorAll('button')].find(b=>b.textContent==='环境与实验参数').click()");
  await pause(100);
  await evaluate("[...document.querySelectorAll('.inspector-tabs button')].find(b=>b.textContent==='物理模式').click()");
  await pause(200); await screenshot("experiment-physical-mode");
  const mutations = requests.filter(request=>!["GET","OPTIONS"].includes(request.method));
  if (mutations.length || errors.length) throw Error(JSON.stringify({mutations,errors}));
  const report = { captured_at:new Date().toISOString(),base,draft,read_only:true,mutations:0,errors,captures,
    interactions:{node_inspector:true,physical_mode_tab:true} };
  await writeFile(new URL('./layout-measurements.json',import.meta.url),JSON.stringify(report,null,2)+'\n');
  console.log(JSON.stringify({draft,mutations:0,captures:captures.map(c=>({viewport:c.viewport,canvas:c.canvas,plan:c.plan.height,profile:c.profile.height}))},null,2));
} finally { socket.close(); await fetch(`http://127.0.0.1:9229/json/close/${page.id}`); }
