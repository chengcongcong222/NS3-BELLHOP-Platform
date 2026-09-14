"""Deterministic offline trajectory production and immutable asset persistence.

Runtime never evaluates these source models.  Every operation materializes the
same local-Cartesian, z-up trajectory asset consumed by the trajectory runtime.
"""
from __future__ import annotations

import csv, hashlib, io, json, math, re, tempfile
from pathlib import Path
from typing import Literal
from pydantic import BaseModel, ConfigDict, Field, model_validator

class Model(BaseModel):
    model_config = ConfigDict(extra="forbid")

class Point(Model):
    time_s: float = Field(ge=0)
    x_m: float
    y_m: float
    z_m: float
    pitch_rad: float | None = None
    yaw_rad: float | None = None
    u_mps: float | None = None
    v_mps: float | None = None
    w_mps: float | None = None
    q_radps: float | None = None
    r_radps: float | None = None

class Track(Model):
    track_id: str = Field(pattern=r"^[A-Za-z0-9_.-]{1,128}$")
    points: list[Point] = Field(min_length=1)
    @model_validator(mode="after")
    def ordered(self):
        if any(b.time_s <= a.time_s for a,b in zip(self.points,self.points[1:])):
            raise ValueError("trajectory times must be strictly increasing")
        values=(v for p in self.points for v in p.model_dump().values() if isinstance(v,float))
        if not all(math.isfinite(v) for v in values): raise ValueError("trajectory values must be finite")
        return self

class Asset(Model):
    asset_id: str = Field(pattern=r"^[A-Za-z0-9_.-]{1,128}$")
    version: int = Field(ge=1)
    name: str = Field(min_length=1,max_length=160)
    coordinate_system: Literal["LocalCartesianMetersZUp"] = "LocalCartesianMetersZUp"
    source: str
    source_notice: str | None = None
    tracks: list[Track] = Field(min_length=1)
    checksum_sha256: str = ""

class GenerateRequest(Model):
    asset_id: str = Field(pattern=r"^[A-Za-z0-9_.-]{1,128}$")
    version: int = Field(default=1,ge=1)
    name: str = Field(min_length=1,max_length=160)
    track_id: str = Field(default="N1",pattern=r"^[A-Za-z0-9_.-]{1,128}$")
    kind: Literal["Fixed","ConstantVelocity","Waypoints","Uuv5Dof"]
    duration_s: float = Field(default=60,gt=0,le=86400)
    dt_s: float = Field(default=1,gt=0,le=60)
    initial_x_m: float = 0; initial_y_m: float = 0; initial_z_m: float = -20
    vx_mps: float = 0; vy_mps: float = 0; vz_mps: float = 0
    waypoints: list[list[float]] = Field(default_factory=list)
    controller: Literal["Direct","WaypointPid"] = "WaypointPid"
    target_speed_mps: float = Field(default=1,ge=0,le=20)
    tau_u_n: float = 0; tau_v_n: float = 0; tau_w_n: float = 0
    tau_q_nm: float = 0; tau_r_nm: float = 0
    effective_mass: list[float] = Field(default_factory=lambda:[120,150,160,80,90],min_length=5,max_length=5)
    linear_damping: list[float] = Field(default_factory=lambda:[18,28,32,14,16],min_length=5,max_length=5)
    quadratic_damping: list[float] = Field(default_factory=lambda:[24,36,42,18,20],min_length=5,max_length=5)

class CsvRequest(Model):
    asset_id: str = Field(pattern=r"^[A-Za-z0-9_.-]{1,128}$")
    version: int = Field(default=1,ge=1)
    name: str = Field(min_length=1,max_length=160)
    content: str = Field(min_length=1,max_length=16*1024*1024)

def _finalize(asset: Asset) -> Asset:
    canonical=asset.model_dump(exclude={"checksum_sha256"})
    digest=hashlib.sha256(json.dumps(canonical,ensure_ascii=False,sort_keys=True,separators=(",",":")).encode()).hexdigest()
    return asset.model_copy(update={"checksum_sha256":digest})

def _times(duration: float,dt: float)->list[float]:
    steps=round(duration/dt)
    if not math.isclose(steps*dt,duration,rel_tol=0,abs_tol=1e-9): raise ValueError("duration must be an exact multiple of fixed dt")
    return [i*dt for i in range(steps+1)]

def _waypoint_position(req:GenerateRequest,t:float)->tuple[float,float,float]:
    points=[[req.initial_x_m,req.initial_y_m,req.initial_z_m],*req.waypoints]
    if len(points)==1:return tuple(points[0])
    lengths=[math.dist(a,b) for a,b in zip(points,points[1:])]; total=sum(lengths)
    distance=min(total,(t/req.duration_s)*total)
    for index,(a,b,length) in enumerate(zip(points,points[1:],lengths)):
        if distance<=length or index==len(lengths)-1:
            f=0 if length==0 else distance/length
            return tuple(a[i]+f*(b[i]-a[i]) for i in range(3))
        distance-=length
    return tuple(points[-1])

