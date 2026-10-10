from ns3_factory_backend.api import _environment_location


def test_file_source_does_not_claim_default_query_coordinates():
    payload = {"acquisition": {"latitudeDeg": 18, "longitudeDeg": 110, "month": 4},
               "sources": {"ssp": {"source": "file", "filename": "ssp.csv"}}}
    assert _environment_location(payload) == {"latitude": None, "longitude": None, "month": None}


def test_reused_import_has_no_location_without_query_provenance():
    payload = {"acquisition": {"latitudeDeg": 18, "longitudeDeg": 110, "month": 4},
               "sources": {"ssp": {"filename": "ssp.csv", "cache_id": "saved-import"}}}
    assert _environment_location(payload)["latitude"] is None


def test_woa_and_existing_saved_acquisition_keep_coordinates():
    selection = {"latitudeDeg": 20, "longitudeDeg": 120, "month": 9}
    for source in ({"provider": "NOAA WOA23", "request": {"month": 9}}, {}):
        assert _environment_location({"acquisition": selection, "sources": {"ssp": source}}) == {
            "latitude": 20, "longitude": 120, "month": 9}
    assert _environment_location({}) == {"latitude": None, "longitude": None, "month": None}
