from __future__ import annotations

import asyncio
from pathlib import Path

import httpx

from ns3_factory_backend.api import BackendSettings, create_app

from helpers import resource_catalog, success_result


REQUEST = {
    "experiment_id": "acceptance4-experiment",
    "experiment_version": "1",
}


class ImmediateGateway:
    def __init__(self, overall: str = "Pass") -> None:
        self.overall = overall

    async def run(self, command, _callback):
        return success_result(command.run_id, self.overall)

    async def shutdown(self) -> None:
        return None


async def wait_completed(client: httpx.AsyncClient, run_id: str) -> None:
    for _ in range(100):
        response = await client.get(f"/runs/{run_id}")
        if response.json()["lifecycle"] == "Completed":
            return
        await asyncio.sleep(0.005)
    raise AssertionError("Run did not complete")


def app_for(overall: str = "Pass"):
    return create_app(
        BackendSettings(
            Path("/missing/worker"),
            Path("/missing/assets"),
            source_revision="test-revision",
            build_configuration="test-ns3-on",
        ),
        gateway=ImmediateGateway(overall),
        id_factory=lambda: "evidence-run",
        resource_catalog=resource_catalog(),
    )


def test_system_info_is_formal_and_has_no_filesystem_paths() -> None:
    async def run() -> None:
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app_for()), base_url="http://test"
        ) as client:
            response = await client.get("/system/info")
            assert response.status_code == 200
            body = response.json()
            assert body["product_baseline"] == "P0-S5-03"
            assert body["release_id"] == "P0-S5-03"
            assert body["build_target"] == "linux-x86_64"
            assert body["reference_environment_asset_id"] == "reference-shallow-water-v1"
            assert body["reference_environment_checksum"] == "fb64e543f9042c52"
            assert body["build"]["source_revision"] == "test-revision"
            assert body["build"]["cxx_standard"] == "23"
            assert body["simulation"] == {
                "engine": "ns-3",
                "version": "3.47",
                "time_authority": "ns3::Simulator",
                "scheduler_authority": "ns3::Simulator",
                "scheduling_gateway": "M1 / Ns3KernelGateway",
            }
            assert "/home/" not in response.text

    asyncio.run(run())


def test_evidence_is_captured_read_only_and_byte_deterministic() -> None:
    async def run() -> None:
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app_for()), base_url="http://test"
        ) as client:
            created = await client.post("/runs", json=REQUEST)
            assert created.status_code == 201
            await wait_completed(client, "evidence-run")
            first = await client.get("/runs/evidence-run/acceptance-evidence")
            second = await client.get("/runs/evidence-run/acceptance-evidence")
            assert first.status_code == 200
            assert first.content == second.content
            body = first.json()
            assert body["immutable_snapshot"] is True
            assert body["manifest"]["run_id"] == "evidence-run"
            assert body["manifest"]["experiment"]["version"] == "1"
            assert body["manifest"]["environment"]["checksum"]["value"]
            assert body["manifest"]["system"]["release_id"] == "P0-S5-03"
            assert body["acceptance_report"]["overall"] == "Pass"
            assert body["semantics"]["verdict_origin"] == "BackendAcceptanceReport"
            assert body["semantics"]["environment_evidence"] == "Reference / modeled"
            assert body["semantics"]["propagation_evidence"] == "Bellhop-derived"
            assert body["semantics"]["ber_evidence_source"] == "Modeled"
            assert "not a hardware measurement" in body["semantics"]["ber_interpretation"]
            assert "floating-point representation floor" in body["semantics"]["ber_interpretation"]
            assert "no Reception" in body["semantics"]["no_arrival"]
            assert "not decoded" in body["semantics"]["not_decoded"]
            assert body["baseline"]["hard_requirements"] == {
                "network_node_count_minimum": "3",
                "network_node_count_maximum": "4",
                "communication_rate_bits_per_second": "60",
                "maximum_bit_error_rate": 0.0001,
                "feature_level_fusion_required": True,
                "minimum_bearing_points": "5",
                "maximum_fusion_period_ns": "180000000000",
            }
            text = await client.get(
                "/runs/evidence-run/acceptance-evidence.txt"
            )
            assert text.status_code == 200
            assert "Overall: Pass" in text.text
            assert "Release: P0-S5-03" in text.text
            assert "Environment evidence: Reference / modeled" in text.text
            assert "Propagation: Bellhop-derived" in text.text
            assert "not a hardware measurement" in text.text
            assert "no acceptance metric is recomputed" in text.text

    asyncio.run(run())


def test_acceptance_fail_is_evidence_not_system_failure() -> None:
    async def run() -> None:
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app_for("Fail")),
            base_url="http://test",
        ) as client:
            await client.post("/runs", json=REQUEST)
            await wait_completed(client, "evidence-run")
            run_response = await client.get("/runs/evidence-run")
            evidence = await client.get(
                "/runs/evidence-run/acceptance-evidence"
            )
            assert run_response.json()["lifecycle"] == "Completed"
            assert evidence.status_code == 200
            assert evidence.json()["acceptance_report"]["overall"] == "Fail"

    asyncio.run(run())
