"""Synthetic binary fixtures exercise binding validation, not UWA evidence."""
import copy
import hashlib
import json
import struct
import asyncio
import shutil
import httpx

import pytest
from pydantic import ValidationError

from ns3_factory_backend.catalog import CatalogError
from ns3_factory_backend.recorded_proxy import ProxyBindings, resolve


def test_physics_identity_excludes_layout_but_not_signal_settings(recording):
    from types import SimpleNamespace
    from ns3_factory_backend.recorded_proxy import physics_digest
    resolved = resolve(recording, ProxyBindings.model_validate(config()))
    definition = {
        "nodes": resolved["nodes"], "recorded_proxy": resolved,
        "experiment": {"name":"first", "waveform_modulation":"Osdm", "deterministic_seed":"23"},
        "scenario_revision":1, "proxy_ui_layout":{"0":{"x":10,"y":20}},
    }
    execution = SimpleNamespace(model_dump=lambda **_: {"duration_ns":"1000000000"})
    initial = physics_digest(definition, execution)
    moved = copy.deepcopy(definition)
    moved.update(scenario_revision=2, proxy_ui_layout={"0":{"x":200,"y":400}})
    moved["experiment"]["name"] = "renamed"
    assert physics_digest(moved, execution) == initial
    moved["recorded_proxy"]["resolved_links"][0]["relative_amplitude"] = .5
    assert physics_digest(moved, execution) != initial


@pytest.fixture
def recording(tmp_path):
    root = tmp_path/"recorded-channels"
    directory = root/"synthetic"/"1"
    directory.mkdir(parents=True)
    samples = struct.pack("<" + "d"*400, *([1., 0., 0., 0.]*100))
    (directory/"channel.f64le").write_bytes(samples)
    (directory/"phase.f64le").write_bytes(b"")
    manifest = {
        "schema": "ns3-factory-recorded-channel-v1", "asset_id": "synthetic", "version": 1,
        "gain_reference": "Normalized", "geometry": "Unspecified",
        "reference_revision": "dbf373ca7cd07ac4eb1b24a191d76bea18f73560",
        "fc": 1000., "fs_delay": 1000., "fs_time": 1., "usable_bandwidth_hz": 1000.,
        "duration_s": 99., "snapshots": 100, "delay_taps": 2, "phase_count": 0,
        "element": 0, "original_axis_count": 1, "axis_kind": "ReceiverElement",
        "resample_up": 1, "resample_down": 1, "tracking": "None",
        "source_sha256": hashlib.sha256(b"synthetic-test-not-a-measurement").hexdigest(),
        "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                  for name, data in (("channel.f64le", samples), ("phase.f64le", b""))},
    }
    (directory/"manifest.json").write_text(json.dumps(manifest))
    return root


def config():
    return {
        "schema": "ns3-factory-recorded-proxy-bindings-v1",
        "nodes": [{"node_id": str(i), "can_transmit": True, "can_receive": True} for i in range(3)],
        "allocation": "GlobalSame",
        "samples": [{"asset_id": "synthetic", "version": 1, "start_sample": "0",
                     "modeled_reference_delay_ns": "200000000", "relative_amplitude": 1.}],
        "overrides": [],
        "receiver_noise": [{"rx_id": str(i), "mode": "None"} for i in range(3)],
        "carrier_hz": 1000, "baseband_rate_hz": 1000,
        "occupied_bandwidth_hz": 500., "duration_ns": "1000000000",
    }


def test_full_fanout_and_canonical_order(recording):
    spec = config()
    first = resolve(recording, ProxyBindings.model_validate(spec))
    assert len(first["resolved_links"]) == 6
    assert not first["execution_ready"]
    assert not first["assumptions"]["real_joint_multitransmitter_measured"]
    spec["nodes"].reverse()
    spec["receiver_noise"].reverse()
    assert first == resolve(recording, ProxyBindings.model_validate(spec))

