from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path

import pytest
from pydantic import ValidationError

from ns3_factory_backend.wire import (
    WaveformSummary,
    Environment,
    Execution,
    Preset,
    StartRunCommand,
    WorkerCompleted,
    WorkerFailed,
    WorkerRunEvent,
    WorkerStarted,
    encode_start_run_command,
    parse_worker_message,
)


def test_waveform_projection_rejects_impossible_error_count() -> None:
    with pytest.raises(ValidationError):
        WaveformSummary(transmission_id="1", receiver_node_id="0",
                        payload_bit_count="120", bit_error_count="121", crc_ok=False)


def test_all_cpp_golden_worker_messages_parse_in_python() -> None:
    fixture = Path(os.environ["PLATFORM_WORKER_WIRE_GOLDEN_FIXTURE_PATH"])
    process = subprocess.run(
        [fixture], check=True, capture_output=True, text=False
    )
    messages = [parse_worker_message(line) for line in process.stdout.splitlines()]
    assert [type(item) for item in messages] == [
        WorkerStarted,
        WorkerRunEvent,
        WorkerCompleted,
        WorkerFailed,
    ]
    visualization = messages[2].result.visualization
    assert visualization is not None
    assert visualization.positions[0].node_id == "0"
    assert visualization.positions[0].z_meters == -3.0
    assert visualization.fusion_sources[0].observation_sequence == "7"
    assert visualization.waveform[0].bit_error_count == "2"
    assert visualization.waveform[0].crc_ok is False


def test_python_command_is_schema_v1_and_preserves_large_decimal_strings() -> None:
    command = StartRunCommand(
        run_id="python-command-run",
        preset=Preset(
            scenario_id="acceptance4-scenario",
            experiment_id="acceptance4-experiment",
            definition_version="9007199254740993",
            acceptance_profile="Acceptance4Node",
        ),
        environment=Environment(
            asset_id="backend-field-v1", asset_format_version="1"
        ),
        execution=Execution(
            simulation_cycle_count="2",
            rx_quality_mode="ModeledBpskAwgn",
            equivalent_noise_power_db_re_1upa2=45.0,
            deterministic_seed="18446744073709551614",
        ),
    )
    document = json.loads(encode_start_run_command(command))
    assert document["schema_version"] == 1
    assert document["preset"]["definition_version"] == "9007199254740993"
    assert document["execution"]["deterministic_seed"] == (
        "18446744073709551614"
    )


def test_internal_wire_model_preserves_extended_profile_compatibility() -> None:
    preset = Preset(
        scenario_id="extended-scenario",
        experiment_id="extended-experiment",
        definition_version="1",
        acceptance_profile="Extended6Node",
    )
    assert preset.acceptance_profile == "Extended6Node"


@pytest.mark.parametrize(
    "bad_value", ["", "00", "01", "-0", "1.0", 1, 1.0]
)
def test_noncanonical_decimal_values_are_rejected(bad_value) -> None:
    with pytest.raises(ValidationError):
        Execution(
            simulation_cycle_count=bad_value,
            rx_quality_mode="None",
            equivalent_noise_power_db_re_1upa2=45.0,
            deterministic_seed="0",
        )
