"""Prepare temporary *inputs*, not results, for real-browser paired-run checks."""
import asyncio
import json
import os
import subprocess
import sys
from pathlib import Path

import httpx

from ns3_factory_backend.api import BackendSettings, create_app
from planning_authoring_test import _scene, _experiment, _payload


async def seed(root):
    repository = root / "repository"
    repository.mkdir(exist_ok=True)
    subprocess.run([os.environ["PLATFORM_REFERENCE_ENVIRONMENT_BUILDER_PATH"],
        str(Path(__file__).resolve().parents[2] / "environment/assets/reference_shallow_water_v1"),
        str(repository)], check=True)
    worker = Path(os.environ["PLATFORM_SIM_WORKER_PATH"])
    app = create_app(BackendSettings(worker_executable=worker,
        environment_repository_root=repository, authoring_workspace_root=root / "workspace",
        resource_catalog_adapter=worker.with_name("platform_resource_catalog_adapter"),
        acceptance_environment_asset_id="reference-shallow-water-v1"))
    async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
        scene = await _scene(client,end_seconds=1000)
        payload = {**scene["payload"], "environment_asset_id": "reference-shallow-water-v1"}
        response = await client.put(f'/authoring/drafts/scenario/{scene["draft_id"]}',
            headers={"If-Match": "1"}, json={"kind": "scenario", "name": "浏览器三节点对照", "payload": payload})
        assert response.status_code == 200, response.text
        experiment = await _experiment(client, {**_payload(scene["draft_id"]),
            "physical_layer": "Channel2Waveform", "waveform_modulation": "Bpsk", "waveform_noise": "None"})
        bearing = await _experiment(client,{**_payload(scene["draft_id"]),
            "physical_layer":"Channel2Waveform","waveform_modulation":"Osdm","waveform_noise":"None",
            "workload":"BearingFusion","flows":[],"fusion_receiver_node_id":"2",
            "planning_cycle_duration_ns":"120000000000","simulation_cycle_count":"3",
            "target_x_m":300.,"target_y_m":150.,
            "osdm":{"block_length":256,"vector_length":16,"cyclic_prefix":64,
                    "constellation":"Qpsk","minimum_sync_correlation":.05,"maximum_sync_search":128}})
        (root / "seed.json").write_text(json.dumps({"draft_id": experiment["draft_id"],
                                                  "bearing_draft_id":bearing["draft_id"]}))
        assert (await client.get("/runs")).json() == []
    await app.state.catalog.shutdown()


if __name__ == "__main__":
    asyncio.run(seed(Path(sys.argv[1])))
