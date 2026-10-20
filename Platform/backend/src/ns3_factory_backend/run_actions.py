"""Run-scoped editing provenance and exact new-Run repetition, without retry state."""
from __future__ import annotations

from collections.abc import Callable
from pathlib import Path

from .authoring import AuthoringStore
from .catalog import CatalogError, RunSnapshot
from .evidence import RunManifest
from .resources import EnvironmentResource, ResourceModel
from .trajectory_authoring import Store as TrajectoryStore
from .wire import StartRunCommand


class EditRunAction(ResourceModel):
    available: bool = False
    experiment_draft_id: str | None = None
    captured_revision: int | None = None
    current_revision: int | None = None
    scenario_draft_id: str | None = None
    captured_scenario_revision: int | None = None
    current_scenario_revision: int | None = None
    reason: str | None = None


class RepeatRunAction(ResourceModel):
    available: bool
    reason: str | None = None


class RunTimingDiagnostic(ResourceModel):
    planned_payload_bytes: str
    frame_overhead_bytes: str
    encoded_bytes: str
    bit_rate_bits_per_second: str
    minimum_airtime_ns: str
    guard_interval_ns: str
    minimum_slot_duration_ns: str
    configured_slot_duration_ns: str
    slot_contains_airtime_and_guard: bool
    explanation: str


class RunActions(ResourceModel):
    run_id: str
    experiment_name: str | None
    scenario_name: str | None
    environment_asset_id: str
    edit: EditRunAction
    rerun: RepeatRunAction
    timing: RunTimingDiagnostic | None = None


def _edit_action(snapshot: RunSnapshot, definition: dict | None,
                 authoring: AuthoringStore) -> EditRunAction:
    if definition is None:
        return EditRunAction(reason="这个运行未保留可验证的原始实验草稿关联。")
    sources: dict[str, tuple[str, int, int | None]] = {}
    for kind, published_id in (("experiment", snapshot.experiment_id), ("scenario", snapshot.scenario_id)):
        try:
            captured = int(definition[f"{kind}_source_revision"])
            draft_id = definition.get(f"{kind}_source_draft_id")
            if draft_id is None:
                # Old definitions omitted draft IDs; the exact immutable
                # publication preserves them. Never infer IDs by trimming '-rN'.
                publication = authoring.get_published(kind, published_id)
                if (publication["published_id"] != published_id or publication["kind"] != kind
                        or publication["source_revision"] != captured):
                    raise ValueError("publication source mismatch")
                draft_id = publication["source_draft_id"]
            if not isinstance(draft_id, str) or captured < 1:
                raise ValueError("invalid source identity")
            try:
                current = authoring.get(kind, draft_id).revision
            except KeyError:
                current = None
            sources[kind] = (draft_id, captured, current)
        except (KeyError, ValueError, TypeError, OSError):
            continue
    experiment = sources.get("experiment")
    scenario = sources.get("scenario")
    available = experiment is not None and experiment[2] is not None
    reason = None
    if not available:
        reason = "原实验草稿已不存在或来源记录缺失；不会跳转到空白新建表单。"
    elif scenario is None or scenario[2] is None:
        reason = "可编辑原实验，但原节点场景草稿已不可用，需要明确重新绑定。"
    elif experiment[1] != experiment[2] or scenario[1] != scenario[2]:
        reason = "原草稿已有更新；编辑入口打开当前草稿，历史运行仍保留原冻结配置。"
    return EditRunAction(available=available, reason=reason,
        experiment_draft_id=experiment[0] if experiment else None,
        captured_revision=experiment[1] if experiment else None,
        current_revision=experiment[2] if experiment else None,
        scenario_draft_id=scenario[0] if scenario else None,
        captured_scenario_revision=scenario[1] if scenario else None,
        current_scenario_revision=scenario[2] if scenario else None)


def validate_repeat_resources(snapshot: RunSnapshot, command: StartRunCommand,
                              manifest: RunManifest | None, repository_root: Path,
                              get_environment: Callable[[str], EnvironmentResource],
                              trajectories: TrajectoryStore) -> dict | None:
    from .run_context import load_captured_definition
    definition = load_captured_definition(snapshot) if snapshot.authored_definition_path else None
    if command.execution.physical_layer == "Channel3Recorded":
        if definition and "recorded_proxy" in definition:
            from .recorded_proxy import verify_frozen_resources
            verify_frozen_resources(Path(snapshot.authored_definition_path).parent.parent,
                                    definition["recorded_proxy"])
        elif not definition or "recorded_channel" not in definition:
            raise CatalogError("RecordedChannelInvalid", "原运行未保留录音绑定。")
        else:
            _verify_legacy_recorded(snapshot, definition)
    else:
        if not (repository_root / command.environment.asset_id).is_dir():
            raise CatalogError("EnvironmentNotFound", "原运行绑定的环境资源已不存在，不能按原配置重新运行。")
        environment = get_environment(command.environment.asset_id)
        if environment.asset_format_version != command.environment.asset_format_version:
            raise CatalogError("InvalidReference", "原运行环境版本不一致，不能替换为其他版本。")
        expected_checksum = (definition["environment"].get("checksum") if definition is not None
                             else manifest.environment.checksum.value if manifest else None)
        if expected_checksum is not None and environment.checksum.value != expected_checksum:
            raise CatalogError("InvalidReference", "原运行环境校验和不一致，不能静默使用变化后的环境。")
    if definition is not None:
        try:
            for frozen in definition["trajectory_assets"]:
                asset = trajectories.get(frozen["asset_id"], frozen["version"])
                if ((asset.asset_id, asset.version) != (frozen["asset_id"], frozen["version"])
                        or asset.checksum_sha256 != frozen["checksum_sha256"]):
                    raise ValueError("trajectory checksum or identity mismatch")
            for binding in definition["trajectory_bindings"]:
                asset = trajectories.get(binding["asset_id"], binding["version"])
                if not any(track.track_id == binding["track_id"] for track in asset.tracks):
                    raise CatalogError("TrajectoryTrackNotFound", "原运行绑定的节点轨迹不存在。")
        except KeyError as error:
            raise CatalogError("TrajectoryNotFound", "原运行绑定的轨迹资源或版本不存在。") from error
        except (ValueError, OSError, TypeError) as error:
            raise CatalogError("TrajectoryResourceInvalid", "原运行绑定的轨迹损坏或校验不一致。") from error
    return definition


