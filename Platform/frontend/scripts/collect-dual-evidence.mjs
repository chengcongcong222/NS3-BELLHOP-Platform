// Explicit local evidence packaging; does not run/activate services or edit data.
import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import {execFileSync} from "node:child_process";
const destination="Platform/docs/acceptance/dual-workbench";
const root="/tmp/dual-proxy-browser.k81ET9";
const sha=b=>crypto.createHash("sha256").update(b).digest("hex");
const output=(name,value)=>fs.writeFileSync(path.join(destination,name),JSON.stringify(value,null,2)+"\n");
fs.mkdirSync(`${destination}/evidence-final`,{recursive:true});
fs.mkdirSync(`${destination}/screenshots`,{recursive:true});
const suites=["legacy-final","paired-bearing","paired-background","paired-fault","paired-coverage",
  "proxy-release","proxy-pool-two","archive-final"];
const records=[];
for(const suite of suites){
 const dir=path.join(root,suite);
 const file=fs.readdirSync(dir).find(f=>f.endsWith("evidence.json"));
 const record=JSON.parse(fs.readFileSync(path.join(dir,file)));
 if(!record.success)throw Error(`Cannot package failed suite as final: ${suite}`);
 fs.copyFileSync(path.join(dir,file),`${destination}/evidence-final/${suite}.json`);
 records.push({suite,record});
 for(const screenshot of fs.readdirSync(dir).filter(f=>f.endsWith(".png")))
  fs.copyFileSync(path.join(dir,screenshot),`${destination}/screenshots/${suite}-${screenshot}`);
}
const cases=fs.readdirSync("/tmp/dual-final-proxy-evidence").filter(f=>f.endsWith(".json")).map(f=>
 JSON.parse(fs.readFileSync(path.join("/tmp/dual-final-proxy-evidence",f))));
output("run_group_audit.json",{executed_by:"real browser / real Worker; explicit process fault wrapper only in named fault/background suites",
 queue_measurements:fs.readdirSync("/tmp/dual-final-pair-evidence").filter(f=>f.endsWith(".json")).map(f=>
  JSON.parse(fs.readFileSync(`/tmp/dual-final-pair-evidence/${f}`))),
 groups:records.filter(r=>r.record.groups).map(r=>({suite:r.suite,groups:r.record.groups}))});
output("recorded_proxy_run_audit.json",{cases,browser_evidence:records.filter(r=>r.suite.startsWith("proxy")).map(r=>({
 suite:r.suite,checks:r.record.checks,group_id:r.record.group?.group_id}))});
const logs=["dual-release-on.log","dual-release-off.log","dual-final-runtime.log","dual-final-ui-release.log",
 "dual-legacy-trace-retry.log","dual-official-replay-final.log","dual-official-noise.log"];
let combined="Actual output; different test scopes are not summed as independent experiments.\n";
for(const log of logs){
 const data=fs.readFileSync(`/tmp/${log}`);
 fs.writeFileSync(`${destination}/evidence-final/${log}`,data);
 combined+=`\n=== ${log} ===\n${data}`;
}
fs.writeFileSync(`${destination}/regression.log`,combined);
for(const mode of ["on","off"])fs.copyFileSync(`/tmp/dual-workbench-build-${mode}/Testing/Temporary/LastTest.log`,
 `${destination}/evidence-final/ctest-${mode}-detail.log`);
// Retain negative results, clearly separate from final pass records.
for(const log of ["dual-official-replay.log","dual-legacy-trace.log","dual-final-ui-2.log"])
 fs.copyFileSync(`/tmp/${log}`,`${destination}/evidence-final/negative-${log}`);
for(const suite of ["mixed-four-check","paired-geometry"]){
 const file=fs.readdirSync(path.join(root,suite)).find(f=>f.endsWith("evidence.json"));
 if(file)fs.copyFileSync(path.join(root,suite,file),`${destination}/evidence-final/negative-${suite}.json`);
}
const files=execFileSync("git",["ls-files","-co","--exclude-standard","Platform"],{encoding:"utf8"}).trim().split("\n")
 .filter(f=>!f.startsWith(destination+"/") && fs.existsSync(f) && fs.statSync(f).isFile()).sort();
const manifest=files.map(f=>({path:f,sha256:sha(fs.readFileSync(f))}));
const binaries=[
 "/tmp/dual-workbench-build-on/worker/platform_sim_worker",
 "/tmp/dual-workbench-build-on/worker/platform_resource_catalog_adapter",
 "/tmp/dual-workbench-build-on/adapters/platform_recorded_replay_probe"];
const assets=[`${root}/workspace/recorded-channels/uwa-blue-1-element-0/1`,
 `${root}/workspace/recorded-channels/uwa-blue-1-element-1/1`,
 "/tmp/channel3-test-noise/blue-noise-seed23/1"].map(p=>{
 const b=fs.readFileSync(`${p}/manifest.json`);
 return {path:p,manifest_sha256:sha(b),manifest:JSON.parse(b)};
});
const main=await(await fetch("http://127.0.0.1:8012/system/info")).json();
const test=await(await fetch("http://127.0.0.1:8097/system/info")).json();
output("evidence-final/tested-identity.json",{
 parent_commit:execFileSync("git",["rev-parse","HEAD"],{encoding:"utf8"}).trim(),
 branch:execFileSync("git",["branch","--show-current"],{encoding:"utf8"}).trim(),
 source_hash_scope:"git tracked and unignored Platform files excluding this acceptance evidence directory",
 source_tree_sha256:sha(JSON.stringify(manifest)),source_files:manifest,
 binaries:binaries.map(p=>({path:p,sha256:sha(fs.readFileSync(p))})),assets,
 main_service_unchanged:main,test_service:test,
 frontend_build:fs.readdirSync("Platform/frontend/dist/assets").map(f=>({
 file:f,sha256:sha(fs.readFileSync(`Platform/frontend/dist/assets/${f}`))})),
});
console.log(`Packaged ${records.length} real browser suites and ${cases.length} real proxy cases.`);
