from __future__ import annotations

import os
import asyncio
import importlib.util
import hashlib
import json
import subprocess
import tempfile
from contextlib import asynccontextmanager
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal

from fastapi import FastAPI, Header, Request, Response, status
from fastapi.exceptions import RequestValidationError
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, PlainTextResponse, StreamingResponse
from starlette.concurrency import run_in_threadpool
from pydantic import BaseModel, ConfigDict

from .catalog import (
    BackendFailure,
    CatalogError,
    FormalResultSnapshot,
    InMemoryRunCatalog,
    RunSnapshot,
)
from .gateway import WorkerGateway
from .resource_index import VerifiedResourceIndex, repository_stamp
from .algorithm_catalog import AlgorithmCatalog
from .resources import inspect_environment
from .acoustic_plots import file_sha256
from .acquisition_stream import acquisition_stream
from .ocean_cache import (store_hydrology_profile, list_hydrology_profiles,
                          get_hydrology_profile, woa_selection)
from .evidence import (
    AcceptanceEvidenceBundle,
    RunManifest,
    load_acceptance_baseline,
    make_evidence,
    make_manifest,
    render_evidence_text,
)
from .resources import (
    EnvironmentResource,
    ExperimentResource,
    ResourceCatalog,
    ResourceCatalogConfigurationError,
    ResourceCatalogError,
    ScenarioResource,
)
from .run_context import RunDefinitionContext, load_captured_definition, make_run_context
from .run_actions import RunActions, make_run_actions, validate_repeat_resources
from .wire import (
    Execution,
    PositiveUIntDecimal,
    AcceptanceReport,
    FusionResult,
    Int64Decimal,
    NodeResult,
    OverallStatus,
    RunProjection,
    RunResult,
    RunVisualization,
    StableId,
    UIntDecimal,
)
from .system_info import SystemInfo, make_system_info
from .authoring import (
    AuthoringStore,
    BuildRequest,
    DraftKind,
    DraftResource,
    DraftWrite,
    EnvironmentBuildResource,
    OceanAcquisitionRequest,
    TerrainAcquisitionRequest,
    TextImportRequest,
    acquire_terrain,
    acquire_woa,
    new_build,
    parse_two_column_text,
    validate_payload,
    _reference_data,
    reference_source,
)
from .trajectory_authoring import Asset as TrajectoryAsset, CsvRequest as TrajectoryCsvRequest, GenerateRequest as TrajectoryGenerateRequest, Store as TrajectoryStore, generate as generate_trajectory, import_csv as import_trajectory_csv
from .trajectory_derivation import TranslateTrajectoryRequest, translate_trajectory


@dataclass(frozen=True)
class BackendSettings:
    worker_executable: Path
    environment_repository_root: Path
    resource_catalog_adapter: Path | None = None
    acceptance_environment_asset_id: str = "backend-field-v1"
    acceptance_baseline_path: Path = field(
        default_factory=lambda: (
            Path(__file__).resolve().parents[3]
            / "acceptance"
            / "acceptance4_baseline_v1.json"
        )
    )
    source_revision: str = "unavailable"
    build_configuration: str = "ns3-on"
    platform_version: str = "0.1.0"
    frontend_release: str = "p0-s5-05"
    release_id: str = "P0-S5-05"
    build_target: str = "linux-x86_64"
    reference_environment_asset_id: str = "reference-shallow-water-v1"
    reference_environment_checksum: str = "fb64e543f9042c52"
    frontend_origin: str = "http://127.0.0.1:4173"
    authoring_workspace_root: Path | None = None
    authoring_reference_asset_root: Path | None = None
    trajectory_generator_executable: Path | None = None
    environment_asset_publisher_executable: Path | None = None


class HttpModel(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)


class CreateRunRequest(HttpModel):
    experiment_id: StableId
    experiment_version: PositiveUIntDecimal


class FailureResource(HttpModel):
    code: str
    message: str


class RunResource(HttpModel):
    run_id: str
    experiment_id: str
    experiment_version: str
    scenario_id: str
    scenario_version: str
    environment_asset_id: str
    environment_format_version: str
    lifecycle: Literal["Created", "Running", "Completed", "Failed"]
    simulation_started_at_ns: str | None
    simulation_ended_at_ns: str | None
    final_snapshot_version: str | None
    event_stream_complete: bool | None
    failure: FailureResource | None
    provenance: dict[str, object] | None = None


class RunSummaryResource(HttpModel):
    catalog_sequence: PositiveUIntDecimal
    run_id: StableId
    experiment_id: StableId
    experiment_version: PositiveUIntDecimal
    scenario_id: StableId
    scenario_version: PositiveUIntDecimal
    environment_asset_id: str
    environment_format_version: str
    lifecycle: Literal["Created", "Running", "Completed", "Failed"]
    event_stream_complete: bool | None
    result_available: bool
    failure: FailureResource | None


class HealthResource(HttpModel):
    status: Literal["ok", "degraded"]
    backend_alive: Literal[True]
    worker_executable_ready: bool


class ReadinessResource(HttpModel):
    status: Literal["ready", "not-ready"]
    backend_alive: Literal[True]
    worker_executable_ready: bool
    resource_catalog_ready: bool
    acceptance_baseline_ready: bool


class RunResultResource(HttpModel):
    run_id: str
    experiment_id: str
    experiment_version: str
    scenario_id: str
    scenario_version: str
    environment_asset_id: str
    environment_format_version: str
    projection: RunProjection
    acceptance_report: AcceptanceReport | None
    fusion_results: list[FusionResult]
    nodes: list[NodeResult]
    visualization: RunVisualization | None = None
    provenance: dict[str, object] | None = None


class ResultSummaryResource(HttpModel):
    catalog_sequence: PositiveUIntDecimal
    run_id: StableId
    experiment_id: StableId
    experiment_version: PositiveUIntDecimal
    scenario_id: StableId
    scenario_version: PositiveUIntDecimal
    environment_asset_id: str
    environment_format_version: str
    acceptance_overall: OverallStatus | None
    simulation_duration_ns: Int64Decimal
    fusion_result_count: UIntDecimal


class ApiErrorResource(HttpModel):
    code: str
    message: str


class ApiErrorEnvelope(HttpModel):
    error: ApiErrorResource


_ERROR_STATUS = {
    "RecordedChannelInvalid": status.HTTP_400_BAD_REQUEST,
    "RecordedChannelUnavailable": status.HTTP_409_CONFLICT,
    "RecordedNoiseUnavailable": status.HTTP_409_CONFLICT,
    "NotFound": status.HTTP_404_NOT_FOUND,
    "AlreadyExists": status.HTTP_409_CONFLICT,
    "InvalidRequest": status.HTTP_400_BAD_REQUEST,
    "RunNotReady": status.HTTP_409_CONFLICT,
    "RunFailed": status.HTTP_409_CONFLICT,
    "BackendBusy": status.HTTP_409_CONFLICT,
    "WorkerProtocolFailure": status.HTTP_502_BAD_GATEWAY,
    "WorkerProcessFailure": status.HTTP_502_BAD_GATEWAY,
    "EnvironmentNotFound": status.HTTP_404_NOT_FOUND,
    "ScenarioNotFound": status.HTTP_404_NOT_FOUND,
    "ScenarioVersionNotFound": status.HTTP_404_NOT_FOUND,
    "ExperimentNotFound": status.HTTP_404_NOT_FOUND,
    "ExperimentVersionNotFound": status.HTTP_404_NOT_FOUND,
    "InvalidReference": status.HTTP_409_CONFLICT,
    "AlgorithmUnavailable": status.HTTP_409_CONFLICT,
    "AlgorithmConfigurationInvalid": status.HTTP_422_UNPROCESSABLE_CONTENT,
    "EvidenceUnavailable": status.HTTP_409_CONFLICT,
    "DraftNotFound": status.HTTP_404_NOT_FOUND,
    "HydrologyCacheNotFound": status.HTTP_404_NOT_FOUND,
    "InvalidHydrologyCache": status.HTTP_422_UNPROCESSABLE_ENTITY,
    "TerrainFeatureFrozen": status.HTTP_410_GONE,
    "BuildNotFound": status.HTTP_404_NOT_FOUND,
    "DraftConflict": status.HTTP_409_CONFLICT,
    "InvalidDraft": status.HTTP_400_BAD_REQUEST,
    "DraftValidationFailed": status.HTTP_400_BAD_REQUEST,
    "BuildNotReady": status.HTTP_409_CONFLICT,
    "ImmutableResourceConflict": status.HTTP_409_CONFLICT,
    "TrajectoryAuthoringFailed": status.HTTP_400_BAD_REQUEST,
    "EnvironmentNotFound": status.HTTP_404_NOT_FOUND,
    "TrajectoryNotFound": status.HTTP_404_NOT_FOUND,
    "TrajectoryTrackNotFound": status.HTTP_404_NOT_FOUND,
    "RunDefinitionUnavailable": status.HTTP_409_CONFLICT,
    "TrajectoryResourceInvalid": status.HTTP_409_CONFLICT,
    "TrajectoryPlacementInvalid": status.HTTP_400_BAD_REQUEST,
    "SceneDomainViolation": status.HTTP_400_BAD_REQUEST,
    "EnvironmentUnavailable": status.HTTP_503_SERVICE_UNAVAILABLE,
}


