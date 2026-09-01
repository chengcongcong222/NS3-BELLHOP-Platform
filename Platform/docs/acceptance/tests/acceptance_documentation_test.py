from __future__ import annotations

import json
import re
import sys
from pathlib import Path


EXPECTED_REQUIREMENTS = {
    "network_node_count": ("3-4 nodes", "4 nodes", "acceptance_report.network_node_count"),
    "communication_rate": ("60 bit/s", "60 bit/s", "acceptance_report.communication_rate"),
    "bit_error_rate": ("BER <= 1e-4", "maximum 0.0; mean 0.0", "acceptance_report.bit_error_rate"),
    "feature_level_fusion": (
        "feature-level fusion required",
        "AcceptanceBearingFusion with formal FusionResult",
        "acceptance_report.feature_level_fusion",
    ),
    "bearing_point_count": (
        "bearing observations >= 5",
        "6 bearing observations",
        "acceptance_report.bearing_point_count",
    ),
    "fusion_period": ("fusion period <= 180 s", "24 s", "acceptance_report.fusion_period"),
}

UI_LABELS = {
    "network_node_count": "Network nodes",
    "communication_rate": "Communication rate",
    "bit_error_rate": "BER",
    "feature_level_fusion": "Feature-level fusion",
    "bearing_point_count": "Bearing points",
    "fusion_period": "Fusion period",
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main(platform: Path) -> None:
    docs = platform / "docs/acceptance"
    matrix_path = docs / "acceptance_evidence_matrix.json"
    matrix = json.loads(read(matrix_path))
    assert matrix["schema_version"] == 1
    assert matrix["baseline"] == {
        "baseline_id": "Acceptance4Node",
        "baseline_version": "1",
        "authority": "Platform/acceptance/acceptance4_baseline_v1.json",
    }
    assert matrix["release"] == {
        "release_id": "P0-S5-05",
        "source_revision": "@SOURCE_REVISION@",
        "build_target": "linux-x86_64",
        "archive_filename": "ns3-bellhop-platform-p0-s5-05-linux-x86_64.tar.gz",
        "archive_sha256": "@ARCHIVE_SHA256@",
    }
    assert matrix["policies"]["verdict_authority"] == "AcceptanceEvidence.acceptance_report"
    assert matrix["policies"]["matrix_role"] == "field-mapping-only-no-recomputation"

    rows = {row["id"]: row for row in matrix["hard_requirements"]}
    assert set(rows) == set(EXPECTED_REQUIREMENTS)
    assert len({row["verdict_path"] for row in rows.values()}) == 6
    for identifier, (requirement, actual, verdict_path) in EXPECTED_REQUIREMENTS.items():
        row = rows[identifier]
        assert row["requirement"] == requirement
        assert row["current_actual"] == actual
        assert row["verdict_path"] == verdict_path
        assert row["actual_paths"] and row["ui_location"]
        assert row["backend_source"] and row["test_references"]
        assert row["evidence_type"] and row["limitation"]
        assert "calculation" not in row and "formula" not in row
        assert (platform.parent / row["backend_source"]).is_file()

    frontend = read(platform / "frontend/src/features/pages.tsx")
    for identifier, label in UI_LABELS.items():
        assert f'metric: "{label}"' in frontend
        verdict_field = rows[identifier]["verdict_path"].split(".")[-1]
        assert f"report.{verdict_field}" in frontend
    assert 'requirement: "3–4 nodes（third-party requirement）"' in frontend
    for field in (
        "maximum_ber",
        "mean_ber",
        "minimum_bearing_points",
        "maximum_fusion_period_ns",
        "required_maximum_ber",
        "required_minimum_bearing_points",
        "required_maximum_fusion_period_ns",
    ):
        assert field in frontend

    golden = matrix["golden_run"]
    assert golden["identity"] == {
        "release_id": "P0-S5-05",
        "experiment_id": "acceptance4-experiment",
        "experiment_version": "1",
        "scenario_id": "acceptance4-scenario",
        "scenario_version": "1",
        "environment_asset_id": "reference-shallow-water-v1",
        "environment_checksum": "fb64e543f9042c52",
        "deterministic_seed": "0",
    }
    assert golden["expected"] == {
        "node_count": "4",
        "communication_rate_bits_per_second": "60",
        "transmission_count": "6",
        "channel_signal_count": "18",
        "reception_count": "18",
        "local_delivery_count": "6",
        "bearing_observation_count": "6",
        "fusion_period_ns": "24000000000",
        "overall": "Pass",
        "ber_source": "Modeled",
    }
    for digest in golden["normalized_hashes"].values():
        assert re.fullmatch(r"[0-9a-f]{64}", digest)

    for relative in matrix["repository_targets"]:
        assert (platform.parent / relative).is_file(), relative

    required_docs = (
        "acceptance_architecture.md",
        "acceptance_dry_run_record.md",
        "acceptance_demo_script.md",
        "acceptance_evidence_index.md",
        "acceptance_evidence_matrix.md",
        "acceptance_presentation_baseline.md",
        "acceptance_q_and_a.md",
        "final_acceptance_presentation_source.md",
        "final_acceptance_report_source.md",
        "manual_screenshot_capture.md",
        "screenshot_manifest.md",
    )
    combined = "\n".join(read(docs / name) for name in required_docs)
    for term in (
        "仿真模型 BER",
        "Reference/proxy modeled environment",
        "Bellhop-derived propagation",
        "ns-3 discrete-event simulation kernel",
        "feature-level fusion",
        "bearing observations",
        "fusion period",
    ):
        assert term in combined
    for prohibited_claim in (
        "fully self-contained Linux package",
        "hardware BER = 0",
        "measured BER = 0",
        "zero-error communication",
    ):
        assert prohibited_claim not in combined
    assert "不声称 GitHub Release 已发布" in combined
    assert "hardware calibration TBD" in combined
    assert "硬件/实测 BER" in combined
    assert "Windows native" in combined
    assert "Extended6Node" in combined and "工程扩展" in combined

    # Contextual claim lint: exact affirmative forms are forbidden in final
    # acceptance sources. Q&A and warning sections use distinct negated wording.
    for prohibited_affirmative in (
        "Requirement: 5 nodes",
        "5 bearing points = 5 nodes",
        "hardware BER passed",
        "measured BER = 0",
        "real-site environment",
        "real measured sound field",
        "real-time Bellhop",
        "hardware source level verified",
    ):
        assert prohibited_affirmative not in combined
    assert not re.search(r"(?<![0-9])60\s*kbps(?![0-9])", combined, flags=re.IGNORECASE)

    architecture = read(docs / "acceptance_architecture.md")
    for evidence in (
        "Simulator::Now",
        "Schedule",
        "Run",
        "Stop",
        "Destroy",
        "platform_ns3_kernel_smoke_test",
        "platform_ns3_event_dispatcher_test",
        "platform_ns3_signal_lifecycle_integration_test",
        "platform_assembly_ns3_gateway_multirun_test",
        "platform_ns3_acceptance_scenario_integration_test",
    ):
        assert evidence in architecture
    assert "Environment 不标记为 M6" in architecture
    for required_claim in (
        "ns-3 discrete-event simulation kernel",
        "ns3::Simulator",
        "M1 / Ns3KernelGateway",
    ):
        assert required_claim in combined
    assert "M1 is scheduler authority" not in combined
    assert "Platform has another scheduler" not in combined

    historical = read(platform / "docs/release/p0_s5_03_historical_release.md")
    report_source = read(docs / "final_acceptance_report_source.md")
    assert "first canonical offline runtime release" in historical
    assert "final acceptance-aligned release" in report_source

    demo = read(docs / "acceptance_demo_script.md")
    headings = re.findall(r"^## (\d+)\. ", demo, flags=re.MULTILINE)
    assert headings == [str(number) for number in range(1, 15)]
    for label in (
        "Operator action",
        "Expected screen",
        "What to say",
        "Evidence to point at",
        "Possible expert question",
        "Approved answer",
    ):
        assert demo.count(label) >= 14
    for recovery in (
        "PLATFORM_NS3_PREFIX",
        "端口占用",
        "Asset checksum",
        "backend 未 ready",
    ):
        assert recovery in demo

    index = read(docs / "acceptance_evidence_index.md")
    for target in re.findall(r"\]\((\.\./\.\./[^)]+)\)", index):
        assert (docs / target).resolve().is_file(), target

    screenshot = read(docs / "screenshot_manifest.md")
    assert screenshot.count("| SS-") == 11
    assert "/system/info" in screenshot

    # No generator is used in S5-04. The machine document is version-controlled;
    # repeated canonical serialization of the same input must be byte-identical.
    first = json.dumps(matrix, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    second = json.dumps(json.loads(read(matrix_path)), ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    assert first == second

    schema = json.loads(read(docs / "acceptance_evidence_matrix_schema_v1.json"))
    assert schema["properties"]["schema_version"]["const"] == 1
    assert schema["properties"]["baseline"]["properties"]["baseline_id"]["const"] == "Acceptance4Node"
    assert set(schema["required"]) == {
        "schema_version", "baseline", "release", "policies", "hard_requirements",
        "golden_run", "evidence_hierarchy", "repository_targets",
    }


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: acceptance_documentation_test.py PLATFORM_ROOT")
    main(Path(sys.argv[1]).resolve())
