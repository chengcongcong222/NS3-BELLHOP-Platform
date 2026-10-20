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
    repository.mkdir()
    subprocess.run([os.environ["PLATFORM_REFERENCE_ENVIRONMENT_BUILDER_PATH"],
        str(Path(__file__).resolve().parents[2] / "environment/assets/reference_shallow_water_v1"),
        str(repository)], check=True)
    worker = Path(os.environ["PLATFORM_SIM_WORKER_PATH"])
    app = create_app(BackendSettings(worker_executable=worker,
        environment_repository_root=repository, authoring_workspace_root=root / "workspace",
        resource_catalog_adapter=worker.with_name("platform_resource_catalog_adapter"),
        acceptance_environment_asset_id="reference-shallow-water-v1"))
    async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
        scene = await _scene(client)
        payload = {**scene["payload"], "environment_asset_id": "reference-shallow-water-v1"}
        response = await client.put(f'/authoring/drafts/scenario/{scene["draft_id"]}',
            headers={"If-Match": "1"}, json={"kind": "scenario", "name": "浏览器三节点对照", "payload": payload})
        assert response.status_code == 200, response.text
        experiment = await _experiment(client, {**_payload(scene["draft_id"]),
            "physical_layer": "Channel2Waveform", "waveform_modulation": "Bpsk", "waveform_noise": "None"})
        (root / "seed.json").write_text(json.dumps({"draft_id": experiment["draft_id"]}))
        assert (await client.get("/runs")).json() == []
    await app.state.catalog.shutdown()


if __name__ == "__main__":
    asyncio.run(seed(Path(sys.argv[1])))
