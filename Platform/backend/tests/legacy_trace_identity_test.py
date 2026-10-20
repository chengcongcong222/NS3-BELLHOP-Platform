"""Re-execute historical frozen inputs; compare full recorded traces, not constants."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest
from ns3_factory_backend.wire import WorkerRunEvent

pytestmark = pytest.mark.skipif(not all(os.environ.get(k) for k in (
    "PLATFORM_SIM_WORKER_PATH","PLATFORM_REFERENCE_ENVIRONMENT_BUILDER_PATH","PLATFORM_LEGACY_TEST_WORKSPACE")),
    reason="requires historical frozen definitions/resources and actual worker")

@pytest.mark.parametrize("index",[0,2,3])
def test_historical_a_b_c_trace_identity(tmp_path,index):
    platform = Path(__file__).resolve().parents[2]
    baseline = json.loads((platform/"docs/channel3/evidence-20261016/final-main-runs.json").read_text())["runs"][index]
    source = Path(os.environ["PLATFORM_LEGACY_TEST_WORKSPACE"])
    repository = tmp_path/"repository"; repository.mkdir()
    workspace = tmp_path/"workspace"; (workspace/"run-definitions").mkdir(parents=True)
    subprocess.run([os.environ["PLATFORM_REFERENCE_ENVIRONMENT_BUILDER_PATH"],
        str(platform/"environment/assets/reference_shallow_water_v1"),str(repository)],check=True,capture_output=True)
    run = baseline["run"]
    command = {"schema_version":1,"type":"StartRunCommand","run_id":f"legacy-trace-{index}",
        "preset":{"scenario_id":run["scenario_id"],"experiment_id":run["experiment_id"],
                  "definition_version":run["scenario_version"],"acceptance_profile":"Acceptance4Node"},
        "environment":{"asset_id":run["environment_asset_id"],"asset_format_version":run["environment_format_version"]},
        "execution":baseline["context"]["execution"],
        "authored_definition_path":None,"authored_definition_sha256":None}
    digest = baseline["context"]["definition_sha256"]
    if digest:
        original = source/"run-definitions"/f"{digest}.json"
        content = original.read_bytes()
        assert hashlib.sha256(content).hexdigest() == digest
        target = workspace/"run-definitions"/original.name
        target.write_bytes(content)
        definition = json.loads(content)
        if "recorded_channel" in definition:
            rc = definition["recorded_channel"]
            m = rc["manifest"]
            shutil.copytree(source/"recorded-channels"/m["asset_id"]/"1",
                            workspace/"recorded-channels"/m["asset_id"]/"1")
            if rc["noise"]["mode"] != "None":
                n = rc["noise"]["manifest"]
                shutil.copytree(source/"recorded-noise"/n["asset_id"]/"1",
                                workspace/"recorded-noise"/n["asset_id"]/"1")
        command.update(authored_definition_path=str(target),authored_definition_sha256=digest)
    completed = subprocess.run([os.environ["PLATFORM_SIM_WORKER_PATH"],str(repository)],
        input=json.dumps(command)+"\n",capture_output=True,text=True,timeout=60)
    assert completed.returncode == 0,completed.stdout+completed.stderr
    messages = [json.loads(line) for line in completed.stdout.splitlines()]
    traces = [WorkerRunEvent.model_validate(m).trace.model_dump(mode="json")
              for m in messages if m["type"]=="WorkerRunEvent"]
    assert traces == baseline["traces"]
    # Preserve the historical API field ordering used by that evidence file.
    node = os.environ.get("PLATFORM_TEST_NODE") or shutil.which("node")
    assert node, "Historical hashes use JavaScript JSON numeric serialization; provide PLATFORM_TEST_NODE"
    encoded = subprocess.check_output([node,"-e",
        "let s='';process.stdin.on('data',d=>s+=d);process.stdin.on('end',()=>process.stdout.write(JSON.stringify(JSON.parse(s))));"],
        input=json.dumps(traces,separators=(",",":")).encode())
    assert hashlib.sha256(encoded).hexdigest() == baseline["event_sha256"]
