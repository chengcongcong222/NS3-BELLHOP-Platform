from ns3_factory_backend.experiment_presets import experiment_presets
from ns3_factory_backend.modem_configuration import validate_osdm


def test_presets_are_complete_valid_and_versioned():
    profiles = experiment_presets()
    assert [(p["id"], p["version"]) for p in profiles] == [
        ("bellhop-osdm", 1), ("recorded-osdm", 1)]
    for p in profiles:
        assert validate_osdm({"physical_layer": p["physical_layer"],
            "execution_mode": "PlanningV1", "waveform_modulation": "Osdm",
            "bit_rate_bits_per_second": "6000", "occupied_bandwidth_hz": 5000,
            "osdm": p["osdm"]}) == []
    assert profiles[0]["osdm"]["cyclic_prefix"] == 64
    assert profiles[1]["osdm"]["cyclic_prefix"] == 128


def test_preset_reads_cannot_mutate_authority_or_historical_values():
    first = experiment_presets()
    first[0]["osdm"]["cyclic_prefix"] = 96
    assert experiment_presets()[0]["osdm"]["cyclic_prefix"] == 64
    assert first[0]["osdm"]["cyclic_prefix"] == 96
