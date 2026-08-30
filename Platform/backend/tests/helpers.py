from __future__ import annotations

import inspect
import json
from collections.abc import Iterable

from ns3_factory_backend.gateway import WorkerGatewayResult
from ns3_factory_backend.resources import ResourceCatalog, ResourceCatalogDocument
from ns3_factory_backend.wire import (
    WorkerCompleted,
    WorkerFailed,
    WorkerMessage,
    WorkerRunEvent,
    WorkerStarted,
    parse_worker_message,
)


def message(value: dict) -> WorkerMessage:
    return parse_worker_message(json.dumps(value, separators=(",", ":")).encode())


def started(run_id: str) -> WorkerStarted:
    value = message(
        {"schema_version": 1, "type": "WorkerStarted", "run_id": run_id}
    )
    assert isinstance(value, WorkerStarted)
    return value


def event(run_id: str, sequence: int) -> WorkerRunEvent:
    value = message(
        {
            "schema_version": 1,
            "type": "WorkerRunEvent",
            "run_id": run_id,
            "sequence": str(sequence),
            "trace": {
                "occurred_at_ns": str(sequence - 1),
                "kind": "CycleCommit",
                "payload": {
                    "cycle_id": str(sequence - 1),
                    "base_snapshot_version": str(sequence - 1),
                    "committed_snapshot_version": str(sequence),
                    "committed_at_ns": str(sequence - 1),
                },
            },
        }
    )
    assert isinstance(value, WorkerRunEvent)
    return value


def _run_record(run_id: str, lifecycle: str, failed: bool = False) -> dict:
    return {
        "run_id": run_id,
        "experiment_id": "acceptance4-experiment",
        "experiment_version": "1",
        "scenario_id": "acceptance4-scenario",
        "scenario_version": "1",
        "environment_asset_id": "backend-field-v1",
        "environment_format_version": "1",
        "lifecycle": lifecycle,
        "simulation_started_at_ns": "0",
        "simulation_ended_at_ns": "1",
        "final_snapshot_version": "1",
        "failure": (
            {"code": "Unavailable", "message": "owned simulation failure"}
            if failed
            else None
        ),
        "event_stream_complete": True,
    }


def _acceptance_report(overall: str) -> dict:
    metric = "Pass" if overall == "Pass" else "Fail"
    return {
        "network_node_count": metric,
        "communication_rate": metric,
        "bit_error_rate": metric,
        "feature_level_fusion": metric,
        "bearing_point_count": metric,
        "fusion_period": metric,
        "overall": overall,
        "evaluated_target_receptions": "1",
        "missing_ber_evidence_count": "0",
        "maximum_ber": 0.00001,
        "mean_ber": 0.00001,
        "required_maximum_ber": 0.0001,
        "minimum_bearing_points": "5",
        "required_minimum_bearing_points": "5",
        "maximum_fusion_period_ns": "1",
        "required_maximum_fusion_period_ns": "24000000000",
        "ber_reason": "",
    }


def completed(run_id: str, overall: str = "Pass") -> WorkerCompleted:
    value = message(
        {
            "schema_version": 1,
            "type": "WorkerCompleted",
            "run": _run_record(run_id, "Completed"),
            "result": {
                "run_id": run_id,
                "projection": {
                    "simulation_started_at_ns": "0",
                    "simulation_ended_at_ns": "1",
                    "simulation_duration_ns": "1",
                    "final_snapshot_version": "1",
                    "cycle_count": "2",
                    "node_count": "4",
                    "transmission_count": "1",
                    "channel_signal_count": "1",
                    "channel_no_arrival_count": "0",
                    "reception_count": "1",
                    "local_delivery_count": "1",
                },
                "acceptance_report": _acceptance_report(overall),
                "fusion_results": [],
                "nodes": [],
            },
        }
    )
    assert isinstance(value, WorkerCompleted)
    return value


def failed(run_id: str) -> WorkerFailed:
    value = message(
        {
            "schema_version": 1,
            "type": "WorkerFailed",
            "run_id": run_id,
            "category": "Simulation",
            "error": {"code": "Unavailable", "message": "owned failure"},
            "run": _run_record(run_id, "Failed", failed=True),
        }
    )
    assert isinstance(value, WorkerFailed)
    return value


async def observe(callback, values: Iterable[WorkerStarted | WorkerRunEvent]):
    for value in values:
        result = callback(value)
        if inspect.isawaitable(result):
            await result


