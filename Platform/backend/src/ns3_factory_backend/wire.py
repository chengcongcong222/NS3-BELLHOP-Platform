from __future__ import annotations

import math
import re
from typing import Annotated, Literal, TypeAlias

from pydantic import (
    AfterValidator,
    BaseModel,
    ConfigDict,
    Field,
    TypeAdapter,
    model_validator,
)


SCHEMA_VERSION = 1
MAXIMUM_INPUT_LINE_BYTES = 1 << 20
MAXIMUM_OUTPUT_MESSAGE_BYTES = 4 << 20
_STABLE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")
_ASSET_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")


def _canonical_unsigned(value: str, maximum: int = (1 << 64) - 1) -> str:
    if not isinstance(value, str) or not value or not value.isascii():
        raise ValueError("value must be a canonical decimal string")
    if not value.isdecimal() or (len(value) > 1 and value[0] == "0"):
        raise ValueError("value must be a canonical decimal string")
    parsed = int(value)
    if parsed > maximum:
        raise ValueError("decimal string is outside its integer range")
    return value


def _positive_unsigned(value: str) -> str:
    value = _canonical_unsigned(value)
    if value == "0":
        raise ValueError("value must be positive")
    return value


def _uint32(value: str) -> str:
    return _canonical_unsigned(value, (1 << 32) - 1)


def _positive_uint32(value: str) -> str:
    value = _uint32(value)
    if value == "0":
        raise ValueError("value must be positive")
    return value


def _canonical_int64(value: str) -> str:
    if not isinstance(value, str) or not value or not value.isascii():
        raise ValueError("value must be a canonical signed decimal string")
    digits = value[1:] if value.startswith("-") else value
    if not digits or not digits.isdecimal():
        raise ValueError("value must be a canonical signed decimal string")
    if len(digits) > 1 and digits[0] == "0":
        raise ValueError("value must be a canonical signed decimal string")
    if value == "-0":
        raise ValueError("value must be a canonical signed decimal string")
    parsed = int(value)
    if parsed < -(1 << 63) or parsed > (1 << 63) - 1:
        raise ValueError("decimal string is outside int64 range")
    return value


def _stable_id(value: str) -> str:
    if not isinstance(value, str) or _STABLE_ID.fullmatch(value) is None:
        raise ValueError("application ID grammar is invalid")
    return value


def _asset_id(value: str) -> str:
    if not isinstance(value, str) or _ASSET_ID.fullmatch(value) is None:
        raise ValueError("environment asset ID grammar is invalid")
    return value


def _finite(value: float) -> float:
    if not math.isfinite(value):
        raise ValueError("physical value must be finite")
    return value


UIntDecimal = Annotated[str, AfterValidator(_canonical_unsigned)]
PositiveUIntDecimal = Annotated[str, AfterValidator(_positive_unsigned)]
UInt32Decimal = Annotated[str, AfterValidator(_uint32)]
PositiveUInt32Decimal = Annotated[str, AfterValidator(_positive_uint32)]
Int64Decimal = Annotated[str, AfterValidator(_canonical_int64)]
StableId = Annotated[str, AfterValidator(_stable_id)]
EnvironmentAssetId = Annotated[str, AfterValidator(_asset_id)]
FiniteFloat = Annotated[float, AfterValidator(_finite)]


class WireModel(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)


class Preset(WireModel):
    scenario_id: StableId
    experiment_id: StableId
    definition_version: PositiveUIntDecimal
    acceptance_profile: Literal["Acceptance4Node", "Extended6Node"]


class Environment(WireModel):
    asset_id: EnvironmentAssetId
    asset_format_version: PositiveUInt32Decimal


