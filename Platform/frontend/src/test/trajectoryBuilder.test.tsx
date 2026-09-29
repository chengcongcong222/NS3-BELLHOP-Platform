import {fireEvent,screen,waitFor} from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import {describe,expect,it} from "vitest";
import {installApi,renderRoute} from "./render";
import {trajectoryPreviewPosition} from "../features/trajectoryBuilder";

const asset={asset_id:"uuv-demo",version:1,name:"UUV 演示",coordinate_system:"LocalCartesianMetersZUp",source:"uuv-5dof-rk4-v1",source_notice:"演示参数，不代表任意真实 UUV。",checksum_sha256:"abc",tracks:[{track_id:"N1",points:[{time_s:0,x_m:0,y_m:0,z_m:-20},{time_s:10,x_m:8,y_m:2,z_m:-25}]}]};
describe("trajectory builder",()=>{
  it("renders formal catalog, asset views and all production sources",async()=>{
    installApi({"GET /trajectories":{body:[asset]}});renderRoute("/trajectories");
    expect((await screen.findAllByText("UUV 演示")).length).toBeGreaterThan(0);
    expect(screen.getByLabelText("XY 平面轨迹")).toBeTruthy();
    const select=screen.getByLabelText("生成方式");
    for(const label of ["静止","匀速直线","航路点","UUV 5-DOF","CSV 导入"])expect(select.textContent).toContain(label);
    expect(screen.getByText("已保存 · 只读")).toBeTruthy();
    expect(screen.getByLabelText("初始深度（m）")).toBeTruthy();
    expect(screen.getByText(/不是仿真频率/)).toBeTruthy();
  });
  it("saves generated output through the backend instead of local fake save",async()=>{
    const fetch=installApi({"GET /trajectories":{body:[]},"POST /authoring/trajectories/generate":{body:asset}});renderRoute("/trajectories");
    await userEvent.click(await screen.findByRole("button",{name:"生成并保存"}));
    await waitFor(()=>expect(fetch).toHaveBeenCalledWith("/authoring/trajectories/generate",expect.objectContaining({method:"POST"})));
    expect(await screen.findByText("t = 0 s")).toBeTruthy();
    expect(screen.getByText(/已保存“UUV 演示”/)).toBeTruthy();
  });
  it("submits explicit initial position and downward speed as z-up coordinates",async()=>{
    const fetch=installApi({"GET /trajectories":{body:[]},"POST /authoring/trajectories/generate":{body:asset}});renderRoute("/trajectories");
    fireEvent.change(await screen.findByLabelText("初始X（m）"),{target:{value:"125"}});
    fireEvent.change(screen.getByLabelText("初始Y（m）"),{target:{value:"-42"}});
    fireEvent.change(screen.getByLabelText("初始深度（m）"),{target:{value:"55"}});
    fireEvent.change(screen.getByLabelText("速度下潜（m/s）"),{target:{value:"0.2"}});
    await userEvent.click(screen.getByRole("button",{name:"生成并保存"}));
    await waitFor(()=>expect(fetch.mock.calls.find(([url])=>url==="/authoring/trajectories/generate")).toBeTruthy());
    const call=fetch.mock.calls.find(([url])=>url==="/authoring/trajectories/generate")!;
    expect(JSON.parse(call[1]!.body as string)).toMatchObject({initial_x_m:125,initial_y_m:-42,initial_z_m:-55,vz_mps:-.2,dt_s:1,duration_s:60});
  });
  it("rejects inconsistent time sampling before making an authoring request",async()=>{
    const fetch=installApi({"GET /trajectories":{body:[]}});renderRoute("/trajectories");
    fireEvent.change(await screen.findByLabelText("总时长（s）"),{target:{value:"11"}});
    fireEvent.change(screen.getByLabelText("采样间隔（s）"),{target:{value:"3"}});
    expect(screen.getByText(/总时长必须是采样间隔的整数倍/)).toBeTruthy();
    expect(screen.getByRole("button",{name:"生成并保存"}).hasAttribute("disabled")).toBe(true);
    expect(fetch.mock.calls.some(([,init])=>init?.method==="POST")).toBe(false);
  });
  it("previews saved timestamps by interpolation, independently of the generation interval",async()=>{
    installApi({"GET /trajectories":{body:[asset]}});renderRoute("/trajectories");
    await screen.findByLabelText("轨迹时间");
    fireEvent.change(screen.getByLabelText("采样间隔（s）"),{target:{value:"4"}});
    fireEvent.change(screen.getByLabelText("轨迹时间"),{target:{value:"5"}});
    expect(screen.getByText("t = 5 s")).toBeTruthy();
    expect(screen.getByText("4 m")).toBeTruthy();
    expect(screen.getByText("22.5 m")).toBeTruthy();
    expect(screen.getByLabelText("轨迹时间").getAttribute("step")).toBe("any");
  });
  it("interpolates exact stored data but does not extend coverage",()=>{
    expect(trajectoryPreviewPosition(asset.tracks[0].points,5)).toEqual({time_s:5,x_m:4,y_m:1,z_m:-22.5});
    expect(trajectoryPreviewPosition(asset.tracks[0].points,10)).toEqual(asset.tracks[0].points[1]);
    expect(trajectoryPreviewPosition(asset.tracks[0].points,11)).toBeNull();
    expect(trajectoryPreviewPosition(asset.tracks[0].points,-1)).toBeNull();
  });
  it("uses the saved asset start time rather than pretending imported tracks start at zero",async()=>{
    const imported={...asset,tracks:[{track_id:"survey",points:[{time_s:30,x_m:5,y_m:6,z_m:-10},{time_s:60,x_m:8,y_m:6,z_m:-10}]}]};
    installApi({"GET /trajectories":{body:[imported]}});renderRoute("/trajectories");
    expect(await screen.findByText("t = 30 s")).toBeTruthy();
    expect(screen.getByLabelText("轨迹时间").getAttribute("min")).toBe("30");
  });
  it("imports entered CSV through the real authoring boundary without implicit start/time edits",async()=>{
    const fetch=installApi({"GET /trajectories":{body:[]},"POST /authoring/trajectories/import-csv":{body:asset}});renderRoute("/trajectories");
    await userEvent.selectOptions(await screen.findByLabelText("生成方式"),"Csv");
    const content="track_id,time_s,x_m,y_m,z_m\nN1,0,4,5,-6\nN1,20,8,5,-6\n";
    fireEvent.change(screen.getByLabelText("CSV 数据"),{target:{value:content}});
    await userEvent.click(screen.getByRole("button",{name:"导入并保存"}));
    await waitFor(()=>expect(fetch.mock.calls.find(([url])=>url==="/authoring/trajectories/import-csv")).toBeTruthy());
    expect(JSON.parse(fetch.mock.calls.find(([url])=>url==="/authoring/trajectories/import-csv")![1]!.body as string).content).toBe(content);
    expect(fetch.mock.calls.some(([url])=>url==="/authoring/trajectories/generate")).toBe(false);
  });
  it("preserves the C++ UUV generation request, with a clearly offline integration step",async()=>{
    const fetch=installApi({"GET /trajectories":{body:[]},"POST /authoring/trajectories/generate":{body:asset}});renderRoute("/trajectories");
    await userEvent.selectOptions(await screen.findByLabelText("生成方式"),"Uuv5Dof");
    await userEvent.selectOptions(screen.getByLabelText("控制方式"),"Direct");
    expect(screen.getByLabelText("积分 / 采样间隔（s）")).toBeTruthy();
    await userEvent.click(screen.getByRole("button",{name:"生成并保存"}));
    await waitFor(()=>expect(fetch.mock.calls.find(([url])=>url==="/authoring/trajectories/generate")).toBeTruthy());
    expect(JSON.parse(fetch.mock.calls.find(([url])=>url==="/authoring/trajectories/generate")![1]!.body as string)).toMatchObject({kind:"Uuv5Dof",controller:"Direct",dt_s:.1,tau_u_n:100,tau_r_nm:8});
  });
  it("labels formally translated resources without exposing implementation jargon",async()=>{
    installApi({"GET /trajectories":{body:[{...asset,source:'spatial-translation-v1;{"source_asset_id":"original","version":2}'}]}});
    renderRoute("/trajectories");
    expect((await screen.findAllByText(/起点平移派生/)).length).toBeGreaterThan(0);
    expect(document.body.textContent).not.toContain("spatial-translation-v1");
  });
});
