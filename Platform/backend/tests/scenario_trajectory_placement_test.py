import pytest

from ns3_factory_backend.catalog import CatalogError
from ns3_factory_backend.scenario_trajectories import compile_bindings, internal_placement
from ns3_factory_backend.trajectory_authoring import GenerateRequest, Store, generate


def fixture(tmp_path):
    store = Store(tmp_path)
    source = store.save(generate(GenerateRequest(asset_id="shared", name="共享路径", kind="ConstantVelocity",
        initial_x_m=100, initial_y_m=20, initial_z_m=-30, vx_mps=2, duration_s=60, dt_s=1)))
    nodes = [{"node_id": str(n), "initial_position": {"x_meters": x, "y_meters": 0, "z_meters": -50}}
             for n, x in enumerate([1000, 2000])]
    bindings = [{"node_id": node["node_id"], "asset_id": "shared", "version": 1,
                 "track_id": "N1", "placement_mode": "RelativeToNodeStart"} for node in nodes]
    return store, source, nodes, bindings


def test_shared_path_has_distinct_origins_and_deterministic_materialization(tmp_path):
    store, source, nodes, bindings = fixture(tmp_path)
    original = (tmp_path / "shared.v1.json").read_bytes()
    compiled, assets = compile_bindings(nodes, bindings, store)
    assert len(assets) == 2 and compiled[0]["asset_id"] != compiled[1]["asset_id"]
    for node, binding in zip(nodes, compiled):
        placed = store.get(binding["asset_id"], binding["version"])
        assert internal_placement(placed)
        for old, point in zip(source.tracks[0].points, placed.tracks[0].points):
            assert point.time_s == old.time_s
            assert point.x_m == node["initial_position"]["x_meters"] + 2 * point.time_s
            assert point.y_m == 0 and point.z_m == -50
            assert point.pitch_rad == old.pitch_rad
    assert compile_bindings(nodes, bindings, store) == (compiled, assets)
    assert len(store.list()) == 3
    assert (tmp_path / "shared.v1.json").read_bytes() == original


def test_old_absolute_binding_is_unchanged(tmp_path):
    store, source, nodes, bindings = fixture(tmp_path)
    for b in bindings:
        b.pop("placement_mode")
    compiled, assets = compile_bindings(nodes, bindings, store)
    assert all(b["asset_id"] == source.asset_id for b in compiled)
    assert list(assets.values()) == [source]
    assert len(store.list()) == 1


@pytest.mark.parametrize("change", ["mode", "missing", "track", "duplicate", "nan", "path"])
def test_invalid_placement_rejected(tmp_path, change):
    store, _, nodes, bindings = fixture(tmp_path)
    if change == "mode": bindings[0]["placement_mode"] = "Guess"
    if change == "missing": bindings[0]["version"] = 9
    if change == "track": bindings[0]["track_id"] = "missing"
    if change == "duplicate": bindings[1] = bindings[0].copy()
    if change == "nan": nodes[0]["initial_position"]["x_meters"] = float("nan")
    if change == "path": bindings[0]["asset_id"] = "../secret"
    with pytest.raises(CatalogError):
        compile_bindings(nodes, bindings, store)


@pytest.mark.parametrize("identity", ["_track", ".track", "-track"])
def test_existing_runtime_identity_character_set_remains_accepted(tmp_path, identity):
    store = Store(tmp_path)
    asset = store.save(generate(GenerateRequest(asset_id=identity, track_id=identity,
        name="已有合法路径", kind="Fixed", duration_s=1, dt_s=1)))
    compiled, assets = compile_bindings([{"node_id": "0"}], [{"node_id": "0",
        "asset_id": identity, "version": 1, "track_id": identity}], store)
    assert compiled[0]["asset_id"] == compiled[0]["track_id"] == identity
    assert list(assets.values()) == [asset]


@pytest.mark.parametrize("field", ["asset_id", "track_id"])
@pytest.mark.parametrize("identity", [".", "..", "a/b", "../outside", "a\\b", ""])
def test_path_like_asset_and_track_ids_are_rejected(tmp_path, field, identity):
    store, _, nodes, bindings = fixture(tmp_path)
    bindings[0][field] = identity
    with pytest.raises(CatalogError) as raised:
        compile_bindings(nodes, bindings, store)
    assert raised.value.code == "TrajectoryResourceInvalid"


@pytest.mark.parametrize("field", ["nodes", "bindings"])
@pytest.mark.parametrize("malformed", [None, {}, [None], ["node"], [{}]])
def test_malformed_node_or_binding_objects_fail_before_asset_writes(tmp_path, field, malformed):
    store, _, nodes, bindings = fixture(tmp_path)
    arguments = {"nodes": nodes, "bindings": bindings}
    arguments[field] = malformed
    with pytest.raises(CatalogError) as raised:
        compile_bindings(**arguments, store=store)
    assert raised.value.code == "InvalidReference"
    assert len(store.list()) == 1


def test_authoring_api_reports_invalid_placement_as_bad_request_without_new_run(tmp_path):
    import asyncio
    import httpx
    from api_test import _wait_terminal
    from run_actions_test import _setup
    from run_definition_context_test import _authored

    async def run():
        app, gateway = _setup(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene, experiment, original = await _authored(client)
            await _wait_terminal(client, original["run_id"])
            payload = scene["payload"]
            payload["trajectory_bindings"][0]["placement_mode"] = "RelativeToNodeStart"
            payload["nodes"][0]["initial_position"]["x_meters"] = "invalid-coordinate"
            updated = await client.put(f"/authoring/drafts/scenario/{scene['draft_id']}",
                headers={"If-Match": "1"}, json={"kind": "scenario", "name": scene["name"], "payload": payload})
            assert updated.status_code == 200
            experiment["payload"]["scenario_revision"] = updated.json()["revision"]
            updated_experiment = await client.put(f"/authoring/drafts/experiment/{experiment['draft_id']}",
                headers={"If-Match": "1"}, json={"kind": "experiment", "name": experiment["name"], "payload": experiment["payload"]})
            assert updated_experiment.status_code == 200
            response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
            assert response.status_code == 400, response.text
            assert response.json()["error"]["code"] == "TrajectoryPlacementInvalid"
            assert len(gateway.commands) == len((await client.get("/runs")).json()) == 1

    asyncio.run(run())
