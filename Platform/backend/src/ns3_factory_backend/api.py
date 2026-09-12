from __future__ import annotations

import os
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
from pydantic import BaseModel, ConfigDict

from .catalog import (
    BackendFailure,
    CatalogError,
    FormalResultSnapshot,
    InMemoryRunCatalog,
    RunSnapshot,
)
from .gateway import WorkerGateway
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
from .wire import (
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
)


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
    "EvidenceUnavailable": status.HTTP_409_CONFLICT,
    "DraftNotFound": status.HTTP_404_NOT_FOUND,
    "BuildNotFound": status.HTTP_404_NOT_FOUND,
    "DraftConflict": status.HTTP_409_CONFLICT,
    "InvalidDraft": status.HTTP_400_BAD_REQUEST,
    "DraftValidationFailed": status.HTTP_400_BAD_REQUEST,
    "BuildNotReady": status.HTTP_409_CONFLICT,
    "ImmutableResourceConflict": status.HTTP_409_CONFLICT,
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


def create_app(
    settings: BackendSettings,
    *,
    gateway: object | None = None,
    id_factory: Callable[[], str] | None = None,
    resource_catalog: ResourceCatalog | None = None,
) -> FastAPI:
    selected_gateway = gateway or WorkerGateway(
        settings.worker_executable, settings.environment_repository_root
    )
    catalog = InMemoryRunCatalog(selected_gateway, id_factory=id_factory)
    selected_resources = resource_catalog
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
    authoring_root = settings.authoring_workspace_root or Path(
        tempfile.mkdtemp(prefix="ns3-platform-authoring-")
    )
    authoring = AuthoringStore(authoring_root)
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

    @app.get("/authoring/drafts", response_model=list[DraftResource])
    async def list_drafts(kind: DraftKind | None = None) -> list[DraftResource]:
        return authoring.list(kind)

    @app.post("/authoring/drafts", response_model=DraftResource, status_code=201)
    async def create_draft(write: DraftWrite) -> DraftResource:
        return authoring.create(write)

    @app.get("/authoring/drafts/{kind}/{draft_id}", response_model=DraftResource)
    async def get_draft(kind: DraftKind, draft_id: str) -> DraftResource:
        try:
            return authoring.get(kind, draft_id)
        except KeyError as error:
            raise CatalogError("DraftNotFound", "Authoring draft was not found.") from error

    @app.put("/authoring/drafts/{kind}/{draft_id}", response_model=DraftResource)
    async def update_draft(kind: DraftKind, draft_id: str, write: DraftWrite,
                           if_match: str | None = Header(default=None, alias="If-Match")) -> DraftResource:
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
        return {"valid": not issues, "issues": issues, "revision": draft.revision}

    @app.post("/authoring/import/ssp")
    async def import_ssp(request: TextImportRequest) -> dict[str, object]:
        try:
            points = parse_two_column_text(request.content, "ssp")
        except ValueError as error:
            raise CatalogError("InvalidHydrologyFile", str(error)) from error
        return {"source": "file", "filename": request.filename,
                "point_count": len(points), "sound_speed_profile": points}

    @app.post("/authoring/import/terrain")
    async def import_terrain(request: TextImportRequest) -> dict[str, object]:
        try:
            points = parse_two_column_text(request.content, "bathymetry")
        except ValueError as error:
            raise CatalogError("InvalidTerrainFile", str(error)) from error
        return {"source": "file", "filename": request.filename,
                "point_count": len(points), "bathymetry": points}

    @app.post("/authoring/acquisition/woa")
    async def fetch_woa(request: OceanAcquisitionRequest) -> dict[str, object]:
        try:
            return acquire_woa(request, authoring_root / "cache" / "woa23", reference_asset_root)
        except (ValueError, RuntimeError) as error:
            raise CatalogError("HydrologyAcquisitionFailed", str(error)) from error

    @app.post("/authoring/acquisition/terrain")
    async def fetch_terrain(request: TerrainAcquisitionRequest) -> dict[str, object]:
        try:
            return acquire_terrain(request, reference_asset_root)
        except (ValueError, RuntimeError) as error:
            raise CatalogError("TerrainAcquisitionFailed", str(error)) from error

    @app.post("/authoring/environment-builds", response_model=EnvironmentBuildResource, status_code=202)
    async def create_environment_build(request: BuildRequest) -> EnvironmentBuildResource:
        try:
            effective = request if request.reference_asset_root else request.model_copy(
                update={"reference_asset_root": str(reference_asset_root)})
            return new_build(authoring, effective)
        except KeyError as error:
            raise CatalogError("DraftNotFound", "Environment draft was not found.") from error

    @app.get("/authoring/environment-builds/{build_id}", response_model=EnvironmentBuildResource)
    async def get_environment_build(build_id: str) -> EnvironmentBuildResource:
        try:
            return authoring.get_build(build_id)
        except KeyError as error:
            raise CatalogError("BuildNotFound", "Environment build was not found.") from error

    @app.post("/authoring/environment-builds/{build_id}/publish")
    async def publish_environment_build(build_id: str) -> dict[str, object]:
        try:
            artifact = authoring.publish_build(build_id)
            return {**artifact, "published": True,
                    "runtime_catalog_available": False}
        except KeyError as error:
            raise CatalogError("BuildNotFound", "Environment build was not found.") from error
        except FileExistsError as error:
            raise CatalogError("ImmutableResourceConflict", str(error)) from error
        except ValueError as error:
            raise CatalogError("BuildNotReady", str(error)) from error

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
        environment = selected_resources.get_environment(
            scenario.environment.environment_asset_id
        )

        def command(run_id: str):
            return selected_resources.resolve_command(
                run_id, request.experiment_id, request.experiment_version
            )

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
    async def create_authoring_experiment_run(draft_id: str) -> RunResource:
        """Run the executable subset of a persisted experiment draft.

        The immutable Scenario/Environment and acceptance preset remain the
        authority.  Only worker-supported execution settings are projected;
        unsupported editor fields are never silently applied.
        """
        try:
            draft = authoring.get("experiment", draft_id)
        except KeyError as error:
            raise CatalogError("DraftNotFound", "Experiment draft was not found.") from error
        if draft.validation_issues:
            raise CatalogError("DraftValidationFailed", "; ".join(draft.validation_issues))
        payload = draft.payload
        scenario_id = str(payload.get("scenario_id", ""))
        candidates = [item for item in selected_resources.list_experiments()
                      if item.scenario.scenario_id == scenario_id]
        if not candidates:
            raise CatalogError(
                "InvalidReference",
                "Draft must reference a Scenario used by a formal Experiment.",
            )
        experiment = max(candidates, key=lambda item: int(item.version))
        scenario = selected_resources.get_scenario(
            experiment.scenario.scenario_id, experiment.scenario.version)
        environment = selected_resources.get_environment(
            scenario.environment.environment_asset_id)
        physical_layer = str(payload.get("physical_layer", "Channel1Abstract"))
        if physical_layer not in {"Channel1Abstract", "Channel2Waveform"}:
            raise CatalogError("InvalidDraft", "Unknown physical layer implementation.")
        modulation = str(payload.get("waveform_modulation", "Bpsk"))
        noise_model = str(payload.get("waveform_noise", "None"))
        if modulation not in {"Bpsk", "Bfsk"} or noise_model not in {"None", "Awgn"}:
            raise CatalogError("InvalidDraft", "Unknown waveform configuration.")

        def command(run_id: str):
            base = selected_resources.resolve_command(
                run_id, experiment.experiment_id, experiment.version)
            execution = base.execution.model_copy(update={
                "simulation_cycle_count": str(payload["simulation_cycle_count"]),
                "deterministic_seed": str(payload["deterministic_seed"]),
                "rx_quality_mode": (
                    "None" if physical_layer == "Channel2Waveform"
                    else experiment.phy.rx_quality_mode
                ),
                "physical_layer": physical_layer,
                "waveform_modulation": modulation,
                "waveform_noise": noise_model,
                "waveform_noise_psd_db_re_1upa2_per_hz": float(
                    payload.get("waveform_noise_psd_db_re_1upa2_per_hz", 0.0)
                ),
            })
            return base.model_copy(update={"execution": execution})

        snapshot = await catalog.create(command)
        run_manifests[snapshot.run_id] = make_manifest(
            snapshot.run_id, system_info, environment, scenario, experiment)
        return _run_resource(snapshot)

    @app.get("/environments", response_model=list[EnvironmentResource])
    async def list_environments() -> tuple[EnvironmentResource, ...]:
        return selected_resources.list_environments()

    @app.get("/environments/{environment_id}", response_model=EnvironmentResource)
    async def get_environment(environment_id: str) -> EnvironmentResource:
        return selected_resources.get_environment(environment_id)

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
            async for event in catalog.stream_after(run_id, cursor):
                data = event.model_dump_json()
                yield (
                    f"id: {event.sequence}\n"
                    "event: run-event\n"
                    f"data: {data}\n\n"
                )

        return StreamingResponse(
            stream(),
            media_type="text/event-stream",
            headers={"Cache-Control": "no-cache"},
        )

    return app
