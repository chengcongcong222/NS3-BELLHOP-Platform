from __future__ import annotations

import asyncio
import json
import os
import subprocess
from pathlib import Path

import httpx
import pytest
from pydantic import ValidationError

from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.resources import (
    ResourceCatalog,
    ResourceCatalogConfigurationError,
    ResourceCatalogDocument,
)

from helpers import event, observe, resource_catalog, started, success_result


FIXTURES = Path(__file__).with_name("fixtures")


class ImmediateGateway:
    async def run(self, command, callback):
        await observe(callback, [started(command.run_id), event(command.run_id, 1)])
        return success_result(command.run_id)


def _app(catalog: ResourceCatalog, run_id: str = "resource-run"):
    return create_app(
        BackendSettings(Path("/missing/worker"), Path("/missing/repository")),
        gateway=ImmediateGateway(),
        id_factory=lambda: run_id,
        resource_catalog=catalog,
    )


async def _terminal(client: httpx.AsyncClient, run_id: str) -> dict:
    for _ in range(200):
        response = await client.get(f"/runs/{run_id}")
        if response.json()["lifecycle"] in {"Completed", "Failed"}:
            return response.json()
        await asyncio.sleep(0.005)
    raise AssertionError("Run did not become terminal")


def _assert_no_path_leakage(value) -> None:
    forbidden = ["repository", "package_path", "absolute_path", "/home/", "/tmp/"]
    text = json.dumps(value, sort_keys=True)
    assert all(token not in text for token in forbidden)


def test_backend_requires_an_authoritative_catalog_adapter_or_injected_catalog() -> None:
    with pytest.raises(ResourceCatalogConfigurationError):
        create_app(
            BackendSettings(Path("/missing/worker"), Path("/missing/repository")),
            gateway=ImmediateGateway(),
        )


@pytest.mark.skipif(
    "PLATFORM_RESOURCE_CATALOG_ADAPTER_PATH" not in os.environ
    or "PLATFORM_WORKER_TEST_ASSET_BUILDER_PATH" not in os.environ,
    reason="C++ authority adapters are supplied by the CMake test build",
)
def test_cpp_adapter_uses_real_repository_and_lists_deterministically(
    tmp_path: Path,
) -> None:
    builder = Path(os.environ["PLATFORM_WORKER_TEST_ASSET_BUILDER_PATH"])
    adapter = Path(os.environ["PLATFORM_RESOURCE_CATALOG_ADAPTER_PATH"])
    repository = tmp_path / "environment-repository"
    for asset_id in ["z-field", "backend-field-v1", "a-field"]:
        subprocess.run([builder, repository, asset_id], check=True)

    catalog = ResourceCatalog.from_adapter(
        adapter, repository, "backend-field-v1"
    )
    assert [item.environment_asset_id for item in catalog.list_environments()] == [
        "a-field",
        "backend-field-v1",
        "z-field",
    ]
    environment = catalog.get_environment("backend-field-v1")
    assert environment.validation_state == "Valid"
    assert environment.axes.frequency.count == "1"
    assert environment.cell_count == "8"
    assert environment.no_arrival_cell_count == "0"
    assert environment.checksum.algorithm == "FNV1A64"
    _assert_no_path_leakage(environment.model_dump(mode="json"))

    scenarios = catalog.list_scenarios()
    experiments = catalog.list_experiments()
    assert [(item.scenario_id, item.version) for item in scenarios] == [
        ("acceptance4-scenario", "1"),
        ("extended6-scenario", "1"),
    ]
    assert [(item.experiment_id, item.version) for item in experiments] == [
        ("acceptance4-experiment", "1"),
        ("extended6-experiment", "1"),
    ]
    assert len(catalog.get_scenario("acceptance4-scenario", "1").nodes) == 4
    assert (
        catalog.get_experiment("acceptance4-experiment", "1").scenario.scenario_id
        == "acceptance4-scenario"
    )

    with pytest.raises(ResourceCatalogConfigurationError):
        ResourceCatalog.from_adapter(adapter, repository, "missing-field")