def _verify_legacy_recorded(snapshot, definition):
    from .recorded_channels import inspect
    original = definition["recorded_channel"]
    current = inspect(Path(snapshot.authored_definition_path).parent.parent / "recorded-channels",
                      original["manifest"]["asset_id"], original["manifest"]["version"], verify=True)
    if current["manifest"] != original["manifest"] or current["manifest_sha256"] != original["manifest_sha256"]:
        raise CatalogError("RecordedChannelInvalid", "原录音版本发生变化，未使用替代数据。")
    if original["noise"]["mode"] == "ContinuousLibrary":
        from .recorded_channels import inspect_noise
        noise = original["noise"]
        current_noise = inspect_noise(Path(snapshot.authored_definition_path).parent.parent / "recorded-noise",
            noise["manifest"]["asset_id"], noise["manifest"]["version"], verify=True)
        if current_noise != {k: noise[k] for k in ("manifest", "manifest_sha256")}:
            raise CatalogError("RecordedChannelInvalid", "原连续噪声版本发生变化，未重新生成替代样本。")


def _timing(definition: dict | None, command: StartRunCommand,
            manifest: RunManifest | None) -> RunTimingDiagnostic | None:
    try:
        config = definition["experiment"] if definition is not None else None
        if config and config["mac_mode"] in {"AdaptiveTdma", "RegisteredMac"}:
            # Slot boundaries are planner output, not a user-configured input.
            return None
        rate = int(config["bit_rate_bps"] if config else manifest.experiment.phy.bit_rate_bits_per_second)
        guard = int(config["guard_interval_ns"] if config else manifest.experiment.mac.guard_interval_ns)
        slot = int(config["slot_duration_ns"] if config else (
            "6000000000" if command.execution.physical_layer == "Channel2Waveform"
            else manifest.experiment.mac.slot_duration_ns))
        if rate <= 0 or guard < 0 or slot <= 0:
            return None
        payload_bytes = 4 if config and config["workload"] == "CommunicationOnly" else 15
        overhead = 4 if command.execution.physical_layer == "Channel2Waveform" else 0
        encoded = payload_bytes + overhead
        airtime = (encoded * 8 * 1_000_000_000 + rate - 1) // rate
        if config and command.execution.waveform_modulation == "Osdm":
            from .modem_configuration import osdm_airtime_ns
            airtime = osdm_airtime_ns(payload_bytes, rate, config["osdm"])
        minimum = airtime + guard
        return RunTimingDiagnostic(planned_payload_bytes=str(payload_bytes), frame_overhead_bytes=str(overhead),
            encoded_bytes=str(encoded), bit_rate_bits_per_second=str(rate),
            minimum_airtime_ns=str(airtime), guard_interval_ns=str(guard),
            minimum_slot_duration_ns=str(minimum), configured_slot_duration_ns=str(slot),
            slot_contains_airtime_and_guard=slot >= minimum,
            explanation="按当前已实现帧长与比特率检查时隙；保护时间已计入。该下限不保证所有传播/多径过程均可容纳，也不保证解调成功。")
    except (KeyError, TypeError, ValueError, AttributeError):
        return None


def make_run_actions(snapshot: RunSnapshot, command: StartRunCommand, manifest: RunManifest | None,
                     authoring: AuthoringStore, repeat_error: str | None = None) -> RunActions:
    from .run_context import load_captured_definition
    definition = None
    if snapshot.authored_definition_path:
        try:
            definition = load_captured_definition(snapshot)
        except CatalogError as error:
            repeat_error = str(error)
    terminal = snapshot.lifecycle in {"Completed", "Failed"}
    return RunActions(run_id=snapshot.run_id,
        experiment_name=(definition.get("experiment", {}).get("name") if definition is not None
                         else manifest.experiment.name if manifest else None),
        scenario_name=(definition.get("scenario_name") if definition is not None
                       else manifest.scenario.name if manifest else None),
        environment_asset_id=snapshot.environment_asset_id,
        edit=_edit_action(snapshot, definition, authoring),
        rerun=RepeatRunAction(available=terminal and repeat_error is None,
            reason=repeat_error or (None if terminal else "原运行尚未结束；重新运行会创建独立的新运行。")),
        timing=_timing(definition, command, manifest))