class Execution(WireModel):
    simulation_cycle_count: PositiveUIntDecimal
    rx_quality_mode: Literal["None", "ModeledBpskAwgn"]
    equivalent_noise_power_db_re_1upa2: FiniteFloat
    deterministic_seed: UIntDecimal
    physical_layer: Literal["Channel1Abstract", "Channel2Waveform"] = (
        "Channel1Abstract"
    )
    waveform_modulation: Literal["Bpsk", "Bfsk"] = "Bpsk"
    waveform_noise: Literal["None", "Awgn"] = "None"
    waveform_noise_psd_db_re_1upa2_per_hz: FiniteFloat = 0.0

    @model_validator(mode="after")
    def validate_physical_layer(self) -> "Execution":
        if self.physical_layer == "Channel2Waveform" and self.rx_quality_mode != "None":
            raise ValueError("Channel2Waveform requires rx_quality_mode=None")
        return self


class StartRunCommand(WireModel):
    schema_version: Literal[1] = SCHEMA_VERSION
    type: Literal["StartRunCommand"] = "StartRunCommand"
    run_id: StableId
    preset: Preset
    environment: Environment
    execution: Execution
    authored_definition_path: str | None = None
    authored_definition_sha256: str | None = None

    @model_validator(mode="after")
    def validate_authored_definition(self) -> "StartRunCommand":
        if (self.authored_definition_path is None) != (self.authored_definition_sha256 is None):
            raise ValueError("authored definition path and checksum must be supplied together")
        if self.authored_definition_sha256 is not None and not re.fullmatch(
            r"[0-9a-f]{64}", self.authored_definition_sha256
        ):
            raise ValueError("authored definition checksum must be lowercase SHA256")
        return self


ErrorCode = Literal[
    "InvalidArgument",
    "OutOfRange",
    "Overflow",
    "NotFound",
    "AlreadyExists",
    "FailedPrecondition",
    "Unsupported",
    "Unavailable",
    "Internal",
]


class OwnedError(WireModel):
    code: ErrorCode
    message: str


class RunFailure(WireModel):
    code: ErrorCode
    message: str


class RunRecord(WireModel):
    run_id: StableId
    experiment_id: StableId
    experiment_version: PositiveUIntDecimal
    scenario_id: StableId
    scenario_version: PositiveUIntDecimal
    environment_asset_id: EnvironmentAssetId
    environment_format_version: PositiveUInt32Decimal
    lifecycle: Literal["Created", "Running", "Completed", "Failed"]
    simulation_started_at_ns: Int64Decimal | None
    simulation_ended_at_ns: Int64Decimal | None
    final_snapshot_version: UIntDecimal | None
    failure: RunFailure | None
    event_stream_complete: bool | None


class CycleCommitPayload(WireModel):
    cycle_id: UIntDecimal
    base_snapshot_version: UIntDecimal
    committed_snapshot_version: UIntDecimal
    committed_at_ns: Int64Decimal


class UnicastTarget(WireModel):
    type: Literal["Unicast"]
    node_id: UIntDecimal


class BroadcastTarget(WireModel):
    type: Literal["Broadcast"]


TransmissionTarget: TypeAlias = Annotated[
    UnicastTarget | BroadcastTarget, Field(discriminator="type")
]


class TransmissionPayload(WireModel):
    transmission_id: UIntDecimal
    packet_id: UIntDecimal
    sender_node_id: UIntDecimal
    target: TransmissionTarget
    started_at_ns: Int64Decimal
    ended_at_ns: Int64Decimal


class SignalOutcome(WireModel):
    type: Literal["Signal"]
    first_arrival_delay_ns: Int64Decimal
    aggregate_transmission_loss_db: FiniteFloat
    path_count: UIntDecimal


class NoArrivalOutcome(WireModel):
    type: Literal["NoArrival"]


ChannelOutcome: TypeAlias = Annotated[
    SignalOutcome | NoArrivalOutcome, Field(discriminator="type")
]


class ChannelOutcomePayload(WireModel):
    transmission_id: UIntDecimal
    receiver_node_id: UIntDecimal
    outcome: ChannelOutcome


class QualitySummary(WireModel):
    signal_to_noise_ratio_db: FiniteFloat
    eb_n0_db: FiniteFloat
    bit_error_rate: FiniteFloat
    source: Literal["Modeled", "Measured", "External"]


