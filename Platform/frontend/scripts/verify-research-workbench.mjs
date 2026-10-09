// Isolated real backend + C++ worker + Bellhop. Never intercepts/mocks API traffic.
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
const [frontend, backend, debug, output] = process.argv.slice(2);
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const api = async (route, body) => {
  const r = await fetch(backend + route, body ? { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) } : undefined);
  const data = await r.json(); assert(r.ok, JSON.stringify(data)); return data;
};
const stamp = Date.now();
const env = await api("/authoring/drafts", { kind: "environment", name: `科研展示验证海域 ${stamp}`, payload: {
  center_frequency_hz: 25000,
  sound_speed_profile: [{ depth_meters: 0, speed_meters_per_second: 1500 }, { depth_meters: 100, speed_meters_per_second: 1495 }],
  bathymetry: [{ range_meters: 0, depth_meters: 100 }, { range_meters: 1000, depth_meters: 100 }],
  bottom: { sound_speed_meters_per_second: 1700, density_grams_per_cubic_centimeter: 1.8, attenuation_db_per_wavelength: .5 },
  bellhop: { beam_count: 301, range_sample_count: 51, source_depths_meters: [8, 60], receiver_depths_meters: [8, 16, 24, 32, 40, 48, 56, 60, 72, 80, 88, 96], minimum_launch_angle_degrees: -60, maximum_launch_angle_degrees: 60, output: "Arrivals" },
} });
const build = await api("/authoring/environment-builds", { draft_id: env.draft_id });
assert.equal(build.state, "Completed", JSON.stringify(build));
const publication = await api(`/authoring/environment-builds/${build.build_id}/publish`, {});
const environmentId = publication.environment_asset_id;
const plot = await api(`/environments/${environmentId}/acoustic-plots?source_index=1&receiver_index=0&range_index=10`);
assert(plot.available && plot.ray_plot.available && plot.impulse.length > 0);
assert.equal(plot.ray_plot.source_depth_m, 60);
assert(plot.ray_plot.rays.every(ray => ray.points[0][1] === 60));
// A real one-depth Bellhop build exercises the curve presentation; no fixture is
// substituted for API data in this browser check.
const singleDepthDraft = await api("/authoring/drafts", { kind: "environment", name: `单深度信道图验证 ${stamp}`,
  payload: { ...env.payload, bellhop: { ...env.payload.bellhop, receiver_depths_meters: [8] } } });
