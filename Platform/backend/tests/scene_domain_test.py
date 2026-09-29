from types import SimpleNamespace as O
import pytest
from ns3_factory_backend.catalog import CatalogError
from ns3_factory_backend.scene_domain import validate_paths
from ns3_factory_backend.trajectory_authoring import Asset, Track, Point

DOMAIN = dict(model="ProfileExtrusion", x_min_m=0, x_max_m=100, y_min_m=-30, y_max_m=30,
              surface_clearance_m=2, bottom_clearance_m=5)
INPUTS = dict(available=True, payload=dict(
    sound_speed_profile=[dict(depth_meters=0, speed_meters_per_second=1500), dict(depth_meters=200, speed_meters_per_second=1500)],
    bathymetry=[dict(range_meters=0, depth_meters=100), dict(range_meters=100, depth_meters=100)]))


def verify(coords, domain=DOMAIN, inputs=INPUTS, minimum=0, maximum=1000, duration=10):
    env = O(coordinate_frame=O(surface_z_meters=0, vertical_axis="PositiveUp"), axes=O(
        source_depth=O(minimum=1, maximum=150), receiver_depth=O(minimum=1, maximum=150), horizontal_range=O(minimum=minimum, maximum=maximum)))
    nodes, bindings, assets = [], [], {}
    for i, path in enumerate(coords):
        nodes.append(dict(node_id=str(i), can_transmit=True, can_receive=True))
        bindings.append(dict(node_id=str(i), asset_id=str(i), version=1, track_id="path"))
        assets[(str(i), 1)] = Asset(asset_id=str(i), version=1, name="test", source="fixture", tracks=[Track(track_id="path",
            points=[Point(time_s=t, x_m=x, y_m=y, z_m=z) for t, x, y, z in path])])
    return validate_paths(nodes, bindings, assets, env, inputs, domain, duration * 10**9)


def test_safe_trajectory_is_valid_without_mutation():
    path = [(0, 10, 0, -30), (10, 90, 0, -40)]
    assert verify([path]) == DOMAIN
    assert path[-1] == (10, 90, 0, -40)


@pytest.mark.parametrize("last,match", [((10, 101, 0, -30), "移出"), ((10, 90, 31, -30), "移出"),
    ((10, 90, 0, 1), "水面"), ((10, 90, 0, -1), "安全距离"), ((10, 90, 0, -98), "离底"),
    ((10, 90, 0, -160), "收发深度")])
def test_valid_start_but_unsafe_movement_is_rejected(last, match):
    with pytest.raises(CatalogError, match=match): verify([[(0, 10, 0, -30), last]])


def test_terrain_ridge_between_safe_endpoints_is_rejected():
    data = {**INPUTS, "payload": {**INPUTS["payload"], "bathymetry": [dict(range_meters=x, depth_meters=d) for x, d in [(0,100),(50,30),(100,100)]]}}
    with pytest.raises(CatalogError, match="5 秒.*海底"):
        verify([[(0,0,0,-50),(10,100,0,-50)]], inputs=data)


def test_ssp_coverage_and_absent_terrain_are_not_invented():
    data = {**INPUTS, "payload": {**INPUTS["payload"], "sound_speed_profile": [dict(depth_meters=0,speed_meters_per_second=1500),dict(depth_meters=20,speed_meters_per_second=1500)]}}
    with pytest.raises(CatalogError, match="声速剖面"):
        verify([[(0,0,0,-50),(10,10,0,-50)]], inputs=data)
    with pytest.raises(CatalogError, match="测线范围"):
        verify([[(0,0,0,-50),(10,10,0,-50)]], inputs={"available": False})


def test_query_range_not_map_width_includes_overhearing_pairs():
    with pytest.raises(CatalogError, match="旁听节点"):
        verify([[(0,0,0,-30),(10,0,0,-30)],[(0,10,0,-30),(10,90,0,-30)]], maximum=50)
    with pytest.raises(CatalogError, match="间距"):
        verify([[(0,0,0,-30),(10,100,0,-30)],[(0,100,0,-30),(10,0,0,-30)]], minimum=10)


def test_only_selected_run_time_is_validated_but_never_extended():
    assert verify([[(0,10,0,-30),(10,90,0,-30),(20,200,0,10)]])
    with pytest.raises(CatalogError, match="覆盖"):
        verify([[(0,10,0,-30),(9,90,0,-30)]])


def test_historical_unmapped_scene_does_not_gain_a_fake_geographic_origin():
    assert verify([[(0,-1000,10,-30),(10,-900,10,-30)]], domain=None) is None
    with pytest.raises(CatalogError, match="水面"):
        verify([[(0,-1000,10,-30),(10,-900,10,5)]], domain=None)


@pytest.mark.parametrize("patch", [dict(x_min_m=-1),dict(y_max_m=-40),dict(model="Real3D"),dict(bottom_clearance_m=-1),dict(x_max_m=float("inf"))])
def test_invalid_bounds_are_rejected(patch):
    with pytest.raises(CatalogError): verify([[(0,10,0,-30),(10,90,0,-30)]], domain={**DOMAIN, **patch})