def _error_response(code: str, message: str) -> JSONResponse:
    body = ApiErrorEnvelope(error=ApiErrorResource(code=code, message=message))
    return JSONResponse(
        status_code=_ERROR_STATUS.get(code, status.HTTP_500_INTERNAL_SERVER_ERROR),
        content=body.model_dump(),
    )


def _run_resource(snapshot: RunSnapshot) -> RunResource:
    terminal = snapshot.terminal_run
    failure: BackendFailure | None = snapshot.failure
    if failure is None and terminal is not None and terminal.failure is not None:
        failure = BackendFailure(
            code="RunFailed", message=terminal.failure.message
        )
    provenance = None
    if snapshot.authored_definition_path:
        try:
            definition = load_captured_definition(snapshot)
            provenance = {
                "definition_sha256": snapshot.authored_definition_sha256,
                "experiment": {"id": definition["experiment_id"], "version": definition["version"]},
                "scenario": {"id": definition["scenario_id"], "version": definition["version"]},
                "environment": definition["environment"],
                "physical_layer": definition["experiment"]["physical_layer"],
                "deterministic_seed": definition["experiment"]["deterministic_seed"],
                **({"recorded_channel": definition["recorded_channel"]}
                   if "recorded_channel" in definition else {}),
                **({"modem": {"modulation": definition["experiment"].get("waveform_modulation"),
                              "osdm": definition["experiment"].get("osdm")}}
                   if definition["experiment"]["physical_layer"] != "Channel1Abstract" else {}),
                **({"mac_algorithm": definition["experiment"]["mac_algorithm"]}
                   if "mac_algorithm" in definition["experiment"] else {}),
                "trajectory_bindings": definition["trajectory_bindings"],
                "trajectory_assets": [{"asset_id": item["asset_id"], "version": item["version"],
                    "checksum_sha256": item["checksum_sha256"]} for item in definition["trajectory_assets"]],
            }
        except (CatalogError, KeyError, ValueError, TypeError):
            provenance = {"definition_sha256": snapshot.authored_definition_sha256,
                          "state": "DefinitionUnavailable"}
    return RunResource(
        run_id=snapshot.run_id,
        experiment_id=snapshot.experiment_id,
        experiment_version=snapshot.experiment_version,
        scenario_id=snapshot.scenario_id,
        scenario_version=snapshot.scenario_version,
        environment_asset_id=snapshot.environment_asset_id,
        environment_format_version=snapshot.environment_format_version,
        lifecycle=snapshot.lifecycle,
        simulation_started_at_ns=(
            terminal.simulation_started_at_ns if terminal else None
        ),
        simulation_ended_at_ns=(
            terminal.simulation_ended_at_ns if terminal else None
        ),
        final_snapshot_version=(
            terminal.final_snapshot_version if terminal else None
        ),
        event_stream_complete=snapshot.event_stream_complete,
        failure=(
            FailureResource(code=failure.code, message=failure.message)
            if failure is not None
            else None
        ),
        provenance=provenance,
    )


def _run_summary(snapshot: RunSnapshot) -> RunSummaryResource:
    failure = snapshot.failure
    return RunSummaryResource(
        catalog_sequence=str(snapshot.catalog_sequence),
        run_id=snapshot.run_id,
        experiment_id=snapshot.experiment_id,
        experiment_version=snapshot.experiment_version,
        scenario_id=snapshot.scenario_id,
        scenario_version=snapshot.scenario_version,
        environment_asset_id=snapshot.environment_asset_id,
        environment_format_version=snapshot.environment_format_version,
        lifecycle=snapshot.lifecycle,
        event_stream_complete=snapshot.event_stream_complete,
        result_available=snapshot.result_available,
        failure=(
            FailureResource(code=failure.code, message=failure.message)
            if failure is not None
            else None
        ),
    )


def _result_summary(snapshot: FormalResultSnapshot) -> ResultSummaryResource:
    report = snapshot.result.acceptance_report
    return ResultSummaryResource(
        catalog_sequence=str(snapshot.run.catalog_sequence),
        run_id=snapshot.run.run_id,
        experiment_id=snapshot.run.experiment_id,
        experiment_version=snapshot.run.experiment_version,
        scenario_id=snapshot.run.scenario_id,
        scenario_version=snapshot.run.scenario_version,
        environment_asset_id=snapshot.run.environment_asset_id,
        environment_format_version=snapshot.run.environment_format_version,
        acceptance_overall=report.overall if report is not None else None,
        simulation_duration_ns=snapshot.result.projection.simulation_duration_ns,
        fusion_result_count=str(len(snapshot.result.fusion_results)),
    )


def _cursor(text: str | None) -> int:
    if text is None:
        return 0
    if (
        not text
        or not text.isascii()
        or not text.isdecimal()
        or (len(text) > 1 and text[0] == "0")
    ):
        raise CatalogError(
            "InvalidRequest", "Last-Event-ID must be canonical nonnegative decimal."
        )
    value = int(text)
    if value > (1 << 64) - 1:
        raise CatalogError("InvalidRequest", "Last-Event-ID is out of range.")
    return value


def _resource_version(text: str) -> str:
    if (
        not text
        or not text.isascii()
        or not text.isdecimal()
        or text == "0"
        or (len(text) > 1 and text[0] == "0")
        or int(text) > (1 << 64) - 1
    ):
        raise CatalogError(
            "InvalidRequest", "Resource version must be canonical positive decimal."
        )
    return text


def _environment_location(payload: dict) -> dict:
    """File imports do not inherit the WOA query form's default location."""
    source = payload.get("sources", {}).get("ssp", {})
    acquisition = payload.get("acquisition", {})
    if source.get("source") == "file" or (source.get("filename") and not source.get("request")):
        acquisition = {}
    return {"latitude": acquisition.get("latitudeDeg"),
            "longitude": acquisition.get("longitudeDeg"), "month": acquisition.get("month")}


