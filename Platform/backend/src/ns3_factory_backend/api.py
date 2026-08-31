from __future__ import annotations

import os
from contextlib import asynccontextmanager
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Literal

from fastapi import FastAPI, Header, Request, status
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse, StreamingResponse
from pydantic import BaseModel, ConfigDict

from .catalog import (
    BackendFailure,
    CatalogError,
    FormalResultSnapshot,
    InMemoryRunCatalog,
    RunSnapshot,
)
from .gateway import WorkerGateway
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
    StableId,
    UIntDecimal,
)


@dataclass(frozen=True)
class BackendSettings:
    worker_executable: Path
    environment_repository_root: Path
    resource_catalog_adapter: Path | None = None
    acceptance_environment_asset_id: str = "backend-field-v1"


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

    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        try:
            yield
        finally:
            await catalog.shutdown()

    app = FastAPI(
        title="NS3-BELLHOP Platform Run API", version="1", lifespan=lifespan
    )
    app.state.catalog = catalog
    app.state.resource_catalog = selected_resources

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

    @app.post(
        "/runs",
        response_model=RunResource,
        status_code=status.HTTP_201_CREATED,
    )
    async def create_run(request: CreateRunRequest) -> RunResource:
        def command(run_id: str):
            return selected_resources.resolve_command(
                run_id, request.experiment_id, request.experiment_version
            )

        return _run_resource(await catalog.create(command))

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