def _uuv(req:GenerateRequest)->list[Point]:
    # State is NED [north,east,down,pitch,yaw,u,v,w,q,r]. Fixed-step RK4.
    s=[req.initial_x_m,req.initial_y_m,-req.initial_z_m,0,0,0,0,0,0,0]
    mass=req.effective_mass; lin=req.linear_damping; quad=req.quadratic_damping
    if any(not math.isfinite(v) or v<=0 for v in mass) or any(not math.isfinite(v) or v<0 for v in [*lin,*quad]):
        raise ValueError("UUV mass must be positive and damping nonnegative finite values")
    targets=[(float(p[0]),float(p[1]),-float(p[2])) for p in req.waypoints if len(p)==3]
    def command(x):
        if req.controller=="Direct" or not targets:return [req.tau_u_n,req.tau_v_n,req.tau_w_n,req.tau_q_nm,req.tau_r_nm]
        target=targets[-1]; desired=math.atan2(target[1]-x[1],target[0]-x[0]); error=math.remainder(desired-x[4],2*math.pi)
        return [80*(req.target_speed_mps-x[5]),0,100*(target[2]-x[2])-45*x[7],-35*x[3],90*error-24*x[9]]
    def derivative(x):
        ct,st,cp,sp=math.cos(x[3]),math.sin(x[3]),math.cos(x[4]),math.sin(x[4]); tau=command(x); vel=x[5:]
        kin=[cp*ct*x[5]-sp*x[6]+cp*st*x[7],sp*ct*x[5]+cp*x[6]+sp*st*x[7],-st*x[5]+ct*x[7],x[8],x[9]/ct]
        cor=[mass[2]*x[7]*x[8]-mass[1]*x[6]*x[9],mass[0]*x[5]*x[9],-mass[0]*x[5]*x[8],0,0]
        acc=[(tau[i]-cor[i]-lin[i]*vel[i]-quad[i]*abs(vel[i])*vel[i])/mass[i] for i in range(5)]
        return kin+acc
    result=[]
    for t in _times(req.duration_s,req.dt_s):
        result.append(Point(time_s=t,x_m=s[0],y_m=s[1],z_m=-s[2],pitch_rad=s[3],yaw_rad=s[4],u_mps=s[5],v_mps=s[6],w_mps=s[7],q_radps=s[8],r_radps=s[9]))
        if t==req.duration_s:break
        add=lambda a,b,k:[x+k*y for x,y in zip(a,b)]
        k1=derivative(s);k2=derivative(add(s,k1,req.dt_s/2));k3=derivative(add(s,k2,req.dt_s/2));k4=derivative(add(s,k3,req.dt_s))
        s=[x+req.dt_s*(a+2*b+2*c+d)/6 for x,a,b,c,d in zip(s,k1,k2,k3,k4)]
    return result

def generate(req:GenerateRequest)->Asset:
    times=_times(req.duration_s,req.dt_s)
    if req.kind=="Uuv5Dof": points=_uuv(req); source="uuv-5dof-rk4-v1"
    elif req.kind=="Waypoints": points=[Point(time_s=t,x_m=(p:=_waypoint_position(req,t))[0],y_m=p[1],z_m=p[2]) for t in times]; source="waypoint-linear-generator-v1"
    else:
        scale=0 if req.kind=="Fixed" else 1
        points=[Point(time_s=t,x_m=req.initial_x_m+scale*req.vx_mps*t,y_m=req.initial_y_m+scale*req.vy_mps*t,z_m=req.initial_z_m+scale*req.vz_mps*t) for t in times]
        source="fixed-generator-v1" if req.kind=="Fixed" else "constant-velocity-generator-v1"
    notice="演示参数，不代表任意真实 UUV。" if req.kind=="Uuv5Dof" else None
    return _finalize(Asset(asset_id=req.asset_id,version=req.version,name=req.name,source=source,source_notice=notice,tracks=[Track(track_id=req.track_id,points=points)]))

def import_csv(req:CsvRequest)->Asset:
    rows=list(csv.DictReader(io.StringIO(req.content)))
    if not rows or set(rows[0])!={"track_id","time_s","x_m","y_m","z_m"}: raise ValueError("CSV header must be track_id,time_s,x_m,y_m,z_m")
    grouped={}
    for row in rows:
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}",row["track_id"]):raise ValueError("invalid track_id")
        values=[float(row[k]) for k in ("time_s","x_m","y_m","z_m")]
        if not all(math.isfinite(v) for v in values) or values[0]<0:raise ValueError("CSV values must be finite and time nonnegative")
        grouped.setdefault(row["track_id"],[]).append(Point(time_s=values[0],x_m=values[1],y_m=values[2],z_m=values[3]))
    tracks=[Track(track_id=k,points=sorted(v,key=lambda p:p.time_s)) for k,v in sorted(grouped.items())]
    starts={t.points[0].time_s for t in tracks};ends={t.points[-1].time_s for t in tracks}
    if len(starts)!=1 or len(ends)!=1:raise ValueError("all tracks must share coverage")
    return _finalize(Asset(asset_id=req.asset_id,version=req.version,name=req.name,source="external-csv-v1; units=s,m; frame=LocalCartesianMetersZUp",tracks=tracks))

class Store:
    def __init__(self,root:Path):self.root=root;root.mkdir(parents=True,exist_ok=True)
    def save(self,asset:Asset)->Asset:
        path=self.root/f"{asset.asset_id}.v{asset.version}.json"
        if path.exists():raise FileExistsError("trajectory asset/version already exists")
        with tempfile.NamedTemporaryFile("w",encoding="utf-8",dir=self.root,delete=False) as h:
            h.write(asset.model_dump_json(indent=2));h.write("\n");temp=Path(h.name)
        temp.replace(path);return asset
    def list(self)->list[Asset]:return [Asset.model_validate_json(p.read_text()) for p in sorted(self.root.glob("*.json"))]