def test_published_versions_are_immutable_and_lists_ignore_input_order() -> None:
    base = resource_catalog()
    environment = base.list_environments()[0]
    scenario = base.list_scenarios()[0]
    experiment = base.list_experiments()[0]
    scenario_z = scenario.model_copy(update={"scenario_id": "z-scenario"})
    scenario_a = scenario.model_copy(update={"scenario_id": "a-scenario"})
    document = ResourceCatalogDocument(
        schema_version=1,
        environments=(
            environment.model_copy(
                update={"environment_asset_id": "z-environment"}
            ),
            environment.model_copy(
                update={"environment_asset_id": "a-environment"}
            ),
            environment,
        ),
        scenarios=(
            scenario_z,
            scenario_a,
            scenario,
        ),
        experiments=(
            experiment.model_copy(
                update={
                    "experiment_id": "z-experiment",
                    "scenario": experiment.scenario.model_copy(
                        update={"scenario_id": scenario_z.scenario_id}
                    ),
                }
            ),
            experiment.model_copy(
                update={
                    "experiment_id": "a-experiment",
                    "scenario": experiment.scenario.model_copy(
                        update={"scenario_id": scenario_a.scenario_id}
                    ),
                }
            ),
            experiment,
        ),
    )
    catalog = ResourceCatalog(document)
    assert [item.environment_asset_id for item in catalog.list_environments()] == [
        "a-environment",
        "backend-field-v1",
        "z-environment",
    ]
    assert [item.scenario_id for item in catalog.list_scenarios()] == [
        "a-scenario",
        "acceptance4-scenario",
        "z-scenario",
    ]
    assert [item.experiment_id for item in catalog.list_experiments()] == [
        "a-experiment",
        "acceptance4-experiment",
        "z-experiment",
    ]
    with pytest.raises(ValidationError):
        scenario.name = "mutated"


@pytest.mark.parametrize(
    "invalid_document",
    [
        "duplicate-environment",
        "duplicate-scenario-version",
        "duplicate-experiment-version",
        "missing-environment",
        "wrong-environment-version",
        "missing-scenario-version",
        "incompatible-definition-version",
    ],
)
def test_catalog_rejects_entire_snapshot_before_publication(
    invalid_document: str,
) -> None:
    base = resource_catalog()
    environment = base.list_environments()[0]
    scenario = base.list_scenarios()[0]
    experiment = base.list_experiments()[0]
    environments = base.list_environments()
    scenarios = base.list_scenarios()
    experiments = base.list_experiments()
    if invalid_document == "duplicate-environment":
        environments += (environment,)
    elif invalid_document == "duplicate-scenario-version":
        scenarios += (scenario,)
    elif invalid_document == "duplicate-experiment-version":
        experiments += (experiment,)
    elif invalid_document == "missing-environment":
        scenarios = (
            scenario.model_copy(
                update={
                    "environment": scenario.environment.model_copy(
                        update={"environment_asset_id": "missing-environment"}
                    )
                }
            ),
        )
    elif invalid_document == "wrong-environment-version":
        scenarios = (
            scenario.model_copy(
                update={
                    "environment": scenario.environment.model_copy(
                        update={"asset_format_version": "2"}
                    )
                }
            ),
        )
    elif invalid_document == "missing-scenario-version":
        experiments = (
            experiment.model_copy(
                update={
                    "scenario": experiment.scenario.model_copy(
                        update={"version": "2"}
                    )
                }
            ),
        )
    else:
        experiments = (experiment.model_copy(update={"version": "2"}),)

    with pytest.raises(ResourceCatalogConfigurationError):
        ResourceCatalog(
            ResourceCatalogDocument(
                schema_version=1,
                environments=environments,
                scenarios=scenarios,
                experiments=experiments,
            )
        )


def test_resource_endpoints_and_owned_missing_reference_errors() -> None:
    async def run() -> None:
        app = _app(resource_catalog())
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            environments = await client.get("/environments")
            assert environments.status_code == 200
            assert environments.json()[0]["environment_asset_id"] == (
                "backend-field-v1"
            )
            assert (await client.get("/environments/backend-field-v1")).status_code == 200
            assert (await client.get("/scenarios")).status_code == 200
            assert (
                await client.get("/scenarios/acceptance4-scenario/versions/1")
            ).status_code == 200
            assert (await client.get("/experiments")).status_code == 200
            assert (
                await client.get("/experiments/acceptance4-experiment/versions/1")
            ).status_code == 200

            cases = [
                ("/environments/missing", "EnvironmentNotFound"),
                ("/scenarios/missing/versions/1", "ScenarioNotFound"),
                (
                    "/scenarios/acceptance4-scenario/versions/2",
                    "ScenarioVersionNotFound",
                ),
                ("/experiments/missing/versions/1", "ExperimentNotFound"),
                (
                    "/experiments/acceptance4-experiment/versions/2",
                    "ExperimentVersionNotFound",
                ),
            ]
            for path, code in cases:
                response = await client.get(path)
                assert response.status_code == 404
                assert response.json()["error"]["code"] == code
            noncanonical = await client.get(
                "/experiments/acceptance4-experiment/versions/01"
            )
            assert noncanonical.status_code == 400
            assert noncanonical.json()["error"]["code"] == "InvalidRequest"
            assert (await client.put(
                "/scenarios/acceptance4-scenario/versions/1", json={}
            )).status_code == 405

    asyncio.run(run())


