from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path


PLATFORM_ROOT = Path(__file__).resolve().parents[2]


def _fake_preflight_tree(tmp_path: Path, *, environment: bool) -> tuple[Path, Path, Path]:
    root = tmp_path / "Platform"
    build = tmp_path / "build"
    repository = tmp_path / "environment-repository"
    for directory in [
        build / "worker",
        build / "backend/venv/bin",
        root / "frontend/scripts",
        root / "frontend/dist",
        root / "frontend/.tools/node-v24.20.0-linux-x64/bin",
        root / "acceptance",
        root / "backend",
        root / "third_party/nodejs",
        root / "third_party/npm_cache",
    ]:
        directory.mkdir(parents=True, exist_ok=True)
    if environment:
        repository.mkdir()

    def executable(path: Path, source: str) -> None:
        path.write_text(source)
        path.chmod(0o755)

    executable(build / "worker/platform_sim_worker", "#!/bin/sh\nexit 0\n")
    executable(
        build / "worker/platform_resource_catalog_adapter",
        "#!/bin/sh\nprintf '%s\\n' '{\"experiments\":[{\"experiment_id\":\"acceptance4-experiment\"}]}'\n",
    )
    executable(
        build / "backend/venv/bin/python",
        "#!/bin/sh\n"
        "if [ \"$1\" = --version ]; then echo 'Python 3.12.3'; exit 0; fi\n"
        "if [ \"$4\" = freeze ]; then exit 0; fi\n"
        "exit 0\n",
    )
    executable(root / "frontend/scripts/npm.sh", "#!/bin/sh\necho '11.19.0'\n")
    executable(
        root / "frontend/.tools/node-v24.20.0-linux-x64/bin/node",
        "#!/bin/sh\necho 'v24.20.0'\n",
    )
    (root / "frontend/dist/index.html").write_text("demo")
    (root / "acceptance/acceptance4_baseline_v1.json").write_text("{}")
    (root / "backend/requirements.lock").write_text("# empty locked fixture\n")
    (root / "third_party/nodejs/node-v24.20.0-linux-x64.tar.xz").write_bytes(b"fixture")
    return root, build, repository


def test_machine_baseline_matches_authoritative_acceptance_resources(
    tmp_path: Path,
) -> None:
    baseline = json.loads(
        (PLATFORM_ROOT / "acceptance/acceptance4_baseline_v1.json").read_text()
    )
    builder = Path(os.environ["PLATFORM_WORKER_TEST_ASSET_BUILDER_PATH"])
    adapter = Path(os.environ["PLATFORM_RESOURCE_CATALOG_ADAPTER_PATH"])
    repository = tmp_path / "environment-repository"
    subprocess.run([builder, repository], check=True)
    completed = subprocess.run(
        [adapter, repository, "backend-field-v1"],
        check=True,
        capture_output=True,
    )
    catalog = json.loads(completed.stdout)
    scenario = next(
        item
        for item in catalog["scenarios"]
        if item["scenario_id"] == "acceptance4-scenario"
    )
    experiment = next(
        item
        for item in catalog["experiments"]
        if item["experiment_id"] == "acceptance4-experiment"
    )
    hard = baseline["hard_requirements"]
    demo = baseline["demo_parameters"]
    assert hard == {
        "network_node_count_minimum": "3",
        "network_node_count_maximum": "4",
        "communication_rate_bits_per_second": "60",
        "maximum_bit_error_rate": 0.0001,
        "feature_level_fusion_required": True,
        "minimum_bearing_points": "5",
        "maximum_fusion_period_ns": "180000000000",
    }
    assert len(scenario["nodes"]) == int(demo["network_node_count"]) == 4
    moving = [
        node
        for node in scenario["nodes"]
        if any(value != 0.0 for value in node["initial_velocity"].values())
    ]
    assert len(moving) == int(demo["moving_sensor_node_count"]) == 3
    fusion = next(
        node
        for node in scenario["nodes"]
        if node["node_id"] == scenario["fusion_center_node_id"]
    )
    assert all(value == 0.0 for value in fusion["initial_velocity"].values())
    assert experiment["phy"]["bit_rate_bits_per_second"] == hard[
        "communication_rate_bits_per_second"
    ]
    assert experiment["fusion"]["maximum_ber"] == hard[
        "maximum_bit_error_rate"
    ]
    assert experiment["fusion"]["minimum_bearing_points"] == hard[
        "minimum_bearing_points"
    ]
    assert experiment["fusion"]["maximum_fusion_period_ns"] == hard[
        "maximum_fusion_period_ns"
    ]
    assert experiment["mac"]["guard_interval_ns"] == demo[
        "tdma_guard_interval_ns"
    ]
    assert experiment["network_update_interval_cycles"] == demo[
        "network_update_interval_cycles"
    ]


def test_launcher_is_offline_relocatable_and_owns_process_groups() -> None:
    launcher = PLATFORM_ROOT / "scripts/platform_demo.sh"
    preflight = PLATFORM_ROOT / "scripts/platform_preflight.py"
    source = launcher.read_text()
    assert os.access(launcher, os.X_OK)
    assert os.access(preflight, os.X_OK)
    assert "/home/ccc" not in source
    assert "curl " not in source
    assert "wget " not in source
    assert "FetchContent" not in source
    assert 'nohup setsid "${backend_python}"' in source
    assert 'nohup setsid "${npm}"' in source
    assert 'kill -- "-${pid}"' in source
    assert "PLATFORM_NS3_PREFIX" in source


def test_preflight_failure_has_stable_diagnostic(tmp_path: Path) -> None:
    completed = subprocess.run(
        [
            PLATFORM_ROOT / "scripts/platform_preflight.py",
            "--platform-root",
            PLATFORM_ROOT,
            "--build-dir",
            tmp_path / "missing-build",
            "--environment-repository",
            tmp_path / "missing-environment",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 2
    assert completed.stderr.startswith("PREFLIGHT_WORKER_UNAVAILABLE:")


def test_preflight_success_is_explicit(tmp_path: Path) -> None:
    root, build, repository = _fake_preflight_tree(tmp_path, environment=True)
    completed = subprocess.run(
        [
            PLATFORM_ROOT / "scripts/platform_preflight.py",
            "--platform-root", root,
            "--build-dir", build,
            "--environment-repository", repository,
            "--backend-port", "38181",
            "--frontend-port", "38182",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0
    assert completed.stdout == "PREFLIGHT_OK\n"


def test_preflight_missing_environment_is_explicit(tmp_path: Path) -> None:
    root, build, repository = _fake_preflight_tree(tmp_path, environment=False)
    completed = subprocess.run(
        [
            PLATFORM_ROOT / "scripts/platform_preflight.py",
            "--platform-root", root,
            "--build-dir", build,
            "--environment-repository", repository,
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 2
    assert completed.stderr.startswith("PREFLIGHT_ENVIRONMENT_REPOSITORY_UNAVAILABLE:")