def success_result(run_id: str, overall: str = "Pass") -> WorkerGatewayResult:
    return WorkerGatewayResult(
        exit_code=0,
        completed=completed(run_id, overall),
        failed=None,
        stderr_diagnostics="fixture diagnostic",
    )


def failure_result(run_id: str) -> WorkerGatewayResult:
    return WorkerGatewayResult(
        exit_code=3,
        completed=None,
        failed=failed(run_id),
        stderr_diagnostics="fixture diagnostic",
    )


def resource_catalog() -> ResourceCatalog:
    return ResourceCatalog(
        ResourceCatalogDocument.model_validate(
            {
                "schema_version": 1,
                "environments": [
                    {
                        "environment_asset_id": "backend-field-v1",
                        "format": "NS3_FACTORY_ACOUSTIC_FIELD",
                        "package_format_version": "1",
                        "asset_format_version": "1",
                        "provenance": {
                            "producer": "Manual",
                            "created_by_build_version": "test",
                            "source_description": "typed test fixture",
                            "raw_source_logical_name": "",
                            "normalization_policy_version": "",
                        },
                        "coordinate_frame": {
                            "surface_z_meters": 0.0,
                            "vertical_axis": "PositiveUp",
                        },
                        "axes": {
                            "frequency": {
                                "unit": "Hz",
                                "count": "1",
                                "minimum": 25000.0,
                                "maximum": 25000.0,
                            },
                            "source_depth": {
                                "unit": "m",
                                "count": "1",
                                "minimum": 0.0,
                                "maximum": 0.0,
                            },
                            "receiver_depth": {
                                "unit": "m",
                                "count": "1",
                                "minimum": 0.0,
                                "maximum": 0.0,
                            },
                            "horizontal_range": {
                                "unit": "m",
                                "count": "1",
                                "minimum": 0.0,
                                "maximum": 0.0,
                            },
                        },
                        "cell_count": "1",
                        "signal_cell_count": "1",
                        "no_arrival_cell_count": "0",
                        "payload_bytes": "1",
                        "checksum": {
                            "algorithm": "FNV1A64",
                            "value": "0000000000000001",
                        },
                        "validation_state": "Valid",
                    }
                ],
                "scenarios": [
                    {
                        "scenario_id": "acceptance4-scenario",
                        "version": "1",
                        "name": "Acceptance 4-Node Scenario",
                        "nodes": [
                            {
                                "node_id": "99",
                                "can_transmit": False,
                                "can_receive": True,
                                "duplex_mode": "HalfDuplex",
                                "initial_position": {
                                    "x_meters": 0.0,
                                    "y_meters": 0.0,
                                    "z_meters": -8.0,
                                },
                                "initial_velocity": {
                                    "x_meters_per_second": 0.0,
                                    "y_meters_per_second": 0.0,
                                    "z_meters_per_second": 0.0,
                                },
                            }
                        ],
                        "environment": {
                            "environment_asset_id": "backend-field-v1",
                            "asset_format_version": "1",
                        },
                        "mobility": {"model": "ConstantVelocity"},
                        "fusion_center_node_id": "99",
                    }
                ],
                "experiments": [
                    {
                        "experiment_id": "acceptance4-experiment",
                        "version": "1",
                        "name": "Acceptance 4-Node Experiment",
                        "scenario": {
                            "scenario_id": "acceptance4-scenario",
                            "version": "1",
                        },
                        "routing": {"mode": "DirectToFusionCenter"},
                        "mac": {
                            "mode": "Tdma",
                            "slot_duration_ns": "4000000000",
                            "guard_interval_ns": "2000000000",
                        },
                        "phy": {
                            "bit_rate_bits_per_second": "60",
                            "center_frequency_hz": 25000.0,
                            "occupied_bandwidth_hz": 4000.0,
                            "source_level_db_re_1upa_at_1m": 110.0,
                            "equivalent_noise_power_db_re_1upa2": 45.0,
                            "rx_quality_mode": "ModeledBpskAwgn",
                        },
                        "fusion": {
                            "workload": "AcceptanceBearingFusion",
                            "acceptance_profile": "Acceptance4Node",
                            "minimum_bearing_points": "5",
                            "maximum_fusion_period_ns": "180000000000",
                            "maximum_ber": 0.0001,
                        },
                        "network_update_interval_cycles": "10",
                        "simulation_cycle_count": "2",
                        "deterministic_seed": "19",
                    }
                ],
            },
            strict=False,
        )
    )