const singleDepthBuild = await api("/authoring/environment-builds", { draft_id: singleDepthDraft.draft_id });
assert.equal(singleDepthBuild.state, "Completed", JSON.stringify(singleDepthBuild));
const singleDepthPublication = await api(`/authoring/environment-builds/${singleDepthBuild.build_id}/publish`, {});
const singleDepthEnvironmentId = singleDepthPublication.environment_asset_id;
const positions = [[0, 20, 0, -8], [1, 150, 10, -60], [2, 400, 40, -60], [3, 700, -50, -60]];
const assetId = `research-tracks-${stamp}`;
const csv = "track_id,time_s,x_m,y_m,z_m\n" + positions.flatMap(([id,x,y,z]) => [`N${id},0,${x},${y},${z}`, `N${id},200,${x+(id?80:0)},${y},${z}`]).join("\n") + "\n";
await api("/authoring/trajectories/import-csv", { asset_id: assetId, version: 1, name: "科研展示验证轨迹", content: csv });
const scene = await api("/authoring/drafts", { kind: "scenario", name: "四节点协作观测", payload: {
  environment_asset_id: environmentId, environment_version: 1, fusion_center_node_id: "0",
  nodes: positions.map(([id,x,y,z]) => ({ node_id: String(id), can_transmit: id !== 0, can_receive: true, duplex_mode: "HalfDuplex", initial_position: { x_meters:x,y_meters:y,z_meters:z }, initial_velocity: {x_meters_per_second:0,y_meters_per_second:0,z_meters_per_second:0} })),
  trajectory_bindings: positions.map(([id]) => ({ node_id: String(id), asset_id: assetId, version: 1, track_id: `N${id}` })),
  spatial_domain: {model:"ProfileExtrusion",x_min_m:0,x_max_m:1000,y_min_m:-150,y_max_m:150,surface_clearance_m:1,bottom_clearance_m:5},
} });
const runs = [];
for (const [physical_layer, noisy] of [["Channel1Abstract", false], ["Channel2Waveform", false], ["Channel2Waveform", true]]) {
  const experiment = await api("/authoring/drafts", {kind:"experiment",name:noisy ? "强噪声链路 · 模式 B" : physical_layer === "Channel1Abstract" ? "协作观测 · 模式 A" : "波形链路 · 模式 B",payload:{scenario_id:scene.draft_id,routing_mode:"DirectToFusionCenter",mac_mode:"Tdma",workload:"BearingFusion",minimum_bearing_points:"5",maximum_fusion_period_ns:"180000000000",bit_rate_bits_per_second:"60",simulation_cycle_count:"2",deterministic_seed:"23",physical_layer,waveform_modulation:"Bpsk",waveform_noise:"Awgn",waveform_noise_psd_db_re_1upa2_per_hz:noisy ? 80.0 : -10.0}});
  const started = await api(`/authoring/experiments/${experiment.draft_id}/runs`, {});
  let terminal;
  for(let i=0;i<300;i++){terminal=await api(`/runs/${started.run_id}`);if(["Completed","Failed"].includes(terminal.lifecycle))break;await pause(100);}
  assert.equal(terminal.lifecycle,"Completed",JSON.stringify(terminal));
  const result=await api(`/runs/${started.run_id}/results`);
  assert.equal(result.projection.transmission_count,"6");
  assert.equal(result.projection.local_delivery_count,noisy ? "0" : "6");
  assert.equal(result.fusion_results.length,noisy ? 0 : 1);
  if(!noisy) assert.equal(result.fusion_results[0].observation_count,"6");
  if(physical_layer === "Channel2Waveform") {
    const intended=result.visualization.waveform.filter(frame=>frame.receiver_node_id === "0");
    assert.equal(intended.length,6);
    assert.equal(intended.reduce((sum,frame)=>sum+Number(frame.payload_bit_count),0),720);
    assert(intended.every(frame=>frame.crc_ok === !noisy));
    const errors=intended.reduce((sum,frame)=>sum+Number(frame.bit_error_count),0);
    if(noisy) assert(errors > 0); else assert.equal(errors,0);
  }
  runs.push({runId:started.run_id,draftId:experiment.draft_id,mode:physical_layer,noisy,result});
}
fs.mkdirSync(output,{recursive:true});
fs.writeFileSync(path.join(output,"real-inputs-and-results.json"),JSON.stringify({environmentId,buildId:build.build_id,singleDepthEnvironmentId,singleDepthBuildId:singleDepthBuild.build_id,sceneId:scene.draft_id,runs,arrivals_sha256:plot.arrivals_sha256,ray_sha256:plot.ray_plot.sha256},null,2));
const page=await(await fetch(debug+"/json/new?about:blank",{method:"PUT"})).json();
const ws=new WebSocket(page.webSocketDebuggerUrl);await new Promise(r=>{ws.onopen=r;});let id=0;const pending=new Map();
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){pending.get(m.id)?.(m);pending.delete(m.id);}};
const send=(method,params={})=>new Promise((resolve,reject)=>{const n=++id;const t=setTimeout(()=>reject(Error(method)),method === "Page.captureScreenshot" || method === "Page.printToPDF" ? 60000 : 15000);pending.set(n,m=>{clearTimeout(t);m.error?reject(Error(JSON.stringify(m.error))):resolve(m.result);});ws.send(JSON.stringify({id:n,method,params}));});
const ev=async expression=>{const r=await send("Runtime.evaluate",{expression,returnByValue:true});assert(!r.exceptionDetails,JSON.stringify(r.exceptionDetails));return r.result.value;};
const wait=async expression=>{for(let i=0;i<200;i++){if(await ev(expression))return;await pause(100);}throw Error("UI timeout "+expression);};
const shot=async name=>{await pause(400);
  assert(await ev("[...document.querySelectorAll('.communication-progress-scroll')].every(e=>e.scrollWidth<=e.clientWidth+2)"),`${name}: communication result is hidden behind horizontal scrolling`);
  const r=await send("Page.captureScreenshot",{format:"png"});fs.writeFileSync(path.join(output,name),Buffer.from(r.data,"base64"));};
