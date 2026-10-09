// Read-only real-service verification: no mocked API, no new runs, no backend restart.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
const [frontend, backend, debug, output] = process.argv.slice(2);
const api = async route => { const r = await fetch(backend + route); assert(r.ok, `${route}: ${r.status}`); return r.json(); };
const runs = await api('/runs');
const run = runs.findLast(r => r.lifecycle === 'Completed' && r.result_available);
assert(run, 'An existing completed run is required.');
const before = await api(`/runs/${run.run_id}/results`);
const pause = ms => new Promise(r => setTimeout(r, ms));
const page = await (await fetch(debug + '/json/new?about:blank', {method:'PUT'})).json();
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(r => { ws.onopen = r; });
let id=0; const pending=new Map();
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){pending.get(m.id)?.(m);pending.delete(m.id);}};
const send=(method,params={})=>new Promise((resolve,reject)=>{const n=++id;const t=setTimeout(()=>reject(Error(method)),30000);pending.set(n,m=>{clearTimeout(t);m.error?reject(Error(JSON.stringify(m.error))):resolve(m.result);});ws.send(JSON.stringify({id:n,method,params}));});
const ev=async expression=>{const r=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});assert(!r.exceptionDetails,JSON.stringify(r.exceptionDetails));return r.result.value;};
const wait=async expression=>{for(let n=0;n<150;n++){if(await ev(expression))return;await pause(100);}throw Error('UI timeout: '+expression);};
const go=async route=>{await send('Page.navigate',{url:frontend+route});await pause(500);};
const shot=async name=>{await pause(200);const r=await send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,name),Buffer.from(r.data,'base64'));};
const checks=[];
fs.mkdirSync(output,{recursive:true});
try {
  for(const [width,height] of [[1440,900],[1920,1080],[2553,1264],[1000,800]]) {
    await send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
    await go(`/runs/${run.run_id}`); await wait("!!document.querySelector('.runtime-log-panel .event-stack button')");
    await ev("[...document.querySelectorAll('button')].find(e=>e.textContent==='完整回放').click()"); await pause(150);
    const dimensions=()=>ev(`(()=>{const rect=s=>{const r=document.querySelector(s).getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right,bottom:r.bottom}};return {viewport:innerWidth,scrollWidth:document.documentElement.scrollWidth,canvas:rect('.run-topology'),timeline:rect('.studio-history'),observation:rect('.runtime-observation-panel'),log:rect('.runtime-log-panel'),slot:rect('.cooperation-status')}})()`);
    const initial=await dimensions();
    await ev("document.querySelector('.lane-signalarrival button')?.click()"); await pause(150);
    const arrival=await dimensions();
    assert(Math.abs(initial.canvas.height-arrival.canvas.height)<1, 'Canvas jumps on arrival');
    assert(Math.abs(initial.slot.height-arrival.slot.height)<1, 'Event slot changed height');
    assert(arrival.scrollWidth<=width+2, 'Horizontal document overflow');
    if(width>=1200) {
      assert(arrival.timeline.right<arrival.observation.x, 'Timeline takes log width');
      assert(Math.abs(arrival.observation.bottom-arrival.timeline.bottom)<2, 'Log column does not extend to bottom');
      assert(arrival.log.height>=180, 'Log panel too short');
    }
    checks.push({width,height,initial,arrival});await shot(`runtime-${width}.png`);
    if(width===1440) {
      await ev("[...document.querySelectorAll('button')].find(e=>e.textContent==='实时跟随').click()");
      await wait("document.querySelectorAll('.runtime-log-panel .event-stack button').length>1");
      await shot('runtime-completed-1440.png');
    }
  }
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
  await send('Emulation.setEmulatedMedia',{media:'print'});await shot('runtime-print.png');await send('Emulation.setEmulatedMedia',{media:'screen'});
  await go('/');await wait("!!document.querySelector('.wb-panel')");await shot('workbench.png');
  await go('/workspace/environment');await wait("!!document.querySelector('.build-step-rail')");
  await ev("[...document.querySelectorAll('.build-step-rail button')].find(b=>b.textContent.includes('Bellhop')).click()");
  await wait("!!document.querySelector('.depth-sampling')");await shot('environment-sampling.png');
  await go(`/workspace/environment?source=${encodeURIComponent(run.environment_asset_id)}`);
  await wait("!!document.querySelector('.acoustic-selectors')");
  assert.equal(await ev("!!document.querySelector('.acoustic-plots [role=tab]')"),false);
  await shot('environment-historical.png');
  const environments=await api('/environments');
  let rayEnvironment;
  for(const environment of environments) {
    const id=environment.environment_asset_id;
    const plots=await api(`/environments/${encodeURIComponent(id)}/acoustic-plots?source_index=0&receiver_index=0&range_index=0`);
    if(plots.available && plots.ray_plot?.available) { rayEnvironment=id;break; }
  }
  assert(rayEnvironment,'Need an existing real ray artifact');
  await go(`/workspace/environment?source=${encodeURIComponent(rayEnvironment)}`);
  await wait("!!document.querySelector('[aria-label=\"Bellhop 真实声线路径\"]')");await shot('environment-rays.png');
  assert.deepEqual(await api(`/runs/${run.run_id}/results`),before, 'Read-only interaction changed results');
  fs.writeFileSync(path.join(output,'layout-checks.json'),JSON.stringify({runId:run.run_id,environment:run.environment_asset_id,rayEnvironment,checks},null,2));
  console.log(JSON.stringify({runId:run.run_id,checks:checks.length,output}));
} finally {ws.close();await fetch(debug+'/json/close/'+page.id);}
