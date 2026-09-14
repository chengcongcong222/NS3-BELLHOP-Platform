from pathlib import Path
import asyncio, json, os, subprocess
import httpx
from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.trajectory_authoring import GenerateRequest, generate
from helpers import resource_catalog

class IdleGateway:
    async def shutdown(self) -> None: pass

def test_generators_are_deterministic_and_use_z_up() -> None:
    bridge=Path(os.environ["PLATFORM_TRAJECTORY_GENERATOR_PATH"])
    request=GenerateRequest(asset_id="uuv-demo",name="UUV demo",kind="Uuv5Dof",duration_s=2,dt_s=.1,controller="Direct",tau_u_n=120,initial_z_m=-12)
    a,b=generate(request,bridge),generate(request,bridge)
    assert a.checksum_sha256==b.checksum_sha256
    assert a.tracks[0].points[0].z_m==-12
    assert a.tracks[0].points[-1].x_m>0
    assert a.source_notice=="演示参数，不代表任意真实 UUV。"
    fixed=generate(GenerateRequest(asset_id="fixed",name="Fixed",kind="Fixed",duration_s=2,dt_s=1,initial_x_m=3))
    assert {p.x_m for p in fixed.tracks[0].points}=={3}
    waypoint=generate(GenerateRequest(asset_id="waypoints",name="Waypoints",kind="Waypoints",duration_s=2,dt_s=1,waypoints=[[2,2,-20]]))
    assert waypoint.tracks[0].points[-1].x_m==2 and waypoint.tracks[0].points[-1].y_m==2

def test_trajectory_api_persists_generated_and_csv_assets(tmp_path: Path) -> None:
    async def run():
        app=create_app(BackendSettings(worker_executable=tmp_path/"worker",environment_repository_root=tmp_path,authoring_workspace_root=tmp_path/"workspace"),gateway=IdleGateway(),resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app),base_url="http://test") as client:
            made=await client.post("/authoring/trajectories/generate",json={"asset_id":"line","name":"Line","kind":"ConstantVelocity","duration_s":10,"dt_s":1,"vx_mps":2})
            assert made.status_code==201 and made.json()["tracks"][0]["points"][-1]["x_m"]==20
            csv="track_id,time_s,x_m,y_m,z_m\nN2,0,0,0,-10\nN2,10,5,0,-10\n"
            imported=await client.post("/authoring/trajectories/import-csv",json={"asset_id":"csv","name":"CSV","content":csv})
            assert imported.status_code==201
            assert [x["asset_id"] for x in (await client.get("/trajectories")).json()]==["csv","line"]
    asyncio.run(run())

def test_backend_uuv_asset_matches_direct_cpp_generator_pointwise(tmp_path: Path) -> None:
    bridge=Path(os.environ["PLATFORM_TRAJECTORY_GENERATOR_PATH"])
    request={"asset_id":"uuv-causal","version":1,"name":"UUV causal","track_id":"N8","kind":"Uuv5Dof","duration_s":2,"dt_s":.1,"initial_x_m":3,"initial_y_m":4,"initial_z_m":-12,"controller":"Direct","tau_u_n":120,"tau_v_n":0,"tau_w_n":0,"tau_q_nm":0,"tau_r_nm":4}
    direct_input={"schema":"ns3-factory-uuv-generation-v1","asset_id":"uuv-causal","version":1,"name":"UUV causal","track_id":"N8","duration_s":2,"dt_s":.1,"initial_ned":{"north_m":3,"east_m":4,"down_m":12},"effective_mass":[120,150,160,80,90],"linear_damping":[18,28,32,14,16],"quadratic_damping":[24,36,42,18,20],"controller":{"type":"Direct","tau":[120,0,0,0,4]}}
    direct=json.loads(subprocess.run([bridge],input=json.dumps(direct_input),text=True,capture_output=True,check=True).stdout)
    async def run():
        app=create_app(BackendSettings(worker_executable=tmp_path/"worker",environment_repository_root=tmp_path,authoring_workspace_root=tmp_path/"workspace",trajectory_generator_executable=bridge),gateway=IdleGateway(),resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app),base_url="http://test") as client:
            response=await client.post("/authoring/trajectories/generate",json=request)
            assert response.status_code==201
            saved=response.json()
            assert (tmp_path/"workspace/published/trajectories/uuv-causal.v1.json").is_file()
            for actual,expected in zip(saved["tracks"][0]["points"],direct["tracks"][0]["points"],strict=True):
                for key in ("time_s","x_m","y_m","z_m","pitch_rad","yaw_rad","u_mps","v_mps","w_mps","q_radps","r_radps"):
                    assert abs(actual[key]-expected[key]) <= 1e-12
            assert saved["tracks"][0]["points"][0]["x_m"]==3
            assert saved["tracks"][0]["points"][0]["y_m"]==4
            assert saved["tracks"][0]["points"][0]["z_m"]==-12
    asyncio.run(run())
