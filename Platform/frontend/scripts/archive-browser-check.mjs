// Run after a normal restart of the SAME temporary backend.
import fs from "node:fs";
const root=process.argv[2], ids=process.argv.slice(3), api="http://127.0.0.1:8097", base="http://127.0.0.1:4197";
const target=(await(await fetch("http://127.0.0.1:9227/json/list")).json()).find(t=>t.type==="page");
const ws=new WebSocket(target.webSocketDebuggerUrl);
await new Promise(r=>ws.addEventListener("open",r,{once:true}));
let seq=0;const pending=new Map(),exceptions=[],evidence={executed_by:"Chromium CDP after temporary backend restart",checks:[],exceptions};
ws.addEventListener("message",e=>{const m=JSON.parse(e.data);if(m.method==="Runtime.exceptionThrown")exceptions.push(m.params);if(pending.has(m.id)){const [ok,no]=pending.get(m.id);pending.delete(m.id);m.error?no(Error(JSON.stringify(m.error))):ok(m.result)}});
const send=(method,params={})=>new Promise((ok,no)=>{const id=++seq;pending.set(id,[ok,no]);ws.send(JSON.stringify({id,method,params}))});
const ev=async expression=>{const r=await send("Runtime.evaluate",{expression,returnByValue:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value};
try{
 await send("Page.enable");await send("Runtime.enable");exceptions.length=0;
 await send("Emulation.setDeviceMetricsOverride",{width:1440,height:900,deviceScaleFactor:1,mobile:false});
 for(const id of ids){
  const saved=await(await fetch(`${api}/run-groups/${id}/export`)).json();
  if(!saved.archived||saved.events_available||!saved.members.some(m=>m.result))throw Error("Invalid restored archive");
  await send("Page.navigate",{url:`${base}/run-groups/${id}`});
  let rendered=false;
  for(let i=0;i<100;i++){if(await ev("document.body.innerText.includes('历史结果已归档')||document.body.innerText.includes('结果与配置已归档')")){rendered=true;break}await new Promise(r=>setTimeout(r,100))}
  if(!rendered)throw Error("Restored page did not explain unavailable event history");
  const href=await ev("[...document.querySelectorAll('a')].find(a=>a.textContent.includes('下载')&&a.href.includes('/export'))?.href");
  if(!href)throw Error("No archive download link");
  const downloaded=await(await fetch(href)).json();
  if(JSON.stringify(saved)!==JSON.stringify(downloaded))throw Error("UI export differed");
  if(saved.members.length===1) {
    await ev(`(()=>{for(const [label,value] of [['筛选发送节点','0'],['筛选接收节点','2']]){const e=document.querySelector('[aria-label="'+label+'"]');if(!e)throw Error('Missing result filter');Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,'value').set.call(e,value);e.dispatchEvent(new Event('change',{bubbles:true}))}})()`);
    await new Promise(r=>setTimeout(r,100));
    const links=await ev("[...document.querySelectorAll('.proxy-result-evidence > .analysis-scroll tbody tr')].map(r=>r.cells[1].textContent)");
    if(!links.length||links.some(l=>l!=='N0 → N2'))throw Error("Archived directed link filter mismatch");
    evidence.checks.push({action:"archived Tx+Rx directed-link filter",group_id:id,links});
  }
  fs.writeFileSync(`${root}/${id}.png`,Buffer.from((await send("Page.captureScreenshot",{format:"png"})).data,"base64"));
  evidence.checks.push({action:"open restored archive and follow real export link",group_id:id,record:downloaded});
 }
 if(exceptions.length)throw Error("Browser exception");
 evidence.success=true;
}catch(e){evidence.success=false;evidence.failure=String(e);process.exitCode=1}
finally{fs.writeFileSync(`${root}/browser-evidence.json`,JSON.stringify(evidence,null,2));console.log(JSON.stringify({success:evidence.success,failure:evidence.failure,groups:ids,exceptions}));ws.close()}
