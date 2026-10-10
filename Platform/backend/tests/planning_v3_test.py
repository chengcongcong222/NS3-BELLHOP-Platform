"""API freeze gates; captured gateway is not claimed as a physical E2E run."""
import asyncio
import json
from copy import deepcopy
from pathlib import Path
import httpx
import pytest
from ns3_factory_backend.algorithm_catalog import AlgorithmCatalog
from ns3_factory_backend.authoring import validate_payload
from ns3_factory_backend.planning_authoring import v3_integer_boundary
from planning_authoring_test import _payload, _scene, _experiment
from run_definition_context_test import _application


def payload():
    value = _payload()
    value.pop("adaptive_tdma")
    value["mac_mode"] = "RegisteredMac"
    value["mac_algorithm"] = {"id": "test.flow", "version": "1.0.0", "package_sha256": "a" * 64,
        "interface": "planning-mac-v2", "parameters": {"guard": "0"}}
    value["network_update_interval_cycles"] = "3"
    value["flows"] = [
        {"flow_id": "9007199254740993", "source_node_id": "0", "destination_node_id": "1", "packets_per_cycle": 1},
        {"flow_id": "0", "source_node_id": "0", "destination_node_id": "1", "packets_per_cycle": 2},
    ]
    return value


def test_flow_scoped_validation_keeps_distinct_same_endpoint_flows():
    value = payload()
    assert validate_payload("experiment", value) == []
    changed = deepcopy(value)
    changed["flows"].reverse()
    assert validate_payload("experiment", changed) == []
    changed["flows"][0]["flow_id"] = changed["flows"][1]["flow_id"]
    assert any("重复" in issue for issue in validate_payload("experiment", changed))
    changed = deepcopy(value)
    changed["flows"][0].pop("flow_id")
    assert validate_payload("experiment", changed)
    changed = deepcopy(value)
    changed["mac_algorithm"]["interface"] = "planning-mac-v1"
    assert validate_payload("experiment", changed)  # no silent reinterpretation


def test_v3_boundary_does_not_narrow_or_convert_algorithm_semver():
    value = {"version": 1, "node_id": (1 << 64) - 1, "can_receive": True, "x_m": 2.5,
        "mac_algorithm": {"version": "1.0.0", "parameters": {"guard": "0"}}}
    encoded = v3_integer_boundary(value, encode=True)
    assert encoded["node_id"] == "18446744073709551615"
    assert v3_integer_boundary(encoded, encode=False) == value
    for bad in (1, True, "01", "18446744073709551616"):
        with pytest.raises(ValueError):
            v3_integer_boundary({"node_id": bad}, encode=False)


def test_api_freezes_v3_and_reopens_stable_flow_identity(tmp_path, monkeypatch):
    value = payload()
    async def catalog(self):
        return {"schema": "platform.algorithm-catalog.v1", "worker_sha256": "b" * 64,
            "algorithms": [{**{key: value["mac_algorithm"][key] for key in ("id", "version", "package_sha256", "interface")},
                "parameters": [{"id": "guard", "type": "duration_ns", "label": "余量", "unit": "s",
                                "min": "0", "max": "1000", "default": "0"}]}]}
    monkeypatch.setattr(AlgorithmCatalog, "current", catalog)
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene = await _scene(client)
            value["scenario_id"] = scene["draft_id"]
            draft = await _experiment(client, value)
            response = await client.post(f"/authoring/experiments/{draft['draft_id']}/runs")
            assert response.status_code == 201, response.text
            await gateway.called.wait()
            frozen = json.loads(Path(gateway.command.authored_definition_path).read_bytes())
            assert frozen["schema"] == "ns3-factory-authored-run-v3"
            assert frozen["nodes"][0]["node_id"] == "0"
            assert frozen["experiment"]["network_update_interval_cycles"] == "3"
            assert frozen["experiment"]["mac_algorithm"]["worker_sha256"] == "b" * 64
            assert [flow["flow_id"] for flow in frozen["experiment"]["flows"]] == ["0", "9007199254740993"]
            context = await client.get(f"/runs/{response.json()['run_id']}/definition-context")
            assert context.status_code == 200, context.text
            assert context.json()["experiment"]["flows"][1]["flow_id"] == "9007199254740993"
            assert context.json()["experiment"]["network_update_interval_cycles"] == "3"
    asyncio.run(run())
