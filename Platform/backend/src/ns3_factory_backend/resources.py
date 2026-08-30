from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Literal

from pydantic import BaseModel, ConfigDict, StringConstraints
from typing_extensions import Annotated

from .wire import (
    Environment,
    Execution,
    FiniteFloat,
    PositiveUInt32Decimal,
    PositiveUIntDecimal,
    Preset,
    StableId,
    StartRunCommand,
    UIntDecimal,
)


ChecksumHex = Annotated[str, StringConstraints(pattern=r"^[0-9a-f]{16}$")]


class ResourceModel(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True, frozen=True)


class ProvenanceSummary(ResourceModel):
    producer: Literal["BellhopRawImport", "Manual", "Measured", "FutureModel"]
    created_by_build_version: str
    source_description: str
    raw_source_logical_name: str
    normalization_policy_version: str


class CoordinateFrameSummary(ResourceModel):
    surface_z_meters: FiniteFloat
    vertical_axis: Literal["PositiveUp", "PositiveDown"]


class AxisSummary(ResourceModel):
    unit: Literal["Hz", "m"]
    count: PositiveUIntDecimal
    minimum: FiniteFloat
    maximum: FiniteFloat


class AxesSummary(ResourceModel):
    frequency: AxisSummary
    source_depth: AxisSummary
    receiver_depth: AxisSummary
    horizontal_range: AxisSummary


class ChecksumSummary(ResourceModel):
    algorithm: Literal["FNV1A64"]
    value: ChecksumHex


class EnvironmentResource(ResourceModel):
    environment_asset_id: StableId
    format: Literal["NS3_FACTORY_ACOUSTIC_FIELD"]
    package_format_version: PositiveUInt32Decimal
    asset_format_version: PositiveUInt32Decimal
    provenance: ProvenanceSummary
    coordinate_frame: CoordinateFrameSummary
    axes: AxesSummary
    cell_count: PositiveUIntDecimal
    signal_cell_count: UIntDecimal
    no_arrival_cell_count: UIntDecimal
    payload_bytes: PositiveUIntDecimal
    checksum: ChecksumSummary
    validation_state: Literal["Valid"]


class PositionSummary(ResourceModel):
    x_meters: FiniteFloat
    y_meters: FiniteFloat
    z_meters: FiniteFloat


class VelocitySummary(ResourceModel):
    x_meters_per_second: FiniteFloat
    y_meters_per_second: FiniteFloat
    z_meters_per_second: FiniteFloat


class NodeSummary(ResourceModel):
    node_id: UIntDecimal
    can_transmit: bool
    can_receive: bool
    duplex_mode: Literal["HalfDuplex", "FullDuplex"]
    initial_position: PositionSummary
    initial_velocity: VelocitySummary


class EnvironmentReferenceResource(ResourceModel):
    environment_asset_id: StableId
    asset_format_version: PositiveUInt32Decimal


class MobilitySummary(ResourceModel):
    model: Literal["ConstantVelocity"]


class ScenarioResource(ResourceModel):
    scenario_id: StableId
    version: PositiveUIntDecimal
    name: str
    nodes: tuple[NodeSummary, ...]
    environment: EnvironmentReferenceResource
    mobility: MobilitySummary
    fusion_center_node_id: UIntDecimal


class ScenarioReferenceResource(ResourceModel):
    scenario_id: StableId
    version: PositiveUIntDecimal


class RoutingSummary(ResourceModel):
    mode: Literal["DirectToFusionCenter"]


class MacSummary(ResourceModel):
    mode: Literal["Tdma"]
    slot_duration_ns: PositiveUIntDecimal
    guard_interval_ns: UIntDecimal


class PhySummary(ResourceModel):
    bit_rate_bits_per_second: PositiveUIntDecimal
    center_frequency_hz: FiniteFloat
    occupied_bandwidth_hz: FiniteFloat
    source_level_db_re_1upa_at_1m: FiniteFloat
    equivalent_noise_power_db_re_1upa2: FiniteFloat
    rx_quality_mode: Literal["None", "ModeledBpskAwgn"]


class FusionSummary(ResourceModel):
    workload: Literal["AcceptanceBearingFusion"]
    acceptance_profile: Literal["Acceptance4Node", "Extended6Node"]
    minimum_bearing_points: PositiveUIntDecimal
    maximum_fusion_period_ns: PositiveUIntDecimal
    maximum_ber: FiniteFloat


class ExperimentResource(ResourceModel):
    experiment_id: StableId
    version: PositiveUIntDecimal
    name: str
    scenario: ScenarioReferenceResource
    routing: RoutingSummary
    mac: MacSummary
    phy: PhySummary
    fusion: FusionSummary
    network_update_interval_cycles: PositiveUIntDecimal
    simulation_cycle_count: PositiveUIntDecimal
    deterministic_seed: UIntDecimal


class ResourceCatalogDocument(ResourceModel):
    schema_version: Literal[1]
    environments: tuple[EnvironmentResource, ...]
    scenarios: tuple[ScenarioResource, ...]
    experiments: tuple[ExperimentResource, ...]


