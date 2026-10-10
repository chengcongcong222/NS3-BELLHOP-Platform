"""Validate an existing unpublished build's ray file, without rerunning Bellhop.

Only auxiliary plot metadata is repaired. Inputs, arrivals and runtime assets
are never rewritten. A metadata backup is retained beside the existing job.
"""
import argparse
import math
import shutil
from pathlib import Path

from ns3_factory_backend.acoustic_plots import file_sha256, ray_plot
from ns3_factory_backend.authoring import AuthoringStore


def repair(workspace: Path, build_id: str) -> dict:
    store = AuthoringStore(workspace)
    build = store.get_build(build_id)
    artifact = build.artifact
    if build.state != "Completed" or not artifact or artifact.get("published"):
        raise ValueError("Only completed, unpublished builds can have plot metadata repaired")
    root = (workspace / "builds" / build_id).resolve()
    arrivals = Path(artifact["arrivals_path"]).resolve()
    if arrivals.parent != root:
        raise ValueError("Build path does not belong to this workspace")
    ray = root / "ray-preview" / "environment.ray"
    if not ray.is_file() or ray.resolve().parent != root / "ray-preview":
        raise ValueError("No local saved ray output to recover")
    # Verify the original authoritative field before attaching a recovered plot.
    if file_sha256(arrivals) != artifact["arrivals_sha256"]:
        raise ValueError("Authoritative arrivals checksum mismatch")
    sha = file_sha256(ray)
    parsed = ray_plot(ray, sha)
    summary = artifact["input_summary"]
    expected = summary["source_depths_meters"]
    if (len(expected) != len(parsed["source_depths_m"])
            or any(not math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-5)
                   for a, b in zip(expected, parsed["source_depths_m"]))
            or not math.isclose(summary["frequency_hz"], parsed["frequency_hz"], rel_tol=1e-7)):
        raise ValueError("Saved ray origins/frequency differ from the frozen build")
    previous = artifact.get("ray_plot", {})
    if previous.get("available"):
        if previous.get("sha256") != sha:
            raise ValueError("Previously registered ray output changed")
        return {"build_id": build_id, "status": "already-available"}
    backup = root / "ray-preview-metadata-before-repair.json"
    if backup.exists():
        raise ValueError("A previous repair backup already exists; inspect it first")
    shutil.copy2(workspace / "builds" / f"{build_id}.json", backup)
    artifact = {**artifact, "ray_plot": {"available": True, "path": str(ray), "sha256": sha}}
    store.save_build(build.model_copy(update={"artifact": artifact}))
    return {"build_id": build_id, "status": "recovered-existing-output", "source_count": len(expected),
            "arrivals_unchanged": True, "backup": str(backup)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("build_id")
    args = parser.parse_args()
    print(repair(args.workspace, args.build_id))