def create_app(
    settings: BackendSettings,
    *,
    gateway: object | None = None,
    id_factory: Callable[[], str] | None = None,
    resource_catalog: ResourceCatalog | None = None,
) -> FastAPI:
    # NetCDF/HDF5 is not thread-safe. Serialize acquisition per application,
    # without blocking the event loop or sharing a mutable global resource.
    ocean_acquisition_lock = asyncio.Lock()
    algorithms = AlgorithmCatalog(settings.worker_executable)
    selected_gateway = gateway or WorkerGateway(
        settings.worker_executable, settings.environment_repository_root
    )
    catalog = InMemoryRunCatalog(selected_gateway, id_factory=id_factory)
    selected_resources = resource_catalog
    initial_stamp = (repository_stamp(settings.environment_repository_root)
                     if settings.resource_catalog_adapter is not None else None)
    if selected_resources is None and settings.resource_catalog_adapter is None:
        raise ResourceCatalogConfigurationError(
            "resource catalog adapter must be configured"
        )
    if selected_resources is None:
        assert settings.resource_catalog_adapter is not None
        selected_resources = ResourceCatalog.from_adapter(
            settings.resource_catalog_adapter,
            settings.environment_repository_root,
            settings.acceptance_environment_asset_id,
        )
    if initial_stamp is not None and initial_stamp != repository_stamp(settings.environment_repository_root):
        raise ResourceCatalogConfigurationError("environment repository changed during startup validation")
    resource_index = (VerifiedResourceIndex(
        settings.environment_repository_root, selected_resources, initial_stamp,
        lambda: ResourceCatalog.from_adapter(settings.resource_catalog_adapter,
            settings.environment_repository_root, settings.acceptance_environment_asset_id))
        if initial_stamp is not None else None)
    resource_io_lock = resource_index.lock if resource_index else asyncio.Lock()
    environment_publication_lock = asyncio.Lock()
    acceptance_baseline = load_acceptance_baseline(
        settings.acceptance_baseline_path
    )
    system_info = make_system_info(
        source_revision=settings.source_revision,
        build_configuration=settings.build_configuration,
        platform_version=settings.platform_version,
        frontend_release=settings.frontend_release,
        release_id=settings.release_id,
        build_target=settings.build_target,
        reference_environment_asset_id=settings.reference_environment_asset_id,
        reference_environment_checksum=settings.reference_environment_checksum,
    )
    run_manifests: dict[str, RunManifest] = {}
    # Observation-only command snapshots, owned by this app just like its Run
    # catalog. In particular a preset's PHY override must not be lost on read.
    run_executions: dict[str, Execution] = {}
    authoring_root = settings.authoring_workspace_root or Path(
        tempfile.mkdtemp(prefix="ns3-platform-authoring-")
    )
    authoring = AuthoringStore(authoring_root)
    trajectories = TrajectoryStore(authoring_root / "published" / "trajectories")
    reference_asset_root = settings.authoring_reference_asset_root or (
        Path(__file__).resolve().parents[3]
        / "environment"
        / "assets"
        / "reference_shallow_water_v1"
    )

    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        try:
            yield
        finally:
            await catalog.shutdown()

    app = FastAPI(
        title="NS3-BELLHOP Platform Run API", version="1", lifespan=lifespan
    )
    app.add_middleware(
        CORSMiddleware,
        allow_origins=[settings.frontend_origin],
        allow_methods=["GET", "POST", "PUT", "DELETE"],
        allow_headers=["Content-Type", "Last-Event-ID", "If-Match"],
    )
    app.state.catalog = catalog
    app.state.resource_catalog = selected_resources
    app.state.system_info = system_info
    app.state.acceptance_baseline = acceptance_baseline
    app.state.run_manifests = run_manifests

    async def current_resources() -> ResourceCatalog:
        return await resource_index.current() if resource_index else selected_resources

    async def verified_environment(asset_id: str) -> EnvironmentResource:
        if settings.resource_catalog_adapter is None:
            return selected_resources.get_environment(asset_id)
        async with resource_io_lock:
            return await run_in_threadpool(inspect_environment,
                settings.resource_catalog_adapter, settings.environment_repository_root, asset_id)

    @app.exception_handler(ResourceCatalogConfigurationError)
    async def resource_configuration_error_handler(_request: Request, _error: ResourceCatalogConfigurationError) -> JSONResponse:
        return _error_response("EnvironmentUnavailable",
            "环境资源校验未通过或正在变更，请检查该环境后重试；未使用替代环境。")

    @app.exception_handler(CatalogError)
    async def catalog_error_handler(
        _request: Request, error: CatalogError
    ) -> JSONResponse:
        return _error_response(error.code, str(error))

    @app.exception_handler(RequestValidationError)
    async def validation_error_handler(
        _request: Request, _error: RequestValidationError
    ) -> JSONResponse:
        return _error_response(
            "InvalidRequest", "The request does not match the P0 Run API schema."
        )

    @app.exception_handler(ResourceCatalogError)
    async def resource_error_handler(
        _request: Request, error: ResourceCatalogError
    ) -> JSONResponse:
        return _error_response(error.code, str(error))

    @app.get("/health", response_model=HealthResource)
    async def health() -> HealthResource:
        ready = (
            settings.worker_executable.is_file()
            and os.access(settings.worker_executable, os.X_OK)
        )
        return HealthResource(
            status="ok" if ready else "degraded",
            backend_alive=True,
            worker_executable_ready=ready,
        )

    @app.get("/ready", response_model=ReadinessResource)
    async def readiness() -> ReadinessResource:
        worker_ready = (
            settings.worker_executable.is_file()
            and os.access(settings.worker_executable, os.X_OK)
        )
        ready = worker_ready and bool(selected_resources.list_experiments())
        return ReadinessResource(
            status="ready" if ready else "not-ready",
            backend_alive=True,
            worker_executable_ready=worker_ready,
            resource_catalog_ready=bool(selected_resources.list_experiments()),
            acceptance_baseline_ready=True,
        )

    @app.get("/system/info", response_model=SystemInfo)
    async def get_system_info() -> SystemInfo:
        return system_info

    @app.get("/trajectories", response_model=list[TrajectoryAsset])
    async def list_trajectories() -> list[TrajectoryAsset]:
        from .scenario_trajectories import internal_placement
        return [asset for asset in trajectories.list() if not internal_placement(asset)]

    @app.get("/authoring/capabilities")
    async def authoring_capabilities():
        return {"relative_trajectory_placement": True, "exact_run_repeat": True, "scene_domain_validation": True}

    @app.get("/algorithms")
    async def algorithm_catalog():
        return await algorithms.current()

    @app.post("/authoring/trajectories/generate", response_model=TrajectoryAsset, status_code=201)
    async def create_generated_trajectory(request: TrajectoryGenerateRequest) -> TrajectoryAsset:
        try:
            return trajectories.save(generate_trajectory(
                request, settings.trajectory_generator_executable
            ))
        except (ValueError, FileExistsError) as error:
            raise CatalogError("TrajectoryAuthoringFailed", str(error)) from error

    @app.post("/authoring/trajectories/import-csv", response_model=TrajectoryAsset, status_code=201)
    async def create_imported_trajectory(request: TrajectoryCsvRequest) -> TrajectoryAsset:
        try:
            return trajectories.save(import_trajectory_csv(request))
        except (ValueError, FileExistsError) as error:
            raise CatalogError("TrajectoryAuthoringFailed", str(error)) from error

    @app.post("/authoring/trajectories/translate", response_model=TrajectoryAsset, status_code=201)
    async def create_translated_trajectory(request: TranslateTrajectoryRequest) -> TrajectoryAsset:
        return translate_trajectory(request, trajectories)

    @app.get("/authoring/drafts", response_model=list[DraftResource])
    async def list_drafts(kind: DraftKind | None = None) -> list[DraftResource]:
        return authoring.list(kind)

    @app.post("/authoring/drafts", response_model=DraftResource, status_code=201)
    async def create_draft(write: DraftWrite) -> DraftResource:
        if write.kind == "experiment":
            await algorithms.freeze(write.payload)
        return authoring.create(write)

    @app.post("/authoring/drafts/{kind}/{draft_id}/publish")
    async def publish_draft(kind: DraftKind, draft_id: str) -> dict[str, object]:
        try:
            return authoring.publish_draft(kind, draft_id)
        except KeyError as error:
            raise CatalogError("DraftNotFound", "Draft was not found.") from error
        except ValueError as error:
            raise CatalogError("DraftValidationFailed", str(error)) from error

    @app.get("/authoring/drafts/{kind}/{draft_id}", response_model=DraftResource)
    async def get_draft(kind: DraftKind, draft_id: str) -> DraftResource:
        try:
            return authoring.get(kind, draft_id)
        except KeyError as error:
            raise CatalogError("DraftNotFound", "Authoring draft was not found.") from error

    @app.put("/authoring/drafts/{kind}/{draft_id}", response_model=DraftResource)
    async def update_draft(kind: DraftKind, draft_id: str, write: DraftWrite,
                           if_match: str | None = Header(default=None, alias="If-Match")) -> DraftResource:
        if kind == "experiment":
            await algorithms.freeze(write.payload)
        try:
            revision = int(if_match.strip('"')) if if_match else None
            return authoring.update(kind, draft_id, write, revision)
        except KeyError as error:
            raise CatalogError("DraftNotFound", "Authoring draft was not found.") from error
        except RuntimeError as error:
            raise CatalogError("DraftConflict", str(error)) from error
        except (ValueError, TypeError) as error:
            raise CatalogError("InvalidDraft", str(error)) from error

    @app.delete("/authoring/drafts/{kind}/{draft_id}", status_code=204)
    async def delete_draft(kind: DraftKind, draft_id: str) -> Response:
        try:
            authoring.delete(kind, draft_id)
        except KeyError as error:
            raise CatalogError("DraftNotFound", "Authoring draft was not found.") from error
        return Response(status_code=204)

    @app.post("/authoring/drafts/{kind}/{draft_id}/validate")
    async def validate_draft(kind: DraftKind, draft_id: str) -> dict[str, object]:
        try:
            draft = authoring.get(kind, draft_id)
        except KeyError as error:
            raise CatalogError("DraftNotFound", "Authoring draft was not found.") from error
        issues = validate_payload(kind, draft.payload)
        if kind == "experiment":
            try:
                await algorithms.freeze(draft.payload)
            except CatalogError as error:
                issues.append(str(error))
        return {"valid": not issues, "issues": issues, "revision": draft.revision}

    @app.post("/authoring/import/ssp")
    async def import_ssp(request: TextImportRequest) -> dict[str, object]:
        try:
            points = parse_two_column_text(request.content, "ssp")
        except ValueError as error:
            raise CatalogError("InvalidHydrologyFile", str(error)) from error
        result = {"source": "file", "filename": request.filename,
                  "point_count": len(points), "sound_speed_profile": points}
        record = store_hydrology_profile(authoring_root / "cache" / "woa23" / "profiles", result)
        return {**result, "cache_id": record["cache_id"]}

    @app.get("/authoring/environment/hydrology-cache")
    async def hydrology_cache() -> list[dict]:
        root = authoring_root / "cache" / "woa23" / "profiles"
        # Migrate only known saved inputs, never scan arbitrary directories or
        # invent a coordinate for an imported profile. Originals stay intact.
        for draft in authoring.list("environment"):
            payload = draft.payload
            points = payload.get("sound_speed_profile")
            if not points:
                continue
            sources = payload.get("sources")
            source = sources.get("ssp") if isinstance(sources, dict) else None
            metadata = dict(source) if isinstance(source, dict) else {}
            request = metadata.pop("request", None)
            metadata.setdefault("source", "saved-draft")
            if metadata["source"] == "saved-draft":
                metadata["filename"] = draft.name
            try:
                selection = woa_selection(request) if isinstance(request, dict) and metadata["source"] == "online-real-data" else None
                store_hydrology_profile(root, {**metadata, "sound_speed_profile": points}, selection=selection)
            except (ValueError, TypeError, KeyError):
                # Incomplete legacy drafts are not offered as valid profiles.
                continue
        return list_hydrology_profiles(root)

    @app.get("/authoring/environment/hydrology-cache/{cache_id}")
    async def hydrology_cache_item(cache_id: str) -> dict:
        try:
            return get_hydrology_profile(authoring_root / "cache" / "woa23" / "profiles", cache_id)
        except KeyError as error:
            raise CatalogError("HydrologyCacheNotFound", "所选水文数据不存在，请刷新列表。") from error
        except ValueError as error:
            raise CatalogError("InvalidHydrologyCache", str(error)) from error

    @app.post("/authoring/import/terrain")
    async def import_terrain(request: TextImportRequest) -> dict[str, object]:
        raise CatalogError("TerrainFeatureFrozen", "地形导入已冻结；已有环境及历史运行输入不受影响。")

    @app.post("/authoring/acquisition/woa")
    async def fetch_woa(request: OceanAcquisitionRequest) -> dict[str, object]:
        try:
            async with ocean_acquisition_lock:
                return await run_in_threadpool(acquire_woa, request, authoring_root / "cache" / "woa23", reference_asset_root)
        except (ValueError, RuntimeError) as error:
            raise CatalogError("HydrologyAcquisitionFailed", str(error)) from error

    @app.post("/authoring/acquisition/woa/stream")
    async def stream_woa(request: OceanAcquisitionRequest) -> StreamingResponse:
        return StreamingResponse(
            acquisition_stream(ocean_acquisition_lock, lambda report: acquire_woa(
                request, authoring_root / "cache" / "woa23", reference_asset_root, report)),
            media_type="application/x-ndjson",
            headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
        )

    @app.post("/authoring/acquisition/terrain")
    async def fetch_terrain(request: TerrainAcquisitionRequest) -> dict[str, object]:
        raise CatalogError("TerrainFeatureFrozen", "地形获取已冻结；新环境请明确选择声学计算边界。")

    @app.post("/authoring/environment-builds", response_model=EnvironmentBuildResource, status_code=202)
    async def create_environment_build(request: BuildRequest) -> EnvironmentBuildResource:
        try:
            effective = request if request.reference_asset_root else request.model_copy(
                update={"reference_asset_root": str(reference_asset_root)})
            return await run_in_threadpool(new_build, authoring, effective)
        except KeyError as error:
            raise CatalogError("DraftNotFound", "Environment draft was not found.") from error
        except RuntimeError as error:
            raise CatalogError("DraftConflict", str(error)) from error

    @app.get("/authoring/environment-builds/{build_id}", response_model=EnvironmentBuildResource)
    async def get_environment_build(build_id: str) -> EnvironmentBuildResource:
        try:
            return authoring.get_build(build_id)
        except KeyError as error:
            raise CatalogError("BuildNotFound", "Environment build was not found.") from error

    @app.get("/authoring/environment-capabilities")
    async def environment_capabilities() -> dict[str, object]:
        missing = [name for name in ("numpy", "netCDF4") if importlib.util.find_spec(name) is None]
        executable = os.environ.get("PLATFORM_BELLHOP_EXECUTABLE", "")
        return {"woa_online_available": not missing, "terrain_authoring_available": False,
                "hydrology_cache_available": True,
                "woa_unavailable_reason": "在线水文获取尚未配置科学计算依赖；可导入文件或明确选择内置参考案例" if missing else None,
                "bellhop_available": bool(executable and Path(executable).is_file() and os.access(executable, os.X_OK))}

    def publish_environment_blocking(build_id: str) -> dict[str, object]:
        try:
            build = authoring.get_build(build_id)
            if build.state != "Completed" or not build.artifact:
                raise ValueError("请先完成环境计算")
            artifact = build.artifact
            if file_sha256(Path(str(artifact["arrivals_path"]))) != artifact["arrivals_sha256"]:
                raise ValueError("环境计算结果已发生变化，不能发布")
            asset_id = str(artifact["environment_asset_id"])
            target = settings.environment_repository_root / asset_id
            if not target.is_dir():
                publisher = settings.environment_asset_publisher_executable
                if publisher is None or not publisher.is_file():
                    raise ValueError("Environment asset publisher is not configured")
                settings.environment_repository_root.mkdir(parents=True, exist_ok=True)
                completed = subprocess.run(
                    [str(publisher), str(artifact["arrivals_path"]),
                     str(settings.environment_repository_root), asset_id],
                    capture_output=True, text=True, timeout=180, check=False)
                if completed.returncode != 0:
                    raise ValueError(completed.stderr.strip() or "Environment publication failed")
            # A pre-existing directory is not proof of a valid publication.
            # Refresh atomically before making the authoring publication visible.
            if resource_index:
                resource_index.refresh_locked().get_environment(asset_id)
            artifact = authoring.publish_build(build_id)
            return {**artifact, "published": True,
                    "runtime_catalog_available": True}
        except KeyError as error:
            raise CatalogError("BuildNotFound", "Environment build was not found.") from error
        except FileExistsError as error:
            raise CatalogError("ImmutableResourceConflict", str(error)) from error
        except (ValueError, OSError, subprocess.SubprocessError) as error:
            raise CatalogError("BuildNotReady", str(error)) from error

    @app.post("/authoring/environment-builds/{build_id}/publish")
    async def publish_environment_build(build_id: str) -> dict[str, object]:
        if environment_publication_lock.locked():
            raise CatalogError("BackendBusy", "正在发布环境，请等待当前发布完成后重试。")
        async with environment_publication_lock:
            async with resource_io_lock:
                return await run_in_threadpool(publish_environment_blocking, build_id)

    @app.post(
        "/runs",
        response_model=RunResource,
        status_code=status.HTTP_201_CREATED,
    )
    async def create_run(request: CreateRunRequest) -> RunResource:
        experiment = selected_resources.get_experiment(
            request.experiment_id, request.experiment_version
        )
        scenario = selected_resources.get_scenario(
            experiment.scenario.scenario_id, experiment.scenario.version
        )
        environment = await verified_environment(
            scenario.environment.environment_asset_id
        )

        def command(run_id: str):
            captured = selected_resources.resolve_command(
                run_id, request.experiment_id, request.experiment_version
            )
            run_executions[run_id] = captured.execution.model_copy(deep=True)
            return captured

        snapshot = await catalog.create(command)
        run_manifests[snapshot.run_id] = make_manifest(
            snapshot.run_id, system_info, environment, scenario, experiment
        )
        return _run_resource(snapshot)

    @app.post(
        "/authoring/experiments/{draft_id}/runs",
        response_model=RunResource,
        status_code=status.HTTP_201_CREATED,
    )
    async def create_authoring_experiment_run(draft_id: str, if_match: str | None = Header(default=None)) -> RunResource:
        """Freeze and run one exact authored Experiment/Scenario composition."""
        try:
            draft = authoring.get("experiment", draft_id)
        except KeyError as error:
            raise CatalogError("DraftNotFound", "Experiment draft was not found.") from error
        if if_match is not None and if_match != str(draft.revision):
            raise CatalogError("DraftConflict", "实验已被其他编辑更新，请重新保存并运行。")
        if draft.validation_issues:
            raise CatalogError("DraftValidationFailed", "; ".join(draft.validation_issues))
        payload = draft.payload
        planning = payload.get("execution_mode") == "PlanningV1"
        frozen_algorithm = await algorithms.freeze(payload)
        flow_scoped = frozen_algorithm is not None and frozen_algorithm["interface"] == "planning-mac-v2"
        current_issues = validate_payload("experiment", payload)
        if current_issues:
            raise CatalogError("DraftValidationFailed", "; ".join(current_issues))
        scenario_id = str(payload.get("scenario_id", ""))
        try:
            scenario_draft = authoring.get("scenario", scenario_id)
        except KeyError as error:
            if planning:
                raise CatalogError("InvalidReference", "规划执行需要绑定具有正式轨迹的场景草稿，请先从预置案例派生场景。") from error
            # A named catalog Scenario is an explicit preset selection, not a
            # fallback. Preserve that compatibility path while authored
            # Scenario IDs are always resolved only from the authoring store.
            candidates = [item for item in selected_resources.list_experiments()
                          if item.scenario.scenario_id == scenario_id]
            if not candidates:
                raise CatalogError(
                    "InvalidReference", "Experiment references an unavailable Scenario."
                ) from error
            experiment = max(candidates, key=lambda item: int(item.version))
            scenario = selected_resources.get_scenario(
                experiment.scenario.scenario_id, experiment.scenario.version)
            environment = await verified_environment(
                scenario.environment.environment_asset_id)
            physical_layer = str(payload.get("physical_layer", "Channel1Abstract"))
            modulation = str(payload.get("waveform_modulation", "Bpsk"))
            noise_model = str(payload.get("waveform_noise", "None"))
            def preset_command(run_id: str):
                base = selected_resources.resolve_command(
                    run_id, experiment.experiment_id, experiment.version)
                captured = base.model_copy(update={"execution": base.execution.model_copy(update={
                    "simulation_cycle_count": str(payload["simulation_cycle_count"]),
                    "deterministic_seed": str(payload["deterministic_seed"]),
                    "rx_quality_mode": "None" if physical_layer == "Channel2Waveform" else experiment.phy.rx_quality_mode,
                    "physical_layer": physical_layer,
                    "waveform_modulation": modulation,
                    "waveform_noise": noise_model,
                    "waveform_noise_psd_db_re_1upa2_per_hz": float(payload.get("waveform_noise_psd_db_re_1upa2_per_hz", 0.0)),
                })})
                run_executions[run_id] = captured.execution.model_copy(deep=True)
                return captured
            snapshot = await catalog.create(preset_command)
            run_manifests[snapshot.run_id] = make_manifest(
                snapshot.run_id, system_info, environment, scenario, experiment)
            return _run_resource(snapshot)
        if payload.get("scenario_revision") is not None and payload["scenario_revision"] != scenario_draft.revision:
            raise CatalogError("DraftConflict", "节点场景已被其他编辑更新，请重新保存当前实验后运行。")
        if scenario_draft.validation_issues:
            raise CatalogError("InvalidReference", "; ".join(scenario_draft.validation_issues))
        scenario_payload = scenario_draft.payload
        recorded_mode = payload.get("physical_layer") == "Channel3Recorded"
        environment_id = str(scenario_payload.get("environment_asset_id", ""))
        environment_version = int(scenario_payload.get("environment_version", 1))
        environment_path = settings.environment_repository_root / environment_id
        if not recorded_mode and not environment_path.is_dir():
            raise CatalogError(
                "EnvironmentNotFound",
                f"Environment {environment_id} v{environment_version} is not in the Runtime repository.",
            )
        environment_checksum = None
        environment_resource = None
        if not recorded_mode and settings.resource_catalog_adapter is not None:
            try:
                environment_resource = await verified_environment(environment_id)
            except ResourceCatalogError as error:
                raise CatalogError("EnvironmentNotFound", "Environment package is unavailable or corrupt.") from error
            if int(environment_resource.asset_format_version) != environment_version:
                raise CatalogError("InvalidReference", "Environment asset format version does not match Scenario.")
            environment_checksum = environment_resource.checksum.value
        physical_layer = str(payload.get("physical_layer", "Channel1Abstract"))
        if physical_layer not in {"Channel1Abstract", "Channel2Waveform", "Channel3Recorded"}:
            raise CatalogError("InvalidDraft", "Unknown physical layer implementation.")
        modulation = str(payload.get("waveform_modulation", "Bpsk"))
        noise_model = str(payload.get("waveform_noise", "None"))
        if modulation not in {"Bpsk", "Bfsk", "Osdm"} or noise_model not in {"None", "Awgn"}:
            raise CatalogError("InvalidDraft", "Unknown waveform configuration.")

        bindings = scenario_payload.get("trajectory_bindings")
        if not isinstance(bindings, list) or not bindings:
            raise CatalogError("InvalidReference", "Every authored node requires a trajectory binding.")
        nodes = scenario_payload.get("nodes", [])
        if planning:
            from .planning_authoring import planning_issues
            issues = planning_issues(payload, nodes)
            if issues:
                raise CatalogError("DraftValidationFailed", "; ".join(issues))
        node_ids = {str(node.get("node_id")) for node in nodes if isinstance(node, dict)}
        if {str(binding.get("node_id")) for binding in bindings if isinstance(binding, dict)} != node_ids:
            raise CatalogError("InvalidReference", "Trajectory bindings must cover every Scenario node exactly.")
        from .scenario_trajectories import compile_bindings
        bindings, assets = compile_bindings(nodes, bindings, trajectories)
        recorded_config = None
        if recorded_mode:
            from .recorded_channels import freeze
            recorded_config = await run_in_threadpool(freeze, authoring_root / "recorded-channels",
                payload, scenario_payload, nodes, assets)
            environment_checksum = recorded_config["manifest_sha256"]
        fusion_center = scenario_payload.get("fusion_center_node_id")
        if fusion_center is None and not planning:
            raise CatalogError("InvalidReference", "旧版执行场景必须保留融合中心；无中心通信请明确使用 PlanningV1 和业务流。")
        if fusion_center is not None:
            if planning and (not isinstance(fusion_center, str) or fusion_center not in node_ids):
                raise CatalogError("InvalidReference", "场景兼容融合中心必须引用已有节点或为空。")
            try:
                fusion_center = int(fusion_center)
            except (TypeError, ValueError) as error:
                raise CatalogError("InvalidReference", "场景兼容融合中心编号无效。") from error
        spatial_domain = None
        if planning or environment_resource is not None or scenario_payload.get("spatial_domain") is not None:
            from .scene_domain import validate_paths
            if environment_resource is None and scenario_payload.get("spatial_domain") is not None:
                environment_resource = (await current_resources()).get_environment(environment_id)
            duration_ns = (int(payload["simulation_cycle_count"]) * int(payload["planning_cycle_duration_ns"])
                           if planning else int(payload["simulation_cycle_count"]) * int(payload.get("slot_duration_ns", 6_000_000_000)) * max(0, len(nodes) - 1))
            spatial_domain = validate_paths(nodes, bindings, assets, environment_resource,
                await environment_authoring_inputs(environment_id) if environment_resource is not None else None,
                scenario_payload.get("spatial_domain"), duration_ns)
        scenario_publication = authoring.publish_draft("scenario", scenario_draft.draft_id)
        experiment_publication = authoring.publish_draft("experiment", draft.draft_id)
        definition_version = max(draft.revision, scenario_draft.revision)
        definition = {
            "schema": "ns3-factory-authored-run-v4" if recorded_mode else "ns3-factory-authored-run-v3" if flow_scoped else "ns3-factory-authored-run-v2" if planning else "ns3-factory-authored-run-v1",
            "scenario_id": scenario_publication["published_id"],
            "scenario_source_revision": scenario_draft.revision,
            "scenario_name": scenario_draft.name,
            "spatial_domain": spatial_domain,
            "experiment_id": experiment_publication["published_id"],
            "experiment_source_revision": draft.revision,
            "version": definition_version,
            "environment": {"asset_id": environment_id, "format_version": environment_version,
                            "checksum": environment_checksum},
            "fusion_center_node_id": fusion_center,
            "nodes": [{
                "node_id": int(node["node_id"]),
                "can_transmit": bool(node["can_transmit"]),
                "can_receive": bool(node["can_receive"]),
                "duplex_mode": str(node["duplex_mode"]),
                "x_m": float(node["initial_position"]["x_meters"]),
                "y_m": float(node["initial_position"]["y_meters"]),
                "z_m": float(node["initial_position"]["z_meters"]),
            } for node in nodes],
            "trajectory_bindings": [{"node_id": int(binding["node_id"]),
                "asset_id": str(binding["asset_id"]), "version": int(binding["version"]),
                "track_id": str(binding["track_id"])} for binding in bindings],
            "trajectory_assets": [{
                "asset_id": asset.asset_id, "version": asset.version, "source": asset.source,
                "checksum_sha256": asset.checksum_sha256,
                "tracks": [{"track_id": track.track_id, "points": [{
                    "time_ns": round(point.time_s * 1_000_000_000),
                    "x_m": point.x_m, "y_m": point.y_m, "z_m": point.z_m,
                } for point in track.points]} for track in asset.tracks],
            } for asset in assets.values()],
            "experiment": {
                "name": draft.name,
                "routing_mode": str(payload.get("routing_mode", "DirectToFusionCenter")),
                "mac_mode": payload["mac_mode"] if planning else "Tdma",
                "workload": str(payload.get("workload", "BearingFusion")),
                "physical_layer": physical_layer,
                "waveform_modulation": modulation,
                "waveform_noise": noise_model,
                "waveform_noise_psd_db_re_1upa2_per_hz": float(payload.get("waveform_noise_psd_db_re_1upa2_per_hz", 0.0)),
                "deterministic_seed": int(payload["deterministic_seed"]),
                "slot_duration_ns": int(payload.get("slot_duration_ns", 6_000_000_000)),
                "guard_interval_ns": int(payload.get("guard_interval_ns", 2_000_000_000)),
                "bit_rate_bps": int(payload["bit_rate_bits_per_second"]),
                "center_frequency_hz": float(payload.get("center_frequency_hz", 25_000)),
                "occupied_bandwidth_hz": float(payload.get("occupied_bandwidth_hz", 4_000)),
                "source_level_db": float(payload.get("source_level_db", payload.get("source_level_db_re_1upa_at_1m", 110))),
                "network_update_interval_cycles": (1 if planning and not flow_scoped else int(payload.get("network_update_interval_cycles", 1 if planning else 10))),
                "minimum_bearing_points": int(payload.get("minimum_bearing_points", 5)),
                "maximum_fusion_period_ns": int(payload.get("maximum_fusion_period_ns", 180_000_000_000)),
                "target_x_m": float(payload.get("target_x_m", 200.0)),
                "target_y_m": float(payload.get("target_y_m", 150.0)),
                "maximum_ber": float(payload.get("maximum_ber", 0.0001)),
            },
        }
        if modulation == "Osdm":
            definition["experiment"]["osdm"] = dict(payload["osdm"])
        if recorded_config is not None:
            definition["recorded_channel"] = recorded_config
        if planning:
            from .planning_authoring import freeze_planning_configuration
            definition["execution_mode"] = "PlanningV1"
            definition["experiment"].update(freeze_planning_configuration(payload))
            if frozen_algorithm is not None:
                definition["experiment"]["mac_algorithm"] = frozen_algorithm
            if payload["mac_mode"] in {"AdaptiveTdma", "RegisteredMac"}:
                definition["experiment"].pop("slot_duration_ns")
                definition["experiment"].pop("guard_interval_ns")
        if flow_scoped:
            from .planning_authoring import v3_integer_boundary
            definition = v3_integer_boundary(definition, encode=True)
        encoded = json.dumps(definition, ensure_ascii=False, sort_keys=True,
                             separators=(",", ":")).encode()
        digest = hashlib.sha256(encoded).hexdigest()
        definition_path = authoring_root / "run-definitions" / f"{digest}.json"
        definition_path.parent.mkdir(parents=True, exist_ok=True)
        if definition_path.exists():
            if definition_path.read_bytes() != encoded:
                raise CatalogError("ImmutableResourceConflict", "Run definition digest collision.")
        else:
            temporary = definition_path.with_suffix(".tmp")
            temporary.write_bytes(encoded)
            temporary.replace(definition_path)
            definition_path.chmod(0o444)

        def command(run_id: str):
            from .wire import Environment, Execution, Preset, StartRunCommand
            execution = Execution(
                simulation_cycle_count=str(payload["simulation_cycle_count"]),
                deterministic_seed=str(payload["deterministic_seed"]),
                rx_quality_mode=(
                    "None" if physical_layer != "Channel1Abstract" else "ModeledBpskAwgn"
                ),
                equivalent_noise_power_db_re_1upa2=float(payload.get("equivalent_noise_power_db_re_1upa2", 45.0)),
                physical_layer=physical_layer,
                waveform_modulation=modulation,
                waveform_noise=noise_model,
                waveform_noise_psd_db_re_1upa2_per_hz=float(
                    payload.get("waveform_noise_psd_db_re_1upa2_per_hz", 0.0)
                ),
            )
            captured = StartRunCommand(
                run_id=run_id,
                preset=Preset(scenario_id=str(scenario_publication["published_id"]),
                              experiment_id=str(experiment_publication["published_id"]),
                              definition_version=str(definition_version),
                              acceptance_profile=str(payload.get("acceptance_profile", "Acceptance4Node"))),
                environment=Environment(asset_id=environment_id,
                                        asset_format_version=str(environment_version)),
                execution=execution,
                authored_definition_path=str(definition_path),
                authored_definition_sha256=digest,
            )
            run_executions[run_id] = captured.execution.model_copy(deep=True)
            return captured

        snapshot = await catalog.create(command)
        return _run_resource(snapshot)

    @app.get("/recorded-channels")
    async def list_recorded_channels():
        from .recorded_channels import list_recordings
        return await run_in_threadpool(list_recordings, authoring_root / "recorded-channels")

    @app.get("/recorded-noise")
    async def list_recorded_noise():
        from .recorded_channels import list_noise
        return await run_in_threadpool(list_noise, authoring_root / "recorded-noise")

    @app.get("/environments", response_model=list[EnvironmentResource])
    async def list_environments() -> tuple[EnvironmentResource, ...]:
        return (await current_resources()).list_environments()

    @app.get("/environments/authoring-summaries")
    async def environment_authoring_summaries() -> dict[str, dict]:
        resources = (await current_resources()).list_environments()
        def summaries() -> dict[str, dict]:
            result = {}
            for item in resources:
                asset_id = item.environment_asset_id
                snapshot = authoring.environment_input_snapshot(asset_id)
                if snapshot:
                    payload = snapshot["payload"]
                    depths = [point["depth_meters"] for point in payload.get("bathymetry", [])]
                    result[asset_id] = {"name": snapshot["name"], "available": True,
                        **_environment_location(payload),
                        "water_depth_m": payload.get("maximum_depth_meters") or (max(depths) if depths else None)}
                elif asset_id == "reference-shallow-water-v1":
                    manifest = reference_source(reference_asset_root)
                    _, terrain = _reference_data(reference_asset_root)
                    origin = manifest["proxy_origin"]
                    result[asset_id] = {"name": "南海浅水参考环境", "available": True,
                        "latitude": origin["latitude_deg"], "longitude": origin["longitude_deg"],
                        "month": manifest["time_reference"]["month"],
                        "water_depth_m": max(point["depth_meters"] for point in terrain)}
                else:
                    result[asset_id] = {"name": "已发布声场（名称未保留）", "available": False,
                        "latitude": None, "longitude": None, "month": None, "water_depth_m": None}
            return result
        return await run_in_threadpool(summaries)

    @app.get("/environments/{environment_id}", response_model=EnvironmentResource)
    async def get_environment(environment_id: str) -> EnvironmentResource:
        return (await current_resources()).get_environment(environment_id)

    async def acoustic_plot_response(artifact: dict, source_index: int, receiver_index: int, range_index: int, view: str) -> dict:
        from .acoustic_plots import arrivals_plot, ray_plot_for_source, rays_only_plot
        try:
            if view == "rays":
                return await run_in_threadpool(rays_only_plot, artifact, source_index)
            plot = await run_in_threadpool(arrivals_plot, Path(artifact["arrivals_path"]), artifact["arrivals_sha256"], source_index, receiver_index, range_index)
            ray = artifact.get("ray_plot", {})
            plot["ray_plot"] = await run_in_threadpool(ray_plot_for_source, ray, plot["source_depths_m"][source_index], plot["frequency_hz"])
            return plot
        except ValueError as error:
            raise CatalogError("InvalidAcousticPlot", str(error)) from error
        except (OSError, KeyError) as error:
            raise CatalogError("InvalidAcousticPlot", "声学图读取失败，请检查离线产物及采样选择。") from error

    @app.get("/environments/{environment_id}/acoustic-plots")
    async def environment_acoustic_plots(environment_id: str, source_index: int = 0, receiver_index: int = 0, range_index: int = 0, view: Literal["all", "rays"] = "all") -> dict:
        (await current_resources()).get_environment(environment_id)
        artifact = authoring.environment_plot_artifact(environment_id)
        if artifact is None and environment_id == "reference-shallow-water-v1":
            path = reference_asset_root / "bellhop/reference_shallow_water_v1.arr"
            # Verify the bundled input against its tracked validation report.
            manifest = json.loads((reference_asset_root / "bellhop/config.json").read_text())
            artifact = {"arrivals_path": str(path), "arrivals_sha256": manifest["files"]["arrival_sha256"]}
        if artifact is None:
            return {"available": False, "reason": "这个历史环境未保存绘图所需到达产物；请另存并重新计算"}
        return await acoustic_plot_response(artifact, source_index, receiver_index, range_index, view)

    @app.get("/authoring/environment-builds/{build_id}/acoustic-plots")
    async def build_acoustic_plots(build_id: str, source_index: int = 0, receiver_index: int = 0, range_index: int = 0, view: Literal["all", "rays"] = "all") -> dict:
        try:
            build = authoring.get_build(build_id)
        except KeyError as error:
            raise CatalogError("BuildNotFound", "环境计算不存在") from error
        if build.state != "Completed" or not build.artifact:
            return {"available": False, "reason": "环境计算完成后可查看信道图"}
        return await acoustic_plot_response(build.artifact, source_index, receiver_index, range_index, view)

    @app.get("/environments/{environment_id}/authoring-inputs")
    async def environment_authoring_inputs(environment_id: str) -> dict[str, object]:
        # Read-only projection of the exact publication snapshot, never the
        # latest mutable draft. Legacy assets without inputs remain explicit.
        (await current_resources()).get_environment(environment_id)
        snapshot = authoring.environment_input_snapshot(environment_id)
        if snapshot:
            return {"available": True, "name": snapshot["name"], "payload": snapshot["payload"],
                    "source_revision": snapshot["revision"], "limitations": []}
        if environment_id == "reference-shallow-water-v1":
            manifest = reference_source(reference_asset_root)
            ssp, terrain = _reference_data(reference_asset_root)
            config = json.loads((reference_asset_root / "bellhop/config.json").read_text())
            origin = manifest["proxy_origin"]
            acquisition = {"latitudeDeg": origin["latitude_deg"], "longitudeDeg": origin["longitude_deg"],
                           "month": manifest["time_reference"]["month"], "requestedSspDepthMeters": max(p["depth_meters"] for p in ssp),
                           "bearingDeg": 90, "requestedRangeMeters": 10000, "terrainSamples": 41, "useReferenceCache": True}
            return {"available": True, "name": "南海浅水参考环境", "source_revision": None,
                    "limitations": ["这是参考案例，不是实测海域。原声场使用三次样条声速插值；当前建设器另存新版本采用线性插值，不替换原声场。"],
                    "payload": {"sound_speed_profile": ssp, "bathymetry": terrain,
                        "description": "北纬18°、东经110°附近的4月参考海域；原传播场查询距离至2500 m。",
                        "center_frequency_hz": config["frequency_hz"], "acquisition": acquisition,
                        "sources": {"ssp": {"grid_coordinate": manifest["sound_speed_profile"]["nearest_grid"], "month": 4}},
                        "bottom": {"sound_speed_meters_per_second": config["bottom_sound_speed_m_per_s"],
                                   "density_grams_per_cubic_centimeter": config["bottom_density_g_per_cm3"],
                                   "attenuation_db_per_wavelength": config["bottom_attenuation_db_per_wavelength"]},
                        "bellhop": {"beam_count": config["computed_beam_count"], "source_depths_meters": config["source_depth_m"],
                                    "receiver_depths_meters": config["receiver_depth_m"], "range_sample_count": config["horizontal_range_m"]["count"],
                                    "minimum_launch_angle_degrees": config["launch_angle_degrees"][0],
                                    "maximum_launch_angle_degrees": config["launch_angle_degrees"][1]}}}
        return {"available": False, "name": environment_id, "payload": {}, "source_revision": None,
                "limitations": ["这个旧环境只保留了声场，未保存原始声速、地形和采集参数。可继续用于实验；如需修改，请重新导入输入，不能从声场猜测剖面。"]}

    @app.get("/scenarios", response_model=list[ScenarioResource])
    async def list_scenarios() -> tuple[ScenarioResource, ...]:
        return selected_resources.list_scenarios()

    @app.get(
        "/scenarios/{scenario_id}/versions/{version}",
        response_model=ScenarioResource,
    )
    async def get_scenario(scenario_id: str, version: str) -> ScenarioResource:
        return selected_resources.get_scenario(
            scenario_id, _resource_version(version)
        )

    @app.get("/experiments", response_model=list[ExperimentResource])
    async def list_experiments() -> tuple[ExperimentResource, ...]:
        return selected_resources.list_experiments()

    @app.get(
        "/experiments/{experiment_id}/versions/{version}",
        response_model=ExperimentResource,
    )
    async def get_experiment(
        experiment_id: str, version: str
    ) -> ExperimentResource:
        return selected_resources.get_experiment(
            experiment_id, _resource_version(version)
        )

    @app.get("/runs/{run_id}", response_model=RunResource)
    async def get_run(run_id: str) -> RunResource:
        return _run_resource(await catalog.get(run_id))

    @app.get("/runs/{run_id}/definition-context", response_model=RunDefinitionContext)
    async def get_run_definition_context(run_id: str) -> RunDefinitionContext:
        snapshot = await catalog.get(run_id)
        context = make_run_context(snapshot, run_executions.get(run_id), run_manifests.get(run_id))
        return context.model_copy(update={"actions": await actions_for(run_id)})

    async def validate_repeat(snapshot: RunSnapshot, command, *, full: bool = False):
        if snapshot.authored_definition_path:
            definition = await run_in_threadpool(load_captured_definition, snapshot)
            await algorithms.freeze(definition["experiment"], frozen=True)
        try:
            environment = None if command.execution.physical_layer == "Channel3Recorded" else (
                await verified_environment(command.environment.asset_id) if full else
                (await current_resources()).get_environment(command.environment.asset_id))
            return await run_in_threadpool(validate_repeat_resources, snapshot, command, run_manifests.get(snapshot.run_id),
                settings.environment_repository_root,
                lambda asset_id: environment, trajectories)
        except ResourceCatalogConfigurationError as error:
            raise CatalogError("InvalidReference", "原运行环境资产无法通过完整性校验，未启动新的运行。") from error

    async def actions_for(run_id: str) -> RunActions:
        snapshot = await catalog.get(run_id)
        command = await catalog.captured_command(run_id)
        reason = None
        try:
            await validate_repeat(snapshot, command)
        except (CatalogError, ResourceCatalogError) as error:
            reason = str(error)
        return make_run_actions(snapshot, command, run_manifests.get(run_id), authoring, reason)

    @app.get("/runs/{run_id}/actions", response_model=RunActions)
    async def get_run_actions(run_id: str) -> RunActions:
        return await actions_for(run_id)

    @app.post("/runs/{run_id}/rerun", response_model=RunResource, status_code=201)
    async def repeat_run(run_id: str) -> RunResource:
        original = await catalog.get(run_id)
        if original.lifecycle not in {"Completed", "Failed"}:
            raise CatalogError("RunNotReady", "原运行尚未结束，不能以它创建独立的重复运行。")
        captured = await catalog.captured_command(run_id)
        await validate_repeat(original, captured, full=True)

        def command(new_run_id: str):
            repeated = captured.model_copy(update={"run_id": new_run_id}, deep=True)
            run_executions[new_run_id] = repeated.execution.model_copy(deep=True)
            return repeated

        created = await catalog.create(command)
        manifest = run_manifests.get(run_id)
        if manifest is not None:
            run_manifests[created.run_id] = manifest.model_copy(update={"run_id": created.run_id}, deep=True)
        return _run_resource(created)

    @app.get("/runs", response_model=list[RunSummaryResource])
    async def list_runs() -> tuple[RunSummaryResource, ...]:
        return tuple(_run_summary(item) for item in await catalog.list_runs())

    @app.get("/results", response_model=list[ResultSummaryResource])
    async def list_results() -> tuple[ResultSummaryResource, ...]:
        return tuple(
            _result_summary(item) for item in await catalog.list_results()
        )

    @app.get("/runs/{run_id}/results", response_model=RunResultResource)
    async def get_results(run_id: str) -> RunResultResource:
        result: RunResult = await catalog.get_result(run_id)
        snapshot = await catalog.get(run_id)
        return RunResultResource(
            run_id=result.run_id,
            experiment_id=snapshot.experiment_id,
            experiment_version=snapshot.experiment_version,
            scenario_id=snapshot.scenario_id,
            scenario_version=snapshot.scenario_version,
            environment_asset_id=snapshot.environment_asset_id,
            environment_format_version=snapshot.environment_format_version,
            projection=result.projection,
            acceptance_report=result.acceptance_report,
            fusion_results=result.fusion_results,
            nodes=result.nodes,
            visualization=result.visualization,
            provenance=_run_resource(snapshot).provenance,
        )

    async def evidence_for(run_id: str) -> AcceptanceEvidenceBundle:
        result = await catalog.get_result(run_id)
        snapshot = await catalog.get(run_id)
        manifest = run_manifests.get(run_id)
        if manifest is None:
            raise CatalogError(
                "EvidenceUnavailable", "The captured RunManifest is unavailable."
            )
        try:
            return make_evidence(
                acceptance_baseline, manifest, snapshot, result
            )
        except ValueError as error:
            raise CatalogError("EvidenceUnavailable", str(error)) from error

    @app.get(
        "/runs/{run_id}/acceptance-evidence",
        response_model=AcceptanceEvidenceBundle,
    )
    async def get_acceptance_evidence(
        run_id: str,
    ) -> AcceptanceEvidenceBundle:
        return await evidence_for(run_id)

    @app.get("/runs/{run_id}/acceptance-evidence.txt")
    async def get_acceptance_evidence_text(run_id: str) -> PlainTextResponse:
        bundle = await evidence_for(run_id)
        return PlainTextResponse(
            render_evidence_text(bundle),
            media_type="text/plain; charset=utf-8",
            headers={
                "Content-Disposition": (
                    f'attachment; filename="{run_id}-acceptance-evidence.txt"'
                )
            },
        )

    @app.get("/runs/{run_id}/events")
    async def get_events(
        run_id: str,
        last_event_id: str | None = Header(default=None, alias="Last-Event-ID"),
    ) -> StreamingResponse:
        cursor = _cursor(last_event_id)
        await catalog.validate_cursor(run_id, cursor)

        async def stream():
            last_sequence = str(cursor)
            async for event in catalog.stream_after(run_id, cursor):
                data = event.model_dump_json()
                last_sequence = event.sequence
                yield (
                    f"id: {event.sequence}\n"
                    "event: run-event\n"
                    f"data: {data}\n\n"
                )
            # Transport completion only: no RunEvent identity, lifecycle, or
            # simulation state is synthesized. GET Run remains authoritative.
            yield "event: stream-end\ndata: " + json.dumps({"last_sequence": last_sequence}, separators=(",", ":")) + "\n\n"

        return StreamingResponse(
            stream(),
            media_type="text/event-stream",
            headers={"Cache-Control": "no-cache"},
        )

    return app
