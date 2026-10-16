"""Import one UWA receiver element/trial without unpacking or freezing time.

The output preserves h_hat, phi/theta and f_resamp for native time-varying
replay. No thresholding, normalization, synthetic geometry or reverse link.
Raw MAT files are not copied into Git. The original checksum and citation
remain in the immutable manifest. This is an offline resource operation.
"""
from __future__ import annotations
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import tempfile
import h5py
import numpy as np


def digest(path: Path, algorithm="sha256"):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def fnv64(path: Path):
    # Integrity cross-check for the dependency-free C++ binary loader.
    value = 14695981039346656037
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            for byte in chunk:
                value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def import_channel(source: Path, root: Path, asset_id: str, name: str,
                   element: int, axis_kind: str, bandwidth_hz: float,
                   citation: str, license_name: str, expected_sha256: str):
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_.-]{0,79}", asset_id) or not name.strip():
        raise ValueError("Explicit stable identity and display name required")
    if type(element) is not int or element < 0 or axis_kind not in {"ReceiverElement", "IndependentTrial"}:
        raise ValueError("Declare receiver-element versus independent-trial semantics")
    if not citation.strip() or not license_name.strip() or not re.fullmatch(r"[a-f0-9]{64}", expected_sha256):
        raise ValueError("Citation, license and expected source SHA256 required")
    if digest(source) != expected_sha256:
        raise ValueError("Source checksum mismatch")
    if not h5py.is_hdf5(source):
        raise ValueError("Save the channel as official MAT -v7.3; MAT v5 import is not enabled")
    target = root / asset_id / "1"
    if target.exists():
        raise ValueError("Recorded asset version already exists; immutable assets cannot be overwritten")
    with h5py.File(source, "r") as f:
        if float(f["version"][0, 0]) < 1:
            raise ValueError("Unsupported source format version")
        shape = f["h_hat"].shape
        if len(shape) != 3 or not 2 <= shape[0] <= 1_000_000 or element >= shape[1] or shape[0]*shape[2] > 20_000_000:
            raise ValueError("Invalid shape, element index or import budget exceeded")
        fc, fd, ft = (float(f["params"][k][0, 0]) for k in ("fc", "fs_delay", "fs_time"))
        if not all(np.isfinite(v) and v > 0 for v in (fc, fd, ft, bandwidth_hz)) or bandwidth_hz > fd:
            raise ValueError("Band must be declared from dataset documentation and fit its delay sampling rate")
        values = f["h_hat"][:, element, :]
        coefficients = np.empty((shape[0], shape[2], 2), dtype="<f8")
        coefficients[..., 0], coefficients[..., 1] = values["real"], values["imag"]
        if not np.isfinite(coefficients).all():
            raise ValueError("Non-finite channel coefficients")
        phase_key = "phi_hat" if "phi_hat" in f else "theta_hat" if "theta_hat" in f else None
        phase = np.array(f[phase_key][:, element], dtype="<f8") if phase_key else np.array([], dtype="<f8")
        if not np.isfinite(phase).all():
            raise ValueError("Non-finite channel phase")
        ratio = Fraction(float(f["f_resamp"][0, 0]) if "f_resamp" in f else 1.).limit_denominator()
        if ratio <= 0 or max(ratio.numerator, ratio.denominator) > 100_000:
            raise ValueError("Resampling ratio exceeds native compute budget")
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=target.parent, prefix=".import-") as temporary:
            directory = Path(temporary)
            coefficients.tofile(directory / "channel.f64le")
            phase.tofile(directory / "phase.f64le")
            files = {key: {"sha256": digest(directory / key), "fnv1a64": fnv64(directory / key),
                           "bytes": (directory / key).stat().st_size} for key in ("channel.f64le", "phase.f64le")}
            manifest = {"schema": "ns3-factory-recorded-channel-v1", "asset_id": asset_id, "version": 1,
                "name": name.strip(), "source_sha256": expected_sha256, "source_name": source.name,
                "citation": citation, "license": license_name, "element": element, "axis_kind": axis_kind,
                "original_axis_count": shape[1], "fc": fc, "fs_delay": fd, "fs_time": ft,
                "snapshots": shape[0], "delay_taps": shape[2], "phase_count": len(phase),
                "tracking": phase_key or "None", "resample_up": ratio.numerator, "resample_down": ratio.denominator,
                "usable_bandwidth_hz": bandwidth_hz, "gain_reference": "Normalized", "geometry": "Unspecified",
                "duration_s": min((shape[0]-1)/ft, len(phase)/fd) if len(phase) else (shape[0]-1)/ft,
                "reference_revision": "dbf373ca7cd07ac4eb1b24a191d76bea18f73560", "files": files}
            (directory / "manifest.json").write_text(json.dumps(manifest, sort_keys=True, indent=2)+"\n")
            directory.rename(target)
    return manifest


if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input",type=Path,required=True)
    p.add_argument("--repository",type=Path,required=True)
    p.add_argument("--id",required=True);p.add_argument("--name",required=True)
    p.add_argument("--element",type=int,required=True)
    p.add_argument("--axis-kind",choices=["ReceiverElement","IndependentTrial"],required=True)
    p.add_argument("--bandwidth",type=float,required=True)
    p.add_argument("--citation",required=True);p.add_argument("--license",required=True)
    p.add_argument("--source-sha256",required=True)
    a=p.parse_args()
    print(json.dumps(import_channel(a.input,a.repository,a.id,a.name,a.element,a.axis_kind,a.bandwidth,
                                   a.citation,a.license,a.source_sha256),ensure_ascii=False))
