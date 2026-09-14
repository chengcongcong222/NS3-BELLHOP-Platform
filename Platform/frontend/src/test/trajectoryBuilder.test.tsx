import {screen,waitFor} from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import {describe,expect,it} from "vitest";
import {installApi,renderRoute} from "./render";

const asset={asset_id:"uuv-demo",version:1,name:"UUV 演示",coordinate_system:"LocalCartesianMetersZUp",source:"uuv-5dof-rk4-v1",source_notice:"演示参数，不代表任意真实 UUV。",checksum_sha256:"abc",tracks:[{track_id:"N1",points:[{time_s:0,x_m:0,y_m:0,z_m:-20},{time_s:10,x_m:8,y_m:2,z_m:-25}]}]};
describe("trajectory builder",()=>{
  it("renders formal catalog, asset views and all production sources",async()=>{
    installApi({"GET /trajectories":{body:[asset]}});renderRoute("/trajectories");
    expect(await screen.findByText("UUV 演示")).toBeTruthy();
    expect(screen.getByLabelText("XY 平面轨迹")).toBeTruthy();
    const select=screen.getByLabelText("来源");
    for(const label of ["静止","匀速直线","航路点","UUV 5-DOF","CSV 导入"])expect(select.textContent).toContain(label);
  });
  it("saves generated output through the backend instead of local fake save",async()=>{
    const fetch=installApi({"GET /trajectories":{body:[]},"POST /authoring/trajectories/generate":{body:asset}});renderRoute("/trajectories");
    await userEvent.click(await screen.findByRole("button",{name:"生成并保存正式 Asset"}));
    await waitFor(()=>expect(fetch).toHaveBeenCalledWith("/authoring/trajectories/generate",expect.objectContaining({method:"POST"})));
    expect(await screen.findByText("t = 0.0 s")).toBeTruthy();
  });
});
