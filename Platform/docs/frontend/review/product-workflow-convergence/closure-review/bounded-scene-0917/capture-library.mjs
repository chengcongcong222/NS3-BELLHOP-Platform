// Read-only capture of the running development UI. No intercepted responses,
// draft writes, Run launches, service restarts, or existing tab navigation.
import { writeFile } from "node:fs/promises";

const page = await (await fetch("http://127.0.0.1:9229/json/new?about:blank", { method: "PUT" })).json();
const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => socket.addEventListener("open", resolve, { once: true }));
let sequence = 0;
const pending = new Map(), requests = [];
socket.addEventListener("message", event => {
  const message = JSON.parse(event.data);
  if (message.method === "Network.requestWillBeSent") requests.push({ method: message.params.request.method, url: message.params.request.url });
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
await call("Page.enable"); await call("Network.enable");
await call("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
const captures = [];
try {
  for (const [name, path, selector] of [
    ["01-workbench", "/", ".wb-home"],
    ["02-case-detail", "/cases/acceptance4-shallow-water", ".wb-case-detail"],
    ["03-run-library", "/runs", ".run-library"],
  ]) {
    await call("Page.navigate", { url: "http://127.0.0.1:4177" + path });
    let ready = false;
    for (let attempt = 0; attempt < 80; ++attempt) {
      await new Promise(resolve => setTimeout(resolve, 100));
      ready = await evaluate(`document.readyState === "complete" && Boolean(document.querySelector(${JSON.stringify(selector)}))`);
      if (ready) break;
    }
    if (!ready) throw Error(`View did not render: ${path}`);
    await evaluate("document.fonts.ready.then(() => true)");
    await new Promise(resolve => setTimeout(resolve, 500));
    const metrics = await evaluate(`(() => {
      const root = document.querySelector(${JSON.stringify(selector)}), rect = root.getBoundingClientRect();
      return { viewport: { width: innerWidth, height: innerHeight }, documentWidth: document.documentElement.scrollWidth,
        documentHeight: document.documentElement.scrollHeight, content: { left:rect.left,top:rect.top,width:rect.width,height:rect.height,bottom:rect.bottom },
        headings: [...root.querySelectorAll('h1,h2')].map(x => x.textContent),
        overflow: [...root.querySelectorAll('section,article,table,svg')].filter(x => x.getBoundingClientRect().right > innerWidth + 1).map(x=>x.className.baseVal??x.className) };
    })()`);
    const screenshot = await call("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
    await writeFile(new URL(`./${name}.png`, import.meta.url), Buffer.from(screenshot.data, "base64"));
    captures.push({ name, path, ...metrics });
  }
  const mutations = requests.filter(request => !["GET", "OPTIONS"].includes(request.method));
  if (mutations.length) throw Error(`Unexpected mutating network requests: ${JSON.stringify(mutations)}`);
  const report = { captured_at: new Date().toISOString(), ui: "http://127.0.0.1:4177", browser: "existing Chrome CDP, independent tab", read_only: true, captures };
  await writeFile(new URL("./library-layout.json", import.meta.url), JSON.stringify(report, null, 2) + "\n");
  process.stdout.write(JSON.stringify(report, null, 2) + "\n");
} finally {
  socket.close();
  await fetch(`http://127.0.0.1:9229/json/close/${page.id}`);
}
