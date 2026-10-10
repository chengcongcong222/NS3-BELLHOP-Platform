import asyncio
import hashlib
import json
from pathlib import Path
import subprocess

import pytest
from ns3_factory_backend.algorithm_catalog import AlgorithmCatalog, validate_selection
from ns3_factory_backend.catalog import CatalogError


def sample_catalog():
    return {"schema": "platform.algorithm-catalog.v1", "worker_sha256": "b" * 64, "algorithms": [{
        "id": "test.mac", "version": "1.0.0", "package_sha256": "a" * 64, "interface": "planning-mac-v1",
        "parameters": [{"id": "gap", "type": "duration_ns", "label": "安全间隔", "unit": "s", "min": "0", "max": "1000000000", "default": "0"}]}]}


def selection():
    item = sample_catalog()["algorithms"][0]
    return {**{k: item[k] for k in ("id", "version", "package_sha256", "interface")}, "parameters": {"gap": "0"}}


@pytest.mark.parametrize("bad", ["-1", "01", "1.5", "1e9", "NaN", "1000000001", True, 0, None])
def test_rejects_lossy_or_out_of_range_parameters(bad):
    candidate = selection()
    candidate["parameters"]["gap"] = bad
    with pytest.raises(CatalogError):
        validate_selection(candidate, sample_catalog())


def test_exact_identity_and_frozen_worker():
    source = selection()
    frozen = validate_selection(source, sample_catalog())
    assert "worker_sha256" not in source
    assert frozen == {**source, "worker_sha256": "b" * 64}
    assert validate_selection(frozen, sample_catalog(), frozen=True) == frozen
    for key in ("id", "version", "package_sha256", "interface"):
        with pytest.raises(CatalogError):
            validate_selection({**source, key: "unavailable"}, sample_catalog())
    for parameters in ({}, {"gap": "0", "unknown": "1"}):
        with pytest.raises(CatalogError):
            validate_selection({**source, "parameters": parameters}, sample_catalog())
    with pytest.raises(CatalogError):
        validate_selection({**frozen, "worker_sha256": "c" * 64}, sample_catalog(), frozen=True)


def test_app_owned_catalog_single_flight_and_no_stale_fallback(tmp_path: Path, monkeypatch):
    worker = tmp_path / "worker"
    worker.write_bytes(b"test executable bytes")
    calls = []
    def invoke(*args, **kwargs):
        calls.append(args)
        return subprocess.CompletedProcess(args[0], 0, json.dumps(sample_catalog()).encode(), b"")
    monkeypatch.setattr(subprocess, "run", invoke)
    index = AlgorithmCatalog(worker)
    async def read():
        rows = await asyncio.gather(*(index.current() for _ in range(8)))
        assert len(calls) == 1
        assert all(row["worker_sha256"] == hashlib.sha256(worker.read_bytes()).hexdigest() for row in rows)
        worker.write_bytes(b"new executable bytes")
        def fail(*args, **kwargs):
            raise subprocess.TimeoutExpired(args[0], 15)
        monkeypatch.setattr(subprocess, "run", fail)
        with pytest.raises(CatalogError):
            await index.current()
    asyncio.run(read())