def test_post_resolves_experiment_and_captures_all_versions() -> None:
    async def run() -> None:
        app = _app(resource_catalog(), "captured-run")
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            created = await client.post(
                "/runs",
                json={
                    "experiment_id": "acceptance4-experiment",
                    "experiment_version": "1",
                },
            )
            assert created.status_code == 201
            assert created.json()["experiment_id"] == "acceptance4-experiment"
            # Run input ownership is captured at creation. Publishing a newer
            # catalog version cannot change its lifecycle or result provenance.
            base = resource_catalog()
            scenario_v2 = base.list_scenarios()[0].model_copy(
                update={"version": "2"}
            )
            experiment_v2 = base.list_experiments()[0].model_copy(
                update={
                    "version": "2",
                    "scenario": base.list_experiments()[0].scenario.model_copy(
                        update={"version": "2"}
                    ),
                }
            )
            app.state.resource_catalog = ResourceCatalog(
                ResourceCatalogDocument(
                    schema_version=1,
                    environments=base.list_environments(),
                    scenarios=base.list_scenarios() + (scenario_v2,),
                    experiments=base.list_experiments() + (experiment_v2,),
                )
            )
            terminal = await _terminal(client, "captured-run")
            assert terminal["experiment_version"] == "1"
            assert terminal["scenario_id"] == "acceptance4-scenario"
            assert terminal["scenario_version"] == "1"
            assert terminal["environment_asset_id"] == "backend-field-v1"
            assert terminal["environment_format_version"] == "1"
            result = await client.get("/runs/captured-run/results")
            assert result.status_code == 200
            assert result.json()["experiment_version"] == "1"
            assert result.json()["scenario_version"] == "1"
            assert result.json()["environment_format_version"] == "1"

        missing_app = _app(resource_catalog(), "missing-run")
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=missing_app), base_url="http://test"
        ) as client:
            for request, code in [
                (
                    {"experiment_id": "missing", "experiment_version": "1"},
                    "ExperimentNotFound",
                ),
                (
                    {
                        "experiment_id": "acceptance4-experiment",
                        "experiment_version": "2",
                    },
                    "ExperimentVersionNotFound",
                ),
            ]:
                missing = await client.post("/runs", json=request)
                assert missing.status_code == 404
                assert missing.json()["error"]["code"] == code

    asyncio.run(run())


def test_invalid_experiment_reference_fails_catalog_startup() -> None:
    base = resource_catalog()
    experiment = base.list_experiments()[0].model_copy(
        update={
            "scenario": base.list_experiments()[0].scenario.model_copy(
                update={"scenario_id": "missing-scenario"}
            )
        }
    )
    with pytest.raises(ResourceCatalogConfigurationError):
        ResourceCatalog(
            ResourceCatalogDocument(
                schema_version=1,
                environments=base.list_environments(),
                scenarios=base.list_scenarios(),
                experiments=(experiment,),
            )
        )


def test_frontend_contract_golden_responses_are_stable() -> None:
    def fixture(name: str) -> dict:
        return json.loads((FIXTURES / name).read_text())

    async def run() -> None:
        app = _app(resource_catalog(), "resource-run")
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            assert (
                await client.get("/environments/backend-field-v1")
            ).json() == fixture("environment_detail.json")
            assert (
                await client.get("/scenarios/acceptance4-scenario/versions/1")
            ).json() == fixture("scenario_detail.json")
            assert (
                await client.get("/experiments/acceptance4-experiment/versions/1")
            ).json() == fixture("experiment_detail.json")
            created = await client.post(
                "/runs",
                json={
                    "experiment_id": "acceptance4-experiment",
                    "experiment_version": "1",
                },
            )
            assert created.status_code == 201
            await _terminal(client, "resource-run")
            assert (await client.get("/runs/resource-run")).json() == fixture(
                "run_detail.json"
            )
            assert (
                await client.get("/runs/resource-run/results")
            ).json() == fixture("result_detail.json")

    asyncio.run(run())
