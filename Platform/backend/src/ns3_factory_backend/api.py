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
    InMemoryRunCatalog,
    RunSnapshot,
)
from .gateway import WorkerGateway
from .wire import (
    Environment,
    EnvironmentAssetId,
    Execution,
    FiniteFloat,
    PositiveUInt32Decimal,
    PositiveUIntDecimal,
    Preset,
    RunResult,
    StartRunCommand,
    UIntDecimal,
)


@dataclass(frozen=True)
class BackendSettings:
    worker_executable: Path
    environment_repository_root: Path


class HttpModel(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)


class CreateRunRequest(HttpModel):
    environment_asset_id: EnvironmentAssetId
    environment_format_version: PositiveUInt32Decimal
    simulation_cycle_count: PositiveUIntDecimal
    rx_quality_mode: Literal["None", "ModeledBpskAwgn"]
    equivalent_noise_power_db_re_1upa2: FiniteFloat
    deterministic_seed: UIntDecimal


class FailureResource(HttpModel):
    code: str
    message: str


class RunResource(HttpModel):
    run_id: str
    lifecycle: Literal["Created", "Running", "Completed", "Failed"]
    simulation_started_at_ns: str | None
    simulation_ended_at_ns: str | None
    final_snapshot_version: str | None
    event_stream_complete: bool | None
    failure: FailureResource | None


class HealthResource(HttpModel):
    status: Literal["ok", "degraded"]
    backend_alive: Literal[True]
    worker_executable_ready: bool


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


def create_app(
    settings: BackendSettings,
    *,
    gateway: object | None = None,
    id_factory: Callable[[], str] | None = None,
) -> FastAPI:
    selected_gateway = gateway or WorkerGateway(
        settings.worker_executable, settings.environment_repository_root
    )
    catalog = InMemoryRunCatalog(selected_gateway, id_factory=id_factory)

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
        def command(run_id: str) -> StartRunCommand:
            return StartRunCommand(
                run_id=run_id,
                preset=Preset(
                    scenario_id="acceptance4-scenario",
                    experiment_id="acceptance4-experiment",
                    definition_version="1",
                    acceptance_profile="Acceptance4Node",
                ),
                environment=Environment(
                    asset_id=request.environment_asset_id,
                    asset_format_version=request.environment_format_version,
                ),
                execution=Execution(
                    simulation_cycle_count=request.simulation_cycle_count,
                    rx_quality_mode=request.rx_quality_mode,
                    equivalent_noise_power_db_re_1upa2=(
                        request.equivalent_noise_power_db_re_1upa2
                    ),
                    deterministic_seed=request.deterministic_seed,
                ),
            )

        return _run_resource(await catalog.create(command))

    @app.get("/runs/{run_id}", response_model=RunResource)
    async def get_run(run_id: str) -> RunResource:
        return _run_resource(await catalog.get(run_id))

    @app.get("/runs/{run_id}/results", response_model=RunResult)
    async def get_results(run_id: str) -> RunResult:
        return await catalog.get_result(run_id)

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
