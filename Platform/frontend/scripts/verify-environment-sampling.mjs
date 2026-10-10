// Read-only browser check against real services. Never saves or runs a user's draft.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
const [frontend, backend, debug, draftId, output, buildId] = process.argv.slice(2);
const drafts = async () => { const response=await fetch(backend+'/authoring/drafts?kind=environment');assert(response.ok);return response.json(); };
const original=(await drafts()).find(d=>d.draft_id===draftId);assert(original);
const page=await(await fetch(debug+'/json/new?about:blank',{method:'PUT'})).json();
const ws=new WebSocket(page.webSocketDebuggerUrl);await new Promise(r=>ws.onopen=r);
let id=0;const pending=new Map();
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){pending.get(m.id)?.(m);pending.delete(m.id);}};
const send=(method,params={})=>new Promise((resolve,reject)=>{const n=++id;const timer=setTimeout(()=>reject(Error(method)),15000);pending.set(n,m=>{clearTimeout(timer);m.error?reject(Error(JSON.stringify(m.error))):resolve(m.result);});ws.send(JSON.stringify({id:n,method,params}));});
const ev=async expression=>{const r=await send('Runtime.evaluate',{expression,returnByValue:true});assert(!r.exceptionDetails,JSON.stringify(r.exceptionDetails));return r.result.value;};
const pause=ms=>new Promise(r=>setTimeout(r,ms));
const wait=async expr=>{for(let i=0;i<100;i++){if(await ev(expr))return;await pause(100);}throw Error(expr);};
const click=label=>ev(`[...document.querySelectorAll('button')].find(b=>b.textContent.trim()===${JSON.stringify(label)}).click()`);
const input=async(label,value)=>{await ev(`(()=>{const el=document.querySelector('[aria-label=${JSON.stringify(label)}]');Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(el,${JSON.stringify(value)});el.dispatchEvent(new Event('input',{bubbles:true}));})()`);await pause(100);};
const shot=async name=>{await pause(200);const r=await send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,name),Buffer.from(r.data,'base64'));};
try {
  fs.mkdirSync(output,{recursive:true});
  await send('Emulation.setDeviceMetricsOverride',{width:1440,height:900,deviceScaleFactor:1,mobile:false});
  await send('Page.navigate',{url:frontend+'/workspace/environment?draft='+encodeURIComponent(draftId)});
  await wait(`document.querySelector('[aria-label="计算规模检查"]')?.textContent.includes('12,288,000')`);
  await ev("[...document.querySelectorAll('.build-step-rail button')].find(b=>b.textContent.includes('配置与预览')).click()");
  assert.equal(await ev("document.querySelectorAll('.build-step-rail button').length"),4);
  assert.equal(await ev("[...document.querySelectorAll('.build-step-rail button')].some(b=>b.textContent.includes('数据预览'))"),false);
  await ev("document.querySelector('.depth-sampling details').open=true");
  assert(await ev("document.querySelector('.sampling-custom-toggle input').getBoundingClientRect().width<=18"));
  assert(await ev("document.querySelector('.environment-preview svg').getBoundingClientRect().height<=280"));
  assert(await ev("[...document.querySelectorAll('button')].find(b=>b.textContent==='开始离线声场构建').disabled"));
  assert(await ev("document.querySelector('.sampling-budget').textContent.includes('2～1000')"));
  await shot('test5-preflight.png');
  await click('距离点数改为 48');await pause(100);
  assert.equal(await ev("[...document.querySelectorAll('button')].find(b=>b.textContent==='开始离线声场构建').disabled"),false,await ev("document.body.innerText"));
  await click('改为全水深均分');await click('改为全水深均分');
  await input('发送深度采样点数','5');await input('接收深度采样点数','5');
  assert(await ev("document.querySelector('.depth-sampling').textContent.includes('1～174 m')"));
  assert.equal(await ev("!!document.querySelector('[aria-label=\"发送深度起点（m）\"]')"),false);
  await shot('automatic-depth-preview.png');
  await input('发送深度采样点数','');
  assert(await ev("[...document.querySelectorAll('button')].find(b=>b.textContent==='开始离线声场构建').disabled"));
  assert(await ev("document.querySelector('.depth-sampling [role=alert]').textContent.includes('请填写')"));
  assert.deepEqual((await drafts()).find(d=>d.draft_id===draftId),original);
  if(buildId){
    const before=await(await fetch(backend+'/authoring/environment-builds/'+buildId)).json();
    await send('Page.navigate',{url:frontend+'/workspace/environment?build='+encodeURIComponent(buildId)});
    await wait(`!!document.querySelector('svg[aria-label="Bellhop 真实声线路径"]')`);
    assert.equal(await ev("document.querySelectorAll('.acoustic-selectors select')[0].options.length"),before.artifact.input_summary.source_depths_meters.length);
    assert.equal(await ev("document.querySelector('svg[aria-label=\"Bellhop 真实声线路径\"]').querySelectorAll('polyline').length"),31);
    await shot('recovered-ray-shallow.png');
    await ev("(()=>{const select=document.querySelector('.acoustic-selectors select');select.value=String(select.options.length-1);select.dispatchEvent(new Event('change',{bubbles:true}));})()");
    await wait(`document.querySelector('.acoustic-plots')?.textContent.includes('发射深度 174 m ·')`);
    assert.equal(await ev("document.querySelector('svg[aria-label=\"Bellhop 真实声线路径\"]').querySelectorAll('polyline').length"),31);
    await shot('recovered-ray-deep.png');
    const after=await(await fetch(backend+'/authoring/environment-builds/'+buildId)).json();
    assert.deepEqual(after,before);
  }
  console.log(JSON.stringify({draftId,invalidBuildBlocked:true,explicitBudgetAdjustment:true,automaticDepthRange:[1,174],emptyCountBlocked:true,originalUnchanged:true,recoveredBuild:buildId,output}));
} finally {ws.close();await fetch(debug+'/json/close/'+page.id);}