def test_node_modems_are_explicit_capabilities_and_common_clock(recording):
    spec = config()
    spec["transmitter_modulations"] = [{"node_id":"0","modulation":"Bpsk"},{"node_id":"1","modulation":"Bfsk"}]
    result = resolve(recording,ProxyBindings.model_validate(spec))
    assert result["transmitter_modulations"] == spec["transmitter_modulations"]
    spec["baseband_rate_hz"] = 999
    with pytest.raises(CatalogError,match="100"):
        resolve(recording,ProxyBindings.model_validate(spec))
    spec["baseband_rate_hz"] = 1000
    spec["transmitter_modulations"].append({"node_id":"0","modulation":"Osdm"})
    with pytest.raises(CatalogError,match="不能重复"):
        resolve(recording,ProxyBindings.model_validate(spec))

def test_resource_load_budget_is_unique_tensor_not_per_link(recording):
    spec = ProxyBindings.model_validate(config())
    # One 3200-byte tensor, six links: retained + two loader copies = 9600.
    resolve(recording,spec,resource_budget_bytes=9600)
    with pytest.raises(CatalogError,match="资源载入预算"):
        resolve(recording,spec,resource_budget_bytes=9599)


def test_explicit_edges_do_not_infer_reverse_or_overhearing(recording):
    spec = config()
    spec["allocation"] = "Explicit"
    spec["overrides"] = [{"tx_id": "0", "rx_id": "1", "sample": spec["samples"][0]}]
    spec["samples"] = []
    with pytest.raises(CatalogError, match="N0 → N2"):
        resolve(recording, ProxyBindings.model_validate(spec))


def test_override_does_not_rewrite_global_sample(recording):
    spec = config()
    sample = {**spec["samples"][0], "relative_amplitude": .5}
    spec["overrides"] = [{"tx_id": "0", "rx_id": "1", "sample": sample}]
    before = copy.deepcopy(spec)
    result = resolve(recording, ProxyBindings.model_validate(spec))
    assert result["resolved_links"][0]["relative_amplitude"] == .5
    assert all(l["relative_amplitude"] == 1. for l in result["resolved_links"][1:])
    assert spec == before


def test_pool_order_and_override_precedence_are_frozen(recording):
    spec = config()
    spec["allocation"] = "GlobalPool"
    spec["samples"].append({**spec["samples"][0], "start_sample": "10"})
    spec["overrides"] = [{"tx_id": "0", "rx_id": "2",
                          "sample": {**spec["samples"][0], "start_sample": "20"}}]
    result = resolve(recording, ProxyBindings.model_validate(spec))
    assert [link["start_sample"] for link in result["resolved_links"]] == ["0", "20", "0", "10", "0", "10"]
    assert result["resolved_links"][1]["origin"] == "EdgeOverride"
    spec["nodes"].reverse()
    assert resolve(recording, ProxyBindings.model_validate(spec)) == result


@pytest.mark.parametrize("change", [
    {"receiver_noise": []}, {"carrier_hz": 2000}, {"duration_ns": "100000000000"},
    {"overrides": [{"tx_id": "0", "rx_id": "0", "sample": config()["samples"][0]}]},
    {"samples": [{**config()["samples"][0], "modeled_reference_delay_ns": "1"}]},
])
def test_incompatible_bindings_rejected(recording, change):
    with pytest.raises(CatalogError):
        resolve(recording, ProxyBindings.model_validate({**config(), **change}))


def test_corrupt_samples_not_replaced(recording):
    (recording/"synthetic/1/channel.f64le").write_bytes(b"\0"*3200)
    with pytest.raises(CatalogError):
        resolve(recording, ProxyBindings.model_validate(config()))


def test_ui_geometry_is_not_a_physical_input():
    spec = config()
    spec["nodes"][0]["ui_x"] = 4
    with pytest.raises(ValidationError):
        ProxyBindings.model_validate(spec)


def test_validation_api_does_not_start_a_worker(recording, tmp_path):
    from run_definition_context_test import _application
    async def check():
        app, gateway = _application(tmp_path/"api")
        shutil.copytree(recording, tmp_path/"api/workspace/recorded-channels")
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            response = await client.post("/authoring/recorded-proxy/validate", json=config())
            assert response.status_code == 200, response.text
            assert len(response.json()["resolved_links"]) == 6
            assert response.json()["execution_ready"] is False
            assert (await client.get("/runs")).json() == []
            assert not gateway.called.is_set()
    asyncio.run(check())