class ReceptionWaveform(WireModel):
    payload_bit_count: PositiveUIntDecimal
    bit_error_count: UIntDecimal
    crc_ok: bool
    signal_power_upa2: FiniteFloat
    noise_power_upa2: FiniteFloat

    @model_validator(mode="after")
    def valid_measurement(self):
        if int(self.bit_error_count) > int(self.payload_bit_count) or self.signal_power_upa2 < 0 or self.noise_power_upa2 < 0:
            raise ValueError("Invalid waveform measurement")
        return self


class ReceptionPayload(WireModel):
    reception_id: UIntDecimal
    transmission_id: UIntDecimal
    packet_id: UIntDecimal
    receiver_node_id: UIntDecimal
    disposition: Literal[
        "NotDecoded", "Overheard", "LocalDelivery", "RelayEnqueue"
    ]
    quality: QualitySummary | None
    waveform: ReceptionWaveform | None = None


class SignalArrivalPayload(WireModel):
    transmission_id: UIntDecimal
    receiver_node_id: UIntDecimal


class NodePositionPayload(WireModel):
    node_id: UIntDecimal
    x_meters: FiniteFloat
    y_meters: FiniteFloat
    z_meters: FiniteFloat


class FusionPayload(WireModel):
    fusion_sequence: UIntDecimal
    fusion_center_node_id: UIntDecimal
    started_at_ns: Int64Decimal
    completed_at_ns: Int64Decimal
    observation_count: PositiveUIntDecimal
    estimated_target_x_meters: FiniteFloat
    estimated_target_y_meters: FiniteFloat


class CycleCommitTrace(WireModel):
    occurred_at_ns: Int64Decimal
    kind: Literal["CycleCommit"]
    payload: CycleCommitPayload


class TransmissionTrace(WireModel):
    occurred_at_ns: Int64Decimal
    kind: Literal["Transmission"]
    payload: TransmissionPayload


class ChannelOutcomeTrace(WireModel):
    occurred_at_ns: Int64Decimal
    kind: Literal["ChannelOutcome"]
    payload: ChannelOutcomePayload


class ReceptionTrace(WireModel):
    occurred_at_ns: Int64Decimal
    kind: Literal["Reception"]
    payload: ReceptionPayload


class SignalArrivalTrace(WireModel):
    occurred_at_ns: Int64Decimal
    kind: Literal["SignalArrival"]
    payload: SignalArrivalPayload


class NodePositionTrace(WireModel):
    occurred_at_ns: Int64Decimal
    kind: Literal["NodePosition"]
    payload: NodePositionPayload


class FusionTrace(WireModel):
    occurred_at_ns: Int64Decimal
    kind: Literal["Fusion"]
    payload: FusionPayload


TraceEvent: TypeAlias = Annotated[
    CycleCommitTrace
    | TransmissionTrace
    | ChannelOutcomeTrace
    | ReceptionTrace
    | SignalArrivalTrace
    | NodePositionTrace
    | FusionTrace,
    Field(discriminator="kind"),
]


class RunProjection(WireModel):
    simulation_started_at_ns: Int64Decimal
    simulation_ended_at_ns: Int64Decimal
    simulation_duration_ns: Int64Decimal
    final_snapshot_version: UIntDecimal
    cycle_count: UIntDecimal
    node_count: UIntDecimal
    transmission_count: UIntDecimal
    channel_signal_count: UIntDecimal
    channel_no_arrival_count: UIntDecimal
    reception_count: UIntDecimal
    local_delivery_count: UIntDecimal


MetricStatus = Literal["Pass", "Fail", "NotEvaluated"]
OverallStatus = Literal["Pass", "Fail", "NotFullyEvaluated"]


class AcceptanceReport(WireModel):
    network_node_count: MetricStatus
    communication_rate: MetricStatus
    bit_error_rate: MetricStatus
    feature_level_fusion: MetricStatus
    bearing_point_count: MetricStatus
    fusion_period: MetricStatus
    overall: OverallStatus
    evaluated_target_receptions: UIntDecimal
    missing_ber_evidence_count: UIntDecimal
    maximum_ber: FiniteFloat | None
    mean_ber: FiniteFloat | None
    required_maximum_ber: FiniteFloat
    minimum_bearing_points: UIntDecimal | None
    required_minimum_bearing_points: UIntDecimal
    maximum_fusion_period_ns: Int64Decimal | None
    required_maximum_fusion_period_ns: Int64Decimal
    ber_reason: str