const go=async route=>{await send("Page.navigate",{url:frontend+route});await wait("!!document.querySelector('main h1')");await pause(500);};
const click=async label=>{assert(await ev(`[...document.querySelectorAll('button')].some(b=>b.textContent.trim()===${JSON.stringify(label)})`),label);await ev(`[...document.querySelectorAll('button')].find(b=>b.textContent.trim()===${JSON.stringify(label)}).click()`);await pause(250);};
const layouts = [];
const recordLayout = async (name, printing = false) => {
  const metrics = await ev(`(()=>{
    const visible=e=>!!(e.getClientRects().length && getComputedStyle(e).visibility !== 'hidden');
    const targets=[...document.querySelectorAll('.runtime-workspace,.r4-studio-layout,.runtime-experiment-panel,.runtime-observation-panel,.event-stack,.communication-progress-scroll,.acoustic-plots')].filter(visible);
    return {width:innerWidth,height:innerHeight,scrollWidth:document.documentElement.scrollWidth,
      panels:targets.map(e=>({className:e.className,clientHeight:e.clientHeight,scrollHeight:e.scrollHeight,overflowY:getComputedStyle(e).overflowY,
        clientWidth:e.clientWidth,scrollWidth:e.scrollWidth,overflowX:getComputedStyle(e).overflowX})),
      background:getComputedStyle(document.querySelector('main')).backgroundColor};
  })()`);
  layouts.push({name,printing,...metrics});
  assert(metrics.scrollWidth <= metrics.width + 2, `${name}: horizontal document overflow ${JSON.stringify(metrics)}`);
  if (printing) for (const panel of metrics.panels) {
    assert(!(panel.scrollHeight > panel.clientHeight + 2 && ['hidden','clip','auto','scroll'].includes(panel.overflowY)), `${name}: print clips panel ${JSON.stringify(panel)}`);
  }
};
try{
  await send("Emulation.setDeviceMetricsOverride",{width:1440,height:900,deviceScaleFactor:1,mobile:false});
  await go("/");await shot("01-workbench.png");
  await go("/environments");await shot("02-environments.png");
  await go(`/workspace/environment?source=${environmentId}`);await wait("!!document.querySelector('[aria-label=\"非相干传输损失距离深度图\"]')");
  assert.equal(await ev("[...document.querySelectorAll('[role=tab]')].some(tab=>tab.textContent==='冲激响应')"),false);
  await shot("03-loss-field-shallow.png");
  await ev(`(()=>{const s=document.querySelector('.acoustic-selectors label:first-child select');Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,'value').set.call(s,'1');s.dispatchEvent(new Event('change',{bubbles:true}));})()`);
  await wait("!!document.querySelector('[aria-label=\"非相干传输损失距离深度图\"]') && document.querySelector('.acoustic-plots').textContent.includes('发射深度 60 m')");
  await shot("04-loss-field.png");await click("声线路径");await wait("!!document.querySelector('[aria-label=\"Bellhop 真实声线路径\"]')");
  assert(await ev("document.querySelector('.acoustic-plots').textContent.includes('发射深度 60 m')"));await shot("05-rays.png");
  await click("输入与计算参数");await shot("06-environment-inputs.png");
  await go(`/workspace/experiment?draft=${runs[0].draftId}`);await wait("!!document.querySelector('[aria-label=\"可编辑场景画布\"]')");await shot("07-experiment.png");
  assert.equal(await ev("getComputedStyle(document.querySelector('.water-boundary')).fill"),"rgb(246, 250, 252)");
  await ev(`document.querySelector('[aria-label="选择节点 N1"]').dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}))`);await wait("[...document.querySelectorAll('button')].some(b=>b.textContent==='新增轨迹')");await click("新增轨迹");await wait("!!document.querySelector('dialog[open]')");await shot("08-trajectory-dialog.png");await click("关闭");
  for(const run of runs){
    const mode = run.noisy ? "b-noisy" : run.mode === "Channel2Waveform" ? "b" : "a";
    await go(`/runs/${run.runId}`);await wait("!!document.querySelector('.runtime-workspace')");await shot(`09-runtime-mode-${mode}.png`);
    assert.equal(await ev("!!document.querySelector('.lane-transmission, .lane-channeloutcome')"),false);
    const before = await ev("document.querySelector('.live-metric-strip').textContent");
    await ev("document.querySelector('.timeline-filter-menu').open=true"); await click("接收完成");
    assert.equal(await ev("!!document.querySelector('.lane-reception')"),false);
    assert.equal(await ev("document.querySelector('.live-metric-strip').textContent"),before);
    await click("接收完成");await ev("document.querySelector('.timeline-filter-menu').open=false");
    for(const name of ["开始发送","实际到达","接收完成","融合结果"]){
      if(name==='开始发送')await ev(`document.querySelector('.timeline-filter-menu').open=true`);
      if(name==='开始发送')await click(name);
      await ev("document.querySelector('.timeline-filter-menu').open=false");
      const lane={开始发送:'transmission',实际到达:'signalarrival',接收完成:'reception',融合结果:'fusion'}[name];
      if(await ev(`!!document.querySelector('.lane-${lane} button')`)){await ev(`document.querySelector('.lane-${lane} button').click()`);await shot(`10-mode-${mode}-${lane}.png`);}
    }
    await click("实时跟随");await click("查看结果");await shot(`11-results-mode-${mode}.png`);await click("关闭结果");
    assert.deepEqual(await api(`/runs/${run.runId}/results`),run.result,"UI interactions changed immutable result");
  }
  for(const [width,height] of [[1440,900],[1920,1080],[2553,1264]]) {
    await send("Emulation.setDeviceMetricsOverride",{width,height,deviceScaleFactor:1,mobile:false});
    await go("/");await recordLayout(`workbench-${width}`);await shot(`13-${width}-workbench.png`);
    await go(`/workspace/environment?source=${environmentId}`);await wait("!!document.querySelector('[aria-label=\"非相干传输损失距离深度图\"]')");await recordLayout(`loss-field-${width}`);await shot(`14-${width}-loss-field.png`);
    await go(`/workspace/environment?source=${singleDepthEnvironmentId}`);await wait("!!document.querySelector('[aria-label=\"单深度传输损失距离曲线\"]')");
    await wait("!!document.querySelector('[aria-label=\"单深度传输损失距离曲线\"] circle')");await recordLayout(`single-depth-${width}`);await shot(`15-${width}-single-depth.png`);
    await go(`/runs/${runs[0].runId}`);await wait("!!document.querySelector('.runtime-workspace')");await recordLayout(`runtime-${width}`);await shot(`16-${width}-runtime.png`);
  }
  await send("Emulation.setDeviceMetricsOverride",{width:1440,height:900,deviceScaleFactor:1,mobile:false});
  await send("Emulation.setEmulatedMedia",{media:"print"});await recordLayout("runtime-print",true);await shot("12-runtime-print.png");
  const pdf=await send("Page.printToPDF",{printBackground:true,landscape:true});fs.writeFileSync(path.join(output,"runtime-print.pdf"),Buffer.from(pdf.data,"base64"));
  fs.writeFileSync(path.join(output,"layout-checks.json"),JSON.stringify(layouts,null,2));
  console.log(JSON.stringify({environmentId,runs:runs.map(({runId,draftId,mode})=>({runId,draftId,mode})),output}));
}finally{ws.close();await fetch(debug+"/json/close/"+page.id);}
