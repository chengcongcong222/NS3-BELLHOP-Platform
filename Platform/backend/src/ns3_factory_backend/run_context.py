"""Read-only views of the exact inputs captured for a Run, not editable drafts."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Literal

from pydantic import Field, ValidationError, model_validator

from .catalog import CatalogError, RunSnapshot
from .evidence import RunManifest
from .run_actions import RunActions
from .resources import (
    ExperimentResource, FusionSummary, PhySummary, ResourceModel,
    ScenarioResource,
)
from .wire import Execution, PositiveUIntDecimal, StableId, UIntDecimal


class TrajectoryBindingSummary(ResourceModel):
    node_id: UIntDecimal
    asset_id: StableId
    version: PositiveUIntDecimal
    track_id: StableId


class RunMobilitySummary(ResourceModel):
    model: Literal["TrajectoryDriven", "ConstantVelocity"]


class RunScenarioResource(ScenarioResource):
    mobility: RunMobilitySummary
    fusion_center_node_id: UIntDecimal | None
    trajectory_bindings: tuple[TrajectoryBindingSummary, ...] = ()
    spatial_domain: dict | None = None


class RunRoutingSummary(ResourceModel):
    mode: Literal["Direct", "DirectToFusionCenter", "ShortestPath"]


class RunMacSummary(ResourceModel):
    mode: Literal["Tdma", "AdaptiveTdma", "RegisteredMac"]
    slot_duration_ns: PositiveUIntDecimal | None = None
    guard_interval_ns: UIntDecimal | None = None

    @model_validator(mode="after")
    def validate_mode_parameters(self):
        if self.mode == "Tdma" and (self.slot_duration_ns is None or self.guard_interval_ns is None):
            raise ValueError("configured TDMA requires its captured timing")
        if self.mode != "Tdma" and (self.slot_duration_ns is not None or self.guard_interval_ns is not None):
            raise ValueError("adaptive TDMA has no configured slot or guard")
        return self


class RunAdaptiveTdmaSummary(ResourceModel):
    safety_margin_ns: UIntDecimal


class RunCommunicationFlow(ResourceModel):
    flow_id: UIntDecimal | None = None
    source_node_id: UIntDecimal
    destination_node_id: UIntDecimal
    packets_per_cycle: int = Field(ge=1, le=1000)


class RunPhySummary(PhySummary):
    physical_layer: Literal["Channel1Abstract", "Channel2Waveform", "Channel3Recorded"]
    waveform_modulation: Literal["Bpsk", "Bfsk", "Osdm"]
    waveform_noise: Literal["None", "Awgn"]
    waveform_noise_psd_db_re_1upa2_per_hz: float
    osdm: dict | None = None


class RunFusionSummary(FusionSummary):
    workload: Literal["AcceptanceBearingFusion", "BearingFusion", "CommunicationOnly"]
    acceptance_profile: Literal["Acceptance4Node", "Extended6Node"] | None


class RunExperimentResource(ExperimentResource):
    routing: RunRoutingSummary
    mac: RunMacSummary
    phy: RunPhySummary
    fusion: RunFusionSummary
    execution_mode: Literal["PlanningV1"] | None = None
    planning_cycle_duration_ns: PositiveUIntDecimal | None = None
    adaptive_tdma: RunAdaptiveTdmaSummary | None = None
    mac_algorithm: dict | None = None
    flows: tuple[RunCommunicationFlow, ...] = ()
    fusion_receiver_node_id: UIntDecimal | None = None


class RunDefinitionContext(ResourceModel):
    run_id: StableId
    source: Literal["AuthoredDefinition", "Preset"]
    definition_sha256: str | None
    scenario: RunScenarioResource
    experiment: RunExperimentResource
    execution: Execution
    limitations: tuple[str, ...] = ()
    actions: RunActions | None = None
    recorded_channel: dict | None = None


def load_captured_definition(snapshot: RunSnapshot) -> dict:
    """Checksum and identity checks precede any projection; never scan drafts."""
    try:
        if not snapshot.authored_definition_path or not snapshot.authored_definition_sha256:
            raise ValueError("missing captured definition")
        encoded = Path(snapshot.authored_definition_path).read_bytes()
        if hashlib.sha256(encoded).hexdigest() != snapshot.authored_definition_sha256:
            raise ValueError("definition checksum mismatch")
        definition = json.loads(encoded)
        if (definition["schema"] not in {"ns3-factory-authored-run-v1", "ns3-factory-authored-run-v2", "ns3-factory-authored-run-v3", "ns3-factory-authored-run-v4"}
                or (definition["schema"] in {"ns3-factory-authored-run-v2", "ns3-factory-authored-run-v3", "ns3-factory-authored-run-v4"}
                    and definition.get("execution_mode") != "PlanningV1")
                or definition["scenario_id"] != snapshot.scenario_id
                or definition["experiment_id"] != snapshot.experiment_id
                or str(definition["version"]) != snapshot.scenario_version
                or str(definition["version"]) != snapshot.experiment_version
                or definition["environment"]["asset_id"] != snapshot.environment_asset_id
                or str(definition["environment"]["format_version"]) != snapshot.environment_format_version):
            raise ValueError("definition identity mismatch")
        if definition["schema"] == "ns3-factory-authored-run-v3":
            from .planning_authoring import v3_integer_boundary
            return v3_integer_boundary(definition, encode=False)
        return definition
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise CatalogError(
            "RunDefinitionUnavailable",
            "本次运行保存的实验定义缺失或校验失败，不能用当前草稿或预置案例替代。",
        ) from error


def _initial_velocity(definition: dict, node: dict) -> dict[str, float]:
    """Compatibility summary of the frozen trajectory's t=0 segment, not a model."""
    bindings = [b for b in definition["trajectory_bindings"] if b["node_id"] == node["node_id"]]
    if len(bindings) != 1:
        raise ValueError("node binding is not unique")
    binding = bindings[0]
    assets = [a for a in definition["trajectory_assets"]
              if (a["asset_id"], a["version"]) == (binding["asset_id"], binding["version"])]
    if len(assets) != 1:
        raise ValueError("trajectory asset is not unique")
    tracks = [t for t in assets[0]["tracks"] if t["track_id"] == binding["track_id"]]
    if len(tracks) != 1 or not tracks[0]["points"]:
        raise ValueError("trajectory track is missing")
    points = tracks[0]["points"]
    first = points[0]
    if first["time_ns"] != 0:
        raise ValueError("trajectory does not cover t=0")
    # Match the Runtime initial-state tolerance (cycle_working_state.hpp),
    # rather than reject a Run that the authoritative state already accepted.
    if any(abs(first[f"{axis}_m"] - node[f"{axis}_m"]) > 1e-8 for axis in "xyz"):
        raise ValueError("initial position differs from frozen trajectory")
    if len(points) == 1:
        return {f"{axis}_meters_per_second": 0.0 for axis in "xyz"}
    second = points[1]
    elapsed_ns = second["time_ns"] - first["time_ns"]
    if elapsed_ns <= 0:
        raise ValueError("trajectory time does not increase")
    return {f"{axis}_meters_per_second":
            (second[f"{axis}_m"] - first[f"{axis}_m"]) * 1_000_000_000 / elapsed_ns
            for axis in "xyz"}