class FusionResult(WireModel):
    fusion_sequence: UIntDecimal
    started_at_ns: Int64Decimal
    completed_at_ns: Int64Decimal
    fusion_period_ns: Int64Decimal
    observation_count: UIntDecimal
    estimated_target_x_meters: FiniteFloat
    estimated_target_y_meters: FiniteFloat


class NodeResult(WireModel):
    node_id: UIntDecimal
    x_meters: FiniteFloat
    y_meters: FiniteFloat
    z_meters: FiniteFloat
    is_fusion_center: bool


class PositionSample(WireModel):
    node_id: UIntDecimal
    at_ns: Int64Decimal
    x_meters: FiniteFloat
    y_meters: FiniteFloat
    z_meters: FiniteFloat


class FusionSource(WireModel):
    fusion_sequence: UIntDecimal
    node_id: UIntDecimal
    observation_sequence: UIntDecimal


class WaveformSummary(WireModel):
    transmission_id: UIntDecimal
    receiver_node_id: UIntDecimal
    payload_bit_count: UIntDecimal
    bit_error_count: UIntDecimal
    crc_ok: bool

    @model_validator(mode="after")
    def valid_error_count(self):
        if int(self.bit_error_count) > int(self.payload_bit_count):
            raise ValueError("bit errors exceed payload bits")
        return self


class RunVisualization(WireModel):
    positions_complete: bool
    generated_feature_count: UIntDecimal
    positions: list[PositionSample]
    fusion_sources: list[FusionSource]
    waveform: list[WaveformSummary]


class RunResult(WireModel):
    run_id: StableId
    projection: RunProjection
    acceptance_report: AcceptanceReport | None
    fusion_results: list[FusionResult]
    nodes: list[NodeResult]
    visualization: RunVisualization | None = None


class WorkerStarted(WireModel):
    schema_version: Literal[1]
    type: Literal["WorkerStarted"]
    run_id: StableId


class WorkerRunEvent(WireModel):
    schema_version: Literal[1]
    type: Literal["WorkerRunEvent"]
    run_id: StableId
    sequence: PositiveUIntDecimal
    trace: TraceEvent


class WorkerCompleted(WireModel):
    schema_version: Literal[1]
    type: Literal["WorkerCompleted"]
    run: RunRecord
    result: RunResult

    @model_validator(mode="after")
    def _consistent(self) -> "WorkerCompleted":
        if self.run.lifecycle != "Completed" or self.run.run_id != self.result.run_id:
            raise ValueError("WorkerCompleted terminal identity is inconsistent")
        return self


class WorkerFailed(WireModel):
    schema_version: Literal[1]
    type: Literal["WorkerFailed"]
    run_id: StableId | None
    category: Literal["Protocol", "Composition", "Simulation"]
    error: OwnedError
    run: RunRecord | None


WorkerMessage: TypeAlias = Annotated[
    WorkerStarted | WorkerRunEvent | WorkerCompleted | WorkerFailed,
    Field(discriminator="type"),
]
_WORKER_MESSAGE_ADAPTER = TypeAdapter(WorkerMessage)


def encode_start_run_command(command: StartRunCommand) -> bytes:
    encoded = command.model_dump_json().encode("utf-8")
    if len(encoded) > MAXIMUM_INPUT_LINE_BYTES:
        raise ValueError("StartRunCommand exceeds the 1 MiB wire limit")
    return encoded


def parse_worker_message(line: bytes) -> WorkerMessage:
    if not line or len(line) > MAXIMUM_OUTPUT_MESSAGE_BYTES:
        raise ValueError("worker message is empty or exceeds the 4 MiB wire limit")
    return _WORKER_MESSAGE_ADAPTER.validate_json(line)
