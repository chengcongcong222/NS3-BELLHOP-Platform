from __future__ import annotations

from pathlib import Path
from typing import Literal

from pydantic import BaseModel, ConfigDict

from .catalog import RunSnapshot
from .resources import EnvironmentResource, ExperimentResource, ScenarioResource
from .system_info import SystemInfo
from .wire import AcceptanceReport, FusionResult, NodeResult, RunProjection, RunResult


class EvidenceModel(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True, frozen=True)


class AcceptanceHardRequirements(EvidenceModel):
    network_node_count_minimum: str
    network_node_count_maximum: str
    communication_rate_bits_per_second: str
    maximum_bit_error_rate: float
    feature_level_fusion_required: bool
    minimum_bearing_points: str
    maximum_fusion_period_ns: str


class AcceptanceDemoParameters(EvidenceModel):
    network_node_count: str
    moving_sensor_node_count: str
    fixed_fusion_center_count: str
    center_frequency_hz: float
    source_level_db_re_1upa_at_1m: float
    source_level_scope: str
    tdma_guard_interval_ns: str
    network_update_interval_cycles: str
    environment_description: str
    nominal_sensor_speed_kilometers_per_hour: float
    approximate_average_range_meters: float


class AcceptanceBaseline(EvidenceModel):
    schema_version: Literal[1]
    baseline_id: Literal["Acceptance4Node"]
    baseline_version: str
    classification: str
    hard_requirements: AcceptanceHardRequirements
    demo_parameters: AcceptanceDemoParameters
    extensions: dict[str, str]


class RunManifest(EvidenceModel):
    schema_version: Literal[1] = 1
    run_id: str
    system: SystemInfo
    environment: EnvironmentResource
    scenario: ScenarioResource
    experiment: ExperimentResource


class CapturedRun(EvidenceModel):
    run_id: str
    lifecycle: str
    experiment_id: str
    experiment_version: str
    scenario_id: str
    scenario_version: str
    environment_asset_id: str
    environment_format_version: str
    event_stream_complete: bool


class EvidenceSemantics(EvidenceModel):
    verdict_origin: Literal["BackendAcceptanceReport"]
    environment_evidence: Literal["Reference / modeled"]
    propagation_evidence: Literal["Bellhop-derived"]
    ber_evidence_source: Literal["Modeled", "NotEvaluated"]
    ber_interpretation: str
    no_arrival: str
    not_decoded: str
    aggregate_policy: str


class AcceptanceEvidenceBundle(EvidenceModel):
    schema_version: Literal[1] = 1
    immutable_snapshot: Literal[True] = True
    baseline: AcceptanceBaseline
    manifest: RunManifest
    run: CapturedRun
    projection: RunProjection
    acceptance_report: AcceptanceReport
    fusion_results: tuple[FusionResult, ...]
    nodes: tuple[NodeResult, ...]
    semantics: EvidenceSemantics


def load_acceptance_baseline(path: Path) -> AcceptanceBaseline:
    return AcceptanceBaseline.model_validate_json(path.read_bytes())


def make_manifest(
    run_id: str,
    system: SystemInfo,
    environment: EnvironmentResource,
    scenario: ScenarioResource,
    experiment: ExperimentResource,
) -> RunManifest:
    return RunManifest(
        run_id=run_id,
        system=system,
        environment=environment,
        scenario=scenario,
        experiment=experiment,
    )


def make_evidence(
    baseline: AcceptanceBaseline,
    manifest: RunManifest,
    snapshot: RunSnapshot,
    result: RunResult,
) -> AcceptanceEvidenceBundle:
    if manifest.experiment.fusion.acceptance_profile != baseline.baseline_id:
        raise ValueError(
            "only Acceptance4Node produces the third-party acceptance bundle"
        )
    if result.acceptance_report is None:
        raise ValueError("formal Result does not contain an AcceptanceReport")
    if snapshot.event_stream_complete is not True:
        raise ValueError("formal Run event stream is not complete")
    source = (
        "Modeled"
        if manifest.experiment.phy.rx_quality_mode == "ModeledBpskAwgn"
        and result.acceptance_report.maximum_ber is not None
        else "NotEvaluated"
    )
    return AcceptanceEvidenceBundle(
        baseline=baseline,
        manifest=manifest,
        run=CapturedRun(
            run_id=snapshot.run_id,
            lifecycle=snapshot.lifecycle,
            experiment_id=snapshot.experiment_id,
            experiment_version=snapshot.experiment_version,
            scenario_id=snapshot.scenario_id,
            scenario_version=snapshot.scenario_version,
            environment_asset_id=snapshot.environment_asset_id,
            environment_format_version=snapshot.environment_format_version,
            event_stream_complete=True,
        ),
        projection=result.projection,
        acceptance_report=result.acceptance_report,
        fusion_results=tuple(result.fusion_results),
        nodes=tuple(result.nodes),
        semantics=EvidenceSemantics(
            verdict_origin="BackendAcceptanceReport",
            environment_evidence="Reference / modeled",
            propagation_evidence="Bellhop-derived",
            ber_evidence_source=source,
            ber_interpretation=(
                "A modeled numerical result, not a hardware measurement; at high SNR "
                "the computed double may reach the floating-point representation floor."
                if source == "Modeled"
                else "No BER model result was available; no BER value is inferred."
            ),
            no_arrival="Valid channel query with no physical arrival; no Reception exists.",
            not_decoded="A physical arrival reached Rx processing but was not decoded.",
            aggregate_policy="No unsupported aggregate is inferred from other counters.",
        ),
    )


def render_evidence_text(bundle: AcceptanceEvidenceBundle) -> str:
    report = bundle.acceptance_report
    lines = [
        "NS3-BELLHOP Acceptance Evidence Bundle",
        f"Run: {bundle.run.run_id}",
        f"Baseline: {bundle.baseline.baseline_id} v{bundle.baseline.baseline_version}",
        f"Source revision: {bundle.manifest.system.build.source_revision}",
        f"Engine: {bundle.manifest.system.simulation.engine} {bundle.manifest.system.simulation.version}",
        f"Overall: {report.overall}",
        f"Network node count: {report.network_node_count}",
        f"Communication rate: {report.communication_rate}",
        f"Bit error rate: {report.bit_error_rate} ({bundle.semantics.ber_evidence_source})",
        f"BER interpretation: {bundle.semantics.ber_interpretation}",
        f"Environment evidence: {bundle.semantics.environment_evidence}",
        f"Propagation: {bundle.semantics.propagation_evidence}",
        f"Feature-level fusion: {report.feature_level_fusion}",
        f"Bearing point count: {report.bearing_point_count}",
        f"Fusion period: {report.fusion_period}",
        f"Event stream complete: {str(bundle.run.event_stream_complete).lower()}",
        f"Channel signals: {bundle.projection.channel_signal_count}",
        f"Channel no-arrival outcomes: {bundle.projection.channel_no_arrival_count}",
        f"Receptions: {bundle.projection.reception_count}",
        "Verdicts are copied from Backend AcceptanceReport; no acceptance metric is recomputed.",
    ]
    return "\n".join(lines) + "\n"