def make_run_context(snapshot: RunSnapshot, execution: Execution | None,
                     manifest: RunManifest | None) -> RunDefinitionContext:
    if execution is None:
        raise CatalogError("RunDefinitionUnavailable", "本次运行的执行参数未保留，无法准确显示配置。")
    if snapshot.authored_definition_path is None:
        if manifest is None:
            raise CatalogError("RunDefinitionUnavailable", "本次运行的预置案例快照未保留。")
        scenario = manifest.scenario.model_dump()
        experiment = manifest.experiment.model_dump()
        # The explicit preset path may have user-selected execution overrides.
        # Read those from the captured command, never the catalog's preset values.
        experiment.update(simulation_cycle_count=execution.simulation_cycle_count,
                          deterministic_seed=execution.deterministic_seed)
        experiment["phy"].update(execution.model_dump(exclude={"simulation_cycle_count", "deterministic_seed"}))
        if execution.physical_layer == "Channel2Waveform":
            # Exact compatibility rule in worker/internal/simulation_worker.hpp
            # MakeAcceptancePresetDefinition: the old waveform preset uses a
            # six-second slot. Authored runs instead read their frozen slot below.
            experiment["mac"]["slot_duration_ns"] = "6000000000"
        return RunDefinitionContext(
            run_id=snapshot.run_id, source="Preset", definition_sha256=None,
            scenario=RunScenarioResource.model_validate(scenario),
            experiment=RunExperimentResource.model_validate(experiment), execution=execution,
            limitations=("这个预置案例未公开轨迹资源绑定详情；运行位置以正式事件记录为准。",),
        )

    definition = load_captured_definition(snapshot)
    try:
        config = definition["experiment"]
        planning = definition["schema"] in {"ns3-factory-authored-run-v2", "ns3-factory-authored-run-v3", "ns3-factory-authored-run-v4"}
        # These fields occur in both the definition and the command. They must
        # agree; read endpoints must not repair or silently select either one.
        for key in ("physical_layer", "waveform_modulation", "waveform_noise",
                    "waveform_noise_psd_db_re_1upa2_per_hz"):
            if config[key] != getattr(execution, key):
                raise ValueError("execution differs from captured definition")
        if str(config["deterministic_seed"]) != execution.deterministic_seed:
            raise ValueError("seed differs from captured definition")
        nodes = tuple({
            "node_id": str(node["node_id"]), "can_transmit": node["can_transmit"],
            "can_receive": node["can_receive"], "duplex_mode": node["duplex_mode"],
            "initial_position": {f"{axis}_meters": node[f"{axis}_m"] for axis in "xyz"},
            "initial_velocity": _initial_velocity(definition, node),
        } for node in definition["nodes"])
        scenario = RunScenarioResource.model_validate({
            "scenario_id": snapshot.scenario_id, "version": snapshot.scenario_version,
            "name": definition["scenario_name"], "nodes": nodes,
            "spatial_domain": definition.get("spatial_domain"),
            "environment": {"environment_asset_id": snapshot.environment_asset_id,
                            "asset_format_version": snapshot.environment_format_version},
            "mobility": {"model": "TrajectoryDriven"},
            "fusion_center_node_id": (str(definition["fusion_center_node_id"])
                                      if definition["fusion_center_node_id"] is not None else None),
            "trajectory_bindings": tuple({**binding, "node_id": str(binding["node_id"]),
                                           "version": str(binding["version"])}
                                          for binding in definition["trajectory_bindings"]),
        })
        experiment = RunExperimentResource.model_validate({
            "experiment_id": snapshot.experiment_id, "version": snapshot.experiment_version,
            "name": config["name"],
            "scenario": {"scenario_id": snapshot.scenario_id, "version": snapshot.scenario_version},
            "routing": {"mode": config["routing_mode"]},
            "mac": {"mode": config["mac_mode"],
                    "slot_duration_ns": (str(config["slot_duration_ns"]) if config["mac_mode"] == "Tdma" else None),
                    "guard_interval_ns": (str(config["guard_interval_ns"]) if config["mac_mode"] == "Tdma" else None)},
            "phy": {"bit_rate_bits_per_second": str(config["bit_rate_bps"]),
                    "center_frequency_hz": config["center_frequency_hz"],
                    "occupied_bandwidth_hz": config["occupied_bandwidth_hz"],
                    "source_level_db_re_1upa_at_1m": config["source_level_db"],
                    "osdm": config.get("osdm"),
                    **execution.model_dump(exclude={"simulation_cycle_count", "deterministic_seed"})},
            "fusion": {"workload": config["workload"], "acceptance_profile": None,
                       "minimum_bearing_points": str(config["minimum_bearing_points"]),
                       "maximum_fusion_period_ns": str(config["maximum_fusion_period_ns"]),
                       "maximum_ber": config["maximum_ber"]},
            "network_update_interval_cycles": str(config["network_update_interval_cycles"]),
            "simulation_cycle_count": execution.simulation_cycle_count,
            "deterministic_seed": execution.deterministic_seed,
            **({
                "execution_mode": definition["execution_mode"],
                "planning_cycle_duration_ns": str(config["planning_cycle_duration_ns"]),
                "mac_algorithm": config.get("mac_algorithm"),
                "adaptive_tdma": ({"safety_margin_ns": str(config["adaptive_tdma"]["safety_margin_ns"])}
                                  if config["adaptive_tdma"] is not None else None),
                "flows": tuple({**flow, **({"flow_id": str(flow["flow_id"])} if "flow_id" in flow else {}), "source_node_id": str(flow["source_node_id"]),
                                "destination_node_id": str(flow["destination_node_id"])} for flow in config["flows"]),
                "fusion_receiver_node_id": (str(config["fusion_receiver_node_id"])
                                            if config["fusion_receiver_node_id"] is not None else None),
            } if planning else {}),
        })
        return RunDefinitionContext(run_id=snapshot.run_id, source="AuthoredDefinition",
                                    definition_sha256=snapshot.authored_definition_sha256,
                                    scenario=scenario, experiment=experiment, execution=execution,
                                    recorded_channel=definition.get("recorded_channel"))
    except (ValueError, KeyError, TypeError, IndexError, ValidationError) as error:
        raise CatalogError("RunDefinitionUnavailable", "本次运行的冻结配置不完整或不一致，不能由当前草稿补齐。") from error