class ResourceCatalogError(RuntimeError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


class ResourceCatalogConfigurationError(RuntimeError):
    pass


class ResourceCatalog:
    """Immutable, deterministic HTTP index over authoritative adapter output."""

    def __init__(self, document: ResourceCatalogDocument) -> None:
        self._environments = tuple(
            sorted(document.environments, key=lambda item: item.environment_asset_id)
        )
        self._scenarios = tuple(
            sorted(document.scenarios, key=lambda item: (item.scenario_id, int(item.version)))
        )
        self._experiments = tuple(
            sorted(
                document.experiments,
                key=lambda item: (item.experiment_id, int(item.version)),
            )
        )
        self._environment_by_id = {
            item.environment_asset_id: item for item in self._environments
        }
        self._scenario_by_key = {
            (item.scenario_id, item.version): item for item in self._scenarios
        }
        self._experiment_by_key = {
            (item.experiment_id, item.version): item for item in self._experiments
        }
        if len(self._environment_by_id) != len(self._environments):
            raise ResourceCatalogConfigurationError("duplicate EnvironmentAssetId")
        if len(self._scenario_by_key) != len(self._scenarios):
            raise ResourceCatalogConfigurationError("duplicate Scenario version")
        if len(self._experiment_by_key) != len(self._experiments):
            raise ResourceCatalogConfigurationError("duplicate Experiment version")
        self._validate_references()

    def _validate_references(self) -> None:
        for scenario in self._scenarios:
            environment = self._environment_by_id.get(
                scenario.environment.environment_asset_id
            )
            if (
                environment is None
                or environment.asset_format_version
                != scenario.environment.asset_format_version
            ):
                raise ResourceCatalogConfigurationError(
                    "Scenario references an unavailable Environment version"
                )
        for experiment in self._experiments:
            scenario = self._scenario_by_key.get(
                (experiment.scenario.scenario_id, experiment.scenario.version)
            )
            if scenario is None:
                raise ResourceCatalogConfigurationError(
                    "Experiment references an unavailable Scenario version"
                )
            if experiment.version != scenario.version:
                raise ResourceCatalogConfigurationError(
                    "Experiment and Scenario versions are incompatible with schema v1"
                )

    @classmethod
    def from_adapter(
        cls,
        executable: Path,
        repository_root: Path,
        acceptance_environment_asset_id: str,
    ) -> "ResourceCatalog":
        try:
            process = subprocess.run(
                [
                    str(executable),
                    str(repository_root),
                    acceptance_environment_asset_id,
                ],
                check=False,
                capture_output=True,
                timeout=30,
            )
        except (OSError, subprocess.SubprocessError) as error:
            raise ResourceCatalogConfigurationError(
                "resource catalog adapter could not be executed"
            ) from error
        if process.returncode != 0:
            raise ResourceCatalogConfigurationError(
                "resource catalog adapter rejected its repository input"
            )
        try:
            document = ResourceCatalogDocument.model_validate_json(process.stdout)
        except ValueError as error:
            raise ResourceCatalogConfigurationError(
                "resource catalog adapter produced an invalid schema v1 document"
            ) from error
        return cls(document)

    def list_environments(self) -> tuple[EnvironmentResource, ...]:
        return self._environments

    def list_scenarios(self) -> tuple[ScenarioResource, ...]:
        return self._scenarios

    def list_experiments(self) -> tuple[ExperimentResource, ...]:
        return self._experiments

    def get_environment(self, asset_id: str) -> EnvironmentResource:
        resource = self._environment_by_id.get(asset_id)
        if resource is None:
            raise ResourceCatalogError(
                "EnvironmentNotFound", "Environment resource was not found."
            )
        return resource

    def get_scenario(self, scenario_id: str, version: str) -> ScenarioResource:
        resource = self._scenario_by_key.get((scenario_id, version))
        if resource is not None:
            return resource
        if any(item.scenario_id == scenario_id for item in self._scenarios):
            raise ResourceCatalogError(
                "ScenarioVersionNotFound", "Scenario version was not found."
            )
        raise ResourceCatalogError("ScenarioNotFound", "Scenario was not found.")

    def get_experiment(
        self, experiment_id: str, version: str
    ) -> ExperimentResource:
        resource = self._experiment_by_key.get((experiment_id, version))
        if resource is not None:
            return resource
        if any(item.experiment_id == experiment_id for item in self._experiments):
            raise ResourceCatalogError(
                "ExperimentVersionNotFound", "Experiment version was not found."
            )
        raise ResourceCatalogError(
            "ExperimentNotFound", "Experiment was not found."
        )

    def resolve_command(
        self, run_id: str, experiment_id: str, experiment_version: str
    ) -> StartRunCommand:
        experiment = self.get_experiment(experiment_id, experiment_version)
        try:
            scenario = self.get_scenario(
                experiment.scenario.scenario_id, experiment.scenario.version
            )
            environment = self.get_environment(
                scenario.environment.environment_asset_id
            )
        except ResourceCatalogError as error:
            raise ResourceCatalogError(
                "InvalidReference",
                "Experiment resource references an unavailable catalog resource.",
            ) from error
        if (
            environment.asset_format_version
            != scenario.environment.asset_format_version
            or experiment.version != scenario.version
        ):
            raise ResourceCatalogError(
                "InvalidReference",
                "Experiment resource references an incompatible resource version.",
            )
        return StartRunCommand(
            run_id=run_id,
            preset=Preset(
                scenario_id=scenario.scenario_id,
                experiment_id=experiment.experiment_id,
                definition_version=experiment.version,
                acceptance_profile=experiment.fusion.acceptance_profile,
            ),
            environment=Environment(
                asset_id=environment.environment_asset_id,
                asset_format_version=environment.asset_format_version,
            ),
            execution=Execution(
                simulation_cycle_count=experiment.simulation_cycle_count,
                rx_quality_mode=experiment.phy.rx_quality_mode,
                equivalent_noise_power_db_re_1upa2=(
                    experiment.phy.equivalent_noise_power_db_re_1upa2
                ),
                deterministic_seed=experiment.deterministic_seed,
            ),
        )
