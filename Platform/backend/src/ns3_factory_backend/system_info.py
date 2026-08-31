from __future__ import annotations

from pydantic import BaseModel, ConfigDict


class MetadataModel(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True, frozen=True)


class BuildMetadata(MetadataModel):
    source_revision: str
    configuration: str
    cxx_standard: str


class SimulationMetadata(MetadataModel):
    engine: str
    version: str
    time_authority: str
    scheduler_authority: str
    scheduling_gateway: str


class InterfaceMetadata(MetadataModel):
    api_schema_version: str
    worker_wire_schema_version: str
    acceptance_evidence_schema_version: str
    frontend_release: str


class SystemInfo(MetadataModel):
    schema_version: int = 1
    platform_name: str
    platform_version: str
    product_baseline: str
    build: BuildMetadata
    simulation: SimulationMetadata
    interfaces: InterfaceMetadata
    runtime_mode: str


def make_system_info(
    *,
    source_revision: str,
    build_configuration: str,
    platform_version: str,
    frontend_release: str,
) -> SystemInfo:
    return SystemInfo(
        platform_name="NS3-BELLHOP Platform",
        platform_version=platform_version,
        product_baseline="P0-S5-02",
        build=BuildMetadata(
            source_revision=source_revision,
            configuration=build_configuration,
            cxx_standard="23",
        ),
        simulation=SimulationMetadata(
            engine="ns-3",
            version="3.47",
            time_authority="ns3::Simulator",
            scheduler_authority="ns3::Simulator",
            scheduling_gateway="M1 / Ns3KernelGateway",
        ),
        interfaces=InterfaceMetadata(
            api_schema_version="1",
            worker_wire_schema_version="1",
            acceptance_evidence_schema_version="1",
            frontend_release=frontend_release,
        ),
        runtime_mode="single-active-run-process-local-catalog",
    )
