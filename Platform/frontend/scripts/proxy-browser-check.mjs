// Chromium CDP, real rendered page/API/ns-3. No intercepted responses.
import fs from "node:fs";
const root = process.argv[2], base = "http://127.0.0.1:4197", api = "http://127.0.0.1:8097";
const experimentName = `浏览器代理网络验收 ${Date.now()}`;
const mixedModems = process.argv.includes("--mixed");
const sequential = process.argv.includes("--sequential");
const poolMode = process.argv.includes("--pool");
const target = (await (await fetch("http://127.0.0.1:9227/json/list")).json()).find(t => t.type === "page");
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise(resolve => ws.addEventListener("open", resolve, { once: true }));
let sequence = 0;
const pending = new Map(), exceptions = [];
ws.addEventListener("message", event => {
  const m = JSON.parse(event.data);
  if (m.method === "Page.javascriptDialogOpening" && m.params.type === "beforeunload")
    void send("Page.handleJavaScriptDialog", {accept:true});
  if (m.method === "Runtime.exceptionThrown") exceptions.push(m.params);
  if (pending.has(m.id)) { const [resolve, reject] = pending.get(m.id); pending.delete(m.id); m.error ? reject(Error(JSON.stringify(m.error))) : resolve(m.result); }
});
const send = (method, params = {}) => new Promise((resolve, reject) => {
  const id = ++sequence; pending.set(id, [resolve, reject]); ws.send(JSON.stringify({ id, method, params }));
});
const evaluate = async expression => {
  const r = await send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) throw Error(JSON.stringify(r.exceptionDetails));
  return r.result.value;
};
const wait = async expression => {
  for (let i = 0; i < 200; i++) {
    if (await evaluate(expression)) return;
    await new Promise(r => setTimeout(r, 150));
  }
  throw Error(`Timeout ${expression}\n${await evaluate("document.body.innerText")}`);
};
const click = text => evaluate(`(()=>{const e=[...document.querySelectorAll('button,summary')].find(e=>e.textContent.trim()===${JSON.stringify(text)}&&!e.disabled);if(!e)throw Error('Missing ${text}');e.click();})()`);
const set = (label, value) => evaluate(`(()=>{
  const l=[...document.querySelectorAll('label')].find(l=>l.textContent.startsWith(${JSON.stringify(label)}));
  const e=l?.querySelector('input,select'); if(!e)throw Error('Missing field ${label}');
  const p=e.tagName==='SELECT'?HTMLSelectElement.prototype:HTMLInputElement.prototype;
  Object.getOwnPropertyDescriptor(p,'value').set.call(e,${JSON.stringify(value)});
  e.dispatchEvent(new Event(e.tagName==='SELECT'?'change':'input',{bubbles:true}));
})()`);
const screenshot = async name => {
  const r = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
  fs.writeFileSync(`${root}/${name}.png`, Buffer.from(r.data, "base64"));
};
const evidence = { executed_by: "Chromium CDP automation; UI writes, read-only API verification", checks: [], exceptions };
try {
  await send("Page.enable"); await send("Runtime.enable"); exceptions.length = 0;
  await send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await send("Page.navigate", { url: `${base}/workspace/experiment?mode=C` });
  await wait("!!document.querySelector('.proxy-mode-switch')");
  await evaluate("document.querySelector('.proxy-mode-switch summary').click()");
  const dialog = async e => { const m = JSON.parse(e.data); if (m.method === "Page.javascriptDialogOpening") await send("Page.handleJavaScriptDialog", { accept: true }); };
  ws.addEventListener("message", dialog);
  await click("新建实录样本代理网络"); ws.removeEventListener("message", dialog);
  await wait("!!document.querySelector('.proxy-editor')");
  await set("主资源", "uwa-blue-1-element-0");
  await set("实验名称", experimentName);
  await set("MAC", "Tdma");
  if (mixedModems) {
    await click("添加节点");
    await set("发送时隙（s）", "3.072");
    await set("规划周期（s）", "32.768");
    await set("周期数", "1");
    await evaluate("document.querySelector('[aria-label=\"节点 N0\"]').dispatchEvent(new MouseEvent('click',{bubbles:true}))");
    await set("此发送节点的调制", "Bpsk");
    await evaluate("document.querySelector('[aria-label=\"节点 N1\"]').dispatchEvent(new MouseEvent('click',{bubbles:true}))");
    await set("此发送节点的调制", "Bfsk");
  }
  await evaluate("document.querySelector('[aria-label=\"链路 N0 到 N2\"]').dispatchEvent(new MouseEvent('click',{bubbles:true}))");
  await set("单边信道覆盖", "uwa-blue-1-element-0");
  await set("建模时延（s）", mixedModems ? "5.12" : sequential ? "2.048" : "4.096");
  await evaluate(`(()=>{const l=[...document.querySelectorAll('label')].find(l=>l.textContent.includes('确认：跨链路'));l.querySelector('input').click();})()`);
  await wait("!document.body.innerText.includes('正在读取 OSDM')");
  await set("分配方式","GlobalPool");
  await evaluate("(()=>{const l=[...document.querySelectorAll('.proxy-controls label')].find(l=>l.textContent.trim()==='Blue 1 · 第1阵元');if(!l)throw Error('Missing pool member');l.querySelector('input').click()})()");
  if(poolMode) await evaluate("(()=>{const l=[...document.querySelectorAll('.proxy-controls label')].find(l=>l.textContent.trim()==='Blue 1 · 第2阵元');if(!l)throw Error('Missing second verified pool member');l.querySelector('input').click()})()");
  await click("检查并展开链路映射");
  await wait("!!document.querySelector('.proxy-editor table tbody tr')");
  if (!await evaluate("document.querySelector('.proxy-editor table').textContent.includes('起点样本 0')"))
    throw Error("Resolved pool did not show concrete fragment");
  await screenshot("proxy-pool-and-override");
  evidence.checks.push({action:"verified Blue pool with explicit per-edge override",pool_size:poolMode?2:1});
  await set("分配方式","GlobalSame");
  if (await evaluate("!!document.querySelector('.proxy-editor table tbody tr')"))
    throw Error("Stale validation remained after allocation changed");
  if (mixedModems) {
    await set("分配方式","Explicit");
    await click("检查并展开链路映射");
    await wait("document.body.innerText.includes('缺少') || document.body.innerText.includes('尚未')");
    if (await evaluate("document.querySelector('.proxy-editor input').value") !== experimentName)
      throw Error("Validation error discarded editor input");
    evidence.checks.push({action:"missing physical binding rejected / input retained"});
    await set("分配方式","GlobalSame");
  }
  if(poolMode) await set("分配方式","GlobalPool");
  await click("检查并展开链路映射");
  await wait("!!document.querySelector('.proxy-editor table tbody tr')");
  await click("保存草稿");
  await wait("document.body.innerText.includes('代理网络草稿已保存')");
  const drafts = await (await fetch(`${api}/authoring/drafts`)).json();
  const draft = drafts.find(d => d.kind === "experiment" && d.name === experimentName);
  if (!draft || draft.payload.recorded_proxy.overrides[0].sample.modeled_reference_delay_ns !== (mixedModems ? "5120000000" : sequential ? "2048000000" : "4096000000")) throw Error("Saved override mismatch");
  if (mixedModems && draft.payload.recorded_proxy.transmitter_modulations.length !== 2) throw Error("Sender modems not saved");
  evidence.checks.push({ action: "create/override/save via UI", draft_id: draft.draft_id });
  const renderStart=performance.now();
  await send("Page.navigate", { url: `${base}/workspace/experiment?draft=${draft.draft_id}` });
  await wait("!!document.querySelector('.proxy-editor')");
  await wait(`document.querySelector('.proxy-editor input')?.value===${JSON.stringify(experimentName)}`);
  evidence.checks.push({action:"editor initial render",wall_ms:performance.now()-renderStart,
    navigation:await evaluate("({dom_content_loaded_ms:performance.getEntriesByType('navigation')[0]?.domContentLoadedEventEnd,js_heap_bytes:performance.memory?.usedJSHeapSize})")});
  for (const [width, height] of [[1600,1000],[1440,900],[1366,768],[1024,768],[800,900]]) {
    await send("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: 1, mobile: false });
    await new Promise(r => setTimeout(r, 150));
    const dimensions = await evaluate("({width:innerWidth,scroll:document.documentElement.scrollWidth})");
    if (dimensions.scroll > width) throw Error(`Overflow at ${width}: ${dimensions.scroll}`);
    await screenshot(`proxy-editor-${width}`);
    evidence.checks.push({ action: "saved editor reopen/viewport", ...dimensions, height });
  }
  await click("保存并运行");
  await wait("location.pathname.startsWith('/runs/run-')");
  const rid = await evaluate("location.pathname.split('/').pop()");
  for (let i=0;i<200;i++) {
    const run=await (await fetch(`${api}/runs/${rid}`)).json();
    if(run.lifecycle==="Failed")throw Error(JSON.stringify(run.failure));
    if(run.lifecycle==="Completed"){evidence.run=run;break;}
    await new Promise(r=>setTimeout(r,150));
  }
  if(!evidence.run)throw Error("Worker did not complete");
  await wait("!!document.querySelector('[aria-label=\"代理网络逻辑画布\"]')");
  await screenshot("proxy-running");
  const stream=await (await fetch(`${api}/runs/${rid}/events`)).text();
  const events=stream.split("\n").filter(l=>l.startsWith("data: ")).map(l=>JSON.parse(l.slice(6))).filter(e=>e.trace);
  const mixed=events.filter(e=>e.trace.kind==="Reception" && Number(e.trace.payload.mixture?.signal_count)>1);
  if(sequential ? mixed.length!==0 : !mixed.length)throw Error("Unexpected mixture evidence");
  evidence.events=events; evidence.checks.push({action:sequential ? "browser start / non-overlapping actual reception" : "browser start / actual mixed reception",run_id:rid,mixed: mixed.length});
  await click("完整回放");
  await wait("!document.body.innerText.includes('等待事件')");
  for(const [width,height] of [[1600,1000],[1440,900],[1366,768],[1024,768],[800,900]]) {
    await send("Emulation.setDeviceMetricsOverride",{width,height,deviceScaleFactor:1,mobile:false});
    await new Promise(r=>setTimeout(r,100));
    const scroll=await evaluate("document.documentElement.scrollWidth");
    if(scroll>width)throw Error(`Running page overflow ${width}`);
    if(await evaluate("[...document.querySelectorAll('.live-metric-strip b')].some(e=>e.clientWidth>0&&e.scrollWidth>e.clientWidth+1)"))
      throw Error(`Metric card value overflow ${width}`);
    await screenshot(`proxy-running-${width}`);
    evidence.checks.push({action:"actual run viewport",width,height,scroll});
  }
  const groups = await (await fetch(`${api}/run-groups`)).json();
  const group = groups.find(g => g.members.length === 1 && g.members[0].run_id === rid);
  if (!group) throw Error("Missing C archive");
  await send("Page.navigate", { url: `${base}/run-groups/${group.group_id}` });
  await wait("document.body.innerText.includes('实录样本代理网络归档')");
  await wait("[...document.querySelectorAll('button')].some(b=>b.textContent==='按原配置重跑 C')");
  await wait("!!document.querySelector('[aria-label=\"筛选发送节点\"]')");
  await evaluate(`(()=>{const e=document.querySelector('[aria-label="筛选发送节点"]');Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,'value').set.call(e,'0');e.dispatchEvent(new Event('change',{bubbles:true}));})()`);
  await wait("!!document.querySelector('.proxy-result-evidence table')");
  const filtered = await evaluate("[...document.querySelectorAll('.proxy-result-evidence > .analysis-scroll tbody tr')].map(r=>r.cells[1].textContent)");
  if (!filtered.length || filtered.some(s=>!s.startsWith('N0'))) throw Error("Result sender filter mismatch");
  await screenshot("proxy-archive");
  await click("按原配置重跑 C");
  await wait(`location.pathname!==${JSON.stringify(`/run-groups/${group.group_id}`)} && location.pathname.startsWith('/run-groups/')`);
  const repeatedId = await evaluate("location.pathname.split('/').pop()");
  let repeated;
  for (let i = 0; i < 400; i++) {
    repeated = await (await fetch(`${api}/run-groups/${repeatedId}`)).json();
    if (repeated.state === "Completed") break;
    if (["Failed","PartialFailed"].includes(repeated.state)) throw Error(JSON.stringify(repeated));
    await new Promise(r=>setTimeout(r,150));
  }
  if (repeated.state !== "Completed") throw Error("C archived repeat did not complete");
  if (repeated.common_snapshot_sha256 !== group.common_snapshot_sha256 ||
      repeated.workload_sha256 !== group.workload_sha256) throw Error("C repeat frozen identity changed");
  evidence.group = group; evidence.repeated_group = repeated;
  evidence.checks.push({action:"archive UI / frozen repeat C",group_id:group.group_id,repeated_group_id:repeatedId});
  if (mixedModems) {
    const beforeContext = await (await fetch(`${api}/runs/${rid}/definition-context`)).json();
    const beforeDraft = (await (await fetch(`${api}/authoring/drafts`)).json()).find(d=>d.draft_id===draft.draft_id);
    await send("Page.navigate",{url:`${base}/workspace/experiment?draft=${draft.draft_id}`});
    await wait("!!document.querySelector('.proxy-editor [aria-label=\"节点 N0\"]')");
    const point = await evaluate(`(()=>{const b=document.querySelector('.proxy-editor [aria-label="节点 N0"] circle').getBoundingClientRect();return {x:b.x+b.width/2,y:b.y+b.height/2};})()`);
    await send("Input.dispatchMouseEvent",{type:"mousePressed",...point,button:"left",buttons:1,clickCount:1});
    await send("Input.dispatchMouseEvent",{type:"mouseMoved",x:point.x+55,y:point.y+40,button:"left",buttons:1});
    await send("Input.dispatchMouseEvent",{type:"mouseReleased",x:point.x+55,y:point.y+40,button:"left",buttons:0,clickCount:1});
    await click("保存草稿");
    await wait("document.body.innerText.includes('代理网络草稿已保存')");
    const afterDraft = (await (await fetch(`${api}/authoring/drafts`)).json()).find(d=>d.draft_id===draft.draft_id);
    if (JSON.stringify(afterDraft.payload.proxy_ui_layout)===JSON.stringify(beforeDraft.payload.proxy_ui_layout))
      throw Error("Pointer drag did not persist logical layout");
    if (afterDraft.payload.scenario_revision!==beforeDraft.payload.scenario_revision)
      throw Error("Layout-only edit republished physical scenario");
    await click("保存并运行");
    await wait("location.pathname.startsWith('/runs/run-')");
    const movedRun = await evaluate("location.pathname.split('/').pop()");
    let final;
    for(let i=0;i<400;i++) {
      final=await (await fetch(`${api}/runs/${movedRun}`)).json();
      if(final.lifecycle==="Completed")break;
      if(final.lifecycle==="Failed")throw Error(JSON.stringify(final.failure));
      await new Promise(r=>setTimeout(r,150));
    }
    if(final.lifecycle!=="Completed")throw Error("Moved layout run incomplete");
    const afterContext = await (await fetch(`${api}/runs/${movedRun}/definition-context`)).json();
    if(!beforeContext.physics_input_sha256 || beforeContext.physics_input_sha256!==afterContext.physics_input_sha256)
      throw Error("Logical layout changed physical input digest");
    const movedStream=await (await fetch(`${api}/runs/${movedRun}/events`)).text();
    const movedTraces=movedStream.split("\n").filter(l=>l.startsWith("data: ")).map(l=>JSON.parse(l.slice(6))).filter(e=>e.trace).map(e=>e.trace);
    if(JSON.stringify(movedTraces)!==JSON.stringify(events.map(e=>e.trace)))throw Error("Logical layout changed events");
    evidence.checks.push({action:"pointer drag / save / repeat",run_id:movedRun,physics_input_sha256:afterContext.physics_input_sha256,
      scenario_revision_unchanged:true,event_identity:true});
  }
  await send("Page.navigate",{url:`${base}/workspace/experiment?draft=${draft.draft_id}`});
  await wait("[...document.querySelectorAll('button')].some(e=>e.textContent==='另存实验副本'&&!e.disabled)");
  const copySource=(await (await fetch(`${api}/authoring/drafts`)).json()).find(d=>d.draft_id===draft.draft_id);
  await click("另存实验副本");
  await wait(`location.search.includes('draft=')&&!location.search.includes(${JSON.stringify(draft.draft_id)})`);
  const copyId=await evaluate("new URLSearchParams(location.search).get('draft')");
  const copied=(await (await fetch(`${api}/authoring/drafts`)).json()).find(d=>d.draft_id===copyId);
  if(!copied || JSON.stringify(copied.payload)!==JSON.stringify(copySource.payload))throw Error("Copy changed frozen source settings");
  evidence.checks.push({action:"copy saved proxy experiment via UI",source:draft.draft_id,copy:copyId,identical_payload:true});
  if(exceptions.length)throw Error("Browser runtime exceptions");
  evidence.success=true;
} catch(error) {
  evidence.success=false;evidence.failure=String(error);await screenshot("proxy-failure");process.exitCode=1;
} finally {
  fs.writeFileSync(`${root}/proxy-browser-evidence.json`,JSON.stringify(evidence,null,2));
  console.log(JSON.stringify({success:evidence.success,checks:evidence.checks,failure:evidence.failure,exceptions}));
  ws.close();
}
