from copy import deepcopy
import asyncio
import json
from pathlib import Path
import httpx
import pytest
from ns3_factory_backend.authoring import validate_payload
from ns3_factory_backend.modem_configuration import osdm_airtime_ns
from planning_authoring_test import _payload, _scene, _experiment
from run_definition_context_test import _application


def payload():
    return {**_payload(), "physical_layer": "Channel2Waveform", "waveform_modulation": "Osdm",
            "osdm": {"block_length": 256, "vector_length": 16, "cyclic_prefix": 64,
                     "constellation": "Qpsk", "minimum_sync_correlation": .05,
                     "maximum_sync_search": 128}}


def test_configuration_and_framing():
    valid = payload()
    assert not validate_payload("experiment", valid)
    assert osdm_airtime_ns(2, 12000, valid["osdm"]) == 106666667
    assert osdm_airtime_ns(4, 60, valid["osdm"]) == 21333333334


@pytest.mark.parametrize("field,value", [("block_length", 255), ("vector_length", 7),
    ("cyclic_prefix", 257), ("maximum_sync_search", 32), ("constellation", "unknown"),
    ("minimum_sync_correlation", 0), ("block_length", True), ("cyclic_prefix", 1.5)])
def test_reject_invalid_osdm(field, value):
    invalid = payload(); invalid["osdm"][field] = value
    assert validate_payload("experiment", invalid)


def test_no_inactive_parameters_or_scalar_osdm():
    invalid = payload(); invalid["waveform_modulation"] = "Bpsk"
    assert validate_payload("experiment", invalid)
    invalid = payload(); invalid["physical_layer"] = "Channel1Abstract"
    assert validate_payload("experiment", invalid)


def test_api_freezes_osdm_and_reopens_exact_parameters(tmp_path: Path):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene = await _scene(client)
            config = payload(); config["scenario_id"] = scene["draft_id"]
            draft = await _experiment(client, config)
            assert not draft["validation_issues"]
            response = await client.post(f"/authoring/experiments/{draft['draft_id']}/runs")
            assert response.status_code == 201, response.text
            await gateway.called.wait()
            frozen = json.loads(Path(gateway.command.authored_definition_path).read_text())
            assert frozen["experiment"]["osdm"] == config["osdm"]
            assert gateway.command.execution.waveform_modulation == "Osdm"
            context = await client.get(f"/runs/{response.json()['run_id']}/definition-context")
            assert context.status_code == 200, context.text
            assert context.json()["experiment"]["phy"]["osdm"] == config["osdm"]
    asyncio.run(run())
